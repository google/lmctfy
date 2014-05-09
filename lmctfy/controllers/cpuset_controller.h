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

#ifndef SRC_CONTROLLERS_CPUSET_CONTROLLER_H_
#define SRC_CONTROLLERS_CPUSET_CONTROLLER_H_

#include <sched.h>
#include <string>
using ::std::string;

#include "base/macros.h"
#include "util/resset.h"
#include "lmctfy/controllers/cgroup_controller.h"
#include "include/lmctfy.pb.h"
#include "util/cpu_mask.h"
#include "util/task/statusor.h"

namespace containers {
namespace lmctfy {

class CgroupFactory;
class CpusetController;

// Factory for CpusetControllers.
//
// Class is thread-safe.
class CpusetControllerFactory
    : public CgroupControllerFactory<CpusetController, CGROUP_CPUSET> {
 public:
  CpusetControllerFactory(const CgroupFactory *cgroup_factory,
                          const KernelApi *kernel,
                          EventFdNotifications *eventfd_notifications)
      : CgroupControllerFactory<CpusetController, CGROUP_CPUSET>(
            cgroup_factory, kernel, eventfd_notifications) {}
  virtual ~CpusetControllerFactory() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(CpusetControllerFactory);
};

// Controller for cpuset cgroup.
// Controls cpu and memory affinity settings for a container.
//
// Class is thread-safe.
class CpusetController : public CgroupController {
 public:
  CpusetController(const string &hierarchy_path, const string &cgroup_path,
                   bool owns_cgroup, const KernelApi *kernel,
                   EventFdNotifications *eventfd_notifications);
  virtual ~CpusetController() {}

  // Set/inherit cpu mask for this cgroup.
  virtual ::util::Status SetCpuMask(
      const ::util::CpuMask &mask);

  // Set/inherit memory nodes accessible to this container.
  virtual ::util::Status SetMemoryNodes(
      const util::ResSet &memory_nodes);

  // All statistics return NOT_FOUND if they were not found or available.

  // Retrieve affinity mask for the container.
  // TODO(jnagal): Returning ResSet here would make it more consistent with
  // GetMemoryNodes(), but Memory node manipulation should go away soon.
  virtual ::util::StatusOr<::util::CpuMask> GetCpuMask()
      const;

  // Retrieve memory nodes setting for this container.
  virtual ::util::StatusOr<util::ResSet> GetMemoryNodes() const;

 private:
  DISALLOW_COPY_AND_ASSIGN(CpusetController);
};

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_CONTROLLERS_CPUSET_CONTROLLER_H_
