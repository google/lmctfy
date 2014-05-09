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

#ifndef SRC_CONTROLLERS_MEMORY_CONTROLLER_H_
#define SRC_CONTROLLERS_MEMORY_CONTROLLER_H_

#include <map>
#include <string>
using ::std::string;

#include "base/integral_types.h"
#include "base/macros.h"
#include "lmctfy/controllers/cgroup_controller.h"
#include "include/lmctfy.pb.h"
#include "util/safe_types/bytes.h"
#include "util/task/statusor.h"

namespace containers {
namespace lmctfy {

class CgroupFactory;
class MemoryController;

// Factory for MemoryControllers.
//
// Class is thread-safe.
class MemoryControllerFactory
    : public CgroupControllerFactory<MemoryController, CGROUP_MEMORY> {
 public:
  // Does not own cgroup_factory or kernel.
  MemoryControllerFactory(const CgroupFactory *cgroup_factory,
                          const KernelApi *kernel,
                          EventFdNotifications *eventfd_notifications)
      : CgroupControllerFactory<MemoryController, CGROUP_MEMORY>(
            cgroup_factory, kernel, eventfd_notifications) {}
  virtual ~MemoryControllerFactory() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(MemoryControllerFactory);
};

// Controller for the memory cgroup.
//
// Class is thread-safe.
class MemoryController : public CgroupController {
 public:
  // Does not take ownership of kernel.
  MemoryController(const string &hierarchy_path, const string &cgroup_path,
                   bool owns_cgroup, const KernelApi *kernel,
                   EventFdNotifications *eventfd_notifications);
  virtual ~MemoryController() {}

  // Set the memory limit of this cgroup.
  virtual ::util::Status SetLimit(::util::Bytes limit);

  // Set the reserved memory or "soft limit" of this cgroup.
  virtual ::util::Status SetSoftLimit(::util::Bytes limit);

  // Set the swap memory limit of this cgroup.
  virtual ::util::Status SetSwapLimit(::util::Bytes limit);

  // Set the age at which pages are considered stale in this cgroup. The age is
  // counted in kstaled scan cycles.
  virtual ::util::Status SetStalePageAge(int32 scan_cycles);

  // Sets the OOM score of the cgroup. Higher scores are of higher priority and
  // thus less likely to OOM.
  virtual ::util::Status SetOomScore(int64 oom_score);

  // Sets the compression sampling ratio
  virtual ::util::Status SetCompressionSamplingRatio(int32 ratio);

  // Set dirty ratios or limits
  virtual ::util::Status SetDirtyRatio(int32 ratio);
  virtual ::util::Status SetDirtyBackgroundRatio(int32 ratio);
  virtual ::util::Status SetDirtyLimit(::util::Bytes limit);
  virtual ::util::Status SetDirtyBackgroundLimit(
      ::util::Bytes limit);

  virtual ::util::Status SetKMemChargeUsage(bool enable);

  // All statistics return NOT_FOUND if they were not found or available.

  // Gets the working set of this cgroup. This is the currently hot memory.
  virtual ::util::StatusOr<::util::Bytes> GetWorkingSet() const;

  // Gets the raw usage of this cgroup.
  virtual ::util::StatusOr<::util::Bytes> GetUsage() const;

  // Gets the max usage seen in this cgroup.
  virtual ::util::StatusOr<::util::Bytes> GetMaxUsage() const;

  // Gets the raw swap usage of this cgroup.
  virtual ::util::StatusOr<::util::Bytes> GetSwapUsage() const;

  // Gets the max swap usage seen in this cgroup.
  virtual ::util::StatusOr<::util::Bytes> GetSwapMaxUsage() const;

  // Gets the memory limit of this cgroup.
  virtual ::util::StatusOr<::util::Bytes> GetLimit() const;

  // Gets the effective limit of this cgroup. This limit may be less than
  // GetLimit() if the hierarchy has less memory available (i.e.: the parent
  // does not have enough memory to satisfy the value returned by GetLimit()).
  // TODO(zohaib): Figure out if memory swap space should be counted as a part
  // of the effective memory limit.
  virtual ::util::StatusOr<::util::Bytes> GetEffectiveLimit() const;

