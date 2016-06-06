// Generated by the protocol buffer compiler.  DO NOT EDIT!
// source: math.proto
// Original file comments:
// Copyright 2015, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
#region Designer generated code

using System;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core;

namespace Math {
  public static class Math
  {
    static readonly string __ServiceName = "math.Math";

    static readonly Marshaller<global::Math.DivArgs> __Marshaller_DivArgs = Marshallers.Create((arg) => global::Google.Protobuf.MessageExtensions.ToByteArray(arg), global::Math.DivArgs.Parser.ParseFrom);
    static readonly Marshaller<global::Math.DivReply> __Marshaller_DivReply = Marshallers.Create((arg) => global::Google.Protobuf.MessageExtensions.ToByteArray(arg), global::Math.DivReply.Parser.ParseFrom);
    static readonly Marshaller<global::Math.FibArgs> __Marshaller_FibArgs = Marshallers.Create((arg) => global::Google.Protobuf.MessageExtensions.ToByteArray(arg), global::Math.FibArgs.Parser.ParseFrom);
    static readonly Marshaller<global::Math.Num> __Marshaller_Num = Marshallers.Create((arg) => global::Google.Protobuf.MessageExtensions.ToByteArray(arg), global::Math.Num.Parser.ParseFrom);

    static readonly Method<global::Math.DivArgs, global::Math.DivReply> __Method_Div = new Method<global::Math.DivArgs, global::Math.DivReply>(
        MethodType.Unary,
        __ServiceName,
        "Div",
        __Marshaller_DivArgs,
        __Marshaller_DivReply);

    static readonly Method<global::Math.DivArgs, global::Math.DivReply> __Method_DivMany = new Method<global::Math.DivArgs, global::Math.DivReply>(
        MethodType.DuplexStreaming,
        __ServiceName,
        "DivMany",
        __Marshaller_DivArgs,
        __Marshaller_DivReply);

    static readonly Method<global::Math.FibArgs, global::Math.Num> __Method_Fib = new Method<global::Math.FibArgs, global::Math.Num>(
        MethodType.ServerStreaming,
        __ServiceName,
        "Fib",
        __Marshaller_FibArgs,
        __Marshaller_Num);

    static readonly Method<global::Math.Num, global::Math.Num> __Method_Sum = new Method<global::Math.Num, global::Math.Num>(
        MethodType.ClientStreaming,
        __ServiceName,
        "Sum",
        __Marshaller_Num,
        __Marshaller_Num);

    /// <summary>Service descriptor</summary>
    public static global::Google.Protobuf.Reflection.ServiceDescriptor Descriptor
    {
      get { return global::Math.MathReflection.Descriptor.Services[0]; }
    }

    /// <summary>Base class for server-side implementations of Math</summary>
    public abstract class MathBase
    {
      /// <summary>
      ///  Div divides args.dividend by args.divisor and returns the quotient and
      ///  remainder.
      /// </summary>
      public virtual global::System.Threading.Tasks.Task<global::Math.DivReply> Div(global::Math.DivArgs request, ServerCallContext context)
      {
        throw new RpcException(new Status(StatusCode.Unimplemented, ""));
      }

      /// <summary>
      ///  DivMany accepts an arbitrary number of division args from the client stream
      ///  and sends back the results in the reply stream.  The stream continues until
      ///  the client closes its end; the server does the same after sending all the
      ///  replies.  The stream ends immediately if either end aborts.
      /// </summary>
      public virtual global::System.Threading.Tasks.Task DivMany(IAsyncStreamReader<global::Math.DivArgs> requestStream, IServerStreamWriter<global::Math.DivReply> responseStream, ServerCallContext context)
      {
        throw new RpcException(new Status(StatusCode.Unimplemented, ""));
      }

      /// <summary>
      ///  Fib generates numbers in the Fibonacci sequence.  If args.limit > 0, Fib
      ///  generates up to limit numbers; otherwise it continues until the call is
      ///  canceled.  Unlike Fib above, Fib has no final FibReply.
      /// </summary>
      public virtual global::System.Threading.Tasks.Task Fib(global::Math.FibArgs request, IServerStreamWriter<global::Math.Num> responseStream, ServerCallContext context)
      {
        throw new RpcException(new Status(StatusCode.Unimplemented, ""));
      }

      /// <summary>
      ///  Sum sums a stream of numbers, returning the final result once the stream
      ///  is closed.
      /// </summary>
      public virtual global::System.Threading.Tasks.Task<global::Math.Num> Sum(IAsyncStreamReader<global::Math.Num> requestStream, ServerCallContext context)
      {
        throw new RpcException(new Status(StatusCode.Unimplemented, ""));
      }

    }

    /// <summary>Client for Math</summary>
    public class MathClient : ClientBase<MathClient>
    {
      public MathClient(Channel channel) : base(channel)
      {
      }
      public MathClient(CallInvoker callInvoker) : base(callInvoker)
      {
      }
      ///<summary>Protected parameterless constructor to allow creation of test doubles.</summary>
      protected MathClient() : base()
      {
      }
      ///<summary>Protected constructor to allow creation of configured clients.</summary>
      protected MathClient(ClientBaseConfiguration configuration) : base(configuration)
      {
      }

