# Copyright 2015, Google Inc.
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

import abc

import six


class FutureTimeoutError(Exception):
  """Indicates that a method call on a Future timed out."""


class FutureCancelledError(Exception):
  """Indicates that the computation underlying a Future was cancelled."""


class Future(six.with_metaclass(abc.ABCMeta)):
  """A representation of a computation in another control flow.

  Computations represented by a Future may be yet to be begun, may be ongoing,
  or may have already completed.
  """

  @abc.abstractmethod
  def cancel(self):
    """Attempts to cancel the computation.

    This method does not block.

    Returns:
      True if the computation has not yet begun, will not be allowed to take
        place, and determination of both was possible without blocking. False
        under all other circumstances including but not limited to the
        computation's already having begun, the computation's already having
        finished, and the computation's having been scheduled for execution on a
        remote system for which a determination of whether or not it commenced
        before being cancelled cannot be made without blocking.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def cancelled(self):
    """Describes whether the computation was cancelled.

    This method does not block.

    Returns:
      True if the computation was cancelled any time before its result became
        immediately available. False under all other circumstances including but
        not limited to this object's cancel method not having been called and
        the computation's result having become immediately available.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def running(self):
    """Describes whether the computation is taking place.

    This method does not block.

    Returns:
      True if the computation is scheduled to take place in the future or is
        taking place now, or False if the computation took place in the past or
        was cancelled.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def done(self):
    """Describes whether the computation has taken place.

    This method does not block.

    Returns:
      True if the computation is known to have either completed or have been
        unscheduled or interrupted. False if the computation may possibly be
        executing or scheduled to execute later.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def result(self, timeout=None):
    """Accesses the outcome of the computation or raises its exception.

    This method may return immediately or may block.

    Args:
      timeout: The length of time in seconds to wait for the computation to
        finish or be cancelled, or None if this method should block until the
        computation has finished or is cancelled no matter how long that takes.

    Returns:
      The return value of the computation.

    Raises:
      TimeoutError: If a timeout value is passed and the computation does not
        terminate within the allotted time.
      CancelledError: If the computation was cancelled.
      Exception: If the computation raised an exception, this call will raise
        the same exception.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def exception(self, timeout=None):
    """Return the exception raised by the computation.

    This method may return immediately or may block.

    Args:
      timeout: The length of time in seconds to wait for the computation to
        terminate or be cancelled, or None if this method should block until
        the computation is terminated or is cancelled no matter how long that
        takes.

    Returns:
      The exception raised by the computation, or None if the computation did
        not raise an exception.

    Raises:
      TimeoutError: If a timeout value is passed and the computation does not
        terminate within the allotted time.
      CancelledError: If the computation was cancelled.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def traceback(self, timeout=None):
    """Access the traceback of the exception raised by the computation.

    This method may return immediately or may block.

    Args:
      timeout: The length of time in seconds to wait for the computation to
        terminate or be cancelled, or None if this method should block until
        the computation is terminated or is cancelled no matter how long that
        takes.

    Returns:
      The traceback of the exception raised by the computation, or None if the
        computation did not raise an exception.

    Raises:
      TimeoutError: If a timeout value is passed and the computation does not
        terminate within the allotted time.
      CancelledError: If the computation was cancelled.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def add_done_callback(self, fn):
    """Adds a function to be called at completion of the computation.

    The callback will be passed this Future object describing the outcome of
    the computation.

    If the computation has already completed, the callback will be called
    immediately.

    Args:
      fn: A callable taking a this Future object as its single parameter.
    """
    raise NotImplementedError()


class RpcError(Exception):
  """"""


class RpcContext(six.with_metaclass(abc.ABCMeta)):
  """Provides RPC-related information and action."""

  @abc.abstractmethod
  def is_active(self):
    """Describes whether the RPC is active or has terminated."""
    raise NotImplementedError()

  @abc.abstractmethod
  def time_remaining(self):
    """Describes the length of allowed time remaining for the RPC.

    Returns:
      A nonnegative float indicating the length of allowed time in seconds
      remaining for the RPC to complete before it is considered to have timed
      out.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def cancel(self):
    """Cancels the RPC.

    Idempotent and has no effect if the RPC has already terminated.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def disable_next_message_compression(self):
    """Disables compression of the next message passed by the application."""
    raise NotImplementedError()


class Call(six.with_metaclass(abc.ABCMeta, RpcContext)):
  """Invocation-side utility object for an RPC."""

  @abc.abstractmethod
  def initial_metadata(self):
    """Accesses the initial metadata from the service-side of the RPC.

    This method blocks until the value is available.

    Returns:
      The initial metadata as a sequence of pairs of bytes.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def terminal_metadata(self):
    """Accesses the terminal metadata from the service-side of the RPC.

    This method blocks until the value is available.

    Returns:
      The terminal metadata as a sequence of pairs of bytes.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def code(self):
    """Accesses the status code emitted by the service-side of the RPC.

    This method blocks until the value is available.

    Returns:
      The integer status code of the RPC.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def details(self):
    """Accesses the details value emitted by the service-side of the RPC.

    This method blocks until the value is available.

    Returns:
      The bytes of the details of the RPC.
    """
    raise NotImplementedError()


