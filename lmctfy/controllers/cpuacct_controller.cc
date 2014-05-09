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

#include "lmctfy/controllers/cpuacct_controller.h"

#include <limits.h>
#include <unistd.h>
#include <memory>
#include <utility>

#include "base/integral_types.h"
#include "lmctfy/kernel_files.h"
#include "include/lmctfy.pb.h"
#include "util/errors.h"
#include "strings/split.h"
#include "strings/strutil.h"
#include "strings/substitute.h"
#include "util/gtl/stl_util.h"
#include "re2/re2.h"
#include "util/task/codes.pb.h"
#include "util/task/status.h"

using ::util::Nanoseconds;
using ::std::map;
using ::std::unique_ptr;
using ::std::vector;
using ::strings::SkipEmpty;
using ::strings::Split;
using ::strings::Substitute;
using ::strings::delimiter::AnyOf;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {

// Default buckets of cpu scheduling histogram for queue_self and
// queue_other. Format: 7 buckets in usecs arranged in increasing order.
static const char kCpuHistogramQueueBuckets[] =
    "1000 5000 10000 25000 75000 100000 500000";

// Default buckets of cpu scheduling histogram for sleep, serve,
// and oncpu. Format: 7 buckets in usecs arranged in increasing
// order.
static const char kCpuHistogramBuckets[] =
    "1000 5000 10000 20000 50000 100000 250000";

const map<string, CpuHistogramType> CpuAcctController::kHistogramNames = {
  { "serve", SERVE },
  { "oncpu", ONCPU },
  { "sleep", SLEEP },
  { "queue_self", QUEUE_SELF },
  { "queue_other", QUEUE_OTHER }};

CpuAcctController::CpuAcctController(
    const string &hierarchy_path, const string &cgroup_path, bool owns_cgroup,
    const KernelApi *kernel, EventFdNotifications *eventfd_notifications)
    : CgroupController(CGROUP_CPUACCT, hierarchy_path, cgroup_path, owns_cgroup,
                       kernel, eventfd_notifications) {}

StatusOr<int64> CpuAcctController::GetCpuUsageInNs() const {
  return GetParamInt(KernelFiles::CPUAcct::kUsage);
}

StatusOr<vector<int64>*> CpuAcctController::GetPerCpuUsageInNs() const {
  string per_cpu_usage_str =
      RETURN_IF_ERROR(GetParamString(KernelFiles::CPUAcct::kUsagePerCPU));
  vector<string> per_cpu_values =
      Split(per_cpu_usage_str, AnyOf(" \n\t"), SkipEmpty());
  unique_ptr<vector<int64>> per_cpu_usage(new vector<int64>);
  for (auto value : per_cpu_values) {
    int64 usage = 0;
    if (!SimpleAtoi(value, &usage)) {
      return Status(::util::error::INTERNAL,
                    Substitute("Usage value \"$0\" is not a number", value));
    }
    per_cpu_usage->push_back(usage);
  }

  // Caller should verify that all cpus were reported.
  return per_cpu_usage.release();
}

Status CpuAcctController::SetupHistograms() {
  // Kernel doesn't return proper error for invalid bucket strings.
  // Drop input validation, ignore the errors, and continue with the
  // ugly one-time setup.
  for (const auto histogram : kHistogramNames) {
    bool queue_type = (histogram.second == QUEUE_SELF ||
                       histogram.second == QUEUE_OTHER);
    RETURN_IF_ERROR(SetParamString(
        KernelFiles::CPUAcct::kHistogram, Substitute("$0 $1", histogram.first,
        (queue_type ? kCpuHistogramQueueBuckets : kCpuHistogramBuckets))));
  }
  return Status::OK;
}

Status CpuAcctController::EnableSchedulerHistograms() const {
  const string kProcHistogramPath = "/proc/sys/kernel/sched_histogram";
  return WriteStringToFile(kProcHistogramPath, "1");
}

namespace {
Nanoseconds TicksToNanoseconds(int64 ticks) {
  static const int64 user_hz = sysconf(_SC_CLK_TCK);
  static const int64 kNanosecondsInSecond = 1000000000;
  return Nanoseconds(ticks * kNanosecondsInSecond / user_hz);
}
}  // namespace

StatusOr<CpuTime> CpuAcctController::GetCpuTime() const {
  string cpu_time_data =
      RETURN_IF_ERROR(GetParamString(KernelFiles::CPUAcct::kStat));
  int64 user_ticks;
  int64 system_ticks;
  if (!RE2::FullMatch(cpu_time_data,
                      "user (\\d+)\n"
                      "system (\\d+)\n",
                      &user_ticks,
                      &system_ticks)) {
    return Status(::util::error::INTERNAL,
                  Substitute("Contents of $0 are malformed: $1",
                             KernelFiles::CPUAcct::kStat,
                             cpu_time_data));
  }
  return CpuTime{TicksToNanoseconds(user_ticks),
                 TicksToNanoseconds(system_ticks)};
}

// Reads in the KernelFiles::CPUAcct::kHistogram and parses out different
// histograms.
// Format:
//   unit: us
//   <histogram name>
//   < bucket count
//   < <bucket> <count>
//   ...
//   <histogram name>
//   < bucket count
//   ...
StatusOr<vector<CpuHistogramData *>*>
CpuAcctController::GetSchedulerHistograms() const {
  vector<CpuHistogramData *> output;
  ElementDeleter d(&output);

  string histogram_str =
      RETURN_IF_ERROR(GetParamString(KernelFiles::CPUAcct::kHistogram));
  vector<string> histogram_lines = Split(histogram_str, "\n", SkipEmpty());
  CpuHistogramData *histogram_data = nullptr;
  for (const auto line : histogram_lines) {
    vector<string> histogram_values = Split(line, AnyOf(" \n\t"), SkipEmpty());
    if (histogram_values[0] == "unit:") {
      // Ignore boilerplate.
      continue;
    }
    if (histogram_values.size() == 1) {
      // New histogram.
      if (!kHistogramNames.count(histogram_values[0])) {
        return Status(::util::error::INTERNAL,
                      Substitute("Unknown histogram name \"$0\"",
                                 histogram_values[0]));
      }
      histogram_data = new CpuHistogramData();
      histogram_data->type = kHistogramNames.at(histogram_values[0]);
      output.push_back(histogram_data);
    } else if (histogram_values.size() == 3) {
      if (histogram_data == nullptr) {
        return Status(::util::error::INTERNAL, "Malformed histogram data.");
      }
      int bucket = 0;
      if (!histogram_values[1].compare("inf")) {
        bucket = INT_MAX;
      } else if (!SimpleAtoi(histogram_values[1], &bucket)) {
        return Status(::util::error::INTERNAL,
                      Substitute("Failed to parse int from string \"$0\"",
                                 histogram_values[1]));
      }
      int64 value = 0;
      if (!SimpleAtoi(histogram_values[2], &value)) {
        return Status(::util::error::INTERNAL,
                      Substitute("Failed to parse int from string \"$0\"",
                                 histogram_values[2]));
      }
      histogram_data->buckets[bucket] = value;
    }
  }

  histogram_data = nullptr;
  vector<CpuHistogramData *> *histograms = new vector<CpuHistogramData *>();
  histograms->swap(output);
  return histograms;
}

}  // namespace lmctfy
}  // namespace containers
