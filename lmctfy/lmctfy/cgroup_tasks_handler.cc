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

#include "lmctfy/cgroup_tasks_handler.h"

#include <stddef.h>
#include <unistd.h>
#include <algorithm>
#include <queue>
#include <memory>

#include "base/logging.h"
#include "file/base/path.h"
#include "util/errors.h"
#include "util/task/codes.pb.h"
#include "util/task/statusor.h"

using ::file::JoinPath;
using ::std::queue;
using ::std::remove_if;
using ::std::sort;
using ::std::unique_ptr;
using ::std::vector;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {

CgroupTasksHandler::CgroupTasksHandler(
    const string &container_name, CgroupController *cgroup_controller,
    const TasksHandlerFactory *tasks_handler_factory)
    : TasksHandler(container_name),
      cgroup_controller_(cgroup_controller),
      tasks_handler_factory_(tasks_handler_factory) {}

CgroupTasksHandler::~CgroupTasksHandler() {}

Status CgroupTasksHandler::Destroy() {
  RETURN_IF_ERROR(cgroup_controller_->Destroy());

  // Destroys itself on success.
  cgroup_controller_.release();

  delete this;
  return Status::OK;
}

Status CgroupTasksHandler::TrackTasks(const vector<pid_t> &tids) {
  // Track all TIDs individually.
  for (pid_t tid : tids) {
    RETURN_IF_ERROR(cgroup_controller_->Enter(tid));
  }

  return Status::OK;
}

Status CgroupTasksHandler::Delegate(::util::UnixUid uid,
                                    ::util::UnixGid gid) {
  return cgroup_controller_->Delegate(uid, gid);
}

StatusOr<vector<string>> CgroupTasksHandler::ListSubcontainers(
    TasksHandler::ListType type) const {
  vector<string> subcontainers =
      RETURN_IF_ERROR(cgroup_controller_->GetSubcontainers());
  // Make the container names absolute by appending the subdirectory name to the
  // current container's name.
  for (string &subcontainer : subcontainers) {
    subcontainer.assign(JoinPath(container_name_, subcontainer));
  }

  if (type == TasksHandler::ListType::RECURSIVE) {
    // Get all subcontainers of the subcontainers, recursively.
    vector<string> to_check;
    to_check.swap(subcontainers);
    while (!to_check.empty()) {
      unique_ptr<TasksHandler> handler(
          RETURN_IF_ERROR(tasks_handler_factory_->Get(to_check.back())));

      // We examine all subcontainers, add them to the result after having done
      // so.
      subcontainers.emplace_back(to_check.back());
      to_check.pop_back();

      // Get subcontainers and add them to the ones we will check.
      const vector<string> containers = RETURN_IF_ERROR(
          handler->ListSubcontainers(TasksHandler::ListType::SELF));
      to_check.insert(to_check.end(), containers.begin(), containers.end());
    }

    // Ensure the result is sorted.
    sort(subcontainers.begin(), subcontainers.end());
  }

  return subcontainers;
}

StatusOr<vector<pid_t>> CgroupTasksHandler::ListProcesses(
    TasksHandler::ListType type) const {
  vector<pid_t> pids;
  RETURN_IF_ERROR(ListProcessesOrThreads(type, PidsOrTids::PIDS, &pids));
  return pids;
}

StatusOr<vector<pid_t>> CgroupTasksHandler::ListThreads(
    TasksHandler::ListType type) const {
  vector<pid_t> tids;
  RETURN_IF_ERROR(ListProcessesOrThreads(type, PidsOrTids::TIDS, &tids));
  return tids;
}

Status CgroupTasksHandler::ListProcessesOrThreads(TasksHandler::ListType type,
                                                  PidsOrTids pids_or_tids,
                                                  vector<pid_t> *output) const {
  if (pids_or_tids == PidsOrTids::PIDS) {
    *output = RETURN_IF_ERROR(cgroup_controller_->GetProcesses());
  } else {
    *output = RETURN_IF_ERROR(cgroup_controller_->GetThreads());
  }

  if (type == TasksHandler::ListType::RECURSIVE) {
    set<pid_t> unique_pids(output->begin(), output->end());

    // Get all subcontainers and list the PIDs/TIDs of those too.
    const vector<string> subcontainers =
        RETURN_IF_ERROR(ListSubcontainers(TasksHandler::ListType::RECURSIVE));
    for (const string &subcontainer : subcontainers) {
      StatusOr<vector<pid_t>> statusor;
      unique_ptr<TasksHandler> handler(
          RETURN_IF_ERROR(tasks_handler_factory_->Get(subcontainer)));
      if (pids_or_tids == PidsOrTids::PIDS) {
        statusor = handler->ListProcesses(TasksHandler::ListType::SELF);
      } else {
        statusor = handler->ListThreads(TasksHandler::ListType::SELF);
      }
      if (!statusor.ok()) {
        return statusor.status();
      }

      // Aggregate PIDs/TIDS uniquely. Although TasksHandler guarantees that no
      // PID/TID will be in two containers at the same time, our queries to the
      // handler are not atomic so PIDs/TIDs may have moved since the last
      // query.
      unique_pids.insert(statusor.ValueOrDie().begin(),
                         statusor.ValueOrDie().end());
    }

    output->assign(unique_pids.begin(), unique_pids.end());
  }

  return Status::OK;
}

Status CgroupTasksHandler::PopulateMachineSpec(MachineSpec *spec) const {
  return cgroup_controller_->PopulateMachineSpec(spec);
}

}  // namespace lmctfy
}  // namespace containers
