# Copyright 2016, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""Service-side implementation of gRPC Python."""

import enum
import logging
import threading
import time

import grpc
from grpc import _common
from grpc._cython import cygrpc

_SHUTDOWN_TAG = 'shutdown'
_REQUEST_CALL_TAG = 'request_call'
_RECEIVE_CLOSE_ON_SERVER_TAG = 'receive_close_on_server'
_SEND_INITIAL_METADATA_TAG = 'send_initial_metadata'
_RECEIVE_MESSAGE_TAG = 'receive_message'
_SEND_MESSAGE_TAG = 'send_message'
_SEND_STATUS_FROM_SERVER_TAG = 'send_status_from_server'

_EMPTY_METADATA = cygrpc.Metadata(())
_EMPTY_FLAGS = 0


def _serialized_request(request_event):
  return request_event.batch_operations[0].received_message.bytes()


def _code(state):
  return cygrpc.StatusCode.ok if state.code is None else state.code


def _details(state):
  return b'' if state.details is None else state.details


class _RPCState(object):

  def __init__(self):
    self.condition = threading.Condition()
    self.due = set()
    self.request = None
    self.initial_metadata_allowed = True
    self.disable_next_compression = False
    self.trailing_metadata = None
    self.code = None
    self.details = None
    self.statused = False
    self.cancelled = False
    self.rpc_errors = []


def _raise_rpc_error(state):
  rpc_error = grpc.RpcError()
  state.rpc_errors.append(rpc_error)
  raise rpc_error


def _drain(state, completion_queue):
  """Drains the completion queue associated with an active call.

  This function may only be called when there is at least one tag due for the
  call.

  Args:
    state: The _RPCState of the active call.
    completion_queue: The completion queue used exclusively in association with
      the active call.
  """
  while True:
    event = completion_queue.poll()
    with state.condition:
      state.due.remove(event.tag)
      if not state.due:
        return


def _abort(state, call, code, details):
  if not state.cancelled:
    if state.initial_metadata_allowed:
      operations = (
          cygrpc.operation_send_initial_metadata(_EMPTY_METADATA, _EMPTY_FLAGS),
          cygrpc.operation_send_status_from_server(
              _common.metadata(state.trailing_metadata), code, details,
              _EMPTY_FLAGS),
      )
    else:
      operations = (
          cygrpc.operation_send_status_from_server(
              _common.metadata(state.trailing_metadata), code, details,
              _EMPTY_FLAGS),
      )
    call.start_batch(
        cygrpc.Operations(operations), _SEND_STATUS_FROM_SERVER_TAG)
    state.due.add(_SEND_STATUS_FROM_SERVER_TAG)
    return True
  else:
    return False


def _handle_event(event, state, call, request_deserializer):
  state.due.remove(event.tag)
  for batch_operation in event.batch_operations:
    if batch_operation.type is cygrpc.OperationType.receive_close_on_server:
      if batch_operation.received_cancelled:
        state.cancelled = True
    elif batch_operation.type is cygrpc.OperationType.receive_message:
      serialized_request = batch_operation.received_message.bytes()
      if serialized_request is not None:
        request = _common.deserialize(serialized_request, request_deserializer)
        if request is None:
          _abort(
              state, call, cygrpc.StatusCode.internal,
              b'Exception deserializing request!')
        else:
          state.request = request


def _drain_tag(tag, rpc_event, state, completion_queue, request_deserializer):
  while True:
    event = completion_queue.poll()
    with state.condition:
      _handle_event(
          event, state, rpc_event.operation_call, request_deserializer)
      if tag not in state.due:
        break


