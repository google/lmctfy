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

#include "lmctfy/controllers/cpuset_controller.h"

#include "lmctfy/kernel_files.h"
#include "include/lmctfy.pb.h"
#include "util/cpu_mask.h"
#include "util/errors.h"
#include "strings/substitute.h"
#include "util/task/status.h"

using ::util::ResSet;
using ::strings::Substitute;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {

using ::util::CpuMask;

// TODO(jnagal): Add formatting support directly to CpuMask or
// implement the logic here rather than borrowing from ResSet.
static ResSet ResSetFromCpuMask(const CpuMask &cpu_mask) {
  ResSet res;
  for (int i = 0; i < CPU_SETSIZE; i++) {
    if (cpu_mask.IsSet(i)) {
      res.insert(i);
    }
  }
  return res;
}

static string FormatCPUs(const CpuMask &cpu_mask) {
  ResSet res_set = ResSetFromCpuMask(cpu_mask);
  string cpu_str;
  res_set.Format(&cpu_str);
  return cpu_str;
}

static CpuMask ResSetToCpuMask(const ResSet &res) {
  CpuMask cpu_mask;
  for (auto it : res) {
    CHECK_LT(it, CPU_SETSIZE);
    cpu_mask.Set(it);
  }
  return cpu_mask;
}

CpusetController::CpusetController(const string &hierarchy_path,
                                   const string &cgroup_path, bool owns_cgroup,
                                   const KernelApi *kernel,
                                   EventFdNotifications *eventfd_notifications)
    : CgroupController(CGROUP_CPUSET, hierarchy_path, cgroup_path, owns_cgroup,
                       kernel, eventfd_notifications) {}

Status CpusetController::SetCpuMask(const CpuMask &mask) {
  string cpu_string = FormatCPUs(mask);
  return SetParamString(KernelFiles::CPUSet::kCPUs, cpu_string);
}

StatusOr<CpuMask> CpusetController::GetCpuMask() const {
  string cpu_string =
      RETURN_IF_ERROR(GetParamString(KernelFiles::CPUSet::kCPUs));
  ResSet res_set;
  res_set.ReadSetString(cpu_string, ",");
  return ResSetToCpuMask(res_set);
}

Status CpusetController::SetMemoryNodes(const ResSet& memory_nodes) {
  string memory_nodes_string;
  memory_nodes.Format(&memory_nodes_string);
  return SetParamString(KernelFiles::CPUSet::kMemNodes, memory_nodes_string);
}

StatusOr<ResSet> CpusetController::GetMemoryNodes() const {
  string memory_nodes_string =
      RETURN_IF_ERROR(GetParamString(KernelFiles::CPUSet::kMemNodes));
  ResSet memory_nodes;
  memory_nodes.ReadSetString(memory_nodes_string, ",");
  return memory_nodes;
}

}  // namespace lmctfy
}  // namespace containers
