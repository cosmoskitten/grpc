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

""""""

import sys
import threading
import time

import grpc
from grpc import _common
from grpc._cython import cygrpc

_INFINITE_FUTURE = cygrpc.Timespec(float('+inf'))

_UNARY_UNARY_INITIAL_DUE = (
    cygrpc.OperationType.send_initial_metadata,
    cygrpc.OperationType.send_message,
    cygrpc.OperationType.send_close_from_client,
    cygrpc.OperationType.receive_initial_metadata,
    cygrpc.OperationType.receive_message,
    cygrpc.OperationType.receive_status_on_client,
)
_UNARY_STREAM_INITIAL_DUE = (
    cygrpc.OperationType.send_initial_metadata,
    cygrpc.OperationType.send_message,
    cygrpc.OperationType.send_close_from_client,
    cygrpc.OperationType.receive_initial_metadata,
    cygrpc.OperationType.receive_status_on_client,
)
_STREAM_UNARY_INITIAL_DUE = (
    cygrpc.OperationType.send_initial_metadata,
    cygrpc.OperationType.receive_initial_metadata,
    cygrpc.OperationType.receive_message,
    cygrpc.OperationType.receive_status_on_client,
)
_STREAM_STREAM_INITIAL_DUE = (
    cygrpc.OperationType.send_initial_metadata,
    cygrpc.OperationType.receive_initial_metadata,
    cygrpc.OperationType.receive_status_on_client,
)


def _deadline(timeout):
  if timeout is None:
    return None, _INFINITE_FUTURE
  else:
    deadline = time.time() + timeout
    return deadline, cygrpc.Timespec(deadline)


def _call_flags(disable_compression):
  if disable_compression:
    return cygrpc.WriteFlag.no_compress
  else:
    return 0


def _unknown_code_details(unknown_cygrpc_code, details):
  return b'Server sent unknown code {} and details "{}"'.format(
      unknown_cygrpc_code, details)


def _wait_once_until(condition, until):
  if until is None:
    condition.wait()
  else:
    remaining = until - time.time()
    if remaining < 0:
      raise grpc.FutureTimeoutError()
    else:
      condition.wait(timeout=remaining)


class _RPCState(object):

  def __init__(self, due, code, details):
    self.condition = threading.Condition()
    # The cygrpc.OperationType objects representing events due from the RPC's
    # completion queue.
    self.due = set(due)
    self.disable_next_compression = False
    self.initial_metadata = None
    self.response = None
    self.trailing_metadata = None
    self.code = code
    self.details = details
    # The semantics of grpc.Future.cancel and grpc.Future.cancelled are
    # slightly wonky, so they have to be tracked separately from the rest of the
    # result of the RPC. This field tracks whether cancellation was requested
    # prior to termination of the RPC.
    self.cancelled = False
    self.callbacks = []


def _handle_event(event, state, response_deserializer):
  for batch_operation in event.batch_operations:
    operation_type = batch_operation.type
    state.due.remove(operation_type)
    if operation_type is cygrpc.OperationType.receive_initial_metadata:
      state.initial_metadata = batch_operation.received_metadata
    elif operation_type is cygrpc.OperationType.receive_message:
      serialized_response = batch_operation.received_message.bytes()
      if serialized_response is not None:
        response = _common.deserialize(
            serialized_response, response_deserializer)
        if response is None:
          state.code = grpc.StatusCode.INTERNAL
          state.details = b'Exception deserializing response!'
        else:
          state.response = response
    elif operation_type is cygrpc.OperationType.receive_status_on_client:
      state.trailing_metadata = batch_operation.received_metadata
      if state.code is None:
        code = _common.CYGRPC_STATUS_CODE_TO_STATUS_CODE.get(
            batch_operation.received_status_code)
        if code is None:
          state.code = grpc.StatusCode.UNKNOWN
          state.details = _unknown_code_details(
              batch_operation.received_status_code,
              batch_operation.received_status_details)
        else:
          state.code = code
          state.details = batch_operation.received_status_details
      for callback in state.callbacks:
        callback()


def _event_handler(state, call, response_deserializer):
  def handle_event(event):
    with state.condition:
      _handle_event(event, state, response_deserializer)
      state.condition.notify_all()
      if state.due:
        return None
      else:
        return call
  return handle_event


