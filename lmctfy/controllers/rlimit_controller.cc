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

#include "lmctfy/controllers/rlimit_controller.h"

#include "lmctfy/kernel_files.h"

using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {

RLimitController::RLimitController(const string &hierarchy_path,
                                   const string &cgroup_path, bool owns_cgroup,
                                   const KernelApi *kernel,
                                   EventFdNotifications *eventfd_notifications)
    : CgroupController(CGROUP_RLIMIT, hierarchy_path, cgroup_path, owns_cgroup,
                       kernel, eventfd_notifications) {}

Status RLimitController::SetFdLimit(int64 limit) {
  return SetParamInt(KernelFiles::RLimit::kFdLimit, limit);
}

StatusOr<int64> RLimitController::GetFdLimit() const {
  return GetParamInt(KernelFiles::RLimit::kFdLimit);
}

StatusOr<int64> RLimitController::GetFdUsage() const {
  return GetParamInt(KernelFiles::RLimit::kFdUsage);
}

StatusOr<int64> RLimitController::GetMaxFdUsage() const {
  return GetParamInt(KernelFiles::RLimit::kFdMaxUsage);
}

StatusOr<int64> RLimitController::GetFdFailCount() const {
  return GetParamInt(KernelFiles::RLimit::kFdFailCount);
}

}  // namespace lmctfy
}  // namespace containers
