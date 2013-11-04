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

#include "lmctfy/controllers/cpuset_controller.h"

#include "lmctfy/kernel_files.h"
#include "include/lmctfy.pb.h"
#include "util/errors.h"
#include "strings/substitute.h"
#include "util/os/core/cpu_set.h"
#include "util/task/status.h"

using ::util::ResSet;
using ::strings::Substitute;
using ::util_os_core::CpuSetInsert;
using ::util_os_core::CpuSetContains;
using ::util_os_core::CpuSetMakeEmpty;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {

// TODO(jnagal): Add formatting support directly to cpu_set_t or
// implement the logic here rather than borrowing from ResSet.
static ResSet ResSetFromCpuSet(const cpu_set_t &cpu_set) {
  ResSet res;
  for (int i = 0; i < CPU_SETSIZE; i++) {
    if (CpuSetContains(i, &cpu_set)) {
      res.insert(i);
    }
  }
  return res;
}

static string FormatCPUs(const cpu_set_t &cpu_set) {
  ResSet res_set = ResSetFromCpuSet(cpu_set);
  string cpu_str;
  res_set.Format(&cpu_str);
  return cpu_str;
}

static cpu_set_t ResSetToCpuSet(const ResSet &res) {
  cpu_set_t cpu_set(CpuSetMakeEmpty());
  for (auto it : res) {
    CHECK_LT(it, CPU_SETSIZE);
    CpuSetInsert(it, &cpu_set);
  }
  return cpu_set;
}

CpusetController::CpusetController(const string &cgroup_path, bool owns_cgroup,
                                   const KernelApi *kernel,
                                   EventFdNotifications *eventfd_notifications)
    : CgroupController(CGROUP_CPUSET, cgroup_path, owns_cgroup, kernel,
                       eventfd_notifications) {}

Status CpusetController::SetCpuMask(cpu_set_t cpuset) {
  string cpu_string = FormatCPUs(cpuset);
  return SetParamString(KernelFiles::CPUSet::kCPUs, cpu_string);
}

StatusOr<cpu_set_t> CpusetController::GetCpuMask() const {
  string cpu_string;
  RETURN_IF_ERROR(GetParamString(KernelFiles::CPUSet::kCPUs), &cpu_string);
  ResSet res_set;
  res_set.ReadSetString(cpu_string, ",");
  return ResSetToCpuSet(res_set);
}

Status CpusetController::SetMemoryNodes(const ResSet& memory_nodes) {
  string memory_nodes_string;
  memory_nodes.Format(&memory_nodes_string);
  return SetParamString(KernelFiles::CPUSet::kMemNodes, memory_nodes_string);
}

StatusOr<ResSet> CpusetController::GetMemoryNodes() const {
  string memory_nodes_string;
  RETURN_IF_ERROR(GetParamString(KernelFiles::CPUSet::kMemNodes),
                  &memory_nodes_string);
  ResSet memory_nodes;
  memory_nodes.ReadSetString(memory_nodes_string, ",");
  return memory_nodes;
}

}  // namespace lmctfy
}  // namespace containers
