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

#ifndef SRC_CONTROLLERS_PERF_CONTROLLER_H_
#define SRC_CONTROLLERS_PERF_CONTROLLER_H_

#include "lmctfy/controllers/cgroup_controller.h"

namespace containers {
namespace lmctfy {

class CgroupFactory;
class PerfController;

// Factory for PerfControllers.
//
// Class is thread-safe.
class PerfControllerFactory
    : public CgroupControllerFactory<PerfController, CGROUP_PERF_EVENT> {
 public:
  // Does not own the cgroup_factory or kernel.
  PerfControllerFactory(const CgroupFactory *cgroup_factory,
                        const KernelApi *kernel,
                        EventFdNotifications *eventfd_notifications)
      : CgroupControllerFactory<PerfController, CGROUP_PERF_EVENT>(
            cgroup_factory, kernel, eventfd_notifications) {}
  virtual ~PerfControllerFactory() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(PerfControllerFactory);
};

// Controller for perf events.
//
// Class is thread-safe.
class PerfController : public CgroupController {
 public:
  // Does not take ownership of kernel.
  PerfController(const string &hierarchy_path, const string &cgroup_path,
                 bool owns_cgroup, const KernelApi *kernel,
                 EventFdNotifications *eventfd_notifications);
  virtual ~PerfController() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(PerfController);
};

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_CONTROLLERS_PERF_CONTROLLER_H_
