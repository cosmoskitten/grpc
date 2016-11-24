﻿#region Copyright notice and license
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
#endregion

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

using Grpc.Core;
using Grpc.Core.Utils;
using Grpc.Reflection.V1Alpha;
using Google.Protobuf.Reflection;

namespace Grpc.Reflection
{
    /// <summary>
    /// Implementation of server reflection service.
    /// </summary>
    public class ReflectionServiceImpl : Grpc.Reflection.V1Alpha.ServerReflection.ServerReflectionBase
    {
        readonly List<string> services;
        readonly SymbolRegistry symbolRegistry;

        /// <summary>
        /// Creates a new instance of <c>ReflectionServiceIml</c>.
        /// </summary>
        public ReflectionServiceImpl(IEnumerable<string> services, SymbolRegistry symbolRegistry)
        {
            this.services = new List<string>(services);
            this.symbolRegistry = symbolRegistry;
        }

        /// <summary>
        /// Creates a new instance of <c>ReflectionServiceIml</c>.
        /// </summary>
        public ReflectionServiceImpl(IEnumerable<ServiceDescriptor> serviceDescriptors)
        {
            this.services = new List<string>(serviceDescriptors.Select((serviceDescriptor) => serviceDescriptor.FullName));
            this.symbolRegistry = SymbolRegistry.FromFiles(serviceDescriptors.Select((serviceDescriptor) => serviceDescriptor.File));
        }

        /// <summary>
        /// Creates a new instance of <c>ReflectionServiceIml</c>.
        /// </summary>
        public ReflectionServiceImpl(params ServiceDescriptor[] serviceDescriptors) : this((IEnumerable<ServiceDescriptor>) serviceDescriptors)
        {
        }

        public override async Task ServerReflectionInfo(IAsyncStreamReader<ServerReflectionRequest> requestStream, IServerStreamWriter<ServerReflectionResponse> responseStream, ServerCallContext context)
        {
            while (await requestStream.MoveNext())
            {
                var response = ProcessRequest(requestStream.Current);
                await responseStream.WriteAsync(response);
            }
        }

        ServerReflectionResponse ProcessRequest(ServerReflectionRequest request)
        {
            switch (request.MessageRequestCase)
            {
                case ServerReflectionRequest.MessageRequestOneofCase.FileByFilename:
                    return FileByFilename(request.FileByFilename);
                case ServerReflectionRequest.MessageRequestOneofCase.FileContainingSymbol:
                    return FileContainingSymbol(request.FileContainingSymbol);
                case ServerReflectionRequest.MessageRequestOneofCase.ListServices:
                    return ListServices();
                case ServerReflectionRequest.MessageRequestOneofCase.AllExtensionNumbersOfType:
                case ServerReflectionRequest.MessageRequestOneofCase.FileContainingExtension:
                default:
                    return CreateErrorResponse(StatusCode.Unimplemented, "Request type not supported by C# reflection service.");
            }
        }

        ServerReflectionResponse FileByFilename(string filename)
        {
            FileDescriptor file = symbolRegistry.FileByName(filename);
            if (file == null)
            {
                // TODO(jtattermusch): what is the right response when file is not found?
                return CreateErrorResponse(StatusCode.NotFound, "File descriptor not found.");
            }

            var transitiveDependencies = new HashSet<FileDescriptor>();
            CollectTransitiveDependencies(file, transitiveDependencies);

            return new ServerReflectionResponse
            {
                FileDescriptorResponse = new FileDescriptorResponse { FileDescriptorProto = { transitiveDependencies.Select((d) => d.SerializedData) } }
            };
        }

        ServerReflectionResponse FileContainingSymbol(string symbol)
        {
            FileDescriptor file = symbolRegistry.FileContainingSymbol(symbol);
            if (file == null)
            {
                // TODO(jtattermusch): what is the right response when symbol is not found?
                return CreateErrorResponse(StatusCode.NotFound, "Symbol not found.");
            }

            var transitiveDependencies = new HashSet<FileDescriptor>();
            CollectTransitiveDependencies(file, transitiveDependencies);

            return new ServerReflectionResponse
            {
                FileDescriptorResponse = new FileDescriptorResponse { FileDescriptorProto = { transitiveDependencies.Select((d) => d.SerializedData) } }
            };
        }

        ServerReflectionResponse ListServices()
        {
            var serviceResponses = new ListServiceResponse();
            foreach (string serviceName in services)
            {
                serviceResponses.Service.Add(new ServiceResponse { Name = serviceName });
            }

            return new ServerReflectionResponse
            {
                ListServicesResponse = serviceResponses
            };
        }

        ServerReflectionResponse CreateErrorResponse(StatusCode status, string message)
        {
            return new ServerReflectionResponse
            {
                ErrorResponse = new ErrorResponse { ErrorCode = (int) status, ErrorMessage = message }
            };
        }

        void CollectTransitiveDependencies(FileDescriptor descriptor, HashSet<FileDescriptor> pool)
        {
            pool.Add(descriptor);
            foreach (var dependency in descriptor.Dependencies)
            {
                if (pool.Add(dependency))
                {
                    CollectTransitiveDependencies(dependency, pool);
                }
            }
        }
    }
}