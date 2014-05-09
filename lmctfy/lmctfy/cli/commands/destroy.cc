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

#include "lmctfy/cli/commands/destroy.h"

#include <sys/types.h>
#include <memory>
#include <string>
using ::std::string;
#include <vector>

#include "gflags/gflags.h"
#include "base/logging.h"
#include "lmctfy/cli/command.h"
#include "include/lmctfy.h"
#include "util/errors.h"
#include "strings/stringpiece.h"
#include "util/task/codes.pb.h"
#include "util/task/statusor.h"

DECLARE_bool(lmctfy_force);

using ::std::unique_ptr;
using ::std::vector;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {
namespace cli {

// Command to destroy a container.
Status DestroyContainer(const vector<string> &argv, const ContainerApi *lmctfy,
                        OutputMap *output) {
  // Args: destroy <container name>
  if (argv.size() != 2) {
    return Status(::util::error::INVALID_ARGUMENT,
                  "See help for supported options.");
  }
  const string container_name = argv[1];

  // Ensure the container exists.
  unique_ptr<Container> container(
      RETURN_IF_ERROR(lmctfy->Get(container_name)));

  // If not force, ensure no children and no PIDs/TIDs since Destroy() is
  // recursive by default.
  if (!FLAGS_lmctfy_force) {
    // Ensure no subcontainers.
    vector<Container *> subcontainers =
        RETURN_IF_ERROR(container->ListSubcontainers(Container::LIST_SELF));
    if (!subcontainers.empty()) {
      return Status(
          ::util::error::FAILED_PRECONDITION,
          "Subcontainers found. Container must not have any subcontainers to "
          "be destroyed without specifying -f");
    }

    // Ensure no PIDs.
    vector<pid_t> pids =
        RETURN_IF_ERROR(container->ListProcesses(Container::LIST_SELF));
    if (!pids.empty()) {
      return Status(
          ::util::error::FAILED_PRECONDITION,
          "Processes found in container. Container must not have any processes "
          "to be destroyed without specifying -f");
    }

    // Ensure no TIDs. At this point since there are no PIDs these are tourist
    // threads.
    pids = RETURN_IF_ERROR(container->ListThreads(Container::LIST_SELF));
    if (!pids.empty()) {
      return Status(
          ::util::error::FAILED_PRECONDITION,
          "Tourist threads found in container. Container must not have any "
          "tourist threads to be destroyed without specifying -f");
    }
  }

  // Destroy the container.
  return lmctfy->Destroy(container.release());
}

void RegisterDestroyCommand() {
  RegisterRootCommand(
      CMD("destroy",
          "Destroy the container with the specified name. Destruction fails if "
          "there are any subcontainers, processes, or threads in the "
          "container. To force destruction you must specify -f",
          "[-f] <container name>",
          CMD_TYPE_SETTER,
          1,
          1,
          &DestroyContainer));
}

}  // namespace cli
}  // namespace lmctfy
}  // namespace containers
