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

#include "lmctfy/controllers/blockio_controller.h"

#include "lmctfy/kernel_files.h"
#include "include/lmctfy.pb.h"
#include "util/errors.h"
#include "strings/numbers.h"
#include "strings/split.h"
#include "strings/substitute.h"

using ::util::FileLines;
using ::std::map;
using ::std::vector;
using ::strings::delimiter::AnyOf;
using ::strings::SkipEmpty;
using ::strings::Split;
using ::strings::Substitute;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {

BlockIoController::BlockIoController(const string &cgroup_path,
                                     bool owns_cgroup, const KernelApi *kernel,
                                     EventFdNotifications *notifications)
    : CgroupController(CGROUP_BLOCKIO, cgroup_path, owns_cgroup, kernel,
                       notifications) {}

static const int kWeightMultiplier = 10;

Status BlockIoController::UpdateDefaultLimit(uint32 limit) {
  static const int32 kMinLimit = 1;
  static const int32 kMaxLimit = 100;

  if (limit < kMinLimit || limit > kMaxLimit) {
    return Status(::util::error::INVALID_ARGUMENT,
                  Substitute("Limit $0 is outside the allowed range\n",
                             limit));
  }

  // cgroup interface allows the range of 10 - 1000.
  return SetParamInt(KernelFiles::BlockIO::kWeight, limit * kWeightMultiplier);
}

StatusOr<uint32> BlockIoController::GetDefaultLimit() const {
  int32 weight = RETURN_IF_ERROR(GetParamInt(KernelFiles::BlockIO::kWeight));

  return weight/kWeightMultiplier;
}

Status BlockIoController::UpdatePerDeviceLimit(
    const BlockIoSpec::DeviceLimitSet &device_limits) {
  return Status(::util::error::UNIMPLEMENTED,
                "per-device limits unimplemented.");
}

Status BlockIoController::UpdateMaxLimit(
    const BlockIoSpec::MaxLimitSet &max_limits) {
  return Status(::util::error::UNIMPLEMENTED,
                "I/O throttling unimplemented.");
}

StatusOr<BlockIoSpec::MaxLimitSet> BlockIoController::GetMaxLimit() const {
  return Status(::util::error::UNIMPLEMENTED,
                "Throttling stats unimplemented.");
}

StatusOr<BlockIoSpec::DeviceLimitSet>
BlockIoController::GetDeviceLimits() const {
  return Status(::util::error::UNIMPLEMENTED,
                "Retrieving per-device limit spec unimplemented.");
}

}  // namespace lmctfy
}  // namespace containers
