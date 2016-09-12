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

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstring>
#include <map>
#include <memory>
#include <ostream>
#include <sstream>
#include <tuple>
#include <vector>

#include "src/compiler/config.h"
#include "src/compiler/protobuf_plugin.h"
#include "src/compiler/python_generator.h"

using grpc::protobuf::FileDescriptor;
using grpc::protobuf::compiler::GeneratorContext;
using grpc::protobuf::io::CodedOutputStream;
using grpc::protobuf::io::ZeroCopyOutputStream;
using std::initializer_list;
using std::make_pair;
using std::map;
using std::pair;
using std::replace;
using std::vector;

namespace grpc_python_generator {

  GeneratorConfiguration::GeneratorConfiguration()
      : grpc_package_root("grpc"), beta_package_root("grpc.beta") {}

  PythonGrpcGenerator::PythonGrpcGenerator(const GeneratorConfiguration& config)
      : config_(config) {}

  PythonGrpcGenerator::~PythonGrpcGenerator() {}

  bool PythonGrpcGenerator::Generate(const FileDescriptor* file,
                                     const grpc::string& parameter,
                                     GeneratorContext* context,
                                     grpc::string* error) const {
    // Get output file name.
    grpc::string file_name;

    ProtoBufFile pbfile(file);

    static const int proto_suffix_length = strlen(".proto");
    if (file->name().size() > static_cast<size_t>(proto_suffix_length) &&
        file->name().find_last_of(".proto") == file->name().size() - 1) {
      file_name =
          file->name().substr(0, file->name().size() - proto_suffix_length) +
          "_pb2.py";
    } else {
      *error = "Invalid proto file name. Proto file must end with .proto";
      return false;
    }

    std::unique_ptr<ZeroCopyOutputStream> output(
        context->OpenForInsert(file_name, "module_scope"));
    CodedOutputStream coded_out(output.get());
    bool success = false;
    grpc::string code = "";
    tie(success, code) = grpc_python_generator::GetServices(&pbfile, config_);
    if (success) {
      coded_out.WriteRaw(code.data(), code.size());
      return true;
    } else {
      return false;
    }
  }

namespace {
//////////////////////////////////
// BEGIN FORMATTING BOILERPLATE //
//////////////////////////////////

// Converts an initializer list of the form { key0, value0, key1, value1, ... }
// into a map of key* to value*. Is merely a readability helper for later code.
map<grpc::string, grpc::string> ListToDict(
  const initializer_list<grpc::string>& values) {
  assert(values.size() % 2 == 0);
  map<grpc::string, grpc::string> value_map;
  auto value_iter = values.begin();
  for (unsigned i = 0; i < values.size() / 2; ++i) {
    grpc::string key = *value_iter;
    ++value_iter;
    grpc::string value = *value_iter;
    value_map[key] = value;
    ++value_iter;
  }
  return value_map;
}

// Provides RAII indentation handling. Use as:
// {
//   IndentScope raii_my_indent_var_name_here(my_py_printer);
//   // constructor indented my_py_printer
//   ...
//   // destructor called at end of scope, un-indenting my_py_printer
// }
class IndentScope {
 public:
  explicit IndentScope(grpc_generator::Printer* printer) : printer_(printer) {
    printer_->Indent();
  }

  ~IndentScope() { printer_->Outdent(); }

