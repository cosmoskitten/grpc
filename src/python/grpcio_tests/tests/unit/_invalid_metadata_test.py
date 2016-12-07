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

"""Test of RPCs made against gRPC Python's application-layer API."""

import itertools
import threading
import unittest
from concurrent import futures

import grpc
from grpc.framework.foundation import logging_pool

from tests.unit.framework.common import test_constants
from tests.unit.framework.common import test_control

_SERIALIZE_REQUEST = lambda bytestring: bytestring * 2
_DESERIALIZE_REQUEST = lambda bytestring: bytestring[len(bytestring) // 2:]
_SERIALIZE_RESPONSE = lambda bytestring: bytestring * 3
_DESERIALIZE_RESPONSE = lambda bytestring: bytestring[:len(bytestring) // 3]

_UNARY_UNARY = '/test/UnaryUnary'
_UNARY_STREAM = '/test/UnaryStream'
_STREAM_UNARY = '/test/StreamUnary'
_STREAM_STREAM = '/test/StreamStream'


def _unary_unary_multi_callable(channel):
  return channel.unary_unary(_UNARY_UNARY)


def _unary_stream_multi_callable(channel):
  return channel.unary_stream(
      _UNARY_STREAM,
      request_serializer=_SERIALIZE_REQUEST,
      response_deserializer=_DESERIALIZE_RESPONSE)


def _stream_unary_multi_callable(channel):
  return channel.stream_unary(
      _STREAM_UNARY,
      request_serializer=_SERIALIZE_REQUEST,
      response_deserializer=_DESERIALIZE_RESPONSE)


def _stream_stream_multi_callable(channel):
  return channel.stream_stream(_STREAM_STREAM)


class RPCTest(unittest.TestCase):

  def setUp(self):
    self._channel = grpc.insecure_channel('localhost:8080')

  def testUnaryRequestBlockingUnaryResponse(self):
    request = b'\x07\x08'
    metadata = (('InVaLiD', 'UnaryRequestBlockingUnaryResponse'),)
    expected_error = ValueError("metadata was invalid: %s" % metadata)
    multi_callable = _unary_unary_multi_callable(self._channel)
    with self.assertRaises(ValueError) as cm:
      response = multi_callable(request, metadata=metadata)
    self.assertEqual(cm.exception.message, expected_error.message)

  def testUnaryRequestBlockingUnaryResponseWithCall(self):
    request = b'\x07\x08'
    metadata = (('InVaLiD', 'UnaryRequestBlockingUnaryResponseWithCall'),)
    expected_error = ValueError("metadata was invalid: %s" % metadata)
    multi_callable = _unary_unary_multi_callable(self._channel)
    with self.assertRaises(ValueError) as cm:
      response, call = multi_callable.with_call(request, metadata=metadata)
    self.assertEqual(cm.exception.message, expected_error.message)

  def testUnaryRequestFutureUnaryResponse(self):
    request = b'\x07\x08'
    metadata = (('InVaLiD', 'UnaryRequestFutureUnaryResponse'),)
    expected_error_details = "metadata was invalid: %s" % metadata
    multi_callable = _unary_unary_multi_callable(self._channel)
    with self.assertRaises(Exception) as cm:
      response_future = multi_callable.future(request, metadata=metadata)
      response = response_future.result()
    self.assertEqual(cm.exception.details(), expected_error_details)
    self.assertEqual(cm.exception.code(), grpc.StatusCode.INTERNAL)

  def testUnaryRequestStreamResponse(self):
    request = b'\x37\x58'
    metadata = (('InVaLiD', 'UnaryRequestStreamResponse'),)
    expected_error_details = "metadata was invalid: %s" % metadata
    multi_callable = _unary_stream_multi_callable(self._channel)
    with self.assertRaises(Exception) as cm:
      response_iterator = multi_callable(request, metadata=metadata)
      responses = tuple(response_iterator)
    self.assertEqual(cm.exception.details(), expected_error_details)
    self.assertEqual(cm.exception.code(), grpc.StatusCode.INTERNAL)

  def testStreamRequestBlockingUnaryResponse(self):
    requests = tuple(b'\x07\x08' for _ in range(test_constants.STREAM_LENGTH))
    request_iterator = iter(requests)
    metadata = (('InVaLiD', 'StreamRequestBlockingUnaryResponse'),)
    expected_error = ValueError("metadata was invalid: %s" % metadata)
    multi_callable = _stream_unary_multi_callable(self._channel)
    with self.assertRaises(ValueError) as cm:
      response = multi_callable(request_iterator, metadata=metadata)
    self.assertEqual(cm.exception.message, expected_error.message)  

  def testStreamRequestBlockingUnaryResponseWithCall(self):
    requests = tuple(b'\x07\x08' for _ in range(test_constants.STREAM_LENGTH))
    request_iterator = iter(requests)
    metadata = (('InVaLiD', 'StreamRequestBlockingUnaryResponseWithCall'),)
    expected_error = ValueError("metadata was invalid: %s" % metadata)
    multi_callable = _stream_unary_multi_callable(self._channel)
    with self.assertRaises(ValueError) as cm:
      response, call = multi_callable.with_call(
          request_iterator, metadata=metadata)
    self.assertEqual(cm.exception.message, expected_error.message)

  def testStreamRequestFutureUnaryResponse(self):
    requests = tuple(b'\x07\x08' for _ in range(test_constants.STREAM_LENGTH))
    request_iterator = iter(requests)
    metadata = (('InVaLiD', 'StreamRequestFutureUnaryResponse'),)
    expected_error_details = "metadata was invalid: %s" % metadata
    multi_callable = _stream_unary_multi_callable(self._channel)
    with self.assertRaises(Exception) as cm:
      response_future = multi_callable.future(
          request_iterator, metadata=metadata)
      response = response_future.result()
    self.assertEqual(cm.exception.details(), expected_error_details)
    self.assertEqual(cm.exception.code(), grpc.StatusCode.INTERNAL)

  def testStreamRequestStreamResponse(self):
    requests = tuple(b'\x77\x58' for _ in range(test_constants.STREAM_LENGTH))
    request_iterator = iter(requests)
    metadata = (('InVaLiD', 'StreamRequestStreamResponse'),)
    expected_error_details = "metadata was invalid: %s" % metadata
    multi_callable = _stream_stream_multi_callable(self._channel)
    with self.assertRaises(Exception) as cm:
      response_iterator = multi_callable(request_iterator, metadata=metadata)
      responses = tuple(response_iterator)
    self.assertEqual(cm.exception.details(), expected_error_details)
    self.assertEqual(cm.exception.code(), grpc.StatusCode.INTERNAL)    
    

if __name__ == '__main__':
  unittest.main(verbosity=2)
