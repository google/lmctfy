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

#include "util/proc_mounts.h"

#include <string>
#include <vector>

#include "base/integral_types.h"
#include "base/macros.h"
#include "strings/numbers.h"
#include "strings/split.h"
#include "strings/substitute.h"
#include "strings/strip.h"
#include "util/file_lines.h"

using ::strings::SkipEmpty;
using ::strings::Split;
using ::strings::Substitute;

namespace util {
namespace proc_mounts_internal {

bool ProcMountsParseLine(const char *line, ProcMountsData *data) {
  // Ensure we have the number of elements we expect.
  const vector<string> elements = Split(line, " ");
  if (elements.size() != 6) {
    LOG(WARNING) << "Could not parse invalid mount line \"" << line << "\"";
    return false;
  }

  // Fill in information about this mount point.
  data->device = elements[0];
  // Strip off deleted suffix.
  data->mountpoint = StripSuffixString(elements[1], R"(\040(deleted))");
  data->type = elements[2];
  data->options = Split(elements[3], ",", SkipEmpty());
  if (!SimpleAtoi(elements[4], &(data->fs_freq))) {
    LOG(WARNING) << "Unable to parse fs_freq from \"" << elements[4] << "\"";
    data->fs_freq = 0;
  }
  if (!SimpleAtoi(elements[5], &(data->fs_passno))) {
    LOG(WARNING) << "Unable to parse fs_passno from \"" << elements[5] << "\"";
    data->fs_passno = 0;
  }

  return true;
}

}  // namespace proc_mounts_internal

ProcMounts::ProcMounts()
    : TypedFileLines<ProcMountsData, proc_mounts_internal::ProcMountsParseLine>(
          "/proc/mounts") {}

ProcMounts::ProcMounts(pid_t pid)
    : TypedFileLines<ProcMountsData,
                     proc_mounts_internal::ProcMountsParseLine>(Substitute(
          "/proc/$0/mounts", pid == 0 ? "self" : Substitute("$0", pid))) {}

}  // namespace util
