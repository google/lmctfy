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

#ifndef UTIL_PROC_MOUNTS_H_
#define UTIL_PROC_MOUNTS_H_

#include <string>
#include <vector>

#include "base/integral_types.h"
#include "base/macros.h"
#include "util/file_lines.h"

namespace util {

// Information about a specific mount point in the system.
class ProcMountsData {
 public:
  // The device that is mounted.
  string device;

  // The absolute path of where the device is mounted.
  string mountpoint;

  // The type of the filesystem that is mounted.
  string type;

  // Mount options.
  ::std::vector<string> options;

  // Determines which filesystems need to be dumped by dump.
  int64 fs_freq;

  // Determines the order in which the filesystems are checked by fsck.
  int64 fs_passno;
};

namespace proc_mounts_internal {

bool ProcMountsParseLine(const char *line, ProcMountsData *data);

}  // namespace proc_mounts_internal

// Iterates over the mounts in the system. These are found through either
// /proc/mounts or /proc/<pid>/mounts.
//
// Class is thread-safe.
class ProcMounts : public TypedFileLines<
    ProcMountsData, proc_mounts_internal::ProcMountsParseLine> {
 public:
  // Iterates over the mounts in /proc/mounts.
  ProcMounts();

  // Iterates over the mounts in /proc/<pid>/mounts. PID 0 corresponds to the
  // current PID.
  explicit ProcMounts(pid_t pid);

  virtual ~ProcMounts() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(ProcMounts);
};

}  // namespace util

#endif  // UTIL_PROC_MOUNTS_H_
