// Copyright 2014 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "nscon/cli/nscon_cli.h"

#include "gflags/gflags.h"
#include "file/base/file.h"
#include "file/base/helpers.h"
#include "google/protobuf/text_format.h"
#include "nscon/namespace_controller_cli.h"
#include "include/namespaces.pb.h"
#include "util/errors.h"
#include "strings/join.h"
#include "strings/split.h"
#include "strings/substitute.h"
#include "strings/util.h"

DEFINE_string(namespace_spec_file, "",
              "path to a file containing the NamespaceSpec.");

using ::strings::Join;
using ::strings::Substitute;
using ::util::error::INVALID_ARGUMENT;
using ::util::error::INTERNAL;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace nscon {
namespace cli {
namespace {
const char *kCreateCommand = "create";
const char *kRunCommand = "run";
const char *kRunShellCommand = "runshell";
const char *kUpdateCommand = "update";
const char *kExecCommand = "exec";

string GetUserCommandFromCommandLineArgs(int argc, char *argv[]) {
  string result;
  int idx = 0;
  while (idx < argc) {
      StrAppend(&result, argv[idx++], " ");
  }
  return result;
}
}  // namespace

const char *NsconCli::kNsconHelp =
    "USAGE: nscon [create|run|runshell|exec|update] ...\n"
    "nscon create [<namespace-spec> | --namespace_spec_file=<spec-file>]"
    " [-- <init-command>]\n"
    "  <namespace-spec>: As defined in include/"
    "namespaces.proto\n"
    "  This can be specified in ASCII or binary format in the command line, "
    "or in a file using --namespace_spec_file flag.\n"
    "  <init-command>: A custom init command to be run. "
    "nsinit is used if none is specified.\n"
    "nscon run <nshandle> -- <command>\n"
    "  <nshandle>: Namespace handle as returned by 'nscon create'\n"
    "  <command>: Command to run inside the namespace jail\n"
    "nscon runshell <nshandle> -- <command>\n"
    "  <nshandle>: Namespace handle as returned by 'nscon create'\n"
    "  <command>: Command to run inside the namespace jail under a 'bash -c' "
    "wrapper\n"
    "nscon exec <nshandle> -- <command>\n"
    "  <nshandle>: Namespace handle as returned by 'nscon create'\n"
    "  <command>: Execs the given command after entering namespace jail\n"
    "nscon update <nshandle> [<namespace-spec> | --namespace_spec_file="
    "<spec-file>]\n"
    "  <nshandle>: Namespace handle as returned by 'nscon create'\n"
    "  <namespace-spec>: As defined in include/"
    "namespaces.proto\n"
    "  This can be specified in ASCII or binary format in the command line, "
    "or in a file using --namespace_spec_file flag.\n";

NsconCli::NsconCli(::std::unique_ptr<NamespaceControllerCli> nscon)
    : nscon_(::std::move(nscon)) {}

NsconCli::~NsconCli() {}

StatusOr<NamespaceSpec> NsconCli::GetNamespaceSpec(
    const string &cmd_line_config) const {
  string config;
  if (!FLAGS_namespace_spec_file.empty() && !cmd_line_config.empty()) {
    return Status(INVALID_ARGUMENT,
                  "Must specify the namespace spec either via "
                  "command line or via the flag '--namespace_spec_file'");
  } else if (!FLAGS_namespace_spec_file.empty()) {
    RETURN_IF_ERROR(::file::GetContents(FLAGS_namespace_spec_file,
                                        &config,
                                        ::file::Defaults()));
  } else if (!cmd_line_config.empty()) {
    config = cmd_line_config;
  } else {
    return Status(INVALID_ARGUMENT,
                  "Namespace spec is neither specified in the command line "
                  "nor via the flag '--namespace_spec_file'");
  }

  NamespaceSpec spec;
  // Try to parse the proto as both ASCII and binary.
  if (!::google::protobuf::TextFormat::ParseFromString(config, &spec) &&
      !spec.ParseFromString(config)) {
    return Status(INVALID_ARGUMENT, "Cannot parse namespace config. ");
  }
  return spec;
}

Status NsconCli::HandleUserInput(const vector<string> &argv,
                                 const vector<string> &user_command) {
  if (argv.size() < 2) {
    return Status(INVALID_ARGUMENT,
                  Substitute("Insufficient arguments to nscon: $0\n$1",
                             Join(argv, " "), kNsconHelp));
  }

  const string nscon_op(argv[1]);
  if (nscon_op == kCreateCommand) {
    const string ns_spec_str = argv.size() > 2 ? argv[2] : "";
    return HandleCreate(RETURN_IF_ERROR(GetNamespaceSpec(ns_spec_str)),
                        user_command);
  } else if (nscon_op == kRunCommand) {
    if (argv.size() < 3) {
      return Status(INVALID_ARGUMENT,
                    Substitute("Invalid arguments for 'run'\nUsage:\n$0",
                               kNsconHelp));
    }
    if (user_command.empty()) {
      return Status(INVALID_ARGUMENT, "Must specify command to run.");
    }
    const string nshandle_str = argv[2];
    return HandleRun(nshandle_str, user_command);
  } else if (nscon_op == kRunShellCommand) {
    if (argv.size() < 3) {
      return Status(INVALID_ARGUMENT,
                    Substitute("Invalid arguments for 'runshell'\nUsage:\n$0",
                               kNsconHelp));
    }
    if (user_command.empty()) {
      return Status(INVALID_ARGUMENT, "Must specify command to run.");
    }
    const string nshandle_str = argv[2];
    return HandleRunShell(nshandle_str, Join(user_command, " "));
  } else if (nscon_op == kUpdateCommand) {
    if (argv.size() < 3 || argv.size() > 4) {
      return Status(INVALID_ARGUMENT,
                    Substitute("Invalid arguments for 'update'\nUsage:\n$0",
                               kNsconHelp));
    }
    const string nshandle_str = argv[2];
    const string ns_spec_str = argv.size() > 3 ? argv[3] : "";
    return HandleUpdate(nshandle_str,
                        RETURN_IF_ERROR(GetNamespaceSpec(ns_spec_str)));
  } else if (nscon_op == kExecCommand) {
    if (argv.size() < 3) {
      return Status(INVALID_ARGUMENT,
                    Substitute("Invalid arguments for 'exec'\nUsage:\n$0",
                               kNsconHelp));
    }
    if (user_command.empty()) {
      return Status(INVALID_ARGUMENT, "Must specify command to exec.");
    }
    const string nshandle_str = argv[2];
    return HandleExec(nshandle_str, user_command);
  }

  return Status(INVALID_ARGUMENT,
                Substitute("Invalid nscon operation: $0\nUsage:\n$1",
                           nscon_op, kNsconHelp));
}

Status NsconCli::HandleCreate(const NamespaceSpec &namespace_spec,
                              const vector<string> &init_argv) const {
  const string namespace_handle =
      RETURN_IF_ERROR(nscon_->Create(namespace_spec, init_argv));

  // Print namespace handle on stdout.
  fprintf(stdout, "%s\n", namespace_handle.c_str());

  return Status::OK;
}

Status NsconCli::HandleRunShell(const string &namespace_handle,
                                const string &command) const {
  pid_t pid =
      RETURN_IF_ERROR(nscon_->RunShellCommand(namespace_handle, command));

  // Print pid of new process on stdout.
  fprintf(stdout, "%d\n", pid);

  return Status::OK;
}

Status NsconCli::HandleRun(const string &namespace_handle,
                           const vector<string> &command) const {
  pid_t pid = RETURN_IF_ERROR(nscon_->Run(namespace_handle, command));

  // Print pid of new process on stdout.
  fprintf(stdout, "%d\n", pid);

  return Status::OK;
}

Status NsconCli::HandleExec(const string &namespace_handle,
                            const vector<string> &command) const {
  return nscon_->Exec(namespace_handle, command);
}

Status NsconCli::HandleUpdate(const string &namespace_handle,
                              const NamespaceSpec &namespace_spec) const {
  return nscon_->Update(namespace_handle, namespace_spec);
}

}  // namespace cli
}  // namespace nscon
}  // namespace containers
