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

#ifndef SRC_CONTROLLERS_FREEZER_CONTROLLER_H_
#define SRC_CONTROLLERS_FREEZER_CONTROLLER_H_

#include <string>
using ::std::string;
#include <vector>

#include "base/macros.h"
#include "lmctfy/controllers/cgroup_controller.h"
#include "util/task/status.h"

namespace containers {
namespace lmctfy {

class FreezerController;

// Factory for FreezerControllers.
//
// Class is thread-safe.
class FreezerControllerFactory
    : public CgroupControllerFactory<FreezerController, CGROUP_FREEZER> {
 public:
  // Does not own cgroup_factory or kernel.
  FreezerControllerFactory(const CgroupFactory *cgroup_factory,
                           const KernelApi *kernel,
                           EventFdNotifications *eventfd_notifications,
                           bool owns_cgroup)
      : CgroupControllerFactory<FreezerController, CGROUP_FREEZER>(
            cgroup_factory, kernel, eventfd_notifications, owns_cgroup) {}

  FreezerControllerFactory(const CgroupFactory *cgroup_factory,
                           const KernelApi *kernel,
                           EventFdNotifications *eventfd_notifications)
      : CgroupControllerFactory<FreezerController, CGROUP_FREEZER>(
            cgroup_factory, kernel, eventfd_notifications) {}

  virtual ~FreezerControllerFactory() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(FreezerControllerFactory);
};

enum class FreezerState {
  UNKNOWN = 0,
  FROZEN = 1,
  FREEZING = 2,
  THAWED = 3,
};

// Controller for the freezer cgroup hierarchy.
//
// Class is thread-safe.
class FreezerController : public CgroupController {
 public:
  // Does not take ownership of kernel.
  FreezerController(const string &hierarchy_path, const string &cgroup_path,
                    bool owns_cgroup, const KernelApi *kernel,
                    EventFdNotifications *eventfd_notifications)
      : CgroupController(CGROUP_FREEZER, hierarchy_path, cgroup_path,
                         owns_cgroup, kernel, eventfd_notifications) {}
  virtual ~FreezerController() {}

  // Freeze this cgroup. This unschedules all tasks and does not allow them to
  // run until the cgroup is thawed. This operation is recursive on all children
  // cgroups, they are all frozen as well.
  //
  // Return:
  //   Status: The status of the operation. Iff OK, all the tasks in this cgroup
  //       are frozen.
  virtual ::util::Status Freeze();

  // Unfreeze this cgroup. This makes all tasks schedulable. This operation sis
  // recursive on all children cgroups, they are thawed as well.
  //
  // Return:
  //   Status: The status of the operation. Iff OK, all the tasks in this cgroup
  //       are unfrozen.
  virtual ::util::Status Unfreeze();

  // Return the current state of the cgroup.
  virtual ::util::StatusOr<FreezerState> State() const;

 private:
  // Checks if any subcontainer exists and hierarchical freezing is supported.
  //
  // Returns:
  //  FAILED_PRECONDITION: If hierarchical freezing is unsupported and
  //  subcontainers exist.
  //  Status: If subcontainer information cannot be identified.
  //  OK: Otherwise.
  ::util::Status SafeToUpdate() const;

  // Returns true iff hierarchical freezing is supported in the current kernel.
  bool IsHierarchicalFreezingSupported() const;

  DISALLOW_COPY_AND_ASSIGN(FreezerController);
};

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_CONTROLLERS_FREEZER_CONTROLLER_H_
