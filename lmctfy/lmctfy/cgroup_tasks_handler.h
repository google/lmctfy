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
#include "util/safe_types/unix_gid.h"
#include "util/safe_types/unix_uid.h"
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
  //   tasks_handler_factory: Factory of TasksHandlers. Does not take ownership.
  CgroupTasksHandler(const string &container_name,
                     CgroupController *cgroup_controller,
                     const TasksHandlerFactory *tasks_handler_factory);
  ~CgroupTasksHandler() override;

  // Further documentation on these methods can be found in the TasksHandler
  // interface definition.

  ::util::Status Destroy() override;
  ::util::Status TrackTasks(const ::std::vector<pid_t> &tids) override;
  ::util::Status Delegate(::util::UnixUid uid,
                          ::util::UnixGid gid) override;
  ::util::Status PopulateMachineSpec(MachineSpec *spec) const override;
  ::util::StatusOr<::std::vector<string>> ListSubcontainers(
      TasksHandler::ListType type) const override;
  ::util::StatusOr<::std::vector<pid_t>> ListProcesses(
      ListType type) const override;
  ::util::StatusOr<::std::vector<pid_t>> ListThreads(
      ListType type) const override;

 private:
  enum class PidsOrTids {
    PIDS,
    TIDS,
  };
  // Lists the PIDs or TIDs inside the container, as specified by the ListType.
  ::util::Status ListProcessesOrThreads(TasksHandler::ListType type,
                                        PidsOrTids pids_or_tids,
                                        vector<pid_t> *output) const;

  // Controller for the underlying cgroup hierarchy.
  ::std::unique_ptr<CgroupController> cgroup_controller_;

  // Wrapper for all calls to the kernel.
  const KernelApi *kernel_;

  // Factory of TasksHandlers for recursive calls.
  const TasksHandlerFactory *tasks_handler_factory_;
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
  ~CgroupTasksHandlerFactory() override {}

  ::util::StatusOr<TasksHandler *> Create(
      const string &container_name, const ContainerSpec &spec) const override {
    // TODO(vmarmol): Consider keeping track of hierarchy mapping in the
    // controller to ensure things like this are valid (i.e.: CgroupTasksHandler
    // expects a 1:1 controller, make sure it gets one).
    // Create the controller. Hierarchy is 1:1.
    CgroupController *cgroup_controller =
        RETURN_IF_ERROR(cgroup_controller_factory_->Create(container_name));

    return new CgroupTasksHandler(container_name, cgroup_controller, this);
  }

  ::util::StatusOr<TasksHandler *> Get(const string &container_name)
      const override {
    // Get the controller. Hierarchy is 1:1.
    CgroupController *cgroup_controller =
        RETURN_IF_ERROR(cgroup_controller_factory_->Get(container_name));

    return new CgroupTasksHandler(container_name, cgroup_controller, this);
  }

  bool Exists(const string &container_name) const override {
    return cgroup_controller_factory_->Exists(container_name);
  }

  ::util::StatusOr<string> Detect(pid_t tid) const override {
    return cgroup_controller_factory_->DetectCgroupPath(tid);
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
