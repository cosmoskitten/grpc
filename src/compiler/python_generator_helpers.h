/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "src/compiler/config.h"
#include "src/compiler/generator_helpers.h"

namespace grpc_python_generator {
  namespace {
    // TODO(https://github.com/google/protobuf/issues/888):
    // Export `ModuleName` from protobuf's
    // `src/google/protobuf/compiler/python/python_generator.cc` file.
    grpc::string ModuleName(const grpc::string& filename) {
      grpc::string basename = grpc_generator::StripProto(filename);
      basename = grpc_generator::StringReplace(basename, "-", "_");
      basename = grpc_generator::StringReplace(basename, "/", ".");
      return basename + "_pb2";
    }

    // TODO(https://github.com/google/protobuf/issues/888):
    // Export `ModuleAlias` from protobuf's
    // `src/google/protobuf/compiler/python/python_generator.cc` file.
    grpc::string ModuleAlias(const grpc::string& filename) {
      grpc::string module_name = ModuleName(filename);
      // We can't have dots in the module name, so we replace each with _dot_.
      // But that could lead to a collision between a.b and a_dot_b, so we also
      // duplicate each underscore.
      module_name = grpc_generator::StringReplace(module_name, "_", "__");
      module_name = grpc_generator::StringReplace(module_name, ".", "_dot_");
      return module_name;
    }

    bool GetModuleAndMessagePath(const grpc::protobuf::Descriptor* type,
                                 const grpc::protobuf::ServiceDescriptor* service,
                                 grpc::string* out) {
      const grpc::protobuf::Descriptor* path_elem_type = type;
      std::vector<const grpc::protobuf::Descriptor*> message_path;
      do {
        message_path.push_back(path_elem_type);
        path_elem_type = path_elem_type->containing_type();
      } while (path_elem_type);  // implicit nullptr comparison; don't be explicit
      grpc::string file_name = type->file()->name();
      static const int proto_suffix_length = strlen(".proto");
      if (!(file_name.size() > static_cast<size_t>(proto_suffix_length) &&
          file_name.find_last_of(".proto") == file_name.size() - 1)) {
        return false;
      }
      grpc::string service_file_name = service->file()->name();
      grpc::string module =
            service_file_name == file_name ? "" : ModuleAlias(file_name) + ".";
      grpc::string message_type;
      for (auto path_iter = message_path.rbegin(); path_iter != message_path.rend();
           ++path_iter) {
          message_type += (*path_iter)->name() + ".";
      }
      // no pop_back prior to C++11
      message_type.resize(message_type.size() - 1);
      *out = module + message_type;
      return true;
    }
  }
} // namespace grpc_python_generator
