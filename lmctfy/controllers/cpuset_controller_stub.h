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

#ifndef SRC_CONTROLLERS_CPUSET_CONTROLLER_STUB_H_
#define SRC_CONTROLLERS_CPUSET_CONTROLLER_STUB_H_

#include "lmctfy/controllers/cgroup_controller.h"
#include "lmctfy/controllers/cpuset_controller.h"
#include "util/os/core/cpu_set.h"
#include "util/task/statusor.h"

namespace containers {
namespace lmctfy {


// A stub implementation of cpuset controller. All interfaces are no-op and
// always return successfully.
// This is useful for injecting in cpu resource handler for subcontainer that
// don't have a cpuset hierarchy visible. All cpuset actions can be ignored on
// subcontainers.

class CpusetControllerStub : public CpusetController {
 public:
  explicit CpusetControllerStub(const string &cgroup_path) :
    CpusetController(cgroup_path, false,
                     reinterpret_cast<KernelApi *>(0xFFFFFFFF),
                     reinterpret_cast<EventFdNotifications *>(0xFFFFFFFF)),
  cpu_set_(::util_os_core::CpuSetMakeEmpty()) {
      // Default cpu mask has 64 cores set.
      for (int i = 0; i < 64; i++) {
        ::util_os_core::CpuSetInsert(i, &cpu_set_);
      }
      // Default memory nodes has 2 nodes set.
      memory_nodes_.ReadSetString("1,2", ",");
    }
  virtual ~CpusetControllerStub() {}

  virtual ::util::Status Destroy() {
    return ::util::Status::OK;
  }

  virtual ::util::Status Enter(pid_t pid) {
    return ::util::Status::OK;
  }

  virtual ::util::Status SetCpuMask(cpu_set_t cpuset) {
    return ::util::Status::OK;
  }

  virtual ::util::Status SetMemoryNodes(
      const util::ResSet& memory_nodes) {
    return ::util::Status::OK;
  }

  // Return mask with 64 cpus set.
  virtual ::util::StatusOr<cpu_set_t> GetCpuMask() const {
    return cpu_set_;
  }

  // Return 2 node topology.
  virtual ::util::StatusOr<util::ResSet> GetMemoryNodes() const {
    return memory_nodes_;
  }

 private:
  cpu_set_t cpu_set_;
  util::ResSet memory_nodes_;

  DISALLOW_COPY_AND_ASSIGN(CpusetControllerStub);
};

}  // namespace lmctfy
}  // namespace containers
#endif  // SRC_CONTROLLERS_CPUSET_CONTROLLER_STUB_H_