def _drain(state, completion_queue, response_deserializer):
  while state.due:
    event = completion_queue.poll()
    _handle_event(event, state, response_deserializer)


def _consume_request_iterator(
    request_iterator, state, call, request_serializer):
  event_handler = _event_handler(state, call, None)
  def consume_request_iterator():
    for request in request_iterator:
      serialized_request = _common.serialize(request, request_serializer)
      with state.condition:
        if state.code is None:
          if serialized_request is None:
            call.cancel()
            state.code = grpc.StatusCode.INTERNAL
            state.details = b'Exception serializing request!'
            return
          else:
            operations = (cygrpc.operation_send_message(serialized_request),)
            call.start_batch(cygrpc.Operations(operations), event_handler)
            state.due.add(cygrpc.OperationType.send_message)
            while True:
              state.condition.wait()
              if state.code is None:
                if cygrpc.OperationType.send_message not in state.due:
                  break
              else:
                return
    with state.condition:
      if state.code is None:
        operations = (cygrpc.operation_send_close_from_client(),)
        call.start_batch(cygrpc.Operations(operations), event_handler)
        state.due.add(cygrpc.OperationType.send_close_from_client)
  thread = threading.Thread(target=consume_request_iterator)
  thread.start()


class _Rendezvous(grpc.Future, grpc.Call, grpc.RpcError):

  def __init__(
      self, state, call, completion_queue, response_deserializer, deadline):
    super(_Rendezvous, self).__init__()
    self._state = state
    self._call = call
    # If None, indicates that the call is a "managed" call and that its events
    # will be obtained by the Channel's polling of its own completion queue.
    self._completion_queue = completion_queue
    self._response_deserializer = response_deserializer
    self._deadline = deadline

  def cancel(self):
    with self._state.condition:
      if self._state.code is None:
        self._call.cancel()
        self._state.cancelled = True
        if self._completion_queue is not None:
          _drain(
              self._state, self._completion_queue, self._response_deserializer)
      self._state.condition.notify_all()
      return False

  def cancelled(self):
    with self._state.condition:
      return self._state.cancelled

  def running(self):
    with self._state.condition:
      return self._state.code is None

  def done(self):
    with self._state.condition:
      return self._state.code is not None

  def result(self, timeout=None):
    until = None if timeout is None else time.time() + timeout
    with self._state.condition:
      while True:
        if self._state.code is None:
          _wait_once_until(self._state.condition, until)
        elif self._state.code == grpc.StatusCode.OK:
          return self._state.response
        elif self._state.cancelled:
          raise grpc.FutureCancelledError()
        else:
          raise self

  def exception(self, timeout=None):
    until = None if timeout is None else time.time() + timeout
    with self._state.condition:
      while True:
        if self._state.code is None:
          _wait_once_until(self._state.condition, until)
        elif self._state.code == grpc.StatusCode.OK:
          return None
        elif self._state.cancelled:
          raise grpc.FutureCancelledError()
        else:
          return self

  def traceback(self, timeout=None):
    until = None if timeout is None else time.time() + timeout
    with self._state.condition:
      while True:
        if self._state.code is None:
          _wait_once_until(self._state.condition, until)
        elif self._state.code == grpc.StatusCode.OK:
          return None
        elif self._state.cancelled:
          raise grpc.FutureCancelledError()
        else:
          try:
            raise self
          except grpc.RpcError:
            return sys.exc_info()[2]

  def add_done_callback(self, fn):
    with self._state.condition:
      if self._state.code is None:
        self._state.callbacks.append(lambda: fn(self))
        return

    fn(self)

  def _next(self):
    with self._state.condition:
      if self._state.code is None:
        self._call.start_batch(
            cygrpc.Operations((cygrpc.operation_receive_message(),)), None)
        self._state.due.add(cygrpc.OperationType.receive_message)
      elif self._state.code == grpc.StatusCode.OK:
        raise StopIteration()
      else:
        raise self
    while True:
      event = self._completion_queue.poll()
      with self._state.condition:
        _handle_event(event, self._state, self._response_deserializer)
        self._state.condition.notify_all()
        if self._state.code is None:
          if self._state.response is not None:
            response = self._state.response
            self._state.response = None
            return response
        elif not self._state.due:
          if self._state.code == grpc.StatusCode.OK:
            raise StopIteration()
          else:
            raise self

  def __iter__(self):
    return self

  def __next__(self):
    return self._next()

  def next(self):
    return self._next()

  def is_active(self):
    with self._state.condition:
      return self._state.code is None

  def time_remaining(self):
    if self._deadline is None:
      return None
    else:
      return max(self._deadline - time.time(), 0)

  def disable_next_message_compression(self):
    with self._state.condition:
      self._state.disable_next_compression = True

  def initial_metadata(self):
    with self._state.condition:
      while self._state.initial_metadata is None:
        if self._completion_queue is None:
          self._state.condition.wait()
        else:
          event = self._completion_queue.poll()
          _handle_event(event, self._state, self._response_deserializer)
      if self._state.initial_metadata is None:
        return ()
      else:
        return self._state.initial_metadata

  def trailing_metadata(self):
    with self._state.condition:
      while self._state.code is None:
        if self._completion_queue is None:
          self._state.condition.wait()
        else:
          event = self._completion_queue.poll()
          _handle_event(event, self._state, self._response_deserializer)
      if self._state.trailing_metadata is None:
        return ()
      else:
        return self._state.trailing_metadata

  def code(self):
    with self._state.condition:
      while self._state.code is None:
        if self._completion_queue is None:
          self._state.condition.wait()
        else:
          event = self._completion_queue.poll()
          _handle_event(event, self._state, self._response_deserializer)
      return self._state.code

  def details(self):
    with self._state.condition:
      while self._state.details is None:
        if self._completion_queue is None:
          self._state.condition.wait()
        else:
          event = self._completion_queue.poll()
          _handle_event(event, self._state, self._response_deserializer)
      return self._state.details

  def _repr(self):
    with self._state.condition:
      if self._state.code is None:
        return '<_Rendezvous object of in-flight RPC>'
      else:
        return '<_Rendezvous of RPC that terminated with ({}, {})>'.format(
            self._state.code, self._state.details)

  def __repr__(self):
    return self._repr()

  def __str__(self):
    return self._repr()

  def __del__(self):
    with self._state.condition:
      if self._state.code is None:
        self._call.cancel()
        if self._completion_queue is not None:
          _drain(self._state, self._completion_queue, None)
        self._state.condition.notify_all()


