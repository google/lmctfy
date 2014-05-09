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

#ifndef SRC_UTIL_PROC_CGROUPS_H_
#define SRC_UTIL_PROC_CGROUPS_H_

#include <string>
using ::std::string;

#include "base/macros.h"
#include "util/file_lines.h"

namespace containers {
namespace lmctfy {

class ProcCgroupsData {
 public:
  // The name of the cgroup hierarchy (e.g.: cpu, memory).
  string hierarchy_name;

  // The ID of the mounted hierarchy.
  int hierarchy_id;

  // The number of cgroups.
  int num_cgroups;

  // Whether the cgroup is enabled.
  bool enabled;
};

namespace proc_cgroups_internal {

bool ParseData(const char *line, ProcCgroupsData *data);

}  // namespace proc_cgroups_internal

class ProcCgroups : public ::util::TypedFileLines<
                        ProcCgroupsData, proc_cgroups_internal::ParseData> {
 public:
  ProcCgroups();
  ~ProcCgroups() override {}

 private:
  DISALLOW_COPY_AND_ASSIGN(ProcCgroups);
};

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_UTIL_PROC_CGROUPS_H_
