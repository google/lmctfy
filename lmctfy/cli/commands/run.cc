// Copyright 2013 Google Inc. All Rights Reserved.
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

#include <sys/types.h>
#include <sys/wait.h>
#include <memory>
#include <string>
using ::std::string;
#include <vector>

#include "gflags/gflags.h"
#include "base/logging.h"
#include "lmctfy/cli/command.h"
#include "lmctfy/cli/output_map.h"
#include "include/lmctfy.h"
#include "util/errors.h"
#include "strings/stringpiece.h"
#include "strings/substitute.h"
#include "util/task/codes.pb.h"
#include "util/task/statusor.h"

DECLARE_bool(lmctfy_no_wait);

using ::std::unique_ptr;
using ::std::vector;
using ::strings::Substitute;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {
namespace cli {

// Command to run a command in a container.
Status RunInContainer(const vector<string> &argv, const ContainerApi *lmctfy,
                      vector<OutputMap> *output) {
  // Args: run <container name> <command>
  if (argv.size() != 3) {
    return Status(::util::error::INVALID_ARGUMENT,
                  "See help for supported options.");
  }
  const string container_name = argv[1];
  const string command = argv[2];

  // Ensure the container exists.
  unique_ptr<Container> container;
  RETURN_IF_ERROR(lmctfy->Get(container_name), &container);

  // Run command and get the child's PID.
  Container::FdPolicy fd_policy = Container::FDS_INHERITED;
  if (FLAGS_lmctfy_no_wait) {
    fd_policy = Container::FDS_DETACHED;
  }
  pid_t pid;
  RETURN_IF_ERROR(container->Run(command, fd_policy), &pid);

  // Output the PID if no wait, else wait for the PID to finish.
  if (FLAGS_lmctfy_no_wait) {
    output->push_back(OutputMap("pid", Substitute("$0", pid)));
  } else {
    int status = 0;

    // Wait for PID and output the exit code.
    if (waitpid(pid, &status, 0) != pid) {
      return Status(
          ::util::error::FAILED_PRECONDITION,
          Substitute("Failed while waiting for child with PID $0", pid));
    }

    // TODO(thockin): The whole process should exit() with the child's status.
    output->push_back(
        OutputMap("exit_code", Substitute("$0", WEXITSTATUS(status))));
  }

  return Status::OK;
}

void RegisterRunCommand() {
  RegisterRootCommand(
      CMD("run",
          "Run the specified command in the specified container. Block waiting "
          "for the command unless -n is specified. Outputs the exit code of "
          "commands run in the foreground.",
          "[-n] <container name> \"<command>\"",
          CMD_TYPE_SETTER,
          2,
          2,
          &RunInContainer));
}

}  // namespace cli
}  // namespace lmctfy
}  // namespace containers