class ServicerContext(six.with_metaclass(abc.ABCMeta, RpcContext)):
  """A context object passed to method implementations."""

  @abc.abstractmethod
  def invocation_metadata(self):
    """Accesses the metadata from the invocation-side of the RPC.

    Returns:
      The invocation metadata object as a sequence of pairs of bytes.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def initial_metadata(self, initial_metadata):
    """Accepts the initial metadata value of the RPC.

    This method need not be called by method implementations if they have no
    service-side initial metadata to transmit.

    Args:
      initial_metadata: The initial metadata of the RPC as a sequence of pairs
        of bytes.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def terminal_metadata(self, terminal_metadata):
    """Accepts the terminal metadata value of the RPC.

    This method need not be called by method implementations if they have no
    service-side terminal metadata to transmit.

    Args:
      terminal_metadata: The terminal metadata of the RPC as a sequence of pairs
        of bytes.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def code(self, code):
    """Accepts the status code of the RPC.

    This method need not be called by method implementations if they wish the
    gRPC runtime to determine the status code of the RPC.

    Args:
      code: The integer status code of the RPC to be transmitted to the
        invocation side of the RPC.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def details(self, details):
    """Accepts the service-side details of the RPC.

    This method need not be called by method implementations if they have no
    details to transmit.

    Args:
      details: The details bytes of the RPC to be transmitted to
        the invocation side of the RPC.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def peer(self):
    """Identifies the peer that invoked the RPC being serviced.

    Returns:
      A string identifying the peer that invoked the RPC being serviced.
    """
    raise NotImplementedError()


class UnaryUnaryMultiCallable(six.with_metaclass(abc.ABCMeta)):
  """Affords invoking a unary-unary RPC."""

  @abc.abstractmethod
  def __call__(
      self, request, timeout=None, metadata=None, disable_compression=False,
      subcall_of=None, credentials=None, with_call=False):
    """Synchronously invokes the underlying RPC.

    Args:
      request: The request value for the RPC.
      timeout: An optional duration of time in seconds to allow for the RPC.
      metadata: An optional sequence of pairs of bytes to be transmitted to the
        service-side of the RPC.
      disable_compression: Whether or not to disable compression of the request.
      subcall_of: ¯\_(ツ)_/¯ A ServicerContext I guess?
      credentials: ¯\_(ツ)_/¯ Per-call credentials of some type?
      with_call: Whether or not to include return a Call for the RPC in addition
        to the response.

    Returns:
      The response value for the RPC, and a Call for the RPC if with_call was
        set to True at invocation.

    Raises:
      RpcError: Indicating that the RPC terminated abnormally. The raised
        RpcError will also be a Call for the RPC affording the RPC's metadata,
        status code, and details.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def future(
      self, request, timeout=None, metadata=None, disable_compression=False,
      subcall_of=None, credentials=None):
    """Asynchronously invokes the underlying RPC.

    Args:
      request: The request value for the RPC.
      timeout: An optional duration of time in seconds to allow for the RPC.
      metadata: An optional sequence of pairs of bytes to be transmitted to the
        service-side of the RPC.
      disable_compression: Whether or not to disable compression of the request.
      subcall_of: ¯\_(ツ)_/¯ A ServicerContext I guess?
      credentials: ¯\_(ツ)_/¯ Per-call credentials of some type?

    Returns:
      An object that is both a Call for the RPC and a Future. In the event of
        RPC completion, the return Future's result value will be the response
        message of the RPC. In the event of RPC abortion, the returned
        Future's exception value will be an RpcError.
    """
    raise NotImplementedError()


class UnaryStreamMultiCallable(six.with_metaclass(abc.ABCMeta)):
  """Affords invoking a unary-stream RPC."""

  @abc.abstractmethod
  def __call__(
      self, request, timeout=None, metadata=None, disable_compression=False,
      subcall_of=None, credentials=None):
    """Invokes the underlying RPC.

    Args:
      request: The request value for the RPC.
      timeout: An optional duration of time in seconds to allow for the RPC.
      metadata: An optional sequence of pairs of bytes to be transmitted to the
        service-side of the RPC.
      disable_compression: Whether or not to disable compression of the request.
      subcall_of: ¯\_(ツ)_/¯ A ServicerContext I guess?
      credentials: ¯\_(ツ)_/¯ Per-call credentials of some type?

    Returns:
      An object that is both a Call for the RPC and an iterator of response
        values. Drawing response values from the returned iterator may raise
        RpcError indicating abnormal termination of the RPC.
    """
    raise NotImplementedError()