 private:
  grpc_generator::Printer* printer_;
};

////////////////////////////////
// END FORMATTING BOILERPLATE //
////////////////////////////////

// Print the comments as a docstring. Any leading space of a line
// will be removed, but the line wrapping will not be changed.
static void PrintAllComments(std::vector<grpc::string> &comments, 
                            grpc_generator::Printer* printer) {
  if (comments.empty()) {
    return;
  }
  printer->Print("\"\"\"");
  for (auto it = comments.begin(); it != comments.end(); ++it) {
    size_t start_pos = it->find_first_not_of(' ');
    if (start_pos != grpc::string::npos) {
      printer->Print(it->c_str() + start_pos);
    }
    printer->Print("\n");
  }
  printer->Print("\"\"\"\n");
}

bool PrintBetaServicer(const grpc_generator::Service* service, 
                        grpc_generator::Printer* out) {
  out->Print("\n\n");
  std::map<grpc::string, grpc::string> vars;
  vars["Service"] = service->name();
  out->Print(vars, "class Beta$Service$Servicer(object):\n");
  {
    IndentScope raii_class_indent(out);
    out->Print(
        "\"\"\"The Beta API is deprecated for 0.15.0 and later.\n"
        "\nIt is recommended to use the GA API (classes and functions in this\n"
        "file not marked beta) for all further purposes. This class was "
        "generated\n"
        "only to ease transition from grpcio<0.15.0 to "
        "grpcio>=0.15.0.\"\"\"\n");
    std::vector<grpc::string> ServiceComments = service->GetAllComments();
    PrintAllComments(ServiceComments, out);
    for (int i = 0; i < service->method_count(); ++i) {
      auto meth = service->method(i);
      grpc::string arg_name =
          meth.get()->ClientStreaming() ? "request_iterator" : "request";
      vars["Method"] = meth.get()->name();
      vars["ArgName"] = arg_name;
      out->Print(vars, "def $Method$(self, $ArgName$, context):\n");
      {
        IndentScope raii_method_indent(out);
        std::vector<grpc::string> MethodComments = meth.get()->GetAllComments();
        PrintAllComments(MethodComments, out);
        out->Print("context.code(beta_interfaces.StatusCode.UNIMPLEMENTED)\n");
      }
    }
  }
  return true;
}

bool PrintBetaStub(const grpc_generator::Service* service, 
                    grpc_generator::Printer* out) {
  out->Print("\n\n");
  std::map<grpc::string, grpc::string> vars;
  vars["Service"] = service->name();
  out->Print(vars, "class Beta$Service$Stub(object):\n");
  {
    IndentScope raii_class_indent(out);
    out->Print(
        "\"\"\"The Beta API is deprecated for 0.15.0 and later.\n"
        "\nIt is recommended to use the GA API (classes and functions in this\n"
        "file not marked beta) for all further purposes. This class was "
        "generated\n"
        "only to ease transition from grpcio<0.15.0 to "
        "grpcio>=0.15.0.\"\"\"\n");
    std::vector<grpc::string> ServiceComments = service->GetAllComments();
    PrintAllComments(ServiceComments, out);
    for (int i = 0; i < service->method_count(); ++i) {
      auto meth = service->method(i);
      grpc::string arg_name =
          meth.get()->ClientStreaming() ? "request_iterator" : "request";
      auto methdict = ListToDict({"Method", meth.get()->name(), "ArgName", arg_name});
      out->Print(methdict,
                 "def $Method$(self, $ArgName$, timeout, metadata=None, "
                 "with_call=False, protocol_options=None):\n");
      {
        IndentScope raii_method_indent(out);
        std::vector<grpc::string> MethodComments = meth.get()->GetAllComments();
        PrintAllComments(MethodComments, out);
        out->Print("raise NotImplementedError()\n");
      }
      if (!meth.get()->ServerStreaming()) {
        out->Print(methdict, "$Method$.future = None\n");
      }
    }
  }
  return true;
}

bool PrintBetaServerFactory(const grpc::string& package_qualified_service_name,
                            const grpc_generator::Service* service, 
                            grpc_generator::Printer* out) {
  std::map<grpc::string, grpc::string> vars;
  out->Print("\n\n");
  vars["Service"] = service->name();
  out->Print(vars,
      "def beta_create_$Service$_server(servicer, pool=None, "
      "pool_size=None, default_timeout=None, maximum_timeout=None):\n");
  {
    IndentScope raii_create_server_indent(out);
    out->Print(
        "\"\"\"The Beta API is deprecated for 0.15.0 and later.\n"
        "\nIt is recommended to use the GA API (classes and functions in this\n"
        "file not marked beta) for all further purposes. This function was\n"
        "generated only to ease transition from grpcio<0.15.0 to grpcio>=0.15.0"
        "\"\"\"\n");
    map<grpc::string, grpc::string> method_implementation_constructors;
    map<grpc::string, grpc::string> input_message_modules_and_classes;
    map<grpc::string, grpc::string> output_message_modules_and_classes;
    for (int i = 0; i < service->method_count(); ++i) {
      auto method = service->method(i);
      const grpc::string method_implementation_constructor =
          grpc::string(method.get()->ClientStreaming() ? "stream_" : "unary_") +
          grpc::string(method.get()->ServerStreaming() ? "stream_" : "unary_") +
          "inline";
      grpc::string input_message_module_and_class;
      if (!method.get()->get_module_message_path_input(&input_message_module_and_class)) {
        return false;
      }
      grpc::string output_message_module_and_class;
      if (!method.get()->get_module_message_path_output(&output_message_module_and_class)) {
        return false;
      }
      method_implementation_constructors.insert(
          make_pair(method.get()->name(), method_implementation_constructor));
      input_message_modules_and_classes.insert(
          make_pair(method.get()->name(), input_message_module_and_class));
      output_message_modules_and_classes.insert(
          make_pair(method.get()->name(), output_message_module_and_class));
    }
    out->Print("request_deserializers = {\n");
    for (auto name_and_input_module_class_pair =
             input_message_modules_and_classes.begin();
         name_and_input_module_class_pair !=
         input_message_modules_and_classes.end();
         name_and_input_module_class_pair++) {
      IndentScope raii_indent(out);
      vars["PackageQualifiedServiceName"] = package_qualified_service_name;
      vars["MethodName"] = name_and_input_module_class_pair->first;
      vars["InputTypeModuleAndClass"] = name_and_input_module_class_pair->second;
      out->Print(vars,
          "(\'$PackageQualifiedServiceName$\', \'$MethodName$\'): "
          "$InputTypeModuleAndClass$.FromString,\n");
    }
    out->Print("}\n");
    out->Print("response_serializers = {\n");
    for (auto name_and_output_module_class_pair =
             output_message_modules_and_classes.begin();
         name_and_output_module_class_pair !=
         output_message_modules_and_classes.end();
         name_and_output_module_class_pair++) {
      IndentScope raii_indent(out);
      vars["PackageQualifiedServiceName"] = package_qualified_service_name;
      vars["MethodName"] = name_and_output_module_class_pair->first;
      vars["OutputTypeModuleAndClass"] = name_and_output_module_class_pair->second;
      out->Print(vars,
          "(\'$PackageQualifiedServiceName$\', \'$MethodName$\'): "
          "$OutputTypeModuleAndClass$.SerializeToString,\n");
    }
    out->Print("}\n");
    out->Print("method_implementations = {\n");
    for (auto name_and_implementation_constructor =
             method_implementation_constructors.begin();
         name_and_implementation_constructor !=
         method_implementation_constructors.end();
         name_and_implementation_constructor++) {
      IndentScope raii_descriptions_indent(out);
      const grpc::string method_name =
          name_and_implementation_constructor->first;
      vars["PackageQualifiedServiceName"] = package_qualified_service_name;
      vars["Method"] = name_and_implementation_constructor->first;
      vars["Constructor"] = name_and_implementation_constructor->second;
      out->Print(vars,
          "(\'$PackageQualifiedServiceName$\', \'$Method$\'): "
          "face_utilities.$Constructor$(servicer.$Method$),\n");
    }
    out->Print("}\n");
    out->Print(
        "server_options = beta_implementations.server_options("
        "request_deserializers=request_deserializers, "
        "response_serializers=response_serializers, "
        "thread_pool=pool, thread_pool_size=pool_size, "
        "default_timeout=default_timeout, "
        "maximum_timeout=maximum_timeout)\n");
    out->Print(
        "return beta_implementations.server(method_implementations, "
        "options=server_options)\n");
  }
  return true;
}

bool PrintBetaStubFactory(const grpc::string& package_qualified_service_name,
                          const grpc_generator::Service* service, 
                          grpc_generator::Printer* out) {
  map<grpc::string, grpc::string> dict = ListToDict({
      "Service", service->name(),
  });
  out->Print("\n\n");
  out->Print(dict,
             "def beta_create_$Service$_stub(channel, host=None,"
             " metadata_transformer=None, pool=None, pool_size=None):\n");
  {
    IndentScope raii_create_server_indent(out);
    out->Print(
        "\"\"\"The Beta API is deprecated for 0.15.0 and later.\n"
        "\nIt is recommended to use the GA API (classes and functions in this\n"
        "file not marked beta) for all further purposes. This function was\n"
        "generated only to ease transition from grpcio<0.15.0 to grpcio>=0.15.0"
        "\"\"\"\n");
    map<grpc::string, grpc::string> method_cardinalities;
    map<grpc::string, grpc::string> input_message_modules_and_classes;
    map<grpc::string, grpc::string> output_message_modules_and_classes;
    for (int i = 0; i < service->method_count(); ++i) {
      auto method = service->method(i);
      const grpc::string method_cardinality =
          grpc::string(method.get()->ClientStreaming() ? "STREAM" : "UNARY") + "_" +
          grpc::string(method.get()->ServerStreaming() ? "STREAM" : "UNARY");
      grpc::string input_message_module_and_class;
      if (!method.get()->get_module_message_path_input(&input_message_module_and_class)) {
        return false;
      }
      grpc::string output_message_module_and_class;
      if (!method.get()->get_module_message_path_output(&output_message_module_and_class)) {
        return false;
      }
      method_cardinalities.insert(
          make_pair(method.get()->name(), method_cardinality));
      input_message_modules_and_classes.insert(
          make_pair(method.get()->name(), input_message_module_and_class));
      output_message_modules_and_classes.insert(
          make_pair(method.get()->name(), output_message_module_and_class));
    }
    out->Print("request_serializers = {\n");
    for (auto name_and_input_module_class_pair =
             input_message_modules_and_classes.begin();
         name_and_input_module_class_pair !=
         input_message_modules_and_classes.end();
         name_and_input_module_class_pair++) {
      IndentScope raii_indent(out);
      dict["PackageQualifiedServiceName"] = package_qualified_service_name;
      dict["MethodName"] = name_and_input_module_class_pair->first;
      dict["InputTypeModuleAndClass"] = name_and_input_module_class_pair->second;
      out->Print(dict,
          "(\'$PackageQualifiedServiceName$\', \'$MethodName$\'): "
          "$InputTypeModuleAndClass$.SerializeToString,\n");
    }
    out->Print("}\n");
    out->Print("response_deserializers = {\n");
    for (auto name_and_output_module_class_pair =
             output_message_modules_and_classes.begin();
         name_and_output_module_class_pair !=
         output_message_modules_and_classes.end();
         name_and_output_module_class_pair++) {
      IndentScope raii_indent(out);
      dict["MethodName"] = name_and_output_module_class_pair->first;
      dict["OutputTypeModuleAndClass"] = name_and_output_module_class_pair->second;
      out->Print(dict,
          "(\'$PackageQualifiedServiceName$\', \'$MethodName$\'): "
          "$OutputTypeModuleAndClass$.FromString,\n");
    }
    out->Print("}\n");
    out->Print("cardinalities = {\n");
    for (auto name_and_cardinality = method_cardinalities.begin();
         name_and_cardinality != method_cardinalities.end();
         name_and_cardinality++) {
      IndentScope raii_descriptions_indent(out);
      dict["Method"] = name_and_cardinality->first;
      dict["Cardinality"] = name_and_cardinality->second;
      out->Print(dict, "\'$Method$\': cardinality.Cardinality.$Cardinality$,\n");
    }
    out->Print("}\n");
    out->Print(
        "stub_options = beta_implementations.stub_options("
        "host=host, metadata_transformer=metadata_transformer, "
        "request_serializers=request_serializers, "
        "response_deserializers=response_deserializers, "
        "thread_pool=pool, thread_pool_size=pool_size)\n");
    out->Print(dict,
        "return beta_implementations.dynamic_stub(channel, "
        "\'$PackageQualifiedServiceName$\', "
        "cardinalities, options=stub_options)\n");
  }
  return true;
}

bool PrintStub(const grpc::string& package_qualified_service_name,
               const grpc_generator::Service* service,
               grpc_generator::Printer* out) {
  out->Print("\n\n");
  std::map<grpc::string, grpc::string> vars;
  vars["Service"] = service->name();
  out->Print(vars, "class $Service$Stub(object):\n");
  {
    IndentScope raii_class_indent(out);
    std::vector<grpc::string> ServiceComments = service->GetAllComments();
    PrintAllComments(ServiceComments, out);
    out->Print("\n");
    out->Print("def __init__(self, channel):\n");
    {
      IndentScope raii_init_indent(out);
      out->Print("\"\"\"Constructor.\n");
      out->Print("\n");
      out->Print("Args:\n");
      {
        IndentScope raii_args_indent(out);
        out->Print("channel: A grpc.Channel.\n");
      }
      out->Print("\"\"\"\n");
      for (int i = 0; i < service->method_count(); ++i) {
        auto method = service->method(i);
        auto multi_callable_constructor =
            grpc::string(method.get()->ClientStreaming() ? "stream" : "unary") +
            "_" + grpc::string(method.get()->ServerStreaming() ? "stream" : "unary");
        grpc::string request_module_and_class;
        if (!method.get()->get_module_message_path_input(&request_module_and_class)) {
          return false;
        }
        grpc::string response_module_and_class;
        if (!method.get()->get_module_message_path_output(&response_module_and_class)) {
          return false;
        }
        vars["Method"] = method.get()->name();
        vars["MultiCallableConstructor"] = multi_callable_constructor;
        out->Print(vars, "self.$Method$ = channel.$MultiCallableConstructor$(\n");
        {
          IndentScope raii_first_attribute_indent(out);
          IndentScope raii_second_attribute_indent(out);
          vars["PackageQualifiedService"] = package_qualified_service_name;
          vars["Method"] = method.get()->name();
          vars["RequestModuleAndClass"] = request_module_and_class;
          vars["ResponseModuleAndClass"] = response_module_and_class;
          out->Print(vars, "'/$PackageQualifiedService$/$Method$',\n");
          out->Print(vars,
              "request_serializer=$RequestModuleAndClass$.SerializeToString,\n");
          out->Print(vars,
              "response_deserializer=$ResponseModuleAndClass$.FromString,\n");
          out->Print(")\n");
        }
      }
    }
  }
  return true;
}

bool PrintServicer(const grpc_generator::Service* service,
                   grpc_generator::Printer* out) {
  std::map<grpc::string, grpc::string> vars;
  out->Print("\n\n");
  vars["Service"] = service->name();
  out->Print(vars, "class $Service$Servicer(object):\n");
  {
    IndentScope raii_class_indent(out);
    std::vector<grpc::string> ServiceComments = service->GetAllComments();
    PrintAllComments(ServiceComments, out);
    for (int i = 0; i < service->method_count(); ++i) {
      auto method = service->method(i);
      grpc::string arg_name =
          method.get()->ClientStreaming() ? "request_iterator" : "request";
      out->Print("\n");
      vars["Method"] = method.get()->name();
      vars["ArgName"] = arg_name;
      out->Print(vars, "def $Method$(self, $ArgName$, context):\n");
      {
        IndentScope raii_method_indent(out);
        std::vector<grpc::string> MethodComments = method.get()->GetAllComments();
        PrintAllComments(MethodComments, out);
        out->Print("context.set_code(grpc.StatusCode.UNIMPLEMENTED)\n");
        out->Print("context.set_details('Method not implemented!')\n");
        out->Print("raise NotImplementedError('Method not implemented!')\n");
      }
    }
  }
  return true;
}

bool PrintAddServicerToServer(
    const grpc::string& package_qualified_service_name,
    const grpc_generator::Service* service, grpc_generator::Printer* out) {
  std::map<grpc::string, grpc::string> vars;
  out->Print("\n\n");
  vars["Service"] = service->name();
  out->Print(vars, "def add_$Service$Servicer_to_server(servicer, server):\n");
  {
    IndentScope raii_class_indent(out);
    out->Print("rpc_method_handlers = {\n");
    {
      IndentScope raii_dict_first_indent(out);
      IndentScope raii_dict_second_indent(out);
      for (int i = 0; i < service->method_count(); ++i) {
        auto method = service->method(i);
        auto method_handler_constructor =
            grpc::string(method.get()->ClientStreaming() ? "stream" : "unary") +
            "_" +
            grpc::string(method.get()->ServerStreaming() ? "stream" : "unary") +
            "_rpc_method_handler";
        grpc::string request_module_and_class;
        if (!method.get()->get_module_message_path_input(&request_module_and_class)) {
          return false;
        }
        grpc::string response_module_and_class;
        if (!method.get()->get_module_message_path_output(&response_module_and_class)) {
          return false;
        }
        vars["Method"] = method.get()->name();
        vars["MethodHandlerConstructor"] = method_handler_constructor;
        vars["RequestModuleAndClass"] = request_module_and_class;
        vars["ResponseModuleAndClass"] = response_module_and_class;
        out->Print(vars, "'$Method$': grpc.$MethodHandlerConstructor$(\n");
        {
          IndentScope raii_call_first_indent(out);
          IndentScope raii_call_second_indent(out);
          out->Print(vars, "servicer.$Method$,\n");
          out->Print(vars,
              "request_deserializer=$RequestModuleAndClass$.FromString,\n");
          out->Print(vars,
              "response_serializer=$ResponseModuleAndClass$.SerializeToString,"
              "\n");
        }
        out->Print("),\n");
      }
    }
    out->Print("}\n");
    out->Print("generic_handler = grpc.method_handlers_generic_handler(\n");
    {
      IndentScope raii_call_first_indent(out);
      IndentScope raii_call_second_indent(out);
      vars["PackageQualifiedServiceName"] = package_qualified_service_name;
      out->Print(vars, "'$PackageQualifiedServiceName$', rpc_method_handlers)\n");
    }
    out->Print("server.add_generic_rpc_handlers((generic_handler,))\n");
  }
  return true;
}

bool PrintPreamble(const grpc_generator::File* file,
                   const GeneratorConfiguration& config, 
                   grpc_generator::Printer* out) {
  std::map<grpc::string, grpc::string> vars;
  vars["Package"] = config.grpc_package_root;
  out->Print(vars, "import $Package$\n");
  vars["Package"] = config.beta_package_root;
  out->Print(vars, "from $Package$ import implementations as beta_implementations\n");
  out->Print(vars, "from $Package$ import interfaces as beta_interfaces\n");
  out->Print("from grpc.framework.common import cardinality\n");
  out->Print(
      "from grpc.framework.interfaces.face import utilities as "
      "face_utilities\n");
  return true;
}

}  // namespace

pair<bool, grpc::string> GetServices(const grpc_generator::File* file,
                                     const GeneratorConfiguration& config) {
  grpc::string output;
  {
    // Scope the output stream so it closes and finalizes output to the string.
    auto out = file->CreatePrinter(&output);
    if (!PrintPreamble(file, config, out.get())) {
      return make_pair(false, "");
    }
    auto package = file->package();
    if (!package.empty()) {
      package = package.append(".");
    }
    for (int i = 0; i < file->service_count(); ++i) {
      auto service = file->service(i);
      auto package_qualified_service_name = package + service.get()->name();
      if (!(PrintStub(package_qualified_service_name, service.get(), out.get()) &&
            PrintServicer(service.get(), out.get()) &&
            PrintAddServicerToServer(package_qualified_service_name, service.get(),
                                     out.get()) &&
            PrintBetaServicer(service.get(), out.get()) && PrintBetaStub(service.get(), out.get()) &&
            PrintBetaServerFactory(package_qualified_service_name, service.get(),
                                   out.get()) &&
            PrintBetaStubFactory(package_qualified_service_name, service.get(),
                                 out.get()))) {
        return make_pair(false, "");
      }
    }
  }
  return make_pair(true, std::move(output));
}

}  // namespace grpc_python_generator
