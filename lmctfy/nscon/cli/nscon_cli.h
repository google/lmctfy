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

#ifndef PRODUCTION_CONTAINERS_NSCON_CLI_NSCON_CLI_H__
#define PRODUCTION_CONTAINERS_NSCON_CLI_NSCON_CLI_H__

#include <memory>

#include "util/task/status.h"
#include "util/task/statusor.h"

namespace containers {
namespace nscon {
class NamespaceControllerCli;
class NamespaceSpec;
class RunSpec;

namespace cli {

// A class that will parse user input and runs user requested namespace
// operations.
// Class is thread hostile.
class NsconCli {
 public:
  // This is the canonical help string for the Nscon CLI.
  static const char *kNsconHelp;

  explicit NsconCli(::std::unique_ptr<NamespaceControllerCli> nscon);

  ~NsconCli();

  // Parses command line argument and performs the user requested namespace
  // operation.
  // Arguments:
  //  argv: List of arguments to nscon after all flags are parsed.
  //  user_command: user provided command.
  // Returns:
  //  On success, a string that needs to be output.
  //  INVALID_ARGUMENT: If the input is invalid.
  //  Any error encountered while running user requested operation.
  ::util::StatusOr<string> HandleUserInput(
       const ::std::vector<string> &argv,
       const ::std::vector<string> &user_command);

 private:
  // Creates a new namespace based on 'namespace_spec'.
  // Returns error upon failure.
  ::util::StatusOr<string> HandleCreate(
       const NamespaceSpec &namespace_spec,
       const ::std::vector<string> &init_argv) const;

  // Runs 'command' under namespace referred to by 'namespace_handle'. The
  // started process is reparented under nsinit automatically if PID namespaces
  // are used.
  // Returns error upon failure.
  ::util::StatusOr<string> HandleRun(
       const string &namespace_handle,
       const ::std::vector<string> &command,
       const RunSpec &run_spec) const;

  // Similar to above, but runs the given command under "bash -c" wrapper. This
  // is a convenience method which can be used to run commands with pipe (|) and
  // input/output redirection.
  ::util::StatusOr<string> HandleRunShell(const string &namespace_handle,
                                          const string &command,
                                          const RunSpec &run_spec) const;

  // Enters namespaces and execs the given command. The exec'd command may not
  // be reparented to nsinit.
  ::util::StatusOr<string> HandleExec(
       const string &namespace_handle,
       const ::std::vector<string> &command) const;

  // Updates namespace referred to by 'namespace_handle' based on
  // 'namespace_spec'. Returns error upon failure.
  ::util::StatusOr<string> HandleUpdate(
       const string &namespace_handle,
       const NamespaceSpec &namespace_spec) const;

  // Returns a NamespaceSpec based on flags or 'cmd_line_config'. Its an error
  // if both are specified.
  // Returns INVALID_ARGUMENT is the input is invalid or if parsing namespace
  // config fails.
  ::util::StatusOr<NamespaceSpec> GetNamespaceSpec(
       const string &cmd_line_config) const;
  // Similar to above, parses and returns the RunSpec from command-line or file.
  ::util::StatusOr<RunSpec> GetRunSpec(const string &cmd_line_config) const;

  ::std::unique_ptr<NamespaceControllerCli> nscon_;

  DISALLOW_COPY_AND_ASSIGN(NsconCli);
};

}  // namespace cli
}  // namespace nscon
}  // namespace containers

#endif  // PRODUCTION_CONTAINERS_NSCON_CLI_NSCON_CLI_H__