class StreamUnaryMultiCallable(six.with_metaclass(abc.ABCMeta)):
  """Affords invoking a stream-unary RPC in any call style."""

  @abc.abstractmethod
  def __call__(
      self, request_iterator, timeout=None, metadata=None,
      disable_compression=False, subcall_of=None, credentials=None,
      with_call=False):
    """Synchronously invokes the underlying RPC.

    Args:
      request_iterator: An iterator that yields request values for the RPC.
      timeout: An optional duration of time in seconds to allow for the RPC.
      metadata: An optional sequence of pairs of bytes to be transmitted to the
        service-side of the RPC.
      disable_compression: Whether or not to disable compression of the request.
      subcall_of: ¯\_(ツ)_/¯ A ServicerContext I guess?
      credentials: ¯\_(ツ)_/¯ Per-call credentials of some type?
      with_call: Whether or not to include return a Call for the RPC in addition
        to the response.

    Returns:
      The response value for the RPC, and a Call for the RPC if with_call was
        set to True at invocation.

    Raises:
      RpcError: Indicating that the RPC terminated abnormally. The raised
        RpcError will also be a Call for the RPC affording the RPC's metadata,
        status code, and details.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def future(
      self, request_iterator, timeout=None, metadata=None,
      disable_compression=False, subcall_of=None, credentials=None):
    """Asynchronously invokes the underlying RPC.

    Args:
      request_iterator: An iterator that yields request values for the RPC.
      timeout: An optional duration of time in seconds to allow for the RPC.
      metadata: An optional sequence of pairs of bytes to be transmitted to the
        service-side of the RPC.
      disable_compression: Whether or not to disable compression of the request.
      subcall_of: ¯\_(ツ)_/¯ A ServicerContext I guess?
      credentials: ¯\_(ツ)_/¯ Per-call credentials of some type?

    Returns:
      An object that is both a Call for the RPC and a Future. In the event of
        RPC completion, the return Future's result value will be the response
        message of the RPC. In the event of RPC abortion, the returned
        Future's exception value will be an RpcError.
    """
    raise NotImplementedError()


class StreamStreamMultiCallable(six.with_metaclass(abc.ABCMeta)):
  """Affords invoking a stream-stream RPC in any call style."""

  @abc.abstractmethod
  def __call__(
      self, request_iterator, timeout=None, metadata=None,
      disable_compression=False, subcall_of=None, credentials=None):
    """Invokes the underlying RPC.

    Args:
      request_iterator: An iterator that yields request values for the RPC.
      timeout: An optional duration of time in seconds to allow for the RPC.
      metadata: An optional sequence of pairs of bytes to be transmitted to the
        service-side of the RPC.
      disable_compression: Whether or not to disable compression of the request.
      subcall_of: ¯\_(ツ)_/¯ A ServicerContext I guess?
      credentials: ¯\_(ツ)_/¯ Per-call credentials of some type?

    Returns:
      An object that is both a Call for the RPC and an iterator of response
        values. Drawing response values from the returned iterator may raise
        RpcError indicating abnormal termination of the RPC.
    """
    raise NotImplementedError()


class Channel(six.with_metaclass(abc.ABCMeta)):
  """Affords RPC invocation via generic methods."""

  @abc.abstractmethod
  def subscribe(self, callback, try_to_connect=None):
    """Subscribes to this Channel's connectivity.

    Args:
      callback: A callable to be invoked and passed an integer value describing
        this Channel's connectivity. The callable will be invoked immediately
        upon subscription and again for every change to this Channel's
        connectivity thereafter until it is unsubscribed.
      try_to_connect: A boolean indicating whether or not this Channel should
        attempt to connect if it is not already connected and ready to conduct
        RPCs.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def unsubscribe(self, callback):
    """Unsubscribes a callback from this Channel's connectivity.

    Args:
      callback: A callable previously registered with this Channel from having
        been passed to its "subscribe" method.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def blocking_unary_unary(
      self, method, request, timeout=None, metadata=None,
      disable_compression=False, subcall_of=None, credentials=None,
      with_call=False):
    """"""
    raise NotImplementedError()

  @abc.abstractmethod
  def future_unary_unary(
      self, method, request, timeout=None, metadata=None,
      disable_compression=False, subcall_of=None, credentials=None):
    """"""
    raise NotImplementedError()

  @abc.abstractmethod
  def inline_unary_stream(
      self, method, request, timeout=None, metadata=None,
      disable_compression=False, subcall_of=None, credentials=None):
    """"""
    raise NotImplementedError()

  @abc.abstractmethod
  def blocking_stream_unary(
      self, method, request_iterator, timeout=None, metadata=None,
      disable_compression=False, subcall_of=None, credentials=None,
      with_call=False):
    """"""
    raise NotImplementedError()

  @abc.abstractmethod
  def future_stream_unary(
      self, method, request_iterator, timeout=None, metadata=None,
      disable_compression=False, subcall_of=None, credentials=None):
    """"""
    raise NotImplementedError()

  @abc.abstractmethod
  def inline_stream_stream(
      self, method, request_iterator, timeout=None, metadata=None,
      disable_compression=False, subcall_of=None, credentials=None):
    """"""
    raise NotImplementedError()

  @abc.abstractmethod
  def unary_unary(self, method):
    """Creates a UnaryUnaryMultiCallable for a unary-unary method.

    Args:
      method: The method identifier of the RPC.

    Returns:
      A UnaryUnaryMultiCallable value for the named unary-unary method.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def unary_stream(self, method):
    """Creates a UnaryStreamMultiCallable for a unary-stream method.

    Args:
      method: The method identifier of the RPC.

    Returns:
      A UnaryStreamMultiCallable value for the name unary-stream method.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def stream_unary(self, method):
    """Creates a StreamUnaryMultiCallable for a stream-unary method.

    Args:
      method: The method identifier of the RPC.

    Returns:
      A StreamUnaryMultiCallable value for the named stream-unary method.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def stream_stream(self, method):
    """Creates a StreamStreamMultiCallable for a stream-stream method.

    Args:
      method: The method identifier of the RPC.

    Returns:
      A StreamStreamMultiCallable value for the named stream-stream method.
    """
    raise NotImplementedError()


