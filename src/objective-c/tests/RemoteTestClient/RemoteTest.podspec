Pod::Spec.new do |s|
  s.name     = "RemoteTest"
  s.version  = "0.0.1"
  s.license  = "New BSD"

  s.ios.deployment_target = '7.1'
  s.osx.deployment_target = '10.9'

  # Run protoc with the Objective-C and gRPC plugins to generate protocol messages and gRPC clients.
  s.prepare_command = <<-CMD
    ROOT=../../../..
    BINDIR=$ROOT/bins/$CONFIG
    PROTOC=$BINDIR/protobuf/protoc
    PLUGIN=$BINDIR/grpc_objective_c_plugin
    $PROTOC --plugin=protoc-gen-grpc=$PLUGIN --objc_out=. --grpc_out=. $ROOT/src/proto/grpc/testing/test.proto -I $ROOT
    $PROTOC --plugin=protoc-gen-grpc=$PLUGIN --objc_out=. --grpc_out=. $ROOT/src/proto/grpc/testing/messages.proto -I $ROOT
    $PROTOC --plugin=protoc-gen-grpc=$PLUGIN --objc_out=. --grpc_out=. $ROOT/src/proto/grpc/testing/empty.proto -I $ROOT
  CMD

  s.subspec "Messages" do |ms|
    ms.source_files = "src/proto/grpc/testing/*.pbobjc.{h,m}"
    ms.header_mappings_dir = "."
    ms.requires_arc = false
    ms.dependency "Protobuf", "~> 3.0.0-alpha-4"
  end

  s.subspec "Services" do |ss|
    ss.source_files = "src/proto/grpc/testing/*.pbrpc.{h,m}"
    ss.header_mappings_dir = "."
    ss.requires_arc = true
    ss.dependency "gRPC", "~> 0.12"
    ss.dependency "#{s.name}/Messages"
  end
end