class _Context(grpc.ServicerContext):

  def __init__(self, rpc_event, state, completion_queue, request_deserializer):
    self._rpc_event = rpc_event
    self._state = state
    self._completion_queue = completion_queue
    self._request_deserializer = request_deserializer

  def is_active(self):
    with self._state.condition:
      return not self._state.cancelled and not self._state.statused

  def time_remaining(self):
    return max(self._rpc_event.request_call_details.deadline - time.time(), 0)

  def cancel(self):
    self._rpc_event.operation_call.cancel()

  def disable_next_message_compression(self):
    with self._state.condition:
      self._state.disable_next_compression = True

  def invocation_metadata(self):
    return tuple(
        (key, value) for key, value in self._rpc_event.request_metadata)

  def initial_metadata(self, initial_metadata):
    with self._state.condition:
      if self._state.initial_metadata_allowed:
        operation = cygrpc.operation_send_initial_metadata(
            cygrpc.Metadata(initial_metadata), _EMPTY_FLAGS)
        self._rpc_event.operation_call.start_batch(
            cygrpc.Operations((operation,)), _SEND_INITIAL_METADATA_TAG)
        self._state.initial_metadata_allowed = False
      else:
        raise ValueError('Initial metadata no longer allowed!')
    _drain_tag(
        _SEND_INITIAL_METADATA_TAG, self._rpc_event, self._state,
        self._completion_queue, self._request_deserializer)
    with self._state.condition:
      if self._state.cancelled:
        _raise_rpc_error(self._state)

  def trailing_metadata(self, trailing_metadata):
    with self._state.condition:
      self._state.trailing_metadata = trailing_metadata

  def code(self, code):
    with self._state.condition:
      self._state.code = code

  def details(self, details):
    with self._state.condition:
      self._state.details = details

  def peer(self):
    return self._rpc_event.operation_call.peer()


class _RequestIterator(object):

  def __init__(self, state, call, completion_queue, request_deserializer):
    self._state = state
    self._call = call
    self._completion_queue = completion_queue
    self._request_deserializer = request_deserializer

  def _raise_or_start_receive_message(self):
    if self._state.cancelled:
      _raise_rpc_error(self._state)
    elif self._state.statused:
      raise StopIteration()
    else:
      self._call.start_batch(
          cygrpc.Operations((cygrpc.operation_receive_message(_EMPTY_FLAGS),)),
          _RECEIVE_MESSAGE_TAG)
      self._state.due.add(_RECEIVE_MESSAGE_TAG)

  def _look_for_request(self):
    if self._state.cancelled:
      _raise_rpc_error(self._state)
    elif self._state.request is None:
      raise StopIteration()
    else:
      request = self._state.request
      self._state.request = None
      return request

  def _next(self):
    with self._state.condition:
      self._raise_or_start_receive_message()
    while True:
      event = self._completion_queue.poll()
      with self._state.condition:
        _handle_event(
            event, self._state, self._call, self._request_deserializer)
        request = self._look_for_request()
        if request is not None:
          return request

  def __iter__(self):
    return self

  def __next__(self):
    return self._next()

  def next(self):
    return self._next()


def _unary_request(rpc_event, state, completion_queue, request_deserializer):
  def unary_request():
    with state.condition:
      rpc_event.operation_call.start_batch(
          cygrpc.Operations((cygrpc.operation_receive_message(_EMPTY_FLAGS),)),
          _RECEIVE_MESSAGE_TAG)
      state.due.add(_RECEIVE_MESSAGE_TAG)
    while True:
      event = completion_queue.poll()
      with state.condition:
        _handle_event(
            event, state, rpc_event.operation_call, request_deserializer)
        if state.request is not None:
          request = state.request
          state.request = None
          return request
        elif state.cancelled:
          drain = bool(state.due)
          break
        elif not state.due:
          # TODO(5992#issuecomment-220761992): really, what status code?
          _abort(
              state, rpc_event.operation_call, cygrpc.StatusCode.unavailable,
              b'This method requires exactly one request message.')
          drain = bool(state.due)
          break
    if drain:
      _drain(state, completion_queue)
    return None
  return unary_request


