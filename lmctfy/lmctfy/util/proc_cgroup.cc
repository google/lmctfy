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

#include "lmctfy/util/proc_cgroup.h"

#include "util/file_lines.h"
#include "strings/numbers.h"
#include "strings/split.h"
#include "strings/strip.h"
#include "strings/substitute.h"

using ::strings::SkipEmpty;
using ::strings::Split;
using ::strings::Substitute;

namespace containers {
namespace lmctfy {

namespace proc_cgroup_internal {

bool ParseProcCgroupData(const char *line, ProcCgroupData *data) {
  const vector<string> elements = Split(line, ":");
  if (elements.size() != 3) {
    return false;
  }

  int hid = -1;
  if (!SimpleAtoi(elements[0], &hid)) {
    return false;
  }
  data->hierarchy_id = hid;
  data->subsystems = Split(elements[1], ",", SkipEmpty());
  data->hierarchy_path = elements[2];
  StripTrailingNewline(&data->hierarchy_path);

  return true;
}

}  // namespace proc_cgroup_internal

ProcCgroup::ProcCgroup(pid_t pid)
    : ::util::TypedFileLines<
          ProcCgroupData, proc_cgroup_internal::ParseProcCgroupData>(Substitute(
          "/proc/$0/cgroup", pid == 0 ? "self" : Substitute("$0", pid))) {}

}  // namespace lmctfy
}  // namespace containers
