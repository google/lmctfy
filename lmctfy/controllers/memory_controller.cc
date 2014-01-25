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

#include "lmctfy/controllers/memory_controller.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "base/integral_types.h"
#include "lmctfy/kernel_files.h"
#include "strings/numbers.h"
#include "strings/split.h"
#include "strings/stringpiece.h"
#include "strings/substitute.h"
#include "util/intops/safe_int.h"
#include "util/task/codes.pb.h"
#include "util/task/status.h"

using ::util::Bytes;
using ::strings::SkipEmpty;
using ::strings::Split;
using ::strings::Substitute;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {

MemoryController::MemoryController(const string &cgroup_path, bool owns_cgroup,
                                   const KernelApi *kernel,
                                   EventFdNotifications *eventfd_notifications)
    : CgroupController(CGROUP_MEMORY, cgroup_path, owns_cgroup, kernel,
                       eventfd_notifications) {}

// Cleanup on the input limits to be done.  For example, even though the kernel
// reports a maxint limit for "infinite", it refuses to read one in and needs
// to be passed -1 instead.
static Bytes ModifyLimit(Bytes limit) {
  if (limit >= Bytes(kint64max)) return Bytes(-1);
  return limit;
}

Status MemoryController::SetLimit(Bytes limit) {
  return SetParamBytes(KernelFiles::Memory::kLimitInBytes, ModifyLimit(limit));
}

Status MemoryController::SetSoftLimit(Bytes limit) {
  return SetParamBytes(KernelFiles::Memory::kSoftLimitInBytes,
                       ModifyLimit(limit));
}

Status MemoryController::SetStalePageAge(uint64 scan_cycles) {
  return SetParamInt(KernelFiles::Memory::kStalePageAge, scan_cycles);
}

Status MemoryController::SetOomScore(int64 oom_score) {
  return SetParamInt(KernelFiles::Memory::kOomScoreBadness, oom_score);
}

StatusOr<Bytes> MemoryController::GetWorkingSet() const {
  // Get usage in bytes.
  StatusOr<Bytes> statusor_bytes =
      GetParamBytes(KernelFiles::Memory::kUsageInBytes);
  if (!statusor_bytes.ok()) {
    return statusor_bytes.status();
  }
  Bytes usage_in_bytes = statusor_bytes.ValueOrDie();

  // Get stale bytes
  StatusOr<map<string, int64>> statusor_stats =
      GetStats(KernelFiles::Memory::kIdlePageStats);
  if (!statusor_stats.ok()) {
    return statusor_stats.status();
  }
  StatusOr<int64> statusor = GetValueFromStats(
      statusor_stats.ValueOrDie(), KernelFiles::Memory::IdlePageStats::kStale);
  if (!statusor.ok()) {
    return statusor.status();
  }
  Bytes stale = Bytes(statusor.ValueOrDie());

  // Working set is the usage minus the cold bytes (those that are stale).
  return max(usage_in_bytes - stale, Bytes(0));
}

StatusOr<Bytes> MemoryController::GetUsage() const {
  return GetParamBytes(KernelFiles::Memory::kUsageInBytes);
}

StatusOr<Bytes> MemoryController::GetMaxUsage() const {
  return GetParamBytes(KernelFiles::Memory::kMaxUsageInBytes);
}

StatusOr<Bytes> MemoryController::GetLimit() const {
  return GetParamBytes(KernelFiles::Memory::kLimitInBytes);
}

StatusOr<Bytes> MemoryController::GetEffectiveLimit() const {
  StatusOr<map<string, int64>> statusor = GetStats(KernelFiles::Memory::kStat);
  if (!statusor.ok()) {
    return statusor.status();
  }

  // Get the hierarchical memory limit from the memory stats.
  StatusOr<int64> statusor_stat =
      GetValueFromStats(statusor.ValueOrDie(),
                        KernelFiles::Memory::Stat::kHierarchicalMemoryLimit);
  if (!statusor_stat.ok()) {
    return statusor_stat.status();
  }

  return Bytes(statusor_stat.ValueOrDie());
}

StatusOr<Bytes> MemoryController::GetSoftLimit() const {
  return GetParamBytes(KernelFiles::Memory::kSoftLimitInBytes);
}

StatusOr<uint64> MemoryController::GetStalePageAge() const {
  return GetParamInt(KernelFiles::Memory::kStalePageAge);
}

StatusOr<int64> MemoryController::GetOomScore() const {
  return GetParamInt(KernelFiles::Memory::kOomScoreBadness);
}

StatusOr<map<string, int64>> MemoryController::GetStats(
    const string &stats_type) const {
  map<string, int64> output;

  StatusOr<string> statusor = GetParamString(stats_type);
  if (!statusor.ok()) {
    return statusor.status();
  }

  // TODO(vmarmol): Use FileLines.
  // Split line by line.
  vector<string> line_parts;
  for (StringPiece line : Split(statusor.ValueOrDie(), "\n", SkipEmpty())) {
    // Each line should be a space-separated key value pair.
    line_parts = Split(line, " ", SkipEmpty());
    if (line_parts.size() != 2) {
      return Status(::util::error::FAILED_PRECONDITION,
                    Substitute("Failed to parse pair from line \"$0\"", line));
    }

    // Parse the value as an int.
    int64 value = 0;
    if (!SimpleAtoi(line_parts[1], &value)) {
      return Status(
          ::util::error::FAILED_PRECONDITION,
          Substitute("Failed to parse int from \"$0\"", line_parts[1]));
    }
    output[line_parts[0]] = value;
  }

  return output;
}

StatusOr<int64> MemoryController::GetValueFromStats(
    const map<string, int64> &stats, const string &value) const {
  auto it = stats.find(value);
  if (it == stats.end()) {
    return Status(
        ::util::error::NOT_FOUND,
        Substitute("Failed to find \"$0\" in memory statistics", value));
  }

  return it->second;
}

Status MemoryController::SetParamBytes(const string &hierarchy_file,
                                       Bytes value) {
  return SetParamInt(hierarchy_file, value.value());
}

StatusOr<Bytes> MemoryController::GetParamBytes(
    const string &hierarchy_file) const {
  StatusOr<int64> statusor = GetParamInt(hierarchy_file);
  if (!statusor.ok()) {
    return statusor.status();
  }

  return Bytes(statusor.ValueOrDie());
}

StatusOr<ActiveNotifications::Handle> MemoryController::RegisterOomNotification(
    CgroupController::EventCallback *callback) {
  return RegisterNotification(KernelFiles::Memory::kOomControl, "", callback);
}

StatusOr<ActiveNotifications::Handle>
MemoryController::RegisterUsageThresholdNotification(
    Bytes usage_threshold, CgroupController::EventCallback *callback) {
  return RegisterNotification(KernelFiles::Memory::kUsageInBytes,
                              Substitute("$0", usage_threshold.value()),
                              callback);
}

}  // namespace lmctfy
}  // namespace containers
