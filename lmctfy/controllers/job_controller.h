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

#ifndef SRC_CONTROLLERS_JOB_CONTROLLER_H_
#define SRC_CONTROLLERS_JOB_CONTROLLER_H_

#include <string>
using ::std::string;
#include <vector>

#include "base/macros.h"
#include "lmctfy/controllers/cgroup_controller.h"
#include "util/task/status.h"

namespace containers {
namespace lmctfy {

class JobController;

// Factory for JobControllers.
//
// Class is thread-safe.
class JobControllerFactory
    : public CgroupControllerFactory<JobController, CGROUP_JOB> {
 public:
  // Does not own cgroup_factory or kernel.
  JobControllerFactory(const CgroupFactory *cgroup_factory,
                       const KernelApi *kernel,
                       EventFdNotifications *eventfd_notifications)
      : CgroupControllerFactory<JobController, CGROUP_JOB>(
            cgroup_factory, kernel, eventfd_notifications) {}
  virtual ~JobControllerFactory() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(JobControllerFactory);
};

// Controller for the job cgroup hierarchy.
//
// Class is thread-safe.
class JobController : public CgroupController {
 public:
  // Does not take ownership of kernel.
  JobController(const string &hierarchy_path, const string &cgroup_path,
                bool owns_cgroup, const KernelApi *kernel,
                EventFdNotifications *eventfd_notifications)
      : CgroupController(CGROUP_JOB, hierarchy_path, cgroup_path, owns_cgroup,
                         kernel, eventfd_notifications) {}
  virtual ~JobController() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(JobController);
};

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_CONTROLLERS_JOB_CONTROLLER_H_