def _start_unary_request(request, timeout, request_serializer):
  deadline, deadline_timespec = _deadline(timeout)
  serialized_request = _common.serialize(request, request_serializer)
  if serialized_request is None:
    state = _RPCState(
        (), grpc.StatusCode.INTERNAL, b'Exception serializing request!')
    rendezvous = _Rendezvous(state, None, None, None, deadline)
    return deadline, deadline_timespec, None, rendezvous
  else:
    return deadline, deadline_timespec, serialized_request, None


def _end_unary_response_blocking(state, with_call, deadline):
  if state.code is grpc.StatusCode.OK:
    if with_call:
      rendezvous = _Rendezvous(state, None, None, None, deadline)
      return state.response, rendezvous
    else:
      return state.response
  else:
    raise _Rendezvous(state, None, None, None, deadline)


class _UnaryUnaryMultiCallable(grpc.UnaryUnaryMultiCallable):

  def __init__(
      self, channel, create_managed_call, method, request_serializer,
      response_deserializer):
    self._channel = channel
    self._create_managed_call = create_managed_call
    self._method = method
    self._request_serializer = request_serializer
    self._response_deserializer = response_deserializer

  def _call(self, request, timeout, metadata):
    deadline, deadline_timespec, serialized_request, rendezvous = (
        _start_unary_request(request, timeout, self._request_serializer))
    if serialized_request is None:
      return None, None, None, None, rendezvous
    else:
      state = _RPCState(_UNARY_UNARY_INITIAL_DUE, None, None)
      operations = (
          cygrpc.operation_send_initial_metadata(_common.metadata(metadata)),
          cygrpc.operation_send_message(serialized_request),
          cygrpc.operation_send_close_from_client(),
          cygrpc.operation_receive_initial_metadata(),
          cygrpc.operation_receive_message(),
          cygrpc.operation_receive_status_on_client(),
      )
      return state, operations, deadline, deadline_timespec, None

  def __call__(
      self, request, timeout=None, metadata=None, disable_compression=False,
      with_call=False):
    state, operations, deadline, deadline_timespec, rendezvous = self._call(
        request, timeout, metadata)
    if rendezvous:
      raise rendezvous
    else:
      completion_queue = cygrpc.CompletionQueue()
      call = self._channel.create_call(
          None, _call_flags(disable_compression), completion_queue,
          self._method, None, deadline_timespec)
      call.start_batch(cygrpc.Operations(operations), None)
      _handle_event(completion_queue.poll(), state, self._response_deserializer)
      return _end_unary_response_blocking(state, with_call, deadline)

  def future(
      self, request, timeout=None, metadata=None, disable_compression=False):
    state, operations, deadline, deadline_timespec, rendezvous = self._call(
        request, timeout, metadata)
    if rendezvous:
      return rendezvous
    else:
      call = self._create_managed_call(
          _call_flags(disable_compression), self._method, deadline_timespec)
      event_handler = _event_handler(state, call, self._response_deserializer)
      with state.condition:
        call.start_batch(cygrpc.Operations(operations), event_handler)
      return _Rendezvous(
          state, call, None, self._response_deserializer, deadline)


