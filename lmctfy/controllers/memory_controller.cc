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

Status MemoryController::SetSwapLimit(Bytes limit) {
  return SetParamBytes(KernelFiles::Memory::Memsw::kLimitInBytes,
                       ModifyLimit(limit));
}

Status MemoryController::SetStalePageAge(int32 scan_cycles) {
  return SetParamInt(KernelFiles::Memory::kStalePageAge, scan_cycles);
}

Status MemoryController::SetOomScore(int64 oom_score) {
  return SetParamInt(KernelFiles::Memory::kOomScoreBadness, oom_score);
}

Status MemoryController::SetCompressionSamplingRatio(int32 ratio) {
  return SetParamInt(KernelFiles::Memory::kCompressionSamplingRatio, ratio);
}

Status MemoryController::SetDirtyRatio(int32 ratio) {
  return SetParamInt(KernelFiles::Memory::kDirtyRatio, ratio);
}

Status MemoryController::SetDirtyBackgroundRatio(int32 ratio) {
  return SetParamInt(KernelFiles::Memory::kDirtyBackgroundRatio, ratio);
}

Status MemoryController::SetDirtyLimit(Bytes limit) {
  return SetParamBytes(KernelFiles::Memory::kDirtyLimitInBytes, limit);
}

Status MemoryController::SetDirtyBackgroundLimit(Bytes limit) {
  return SetParamBytes(KernelFiles::Memory::kDirtyBackgroundLimitInBytes,
                       limit);
}

StatusOr<Bytes> MemoryController::GetStaleBytes() const {
  map<string, int64> stats =
      RETURN_IF_ERROR(GetStats(KernelFiles::Memory::kIdlePageStats));
  int64 stale = RETURN_IF_ERROR(GetValueFromStats(stats,
      KernelFiles::Memory::IdlePageStats::kStale));
  return Bytes(stale);
}

StatusOr<Bytes> MemoryController::GetInactiveBytes() const {
  map<string, int64> stats =
      RETURN_IF_ERROR(GetStats(KernelFiles::Memory::kStat));
  int64 inactive_anon = RETURN_IF_ERROR(GetValueFromStats(stats,
      KernelFiles::Memory::Stat::kTotalInactiveAnon));

  int64 inactive_file = RETURN_IF_ERROR(GetValueFromStats(stats,
      KernelFiles::Memory::Stat::kTotalInactiveFile));
  return Bytes(inactive_anon + inactive_file);
}

