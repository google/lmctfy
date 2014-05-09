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

#ifndef SRC_CONTROLLERS_RLIMIT_CONTROLLER_H_
#define SRC_CONTROLLERS_RLIMIT_CONTROLLER_H_

#include "lmctfy/controllers/cgroup_controller.h"

#include "util/task/status.h"
#include "util/task/statusor.h"

namespace containers {
namespace lmctfy {

class CgroupFactory;
class RLimitController;

// Factory for RLimitControllers.
//
// The rlimit cgroup is not hierarchical, usage of children is not reflected in
// the parent and limits of parents do not affect children.
//
// Class is thread-safe.
class RLimitControllerFactory
    : public CgroupControllerFactory<RLimitController, CGROUP_RLIMIT> {
 public:
  // Does not own the cgroup_factory or kernel.
  RLimitControllerFactory(const CgroupFactory *cgroup_factory,
                          const KernelApi *kernel,
                          EventFdNotifications *eventfd_notifications)
      : CgroupControllerFactory<RLimitController, CGROUP_RLIMIT>(
            cgroup_factory, kernel, eventfd_notifications) {}
  virtual ~RLimitControllerFactory() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(RLimitControllerFactory);
};

// Controller for rlimits.
//
// Class is thread-safe.
class RLimitController : public CgroupController {
 public:
  // Does not take ownership of kernel.
  RLimitController(const string &hierarchy_path, const string &cgroup_path,
                   bool owns_cgroup, const KernelApi *kernel,
                   EventFdNotifications *eventfd_notifications);
  virtual ~RLimitController() {}

  // Set the max number of FDs allowed.
  virtual ::util::Status SetFdLimit(int64 limit);

  // Get the max number of FDs allowed.
  virtual ::util::StatusOr<int64> GetFdLimit() const;

  // Get the current number of FDs in use.
  virtual ::util::StatusOr<int64> GetFdUsage() const;

  // Get the max number of FDs used in this controller's lifetime.
  virtual ::util::StatusOr<int64> GetMaxFdUsage() const;

  // Get the number of times container failed to get an FD because
  // it hit the limit.
  virtual ::util::StatusOr<int64> GetFdFailCount() const;

 private:
  DISALLOW_COPY_AND_ASSIGN(RLimitController);
};

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_CONTROLLERS_RLIMIT_CONTROLLER_H_