class _UnaryStreamMultiCallable(grpc.UnaryStreamMultiCallable):

  def __init__(
      self, channel, method, request_serializer, response_deserializer):
    self._channel = channel
    self._method = method
    self._request_serializer = request_serializer
    self._response_deserializer = response_deserializer

  def __call__(
      self, request, timeout=None, metadata=None, disable_compression=False,
      with_call=False):
    deadline, deadline_timespec, serialized_request, rendezvous = (
        _start_unary_request(request, timeout, self._request_serializer))
    if serialized_request is None:
      raise rendezvous
    else:
      state = _RPCState(_UNARY_STREAM_INITIAL_DUE, None, None)
      completion_queue = cygrpc.CompletionQueue()
      call = self._channel.create_call(
          None, _call_flags(disable_compression), completion_queue,
          self._method, None, deadline_timespec)
      operations = (
          cygrpc.operation_send_initial_metadata(_common.metadata(metadata)),
          cygrpc.operation_send_message(serialized_request),
          cygrpc.operation_send_close_from_client(),
          cygrpc.operation_receive_initial_metadata(),
          cygrpc.operation_receive_status_on_client(),
      )
      call.start_batch(cygrpc.Operations(operations), None)
      return _Rendezvous(
          state, call, completion_queue, self._response_deserializer, deadline)


class _StreamUnaryMultiCallable(grpc.StreamUnaryMultiCallable):

  def __init__(
      self, channel, create_managed_call, method, request_serializer,
      response_deserializer):
    self._channel = channel
    self._create_managed_call = create_managed_call
    self._method = method
    self._request_serializer = request_serializer
    self._response_deserializer = response_deserializer

  def __call__(
      self, request_iterator, timeout=None, metadata=None, with_call=False):
    deadline, deadline_timespec = _deadline(timeout)
    state = _RPCState(_STREAM_UNARY_INITIAL_DUE, None, None)
    completion_queue = cygrpc.CompletionQueue()
    call = self._channel.create_call(
        None, 0, completion_queue, self._method, None, deadline_timespec)
    with state.condition:
      call.start_batch(
          cygrpc.Operations((cygrpc.operation_receive_initial_metadata(),)),
          None)
      operations = (
          cygrpc.operation_send_initial_metadata(_common.metadata(metadata)),
          cygrpc.operation_receive_message(),
          cygrpc.operation_receive_status_on_client(),
      )
      call.start_batch(cygrpc.Operations(operations), None)
      _consume_request_iterator(
          request_iterator, state, call, self._request_serializer)
    while True:
      event = completion_queue.poll()
      with state.condition:
        _handle_event(event, state, self._response_deserializer)
        state.condition.notify_all()
        if not state.due:
          break
    return _end_unary_response_blocking(state, with_call, deadline)

  def future(self, request_iterator, timeout=None, metadata=None):
    deadline, deadline_timespec = _deadline(timeout)
    state = _RPCState(_STREAM_UNARY_INITIAL_DUE, None, None)
    call = self._create_managed_call(0, self._method, deadline_timespec)
    receive_initial_metadata_event_handler = _event_handler(
        state, call, self._response_deserializer)
    other_operations_event_handler = _event_handler(
        state, call, self._response_deserializer)
    with state.condition:
      call.start_batch(
          cygrpc.Operations((cygrpc.operation_receive_initial_metadata(),)),
          receive_initial_metadata_event_handler)
      operations = (
          cygrpc.operation_send_initial_metadata(_common.metadata(metadata)),
          cygrpc.operation_receive_message(),
          cygrpc.operation_receive_status_on_client(),
      )
      call.start_batch(
          cygrpc.Operations(operations), other_operations_event_handler)
      _consume_request_iterator(
          request_iterator, state, call, self._request_serializer)
    return _Rendezvous(state, call, None, self._response_deserializer, deadline)


