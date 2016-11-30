import grpc
from grpc.framework.common import cardinality
from grpc.framework.interfaces.face import utilities as face_utilities

import grpc_reflection.v1alpha.reflection_pb2 as grpc__reflection_dot_v1alpha_dot_reflection__pb2


class ServerReflectionStub(object):

  def __init__(self, channel):
    """Constructor.

    Args:
      channel: A grpc.Channel.
    """
    self.ServerReflectionInfo = channel.stream_stream(
        '/grpc.reflection.v1alpha.ServerReflection/ServerReflectionInfo',
        request_serializer=grpc__reflection_dot_v1alpha_dot_reflection__pb2.ServerReflectionRequest.SerializeToString,
        response_deserializer=grpc__reflection_dot_v1alpha_dot_reflection__pb2.ServerReflectionResponse.FromString,
        )


class ServerReflectionServicer(object):

  def ServerReflectionInfo(self, request_iterator, context):
    """The reflection service is structured as a bidirectional stream, ensuring
    all related requests go to a single server.
    """
    context.set_code(grpc.StatusCode.UNIMPLEMENTED)
    context.set_details('Method not implemented!')
    raise NotImplementedError('Method not implemented!')


def add_ServerReflectionServicer_to_server(servicer, server):
  rpc_method_handlers = {
      'ServerReflectionInfo': grpc.stream_stream_rpc_method_handler(
          servicer.ServerReflectionInfo,
          request_deserializer=grpc__reflection_dot_v1alpha_dot_reflection__pb2.ServerReflectionRequest.FromString,
          response_serializer=grpc__reflection_dot_v1alpha_dot_reflection__pb2.ServerReflectionResponse.SerializeToString,
      ),
  }
  generic_handler = grpc.method_handlers_generic_handler(
      'grpc.reflection.v1alpha.ServerReflection', rpc_method_handlers)
  server.add_generic_rpc_handlers((generic_handler,))