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

#include "lmctfy/cli/commands/list.h"

#include <unistd.h>
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
#include "strings/substitute.h"
#include "util/gtl/stl_util.h"
#include "util/task/statusor.h"

DECLARE_bool(lmctfy_recursive);

using ::std::unique_ptr;
using ::std::vector;
using ::strings::Substitute;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {
namespace cli {

// Get the container name if it was specified in the argv or assume self and
// detect it.
static StatusOr<string> ContainerNameOrSelf(const vector<string> & argv,
                                            const ContainerApi *lmctfy) {
  if (argv.size() == 2) {
    return argv[1];
  } else {
    return lmctfy->Detect(getppid());
  }
}

// Command to list subcontainers.
Status ListContainers(const vector<string> &argv, const ContainerApi *lmctfy,
                      OutputMap *output) {
  // Args: containers [<container name>]
  if (argv.size() < 1 || argv.size() > 2) {
    return Status(::util::error::INVALID_ARGUMENT,
                  "See help for supported options.");
  }

  // Get the container name.
  string container_name =
      RETURN_IF_ERROR(ContainerNameOrSelf(argv, lmctfy));

  // Ensure the container exists.
  unique_ptr<Container> container(
      RETURN_IF_ERROR(lmctfy->Get(container_name)));

  // Get subcontainers.
  Container::ListPolicy list_policy =
      FLAGS_lmctfy_recursive ? Container::LIST_RECURSIVE
                               : Container::LIST_SELF;
  vector<Container *> subcontainers =
      RETURN_IF_ERROR(container->ListSubcontainers(list_policy));
  ElementDeleter d(&subcontainers);

  // Output subcontainer names.
  for (Container *cont : subcontainers) {
    output->Add("name", cont->name());
  }

  return Status::OK;
}

// Whether to list PIDs or TIDs for ListPidsOrTids().
enum ListType {
  LIST_PIDS,
  LIST_TIDS
};

// Helper for use by ListPids/ListTids.
static Status ListPidsOrTids(const vector<string> &argv,
                             const ContainerApi *lmctfy,
                             OutputMap *output,
                             ListType list_type) {
  // Args: pids|tids [container name]
  if (argv.size() < 1 || argv.size() > 2) {
    return Status(::util::error::INVALID_ARGUMENT,
                  "See help for supported options.");
  }

  // Get the container name.
  StatusOr<string> statusor = ContainerNameOrSelf(argv, lmctfy);
  if (!statusor.ok()) {
    return statusor.status();
  }

  // Ensure the container exists.
  StatusOr<Container *> statusor_container = lmctfy->Get(
      statusor.ValueOrDie());
  if (!statusor_container.ok()) {
    return statusor_container.status();
  }
  unique_ptr<Container> container(statusor_container.ValueOrDie());

  // Get PIDs/TIDs as specified.
  Container::ListPolicy list_policy =
      FLAGS_lmctfy_recursive ? Container::LIST_RECURSIVE
                               : Container::LIST_SELF;
  StatusOr<vector<pid_t>> statusor_pids;
  if (list_type == LIST_PIDS) {
    statusor_pids = container->ListProcesses(list_policy);
  } else {
    statusor_pids = container->ListThreads(list_policy);
  }
  if (!statusor_pids.ok()) {
    return statusor_pids.status();
  }

  // Output subcontainer names.
  const string output_type = list_type == LIST_PIDS ? "pid" : "tid";
  for (pid_t pid : statusor_pids.ValueOrDie()) {
    output->Add(output_type, Substitute("$0", pid));
  }

  return Status::OK;
}

// Command to list PIDs.
Status ListPids(const vector<string> &argv, const ContainerApi *lmctfy,
                OutputMap *output) {
  return ListPidsOrTids(argv, lmctfy, output, LIST_PIDS);
}

// Command to list TIDs.
Status ListTids(const vector<string> &argv, const ContainerApi *lmctfy,
                OutputMap *output) {
  return ListPidsOrTids(argv, lmctfy, output, LIST_TIDS);
}

void RegisterListCommands() {
  RegisterRootCommand(
      SUB("list",
          "List information about a container.",
          "<list type> <container name>", {
            CMD("containers",
                "List the containers in the specified container. If no "
                "container is specified, those of the calling process' "
                "container are listed. To recursively list subcontainers, "
                "specify -r",
                "[-r] [<container name>]",
                CMD_TYPE_GETTER,
                0,
                1,
                &ListContainers),
            CMD("pids",
                "List the PIDs (processes) in the specified container. If no "
                "container is specified, those of the calling process' "
                "container are listed. To recursively list pids, specify -r",
                "[-r] [<container name>]",
                CMD_TYPE_GETTER,
                0,
                1,
                &ListPids),
            CMD("tids",
                "List the TIDs (threads) in the specified container. If no "
                "container is specified, those of the calling process' "
                "container are listed. To recursively list tids, specify -r",
                "[-r] [<container name>]",
                CMD_TYPE_GETTER,
                0,
                1,
                &ListTids)
          }));
}

}  // namespace cli
}  // namespace lmctfy
}  // namespace containers