class _StreamStreamMultiCallable(grpc.StreamStreamMultiCallable):

  def __init__(
      self, channel, method, request_serializer, response_deserializer):
    self._channel = channel
    self._method = method
    self._request_serializer = request_serializer
    self._response_deserializer = response_deserializer

  def __call__(
      self, request_iterator, timeout=None, metadata=None, credentials=None):
    deadline, deadline_timespec = _deadline(timeout)
    state = _RPCState(_STREAM_STREAM_INITIAL_DUE, None, None)
    completion_queue = cygrpc.CompletionQueue()
    call = self._channel.create_call(
        None, 0, completion_queue, self._method, None, deadline_timespec)
    with state.condition:
      call.start_batch(
          cygrpc.Operations((cygrpc.operation_receive_initial_metadata(),)),
          None)
      operations = (
          cygrpc.operation_send_initial_metadata(_common.metadata(metadata)),
          cygrpc.operation_receive_status_on_client(),
      )
      call.start_batch(cygrpc.Operations(operations), None)
      _consume_request_iterator(
          request_iterator, state, call, self._request_serializer)
    return _Rendezvous(
        state, call, completion_queue, self._response_deserializer, deadline)


class _Channel(grpc.Channel):

  def __init__(self, address, args):
    self._lock = threading.Lock()
    self._channel = cygrpc.Channel(address)
    self._completion_queue = cygrpc.CompletionQueue()
    self._managed_calls = None

  def _spin(self):
    while True:
      event = self._completion_queue.poll()
      completed_call = event.tag(event)
      if completed_call:
        with self._lock:
          self._managed_calls.remove(completed_call)
          if not self._managed_calls:
            self._managed_calls = None
            return

  #INVARIANT: the returned call must conduct at least one operation
  def _create_channel_managed_call(self, flags, method, deadline):
    with self._lock:
      call = self._channel.create_call(
          None, flags, self._completion_queue, method, None, deadline)
      if self._managed_calls is None:
        self._managed_calls = set((call,))
        spin_thread = threading.Thread(target=self._spin)
        spin_thread.start()
      else:
        self._managed_calls.add(call)
      return call

  def subscribe(self, callback, try_to_connect=None):
    raise NotImplementedError

  def unsubscribe(self, callback):
    raise NotImplementedError

  def unary_unary(
      self, method, request_serializer=None, response_deserializer=None):
    return _UnaryUnaryMultiCallable(
        self._channel, self._create_channel_managed_call, method,
        request_serializer, response_deserializer)

  def unary_stream(
      self, method, request_serializer=None, response_deserializer=None):
    return _UnaryStreamMultiCallable(
        self._channel, method, request_serializer, response_deserializer)

  def stream_unary(
      self, method, request_serializer=None, response_deserializer=None):
    return _StreamUnaryMultiCallable(
        self._channel, self._create_channel_managed_call, method,
        request_serializer, response_deserializer)

  def stream_stream(
      self, method, request_serializer=None, response_deserializer=None):
    return _StreamStreamMultiCallable(
        self._channel, method, request_serializer, response_deserializer)


def insecure_channel(address, args):
  return _Channel(address, args)
