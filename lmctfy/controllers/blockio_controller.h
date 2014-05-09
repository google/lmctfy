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

#ifndef SRC_CONTROLLERS_BLOCKIO_CONTROLLER_H_
#define SRC_CONTROLLERS_BLOCKIO_CONTROLLER_H_

#include <string>
using ::std::string;

#include "base/macros.h"
#include "lmctfy/controllers/cgroup_controller.h"
#include "include/lmctfy.pb.h"
#include "util/task/status.h"
#include "util/task/statusor.h"

namespace containers {
namespace lmctfy {

class CgroupFactory;
class BlockIoController;

// Factory for BlockIoControllers.
//
// Class is thread-safe.
class BlockIoControllerFactory
    : public CgroupControllerFactory<BlockIoController, CGROUP_BLOCKIO> {
 public:
    BlockIoControllerFactory(const CgroupFactory *cgroup_factory,
                            const KernelApi *kernel,
                            EventFdNotifications *eventfd_notifications)
        : CgroupControllerFactory<BlockIoController, CGROUP_BLOCKIO>(
            cgroup_factory, kernel, eventfd_notifications) {}
    ~BlockIoControllerFactory() override {}
 private:
    DISALLOW_COPY_AND_ASSIGN(BlockIoControllerFactory);
};

// Controller for disk io cgroup
//
// Class is thread-safe.
class BlockIoController : public CgroupController {
 public:
  BlockIoController(const string &hierarchy_path, const string &cgroup_path,
                    bool owns_cgroup, const KernelApi *kernel,
                    EventFdNotifications *eventfd_notifications);
  ~BlockIoController() override {}

  // Update default limit for all devices.
  // Limit is within [1, 100]
  virtual ::util::Status UpdateDefaultLimit(uint32 limit);

  // Update per-device limit overrides.
  // Limit is within [1, 100]
  virtual ::util::Status UpdatePerDeviceLimit(
      const BlockIoSpec::DeviceLimitSet &device_limits);

  // Update max limits.
  virtual ::util::Status UpdateMaxLimit(
      const BlockIoSpec::MaxLimitSet &max_limits);

  // Get current default limit.
  virtual ::util::StatusOr<uint32> GetDefaultLimit() const;

  // Get per device limit overrides.
  virtual ::util::StatusOr<BlockIoSpec::DeviceLimitSet> GetDeviceLimits() const;

  // Get current setting for max limits.
  virtual ::util::StatusOr<BlockIoSpec::MaxLimitSet> GetMaxLimit() const;

 private:
  ::util::StatusOr<string> FormatWeightString(
      const BlockIoSpec::DeviceLimit &device, int64 multiplier) const;

  ::util::Status FillLimitSpec(
      google::protobuf::RepeatedPtrField<BlockIoSpec::DeviceLimit> *limits,
      const string &spec_file) const;

  ::util::Status FillThrottlingSpec(BlockIoSpec::MaxLimitSet *max_limit_set,
                                    const string &spec_file) const;

  ::util::Status IsValidLimit(uint64 limit);

  DISALLOW_COPY_AND_ASSIGN(BlockIoController);
};

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_CONTROLLERS_BLOCKIO_CONTROLLER_H_
