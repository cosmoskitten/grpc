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

import collections
from concurrent import futures
import contextlib
import distutils.spawn
import errno
import importlib
import os
import os.path
import pkgutil
import shutil
import subprocess
import sys
import tempfile
import threading
import unittest

from six import moves

import grpc
from grpc.tools import protoc
from tests.unit.framework.common import test_constants

@contextlib.contextmanager
def _system_path(path):
  old_system_path = sys.path
  sys.path = path
  yield
  sys.path = old_system_path


class DummySplitServicer(object):

  def __init__(self, request_class, response_class):
    self.request_class = request_class
    self.response_class = response_class

  def Call(self, request, context):
    return self.response_class()

class SplitDefinitionsTest(unittest.TestCase):

  def setUp(self):
    split_test_proto_contents = pkgutil.get_data(
        'tests.protoc_plugin.protos.split', 'split_test.proto')
    self.directory = tempfile.mkdtemp()
    self.proto_directory = os.path.join(self.directory, 'proto_path')
    self.python_out_directory = os.path.join(self.directory, 'python_out')
    self.grpc_python_out_directory = os.path.join(self.directory, 'grpc_python_out')
    os.makedirs(self.proto_directory)
    os.makedirs(self.python_out_directory)
    os.makedirs(self.grpc_python_out_directory)
    split_test_proto_file = os.path.join(self.proto_directory, 'split_test.proto')
    open(split_test_proto_file, 'w').write(split_test_proto_contents)
    protoc_result = protoc.main([
        '',
        '--proto_path={}'.format(self.proto_directory),
        '--python_out={}'.format(self.python_out_directory),
        '--grpc_python_out={}'.format(self.grpc_python_out_directory),
        split_test_proto_file,
    ])
    if protoc_result != 0:
      raise Exception("unexpected protoc error")
    open(os.path.join(self.grpc_python_out_directory, '__init__.py'), 'w').write('')
    open(os.path.join(self.python_out_directory, '__init__.py'), 'w').write('')

  def tearDown(self):
    shutil.rmtree(self.directory)

  def testImportAttributes(self):
    with _system_path([self.python_out_directory]):
      import split_test_pb2
    split_test_pb2.Request
    split_test_pb2.Response
    with self.assertRaises(AttributeError):
      split_test_pb2.TestServiceServicer

    with _system_path([self.grpc_python_out_directory]):
      import split_test_pb2_grpc
    split_test_pb2_grpc.TestServiceServicer
    with self.assertRaises(AttributeError):
      split_test_pb2_grpc.Request
    with self.assertRaises(AttributeError):
      split_test_pb2_grpc.Response

  def testCall(self):
    with _system_path([self.python_out_directory]):
      import split_test_pb2
    with _system_path([self.grpc_python_out_directory]):
      import split_test_pb2_grpc
    server = grpc.server(
        futures.ThreadPoolExecutor(max_workers=test_constants.POOL_SIZE))
    split_test_pb2_grpc.add_TestServiceServicer_to_server(
        DummySplitServicer(
            split_test_pb2.Request, split_test_pb2.Response), server)
    port = server.add_insecure_port('[::]:0')
    server.start()
    channel = grpc.insecure_channel('localhost:{}'.format(port))
    stub = split_test_pb2_grpc.TestServiceStub(channel)
    request = split_test_pb2.Request()
    expected_response = split_test_pb2.Response()
    response = stub.Call(request)
    self.assertEqual(expected_response, response)


if __name__ == '__main__':
  unittest.main(verbosity=2)
