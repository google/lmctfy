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

#include "lmctfy/util/proc_cgroups.h"

#include <vector>

#include "strings/numbers.h"
#include "strings/split.h"

using ::std::vector;
using ::strings::SkipWhitespace;
using ::strings::Split;

namespace containers {
namespace lmctfy {
namespace proc_cgroups_internal {

// Expected format of /proc/cgroups:
//
//   #subsys_name    hierarchy       num_cgroups     enabled
//   cpuset  10      1       1

bool ParseData(const char *line, ProcCgroupsData *data) {
  // Ignore the first line.
  if (line[0] == '#') {
    return false;
  }

  const vector<string> elements = Split(line, " ", SkipWhitespace());
  if (elements.size() != 4) {
    return false;
  }

  data->hierarchy_name = elements[0];
  if (!SimpleAtoi(elements[1], &data->hierarchy_id)) {
    return false;
  }
  if (!SimpleAtoi(elements[2], &data->num_cgroups)) {
    return false;
  }

  int enabled = 0;
  if (!SimpleAtoi(elements[3], &enabled)) {
    return false;
  }
  data->enabled = (enabled == 1);

  return true;
}

}  // namespace proc_cgroups_internal

ProcCgroups::ProcCgroups()
    : ::util::TypedFileLines<
          ProcCgroupsData, proc_cgroups_internal::ParseData>("/proc/cgroups") {}

}  // namespace lmctfy
}  // namespace containers