      /// <summary>
      ///  Div divides args.dividend by args.divisor and returns the quotient and
      ///  remainder.
      /// </summary>
      public virtual global::Math.DivReply Div(global::Math.DivArgs request, Metadata headers = null, DateTime? deadline = null, CancellationToken cancellationToken = default(CancellationToken))
      {
        return Div(request, new CallOptions(headers, deadline, cancellationToken));
      }
      /// <summary>
      ///  Div divides args.dividend by args.divisor and returns the quotient and
      ///  remainder.
      /// </summary>
      public virtual global::Math.DivReply Div(global::Math.DivArgs request, CallOptions options)
      {
        return CallInvoker.BlockingUnaryCall(__Method_Div, null, options, request);
      }
      /// <summary>
      ///  Div divides args.dividend by args.divisor and returns the quotient and
      ///  remainder.
      /// </summary>
      public virtual AsyncUnaryCall<global::Math.DivReply> DivAsync(global::Math.DivArgs request, Metadata headers = null, DateTime? deadline = null, CancellationToken cancellationToken = default(CancellationToken))
      {
        return DivAsync(request, new CallOptions(headers, deadline, cancellationToken));
      }
      /// <summary>
      ///  Div divides args.dividend by args.divisor and returns the quotient and
      ///  remainder.
      /// </summary>
      public virtual AsyncUnaryCall<global::Math.DivReply> DivAsync(global::Math.DivArgs request, CallOptions options)
      {
        return CallInvoker.AsyncUnaryCall(__Method_Div, null, options, request);
      }
      /// <summary>
      ///  DivMany accepts an arbitrary number of division args from the client stream
      ///  and sends back the results in the reply stream.  The stream continues until
      ///  the client closes its end; the server does the same after sending all the
      ///  replies.  The stream ends immediately if either end aborts.
      /// </summary>
      public virtual AsyncDuplexStreamingCall<global::Math.DivArgs, global::Math.DivReply> DivMany(Metadata headers = null, DateTime? deadline = null, CancellationToken cancellationToken = default(CancellationToken))
      {
        return DivMany(new CallOptions(headers, deadline, cancellationToken));
      }
      /// <summary>
      ///  DivMany accepts an arbitrary number of division args from the client stream
      ///  and sends back the results in the reply stream.  The stream continues until
      ///  the client closes its end; the server does the same after sending all the
      ///  replies.  The stream ends immediately if either end aborts.
      /// </summary>
      public virtual AsyncDuplexStreamingCall<global::Math.DivArgs, global::Math.DivReply> DivMany(CallOptions options)
      {
        return CallInvoker.AsyncDuplexStreamingCall(__Method_DivMany, null, options);
      }
      /// <summary>
      ///  Fib generates numbers in the Fibonacci sequence.  If args.limit > 0, Fib
      ///  generates up to limit numbers; otherwise it continues until the call is
      ///  canceled.  Unlike Fib above, Fib has no final FibReply.
      /// </summary>
      public virtual AsyncServerStreamingCall<global::Math.Num> Fib(global::Math.FibArgs request, Metadata headers = null, DateTime? deadline = null, CancellationToken cancellationToken = default(CancellationToken))
      {
        return Fib(request, new CallOptions(headers, deadline, cancellationToken));
      }
      /// <summary>
      ///  Fib generates numbers in the Fibonacci sequence.  If args.limit > 0, Fib
      ///  generates up to limit numbers; otherwise it continues until the call is
      ///  canceled.  Unlike Fib above, Fib has no final FibReply.
      /// </summary>
      public virtual AsyncServerStreamingCall<global::Math.Num> Fib(global::Math.FibArgs request, CallOptions options)
      {
        return CallInvoker.AsyncServerStreamingCall(__Method_Fib, null, options, request);
      }
      /// <summary>
      ///  Sum sums a stream of numbers, returning the final result once the stream
      ///  is closed.
      /// </summary>
      public virtual AsyncClientStreamingCall<global::Math.Num, global::Math.Num> Sum(Metadata headers = null, DateTime? deadline = null, CancellationToken cancellationToken = default(CancellationToken))
      {
        return Sum(new CallOptions(headers, deadline, cancellationToken));
      }
      /// <summary>
      ///  Sum sums a stream of numbers, returning the final result once the stream
      ///  is closed.
      /// </summary>
      public virtual AsyncClientStreamingCall<global::Math.Num, global::Math.Num> Sum(CallOptions options)
      {
        return CallInvoker.AsyncClientStreamingCall(__Method_Sum, null, options);
      }
      protected override MathClient NewInstance(ClientBaseConfiguration configuration)
      {
        return new MathClient(configuration);
      }
    }

    /// <summary>Creates a new client for Math</summary>
    public static MathClient NewClient(Channel channel)
    {
      return new MathClient(channel);
    }

    /// <summary>Creates service definition that can be registered with a server</summary>
    public static ServerServiceDefinition BindService(MathBase serviceImpl)
    {
      return ServerServiceDefinition.CreateBuilder()
          .AddMethod(__Method_Div, serviceImpl.Div)
          .AddMethod(__Method_DivMany, serviceImpl.DivMany)
          .AddMethod(__Method_Fib, serviceImpl.Fib)
          .AddMethod(__Method_Sum, serviceImpl.Sum).Build();
    }

  }
}
#endregion
