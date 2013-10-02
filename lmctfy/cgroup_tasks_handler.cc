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

#include "lmctfy/cgroup_tasks_handler.h"

#include <stddef.h>
#include <unistd.h>
#include <algorithm>
#include <memory>

#include "base/logging.h"
#include "file/base/path.h"
#include "util/errors.h"
#include "util/task/codes.pb.h"
#include "util/task/statusor.h"

using ::file::JoinPath;
using ::std::vector;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {

CgroupTasksHandler::CgroupTasksHandler(const string &container_name,
                                       CgroupController *cgroup_controller)
    : TasksHandler(container_name), cgroup_controller_(cgroup_controller) {}

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

StatusOr<vector<string>> CgroupTasksHandler::ListSubcontainers() const {
  vector<string> subdirs;
  RETURN_IF_ERROR(cgroup_controller_->GetSubcontainers(), &subdirs);

  // Make the container names absolute by appending the subdirectory name to the
  // current container's name.
  for (size_t i = 0; i < subdirs.size(); ++i) {
    subdirs[i].assign(JoinPath(container_name_, subdirs[i]));
  }

  return subdirs;
}

StatusOr<vector<pid_t>> CgroupTasksHandler::ListProcesses() const {
  return cgroup_controller_->GetProcesses();
}

StatusOr<vector<pid_t>> CgroupTasksHandler::ListThreads() const {
  return cgroup_controller_->GetThreads();
}

}  // namespace lmctfy
}  // namespace containers
