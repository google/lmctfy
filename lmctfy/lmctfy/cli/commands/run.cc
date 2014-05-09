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

#include "lmctfy/cli/commands/run.h"

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
                      OutputMap *output) {
  // Args: run <container name> command...
  if (argv.size() < 3) {
    return Status(::util::error::INVALID_ARGUMENT,
                  "Insufficient arguments. See help.");
  }
  const string container_name = argv[1];
  vector<string> args;
  if ((argv.size() == 3) && (argv[2].find(" ") != string::npos)) {
    // Command is a single word with a space. For backwards compatibility, run
    // the specified command through /bin/sh.
    args.push_back("/bin/sh");
    args.push_back("-c");
    args.push_back(argv[2]);
  } else {
    for (auto it = argv.begin() + 2; it != argv.end(); ++it) {
      args.push_back(*it);
    }
  }

  // Ensure the container exists.
  unique_ptr<Container> container(
      RETURN_IF_ERROR(lmctfy->Get(container_name)));

  // If no wait, run and output the PID, else exec the command.
  if (FLAGS_lmctfy_no_wait) {
    RunSpec spec;
    spec.set_fd_policy(RunSpec::DETACHED);

    pid_t pid =
        RETURN_IF_ERROR(container->Run(args, spec));
    output->Add("pid", Substitute("$0", pid));
  } else {
    RETURN_IF_ERROR(container->Exec(args));
  }

  return Status::OK;
}

void RegisterRunCommand() {
  RegisterRootCommand(
      CMD("run",
          "Run the specified command in the specified container. Execs the "
          "specified command under execv(). If -n is specified, runs the "
          "command in the background and returns the PID of the new process",
          "[-n] <container name> <command...>", CMD_TYPE_SETTER, 2, INT_MAX,
          &RunInContainer));
}

}  // namespace cli
}  // namespace lmctfy
}  // namespace containers
