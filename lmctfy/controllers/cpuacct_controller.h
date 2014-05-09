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

#ifndef SRC_CONTROLLERS_CPUACCT_CONTROLLER_H_
#define SRC_CONTROLLERS_CPUACCT_CONTROLLER_H_

#include <string>
using ::std::string;

#include "base/integral_types.h"
#include "base/macros.h"
#include "lmctfy/controllers/cgroup_controller.h"
#include "include/lmctfy.pb.h"
#include "util/safe_types/time.h"
#include "util/task/statusor.h"

namespace containers {
namespace lmctfy {

class CgroupFactory;
class CpuAcctController;

// Factory for CpuAcctControllers.
//
// Class is thread-safe.
class CpuAcctControllerFactory
    : public CgroupControllerFactory<CpuAcctController, CGROUP_CPUACCT> {
 public:
  CpuAcctControllerFactory(const CgroupFactory *cgroup_factory,
                           const KernelApi *kernel,
                           EventFdNotifications *eventfd_notifications)
      : CgroupControllerFactory<CpuAcctController, CGROUP_CPUACCT>(
            cgroup_factory, kernel, eventfd_notifications) {}
  virtual ~CpuAcctControllerFactory() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(CpuAcctControllerFactory);
};

// TODO(jnagal): Move histogram processing into it's own class.
// Currently, it requires access to cgroup_controller to read/write from
// cgroup interfaces.

// Holds a cpu histogram with type and bucket:value pairs.
struct CpuHistogramData {
  CpuHistogramType type;
  ::std::map<int, int64> buckets;
};

struct CpuTime {
  ::util::Nanoseconds user;
  ::util::Nanoseconds system;
};

// Controller for cpu accounting cgroup.
//
// Class is thread-safe.
class CpuAcctController : public CgroupController {
 public:
  CpuAcctController(const string &hierarchy_path, const string &cgroup_path,
                    bool owns_cgroup, const KernelApi *kernel,
                    EventFdNotifications *eventfd_notifications);
  virtual ~CpuAcctController() {}

  // Setup measurement buckets for scheduler histograms.
  virtual ::util::Status SetupHistograms();

  // Called once to enable scheduler histogram collection in kernel.
  // TODO(jnagal): Move it to CpuAcctControllerFactory so it can be called at
  // InitMachine time.
  virtual ::util::Status EnableSchedulerHistograms() const;

  // All statistics return NOT_FOUND if they were not found or available.

  // Get cpu usage in nanoseconds.
  virtual ::util::StatusOr<int64> GetCpuUsageInNs() const;

  // Get cpu usage in nanoseconds divided on user and system time.
  virtual ::util::StatusOr<CpuTime> GetCpuTime() const;

  // Get per-cpu usage in nanoseconds.
  // Caller owns the newly allocated vector.
  virtual ::util::StatusOr< ::std::vector<int64> *> GetPerCpuUsageInNs() const;

  // Get Scheduler performance histograms.
  // Caller owns the newly allocated vector.
  // TODO(jnagal): Considering returning by value after evaluating overheads.
  virtual ::util::StatusOr< ::std::vector<CpuHistogramData *> *>
      GetSchedulerHistograms() const;

 private:
  ::util::Status ConfigureHistogramBucket(const string &histogram_path,
                                          const string &buckets);

  static const ::std::map<string, CpuHistogramType> kHistogramNames;

  friend class CpuAcctControllerTest;

  DISALLOW_COPY_AND_ASSIGN(CpuAcctController);
};

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_CONTROLLERS_CPUACCT_CONTROLLER_H_