def _call_behavior(
    rpc_event, state, completion_queue, behavior, argument,
    request_deserializer):
  context = _Context(rpc_event, state, completion_queue, request_deserializer)
  try:
    return behavior(argument, context)
  except Exception as e:  # pylint: disable=broad-except
    with state.condition:
      if e not in state.rpc_errors:
        details = b'Exception calling application: {}'.format(e)
        logging.exception(details)
        _abort(
            state, rpc_event.operation_call, cygrpc.StatusCode.unknown, details)
      drain = bool(state.due)
    if drain:
      _drain(state, completion_queue)
    return None


def _take_response_from_response_iterator(
    rpc_event, state, completion_queue, response_iterator):
  try:
    return next(response_iterator), True
  except StopIteration:
    return None, True
  except Exception as e:  # pylint: disable=broad-except
    with state.condition:
      if e not in state.rpc_errors:
        details = b'Exception iterating responses: {}'.format(e)
        logging.exception(details)
        _abort(
            state, rpc_event.operation_call, cygrpc.StatusCode.unknown, details)
      drain = bool(state.due)
    if drain:
      _drain(state, completion_queue)
    return None, False


def _serialize_response(
    rpc_event, state, completion_queue, response, response_serializer):
  serialized_response = _common.serialize(response, response_serializer)
  if serialized_response is None:
    with state.condition:
      _abort(
          state, rpc_event.operation_call, cygrpc.StatusCode.internal,
          b'Failed to serialize response!')
      drain = bool(state.due)
    if drain:
      _drain(state, completion_queue)
    return None
  else:
    return serialized_response


def _send_response(
    rpc_event, state, completion_queue, serialized_response,
    request_deserializer):
  with state.condition:
    if state.initial_metadata_allowed:
      operations = (
          cygrpc.operation_send_initial_metadata(
              _EMPTY_METADATA, _EMPTY_FLAGS),
          cygrpc.operation_send_message(serialized_response, _EMPTY_FLAGS),
      )
      state.initial_metadata_allowed = False
    else:
      operations = (
          cygrpc.operation_send_message(serialized_response, _EMPTY_FLAGS),)
    rpc_event.operation_call.start_batch(
        cygrpc.Operations(operations), _SEND_MESSAGE_TAG)
    state.due.add(_SEND_MESSAGE_TAG)
  _drain_tag(
      _SEND_MESSAGE_TAG, rpc_event, state, completion_queue,
      request_deserializer)


def _status(rpc_event, state, completion_queue, serialized_response):
  with state.condition:
    if not state.cancelled:
      trailing_metadata = _common.metadata(state.trailing_metadata)
      code = _code(state)
      details = _details(state)
      operations = [
          cygrpc.operation_send_status_from_server(
              trailing_metadata, code, details, _EMPTY_FLAGS),
      ]
      if state.initial_metadata_allowed:
        operations.append(
            cygrpc.operation_send_initial_metadata(
                _EMPTY_METADATA, _EMPTY_FLAGS))
      if serialized_response is not None:
        operations.append(
            cygrpc.operation_send_message(serialized_response, _EMPTY_FLAGS))
      rpc_event.operation_call.start_batch(
          cygrpc.Operations(operations), _SEND_STATUS_FROM_SERVER_TAG)
      state.statused = True
      state.due.add(_SEND_STATUS_FROM_SERVER_TAG)
    drain = bool(state.due)
  if drain:
    _drain(state, completion_queue)


def _unary_response_in_pool(
    rpc_event, state, completion_queue, behavior, argument_thunk,
    request_deserializer, response_serializer):
  argument = argument_thunk()
  if argument is not None:
    response = _call_behavior(
        rpc_event, state, completion_queue, behavior, argument,
        request_deserializer)
    if response is not None:
      serialized_response = _serialize_response(
          rpc_event, state, completion_queue, response, response_serializer)
      if serialized_response is not None:
        _status(rpc_event, state, completion_queue, serialized_response)