// POPULATE_STAT requires the proto buffer field name to match the post-prefix
// part of the stat name.
#define POPULATE_STAT(stats, prefix, output, name)         \
{                                                          \
  int64 val;                                               \
  if (GetStatFromMap(stats, prefix + #name, &val)) {  \
    output->set_##name(val);                               \
  }                                                        \
}

namespace {
bool GetStatFromMap(const map<string, int64> &stats, const string &key,
                    int64 *val) {
  const auto it = stats.find(key);
  if (it == stats.end()) return false;
  *val = it->second;
  return true;
}

void ProcessKernelStats(const map<string, int64> &stats,
                        const string &prefix,
                        MemoryStats_MemoryData_Kernel *output) {
  POPULATE_STAT(stats, prefix, output, memory);
  POPULATE_STAT(stats, prefix, output, slab_memory);
  POPULATE_STAT(stats, prefix, output, stack_memory);
  POPULATE_STAT(stats, prefix, output, pgtable_memory);
  POPULATE_STAT(stats, prefix, output, vmalloc_memory);
  POPULATE_STAT(stats, prefix, output, misc_memory);
  POPULATE_STAT(stats, prefix, output, targeted_slab_memory);
  POPULATE_STAT(stats, prefix, output, compressed_memory);
}

void ProcessThpStats(const map<string, int64> &stats,
                     const string &prefix,
                     MemoryStats_MemoryData_THP *output) {
  POPULATE_STAT(stats, prefix, output, fault_alloc);
  POPULATE_STAT(stats, prefix, output, fault_fallback);
  POPULATE_STAT(stats, prefix, output, collapse_alloc);
  POPULATE_STAT(stats, prefix, output, collapse_alloc_failed);
  POPULATE_STAT(stats, prefix, output, split);
}

void ProcessMemoryStats(const map<string, int64> &stats,
                        const string &prefix,
                        MemoryStats_MemoryData *output) {
  POPULATE_STAT(stats, prefix, output, cache);
  POPULATE_STAT(stats, prefix, output, rss);
  POPULATE_STAT(stats, prefix, output, rss_huge);
  POPULATE_STAT(stats, prefix, output, mapped_file);
  POPULATE_STAT(stats, prefix, output, pgpgin);
  POPULATE_STAT(stats, prefix, output, pgfault);
  POPULATE_STAT(stats, prefix, output, pgmajfault);
  POPULATE_STAT(stats, prefix, output, dirty);
  POPULATE_STAT(stats, prefix, output, writeback);
  POPULATE_STAT(stats, prefix, output, inactive_anon);
  POPULATE_STAT(stats, prefix, output, active_anon);
  POPULATE_STAT(stats, prefix, output, inactive_file);
  POPULATE_STAT(stats, prefix, output, active_file);
  POPULATE_STAT(stats, prefix, output, unevictable);

  ProcessThpStats(stats, prefix + "thp_", output->mutable_thp());

  ProcessKernelStats(stats, prefix + "kernel_", output->mutable_kernel());
  ProcessKernelStats(stats, prefix + "kernel_noncharged_",
                     output->mutable_kernel_noncharged());

  POPULATE_STAT(stats, prefix, output, compressed_pool_pages);
  POPULATE_STAT(stats, prefix, output, compressed_stored_pages);
  POPULATE_STAT(stats, prefix, output, compressed_reject_compress_poor);
  POPULATE_STAT(stats, prefix, output, zswap_zsmalloc_fail);
  POPULATE_STAT(stats, prefix, output, zswap_kmemcache_fail);
  POPULATE_STAT(stats, prefix, output, zswap_duplicate_entry);
  POPULATE_STAT(stats, prefix, output, zswap_compressed_pages);
  POPULATE_STAT(stats, prefix, output, zswap_decompressed_pages);
  POPULATE_STAT(stats, prefix, output, zswap_compression_nsec);
  POPULATE_STAT(stats, prefix, output, zswap_decompression_nsec);
  POPULATE_STAT(stats, prefix, output, cache);
}

}  // namespace

Status MemoryController::GetMemoryStats(MemoryStats *memory_stats) const {
  map<string, int64> stats =
      RETURN_IF_ERROR(GetStats(KernelFiles::Memory::kStat));
  ProcessMemoryStats(stats, "", memory_stats->mutable_container_data());
  ProcessMemoryStats(stats, "total_", memory_stats->mutable_total_data());
  POPULATE_STAT(stats, , memory_stats, hierarchical_memory_limit);
  return Status::OK;
}
#undef POPULATE_STAT

StatusOr<Bytes> MemoryController::GetWorkingSet() const {
  // Get usage in bytes.
  Bytes usage_in_bytes =
      RETURN_IF_ERROR(GetParamBytes(KernelFiles::Memory::kUsageInBytes));

  // Get stale bytes
  StatusOr<Bytes> statusor_stale = GetStaleBytes();
  if (!statusor_stale.ok()) {
    if (statusor_stale.status().error_code() != ::util::error::NOT_FOUND) {
      return statusor_stale.status();
    } else {
      // Either the idle page stat file is not found, or entry for stale pages
      // in the file is not found. Use total inactive bytes.
      statusor_stale = RETURN_IF_ERROR(GetInactiveBytes());
    }
  }
  Bytes stale = Bytes(statusor_stale.ValueOrDie());

  // Working set is the usage minus the cold bytes (those that are stale).
  return max(usage_in_bytes - stale, Bytes(0));
}

StatusOr<Bytes> MemoryController::GetUsage() const {
  return GetParamBytes(KernelFiles::Memory::kUsageInBytes);
}

StatusOr<Bytes> MemoryController::GetMaxUsage() const {
  return GetParamBytes(KernelFiles::Memory::kMaxUsageInBytes);
}

StatusOr<Bytes> MemoryController::GetSwapMaxUsage() const {
  return GetParamBytes(KernelFiles::Memory::Memsw::kMaxUsageInBytes);
}

StatusOr<Bytes> MemoryController::GetSwapLimit() const {
  return GetParamBytes(KernelFiles::Memory::Memsw::kLimitInBytes);
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

StatusOr<Bytes> MemoryController::GetSwapUsage() const {
  return GetParamBytes(KernelFiles::Memory::Memsw::kUsageInBytes);
}

StatusOr<int32> MemoryController::GetStalePageAge() const {
  return GetParamInt(KernelFiles::Memory::kStalePageAge);
}

StatusOr<int64> MemoryController::GetOomScore() const {
  return GetParamInt(KernelFiles::Memory::kOomScoreBadness);
}

StatusOr<int32> MemoryController::GetCompressionSamplingRatio() const {
  return GetParamInt(KernelFiles::Memory::kCompressionSamplingRatio);
}

StatusOr<int32> MemoryController::GetDirtyRatio() const {
  return GetParamInt(KernelFiles::Memory::kDirtyRatio);
}

StatusOr<int32> MemoryController::GetDirtyBackgroundRatio() const {
  return GetParamInt(KernelFiles::Memory::kDirtyBackgroundRatio);
}

StatusOr<Bytes> MemoryController::GetDirtyLimit() const {
  return GetParamBytes(KernelFiles::Memory::kDirtyLimitInBytes);
}

StatusOr<Bytes> MemoryController::GetDirtyBackgroundLimit() const {
  return GetParamBytes(KernelFiles::Memory::kDirtyBackgroundLimitInBytes);
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
