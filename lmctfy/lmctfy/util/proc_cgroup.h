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

#ifndef SRC_UTIL_PROC_CGROUP_H_
#define SRC_UTIL_PROC_CGROUP_H_

#include <string>
using ::std::string;

#include "base/integral_types.h"
#include "base/macros.h"
#include "util/file_lines.h"

namespace containers {
namespace lmctfy {

// Information about active cgroups on a process.
class ProcCgroupData {
 public:
  // The ID of the mounted hierarchy.
  int hierarchy_id;

  // The cgroup subsystems mounted on this hierarchy.
  ::std::vector<string> subsystems;

  // The path within the hierarchy that this process belongs to.
  // i.e.: If the process is at /dev/cgroup/test/sub (the cgroup hierarchy is
  // mounted at /dev/cgroup) then the hierarchy_path is /test/sub.
  string hierarchy_path;
};

namespace proc_cgroup_internal {

bool ParseProcCgroupData(const char *line, ProcCgroupData *data);

}  // namespace proc_cgroup_internal

// Iterates over the mounted cgroup hierarchies a process belongs to. These are
// found in /proc/<pid>/cgroup.
//
// Class is thread-compatible.
class ProcCgroup
    : public ::util::TypedFileLines<
          ProcCgroupData, proc_cgroup_internal::ParseProcCgroupData> {
 public:
  // PID 0 corresponds to the current PID.
  explicit ProcCgroup(pid_t pid);
  virtual ~ProcCgroup() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(ProcCgroup);
};

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_UTIL_PROC_CGROUP_H_