def _stream_response_in_pool(
    rpc_event, state, completion_queue, behavior, argument_thunk,
    request_deserializer, response_serializer):
  argument = argument_thunk()
  if argument is not None:
    response_iterator = _call_behavior(
        rpc_event, state, completion_queue, behavior, argument,
        request_deserializer)
    if response_iterator is not None:
      while True:
        response, proceed = _take_response_from_response_iterator(
            rpc_event, state, completion_queue, response_iterator)
        if proceed:
          if response is None:
            _status(rpc_event, state, completion_queue, None)
            break
          else:
            serialized_response = _serialize_response(
                rpc_event, state, completion_queue, response,
                response_serializer)
            if serialized_response is not None:
              _send_response(
                  rpc_event, state, completion_queue, serialized_response,
                  request_deserializer)
            else:
              break
        else:
          break


def _handle_unary_unary(
    rpc_event, state, completion_queue, method_handler, thread_pool):
  unary_request = _unary_request(
      rpc_event, state, completion_queue,
      method_handler.request_deserializer)
  thread_pool.submit(
      _unary_response_in_pool, rpc_event, state, completion_queue,
      method_handler.unary_unary, unary_request,
      method_handler.request_deserializer,
      method_handler.response_serializer)


def _handle_unary_stream(
    rpc_event, state, completion_queue, method_handler, thread_pool):
  unary_request = _unary_request(
      rpc_event, state, completion_queue,
      method_handler.request_deserializer)
  thread_pool.submit(
      _stream_response_in_pool, rpc_event, state, completion_queue,
      method_handler.unary_stream, unary_request,
      method_handler.request_deserializer,
      method_handler.response_serializer)


def _handle_stream_unary(
    rpc_event, state, completion_queue, method_handler, thread_pool):
  request_iterator = _RequestIterator(
      state, rpc_event.operation_call, completion_queue,
      method_handler.request_deserializer)
  thread_pool.submit(
      _unary_response_in_pool, rpc_event, state, completion_queue,
      method_handler.stream_unary, lambda: request_iterator,
      method_handler.request_deserializer,
      method_handler.response_serializer)


def _handle_stream_stream(
    rpc_event, state, completion_queue, method_handler, thread_pool):
  request_iterator = _RequestIterator(
      state, rpc_event.operation_call, completion_queue,
      method_handler.request_deserializer)
  thread_pool.submit(
      _stream_response_in_pool, rpc_event, state, completion_queue,
      method_handler.stream_stream, lambda: request_iterator,
      method_handler.request_deserializer,
      method_handler.response_serializer)


def _find_method_handler(method, generic_handlers):
  for generic_handler in generic_handlers:
    method_handler = generic_handler(method)
    if method_handler is not None:
      return method_handler
  else:
    return None


def _handle_unrecognized_method(rpc_event, completion_queue):
  operations = (
      cygrpc.operation_send_initial_metadata(_EMPTY_METADATA, _EMPTY_FLAGS),
      cygrpc.operation_receive_close_on_server(_EMPTY_FLAGS),
      cygrpc.operation_send_status_from_server(
          _EMPTY_METADATA, cygrpc.StatusCode.unimplemented,
          b'Method not found!', _EMPTY_FLAGS),
  )
  rpc_event.operation_call.start_batch(operations, None)
  completion_queue.poll()


def _handle_with_method_handler(
    rpc_event, completion_queue, method_handler, thread_pool):
  state = _RPCState()
  with state.condition:
    rpc_event.operation_call.start_batch(
        cygrpc.Operations(
            (cygrpc.operation_receive_close_on_server(_EMPTY_FLAGS),)),
        _RECEIVE_CLOSE_ON_SERVER_TAG)
    state.due.add(_RECEIVE_CLOSE_ON_SERVER_TAG)
    if method_handler.request_streaming:
      if method_handler.response_streaming:
        _handle_stream_stream(
            rpc_event, state, completion_queue, method_handler, thread_pool)
      else:
        _handle_stream_unary(
            rpc_event, state, completion_queue, method_handler, thread_pool)
    else:
      if method_handler.response_streaming:
        _handle_unary_stream(
            rpc_event, state, completion_queue, method_handler, thread_pool)
      else:
        _handle_unary_unary(
            rpc_event, state, completion_queue, method_handler, thread_pool)


