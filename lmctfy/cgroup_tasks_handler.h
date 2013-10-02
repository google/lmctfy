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

#ifndef SRC_CGROUP_TASKS_HANDLER_H_
#define SRC_CGROUP_TASKS_HANDLER_H_

#include <sys/types.h>
#include <memory>
#include <string>
using ::std::string;
#include <vector>

#include "system_api/kernel_api.h"
#include "lmctfy/controllers/cgroup_controller.h"
#include "lmctfy/tasks_handler.h"
#include "util/errors.h"
#include "strings/split.h"
#include "strings/substitute.h"
#include "util/task/statusor.h"

namespace containers {
namespace lmctfy {

typedef ::system_api::KernelAPI KernelApi;

// Cgroup-based TasksHandler for a specific container.
//
// The Cgroup-based TasksHandler has a 1-to-1 mapping of container name to a
// cgroup hierarchy, e.g.:
//
// /             -> /dev/cgroup/<hierarchy>
// /sys          -> /dev/cgroup/<hierarchy>/sys
// /task/subtask -> /dev/cgroup/<hierarchy>/task/subtask
//
// Class is thread-compatible.
class CgroupTasksHandler : public TasksHandler {
 public:
  // Arguments:
  //   container_name: The absolute name of the container this TasksHandler will
  //       handle.
  //   cgroup_controller: Controller for the underlying cgroup hierarchy. Takes
  //       ownership of pointer.
  //   kernel: Wrapper for kernel syscalls. Does not take ownership.
  CgroupTasksHandler(const string &container_name,
                     CgroupController *cgroup_controller);
  virtual ~CgroupTasksHandler();

  // Further documentation on these methods can be found in the TasksHandler
  // interface definition.

  virtual ::util::Status Destroy();
  virtual ::util::Status TrackTasks(const ::std::vector<pid_t> &tids);
  virtual ::util::StatusOr< ::std::vector<string>> ListSubcontainers() const;
  virtual ::util::StatusOr< ::std::vector<pid_t>> ListProcesses() const;
  virtual ::util::StatusOr< ::std::vector<pid_t>> ListThreads() const;

 private:
  // Controller for the underlying cgroup hierarchy.
  ::std::unique_ptr<CgroupController> cgroup_controller_;

  // Wrapper for all calls to the kernel.
  const KernelApi *kernel_;
};

// Factory of Cgroup-based TasksHandlers.
//
// Class is thread-safe.
template <typename ControllerType>
class CgroupTasksHandlerFactory : public TasksHandlerFactory {
 public:
  typedef CgroupControllerFactoryInterface<ControllerType>
      CgroupControllerFactoryType;

  // Arguments:
  //   cgroup_controller_factory: Factory of CgroupControllers, takes
  // ownership
  //       of pointer.
  //   kernel: Wrapper for kernel syscalls. Does not take ownership.
  CgroupTasksHandlerFactory(
      CgroupControllerFactoryType *cgroup_controller_factory,
      const KernelApi *kernel)
      : TasksHandlerFactory(),
        cgroup_controller_factory_(cgroup_controller_factory),
        kernel_(kernel) {}
  virtual ~CgroupTasksHandlerFactory() {}

  virtual ::util::StatusOr<TasksHandler *> Create(
      const string &container_name) const {
    // TODO(vmarmol): Consider keeping track of hierarchy mapping in the
    // controller to ensure things like this are valid (i.e.: CgroupTasksHandler
    // expects a 1:1 controller, make sure it gets one).
    // Create the controller. Hierarchy is 1:1.
    CgroupController *cgroup_controller;
    RETURN_IF_ERROR(cgroup_controller_factory_->Create(container_name),
                    &cgroup_controller);

    return new CgroupTasksHandler(container_name, cgroup_controller);
  }

  virtual ::util::StatusOr<TasksHandler *> Get(
      const string &container_name) const {
    // Get the controller. Hierarchy is 1:1.
    CgroupController *cgroup_controller;
    RETURN_IF_ERROR(cgroup_controller_factory_->Get(container_name),
                    &cgroup_controller);

    return new CgroupTasksHandler(container_name, cgroup_controller);
  }

  virtual bool Exists(const string &container_name) const {
    return cgroup_controller_factory_->Exists(container_name);
  }

  // TODO(vmarmol): Use a specialization of FileLines for most of this.
  virtual ::util::StatusOr<string> Detect(pid_t tid) const {
    // 0 is an alias for self.
    string proc_cgroup_path;
    if (tid == 0) {
      proc_cgroup_path = "/proc/self/cgroup";
    } else {
      proc_cgroup_path = ::strings::Substitute("/proc/$0/cgroup", tid);
    }

    string contents;
    if (!kernel_->ReadFileToString(proc_cgroup_path, &contents)) {
      return ::util::Status(
          ::util::error::FAILED_PRECONDITION,
          ::strings::Substitute(
              "Failed to read \"$0\" while detecting container",
              proc_cgroup_path));
    }

    // Get the name of the subsystem (cgroup hierarchy).
    const string subsystem_name = cgroup_controller_factory_->HierarchyName();

    // /proc/<tid>/cgroups is in the following format:
    // <mount integernumber>:<comma-separated list of subsytem names>:<cgroup
    // path in the subsystem>
    //
    // e.g. (for container /sys):
    //   7:net:/sys
    //   6:memory:/sys
    //   5:job:/sys
    //   4:io:/sys
    //   3:cpuset:/sys
    //   2:cpuacct,cpu:/sys
    //   1:bcache,rlimit,perf_event:/sys

    // Go through each line looking for this subsystem's mount point.
    for (const auto &line :
         ::strings::Split(contents, "\n", ::strings::SkipEmpty())) {
      // Ensure the line is as we expected, else skip it.
      const vector<string> elements =
          ::strings::Split(line, ":", ::strings::SkipEmpty());
      if (elements.size() < 3) {
        LOG(WARNING) << "Failed to parse line \"" << line.ToString()
                     << "\" from file \"" << proc_cgroup_path
                     << "\", skipping line";
        continue;
      }

      // Check if this subsystem is in this line.
      const vector<string> subsystems =
          ::strings::Split(elements[1], ",", ::strings::SkipEmpty());
      if (find(subsystems.begin(), subsystems.end(), subsystem_name) !=
          subsystems.end()) {
        // The container name is the cgroup path since this TasksHandler makes a
        // 1:1 mapping.
        // TODO(vmarmol): Check for bad container name
        return elements[2];
      }
    }

    return ::util::Status(
        ::util::error::NOT_FOUND,
        ::strings::Substitute("Could not detect the container for TID \"$0\"",
                              tid));
  }

 private:
  // Factory for CgroupControllers.
  ::std::unique_ptr<CgroupControllerFactoryType> cgroup_controller_factory_;

  // Wrapper for all calls to the kernel.
  const KernelApi *kernel_;
};

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_CGROUP_TASKS_HANDLER_H_