class Server(six.with_metaclass(abc.ABCMeta)):
  """Services RPCs."""

  @abc.abstractmethod
  def add_service(self, service):
    """Something something https://github.com/grpc/grpc/issues/4418?"""
    raise NotImplementedError()

  @abc.abstractmethod
  def add_insecure_port(self, address):
    """Reserves a port for insecure RPC service once this Server becomes active.

    This method may only be called before calling this Server's start method is
    called.

    Args:
      address: The address for which to open a port.

    Returns:
      An integer port on which RPCs will be serviced after this link has been
        started. This is typically the same number as the port number contained
        in the passed address, but will likely be different if the port number
        contained in the passed address was zero.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def start(self):
    """Starts this Server's service of RPCs.

    This method may only be called while the server is not serving RPCs (i.e. it
    is not idempotent).
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def stop(self, grace):
    """Stops this Server's service of RPCs.

    All calls to this method immediately stop service of new RPCs. When existing
    RPCs are aborted is controlled by the grace period parameter passed to this
    method.

    This method may be called at any time and is idempotent. Passing a smaller
    grace value than has been passed in a previous call will have the effect of
    stopping the Server sooner. Passing a larger grace value than has been
    passed in a previous call will not have the effect of stopping the sooner
    later.

    Args:
      grace: A duration of time in seconds to allow existing RPCs to complete
        before being aborted by this Server's stopping. May be zero for
        immediate abortion of all in-progress RPCs.

    Returns:
      A threading.Event that will be set when this Server has completely
      stopped. The returned event may not be set until after the full grace
      period (if some ongoing RPC continues for the full length of the period)
      of it may be set much sooner (such as if this Server had no RPCs underway
      at the time it was stopped or if all RPCs that it had underway completed
      very early in the grace period).
    """
    raise NotImplementedError()


def channel_ready_future(channel):
  """Creates a Future tracking when a Channel is ready.

  Cancelling the returned Future does not tell the given
  Channel to abandon attempts it may have been making to
  connect; cancelling merely deactivates the return Future's
  subscription to the given Channel's connectivity.

  Args:
    channel: A Channel.

  Returns:
    A Future that matures when the given Channel has connectivity ¯\_(ツ)_/¯ ("2"?)
  """
  raise NotImplementedError


def insecure_channel(address, options):
  """Creates an insecure Channel to a server.

  Args:
    address: ¯\_(ツ)_/¯ Should this be named "target"?
    options: ¯\_(ツ)_/¯

  Returns:
    A Channel to the remote host through which RPCs may be conducted.
  """
  raise NotImplementedError


def server(service_implementations, options=None):
  """Creates a Server with which RPCs can be serviced.

  Args:
    service_implementations: ¯\_(ツ)_/¯
    options: ¯\_(ツ)_/¯ (thread pool?)

  Returns:
    A Server with which RPCs can be serviced.
  """
  raise NotImplementedError