def _handle_call(completion_queue, generic_handlers, thread_pool):
  def handle_call(rpc_event):
    if rpc_event.request_call_details.method is not None:
      method_handler = _find_method_handler(
          rpc_event.request_call_details.method, generic_handlers)
      if method_handler is None:
        _handle_unrecognized_method(rpc_event, completion_queue)
      else:
        _handle_with_method_handler(
            rpc_event, completion_queue, method_handler, thread_pool)
  return handle_call


@enum.unique
class _ServerStage(enum.Enum):
  STOPPED = 'stopped'
  STARTED = 'started'
  GRACE = 'grace'


class _ServerState(object):

  def __init__(self, completion_queue, server, generic_handlers, thread_pool):
    self.lock = threading.Lock()
    self.completion_queue = completion_queue
    self.server = server
    self.generic_handlers = list(generic_handlers)
    self.thread_pool = thread_pool
    self.stage = _ServerStage.STOPPED
    self.shutdown_events = None


def _add_generic_handlers(state, generic_handlers):
  with state.lock:
    state.generic_handlers.extend(generic_handlers)


def _add_insecure_port(state, address):
  with state.lock:
    return state.server.add_http2_port(address)


def _request_call(state):
  call_completion_queue = cygrpc.CompletionQueue()
  tag = _handle_call(
      call_completion_queue, state.generic_handlers, state.thread_pool)
  state.server.request_call(
      call_completion_queue, state.completion_queue, tag)


def _serve(state):
  while True:
    event = state.completion_queue.poll()
    if event.type is cygrpc.CompletionType.queue_shutdown:
      with state.lock:
        for shutdown_event in state.shutdown_events:
          shutdown_event.set()
        state.stage = _ServerStage.STOPPED
      break
    elif event.tag is _SHUTDOWN_TAG:
      with state.lock:
        state.completion_queue.shutdown()
    else:
      event.tag(event)
      with state.lock:
        if state.stage is _ServerStage.STARTED:
          _request_call(state)


def _start(state):
  with state.lock:
    if state.stage is not _ServerStage.STOPPED:
      raise ValueError('Cannot start already-started server!')
    state.server.start()
    state.stage = _ServerStage.STARTED
    _request_call(state)
    thread = threading.Thread(target=_serve, args=(state,))
    thread.start()


def _stop(state, grace):
  with state.lock:
    if state.stage is _ServerStage.STOPPED:
      shutdown_event = threading.Event()
      shutdown_event.set()
      return shutdown_event
    else:
      if state.stage is _ServerStage.STARTED:
        state.server.shutdown(state.completion_queue, _SHUTDOWN_TAG)
        state.stage = _ServerStage.GRACE
        state.shutdown_events = []
      shutdown_event = threading.Event()
      state.shutdown_events.append(shutdown_event)
      if grace is None:
        state.server.cancel_all_calls()
      else:
        def cancel_all_calls_after_grace():
          shutdown_event.wait(timeout=grace)
          with state.lock:
            state.server.cancel_all_calls()
        thread = threading.Thread(target=cancel_all_calls_after_grace)
        thread.start()
        return shutdown_event
  shutdown_event.wait()
  return shutdown_event


class Server(grpc.Server):

  def __init__(self, generic_handlers, thread_pool):
    completion_queue = cygrpc.CompletionQueue()
    server = cygrpc.Server()
    server.register_completion_queue(completion_queue)
    self._state = _ServerState(
        completion_queue, server, generic_handlers, thread_pool)

  def add_generic_rpc_handlers(self, generic_rpc_handlers):
    _add_generic_handlers(self._state, generic_rpc_handlers)

  def add_insecure_port(self, address):
    return _add_insecure_port(self._state, address)

  def start(self):
    _start(self._state)

  def stop(self, grace):
    return _stop(self._state, grace)

  def __del__(self):
    _stop(self._state, None)
