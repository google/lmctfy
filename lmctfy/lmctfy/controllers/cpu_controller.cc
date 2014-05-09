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

#include "lmctfy/controllers/cpu_controller.h"

#include "lmctfy/kernel_files.h"
#include "include/lmctfy.pb.h"
#include "util/errors.h"
#include "strings/split.h"
#include "strings/substitute.h"

using ::strings::SkipEmpty;
using ::strings::Split;
using ::strings::Substitute;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {

// Throughput settings.
// CFS cannot accept share values lower than 2.
static const int64 kMinShares = 2;
// cpurate to CFS share conversion factor: 1 cpu-secs/sec is 1024 shares.
static const int kPerCpuShares = 1024;
static const int kCpusToMilliCpus = 1000;

// Throttling settings.
// Use a default throttling period of 250ms. New quota is issued every period
// when a container is being throttled. Setting a period that's too large can
// show up as latency delays. Smaller periods can cause extra scheduler
// overhead. 250ms seems to work fine for most jobs.
static const int kHardcapPeriodUsecs = 250000;
static const int kUsecsPerMilliSecs = 1000;

// Latency settings.
static const int kPremierLatency = 25;
static const int kPriorityLatency = 50;
static const int kNormalLatency = 100;
static const int kNoLatency = -1;  // No latency guarantees.

CpuController::CpuController(const string &hierarchy_path,
                             const string &cgroup_path, bool owns_cgroup,
                             const KernelApi *kernel,
                             EventFdNotifications *eventfd_notifications)
    : CgroupController(CGROUP_CPU, hierarchy_path, cgroup_path, owns_cgroup,
                       kernel, eventfd_notifications) {}

int64 CpuController::MilliCpusToShares(int64 milli_cpus) const {
  return max(kMinShares, (milli_cpus * kPerCpuShares) / kCpusToMilliCpus);
}

int64 CpuController::SharesToMilliCpus(int64 shares) const {
  if (shares < kMinShares) {
    return 0;
  }
  return (kCpusToMilliCpus * shares) / kPerCpuShares;
}

Status CpuController::SetMilliCpus(int64 milli_cpus) {
  int shares = MilliCpusToShares(milli_cpus);
  return SetParamInt(KernelFiles::Cpu::kShares, shares);
}

Status CpuController::SetMaxMilliCpus(int64 max_milli_cpus) {
  const int kMinHardcapQuotaUsecs = 1000;

  int64 quota_usecs =
      (max_milli_cpus * kHardcapPeriodUsecs) / kUsecsPerMilliSecs;
  if (quota_usecs < kMinHardcapQuotaUsecs) {
    return Status(::util::error::INVALID_ARGUMENT,
                  Substitute("Requested max millicpu of \"$0\" is too low.",
                             max_milli_cpus));
  }

  RETURN_IF_ERROR(SetParamInt(KernelFiles::Cpu::kHardcapPeriod,
                              kHardcapPeriodUsecs));
  return SetParamInt(KernelFiles::Cpu::kHardcapQuota, quota_usecs);
}

Status CpuController::SetLatency(SchedulingLatency latency_class) {
  int latency = kNoLatency;

  switch (latency_class) {
    case PREMIER:
      latency = kPremierLatency;
      break;
    case PRIORITY:
      latency = kPriorityLatency;
      break;
    case NORMAL:
      latency = kNormalLatency;
      break;
    default:
      latency = kNoLatency;
      break;
  }
  return SetParamInt(KernelFiles::Cpu::kLatency, latency);
}

// TODO(jnagal): Add placement strategies as separate bits.
Status CpuController::SetPlacementStrategy(int64 placement) {
  return SetParamInt(KernelFiles::Cpu::kPlacementStrategy, placement);
}

// TODO(jnagal): Verify placement setting returned by kernel.
StatusOr<int64> CpuController::GetPlacementStrategy() const {
  return GetParamInt(KernelFiles::Cpu::kPlacementStrategy);
}

StatusOr<int> CpuController::GetNumRunnable() const {
  return GetParamInt(KernelFiles::Cpu::kNumRunning);
}

StatusOr<int64> CpuController::GetMilliCpus() const {
  int64 shares = RETURN_IF_ERROR(GetParamInt(KernelFiles::Cpu::kShares));

  return SharesToMilliCpus(shares);
}

StatusOr<int64> CpuController::GetMaxMilliCpus() const {
  int64 quota_usecs =
      RETURN_IF_ERROR(GetParamInt(KernelFiles::Cpu::kHardcapQuota));

  if (quota_usecs == -1) {
    // Unthrottled container.
    return quota_usecs;
  }
  return (quota_usecs * kUsecsPerMilliSecs) / kHardcapPeriodUsecs;
}

StatusOr<SchedulingLatency> CpuController::GetLatency() const {
  int64 latency_class =
      RETURN_IF_ERROR(GetParamInt(KernelFiles::Cpu::kLatency));
  SchedulingLatency latency = BEST_EFFORT;
  switch (latency_class) {
    case kPremierLatency:
      latency = PREMIER;
      break;
    case kPriorityLatency:
      latency = PRIORITY;
      break;
    case kNormalLatency:
      latency = NORMAL;
      break;
    case kNoLatency:
      latency = BEST_EFFORT;
      break;
    default:
      return Status(::util::error::INTERNAL,
                    Substitute("Unknown latency of \"$0\" returned by kernel.",
                               latency_class));
  }
  return latency;
}

StatusOr<ThrottlingStats> CpuController::GetThrottlingStats() const {
  string stats_str =
      RETURN_IF_ERROR(GetParamString(KernelFiles::Cpu::kThrottlingStats));
  const int kNumThrottlingStats = 3;
  vector<string> stat_lines =
      strings::Split(stats_str, "\n", strings::SkipEmpty());
  if (stat_lines.size() < kNumThrottlingStats) {
    return Status(::util::error::INTERNAL, Substitute(
        "Invalid throttling stats returned by kernel: \"$0\"", stats_str));
  }
  ThrottlingStats stats;
  int found_fields = 0;
  // TODO(vmarmol): Add stats parsing logic to base CgroupController class.
  for (auto line : stat_lines) {
    // Expected format per line is:
    // <field_name> <value>
    const vector<string> values = Split(line, " ", SkipEmpty());
    if (values.size() != 2) {
      // Ignore malformed lines
      continue;
    }

    int64 intval = 0;
    if (!SimpleAtoi(values[1], &intval)) {
      return Status(
          ::util::error::INTERNAL,
          Substitute("Failed to parse int from string \"$0\"", values[1]));
    }
    if (values[0] == "nr_periods") {
      // Number of periods when the container was hardcapped.
      stats.nr_periods = intval;
      ++found_fields;
    } else if (values[0] == "nr_throttled") {
      // Number of hardcapped periods when container was throttled
      // for some time.
      stats.nr_throttled = intval;
      ++found_fields;
    } else if (values[0] == "throttled_time") {
      // Throttled time is reported in nanoseconds by kernel.
      stats.throttled_time = intval;
      ++found_fields;
    }
    // We ignore new added fields that aren't known to us yet.
  }
  // Check that we got all fields we are interested in.
  if (found_fields != kNumThrottlingStats) {
    return Status(
        ::util::error::INTERNAL,
        Substitute("Missing throttling stat fields in \"$0\"", stats_str));
  }
  return stats;
}

StatusOr<int64> CpuController::GetThrottlingPeriodInMs() const {
  return kHardcapPeriodUsecs / kUsecsPerMilliSecs;
}

}  // namespace lmctfy
}  // namespace containers
