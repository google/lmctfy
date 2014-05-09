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

#ifndef SRC_CONTROLLERS_CPU_CONTROLLER_H_
#define SRC_CONTROLLERS_CPU_CONTROLLER_H_

#include <string>
using ::std::string;

#include "base/macros.h"
#include "lmctfy/controllers/cgroup_controller.h"
#include "include/lmctfy.pb.h"
#include "util/task/status.h"
#include "util/task/statusor.h"

namespace containers {
namespace lmctfy {

// TODO(jnagal): Replace with throttling data proto from lmctfy.proto.
struct ThrottlingStats {
  ThrottlingStats() : nr_periods(0), nr_throttled(0), throttled_time(0) {}

  // Number of periods since container creation.
  int64 nr_periods;

  // Number of periods when a container hit its hardcap limit and was
  // throttled.
  int64 nr_throttled;

  // Aggregate time, in nanoseconds, a container was throttled for.
  int64 throttled_time;
};

class CgroupFactory;
class CpuController;

// Factory for CpuControllers.
//
// Class is thread-safe.
class CpuControllerFactory
    : public CgroupControllerFactory<CpuController, CGROUP_CPU> {
 public:
  CpuControllerFactory(const CgroupFactory *cgroup_factory,
                       const KernelApi *kernel,
                       EventFdNotifications *eventfd_notifications)
      : CgroupControllerFactory<CpuController, CGROUP_CPU>(
            cgroup_factory, kernel, eventfd_notifications) {}
  virtual ~CpuControllerFactory() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(CpuControllerFactory);
};

// Controller for cpu cgroup
// Controls CFS scheduler settings for a container.
//
// Class is thread-safe.
class CpuController : public CgroupController {
 public:
  CpuController(const string &hierarchy_path, const string &cgroup_path,
                bool owns_cgroup, const KernelApi *kernel,
                EventFdNotifications *eventfd_notifications);
  virtual ~CpuController() {}

  // Set millicpus/sec for this cgroup.
  // TODO(jnagal): Use StrongInts for milli_cpus.
  virtual ::util::Status SetMilliCpus(int64 milli_cpus);

  // Set maximum allowed cpu rate of millicpus/sec for this cgroup.
  virtual ::util::Status SetMaxMilliCpus(int64 max_milli_cpus);

  // Set desired cpu latency for this cgroup.
  virtual ::util::Status SetLatency(SchedulingLatency latency);

  // Set placement policy for this cgroup.
  // TODO(jnagal): Should this be an interface at all? We always write the same
  // value for each cgroup.
  virtual ::util::Status SetPlacementStrategy(int64 placement);

  // All statistics return NOT_FOUND if they were not found or available.

  // Get number of runnable processes for this cgroup.
  virtual ::util::StatusOr<int> GetNumRunnable() const;

  // Retrieve cpu limit set for this cgroup.
  virtual ::util::StatusOr<int64> GetMilliCpus() const;

  // Retrieve maximum cpu limit set for this cgroup.
  // Return value of -1 means uncapped container.
  virtual ::util::StatusOr<int64> GetMaxMilliCpus() const;

  // Retrieve latency setting for this cgroup.
  virtual ::util::StatusOr<SchedulingLatency> GetLatency() const;

  // Retrieve placement setting for this cgroup.
  virtual ::util::StatusOr<int64> GetPlacementStrategy() const;

  // Gets throttling stats for this cgroup.
  virtual ::util::StatusOr<ThrottlingStats> GetThrottlingStats() const;

  // Get default throttling period in milliseconds.
  virtual ::util::StatusOr<int64> GetThrottlingPeriodInMs() const;

 private:
  int64 MilliCpusToShares(int64 milli_cpus) const;
  int64 SharesToMilliCpus(int64 shares) const;

  friend class CpuControllerTest;

  DISALLOW_COPY_AND_ASSIGN(CpuController);
};

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_CONTROLLERS_CPU_CONTROLLER_H_