  // Gets the reserved memory or "soft limit" for this cgroup.
  virtual ::util::StatusOr<::util::Bytes> GetSoftLimit() const;

  // Gets the swap memory limit of this cgroup.
  virtual ::util::StatusOr<::util::Bytes> GetSwapLimit() const;

  // Gets the age at which pages are considered stale in this cgroup.
  virtual ::util::StatusOr<int32> GetStalePageAge() const;

  // Gets the OOM score of this cgroup.
  virtual ::util::StatusOr<int64> GetOomScore() const;

  // Gets the compression sampling ratio.
  virtual ::util::StatusOr<int32> GetCompressionSamplingRatio() const;

  // Get dirty ratios and limits
  virtual ::util::StatusOr<int32> GetDirtyRatio() const;
  virtual ::util::StatusOr<int32> GetDirtyBackgroundRatio() const;
  virtual ::util::StatusOr<::util::Bytes> GetDirtyLimit() const;
  virtual ::util::StatusOr<::util::Bytes> GetDirtyBackgroundLimit()
      const;

  virtual ::util::StatusOr<bool> GetKMemChargeUsage() const;

  // Register a notification for an OOM event. The handler for the event is
  // returned on success.
  virtual ::util::StatusOr<ActiveNotifications::Handle> RegisterOomNotification(
      CgroupController::EventCallback *callback);

  // Register a notification for when memory usage goes above the specified
  // usage_threshold. The handler for the event is returned on success.
  virtual ::util::StatusOr<ActiveNotifications::Handle>
      RegisterUsageThresholdNotification(
          ::util::Bytes usage_threshold,
          CgroupController::EventCallback *callback);

  // Get all stats from the memory.stat file
  virtual ::util::Status GetMemoryStats(MemoryStats *memory_stats) const;

  // Get all stats from the memory.numa_stat file
  virtual ::util::Status GetNumaStats(MemoryStats_NumaStats *numa_stats) const;

  // Get all stats from the memory.idle_page_stats file
  virtual ::util::Status GetIdlePageStats(
      MemoryStats_IdlePageStats *idle_page_stats) const;

  virtual ::util::Status GetCompressionSamplingStats(
      MemoryStats_CompressionSamplingStats *compression_sampling_stats) const;

  virtual ::util::StatusOr<int64> GetFailCount() const;

 private:
  // Gets a mapping of field_name to integer value of the specified stats file.
  //
  // Expected stats format is space-separated key value pairs, one per line. The
  // values are expected to be integers.
  // i.e.:
  // field_name1 37
  // field_name2 42
  //
  // NOTE: This does not return Bytes because not all integer values are bytes!
  //
  // Arguments:
  //   stats_type: The name of a hierarchy file containing statistics in the
  //       expected format.
  // Return:
  //   StatusOr<map<string, int64>>: Status or the operation. Iff OK, returns a
  //       map of field_name to integer value.
  ::util::StatusOr< ::std::map<string, int64>> GetStats(
      const string &stats_type) const;

  // Get the stale bytes.
  ::util::StatusOr<::util::Bytes> GetStaleBytes() const;

  // Gets the total inactive bytes (file and anonymous).
  ::util::StatusOr<::util::Bytes> GetInactiveBytes() const;

  // Gets the specified value from the specified stats if it is available.
  ::util::StatusOr<int64> GetValueFromStats(
      const ::std::map<string, int64> &stats, const string &value) const;

  // Type-safe wrappers for setting/getting memory hierarchy params.
  ::util::Status SetParamBytes(const string &hierarchy_file,
                               ::util::Bytes value);
  ::util::StatusOr< ::util::Bytes> GetParamBytes(
      const string &hierarchy_file) const;

  friend class MemoryControllerTest;

  DISALLOW_COPY_AND_ASSIGN(MemoryController);
};

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_CONTROLLERS_MEMORY_CONTROLLER_H_
