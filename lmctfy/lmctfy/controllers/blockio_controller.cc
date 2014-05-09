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

#include <stdio.h>

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

BlockIoController::BlockIoController(const string &hierarchy_path,
                                     const string &cgroup_path,
                                     bool owns_cgroup, const KernelApi *kernel,
                                     EventFdNotifications *notifications)
    : CgroupController(CGROUP_BLOCKIO, hierarchy_path, cgroup_path, owns_cgroup,
                       kernel, notifications) {}

static const int kWeightMultiplier = 10;

Status BlockIoController::IsValidLimit(uint64 limit) {
  static const int32 kMinLimit = 1;
  static const int32 kMaxLimit = 100;

  if (limit < kMinLimit || limit > kMaxLimit) {
    return Status(::util::error::INVALID_ARGUMENT,
                  Substitute("Limit $0 is outside the allowed range\n",
                             limit));
  }
  return Status::OK;
}

Status BlockIoController::UpdateDefaultLimit(uint32 limit) {
  RETURN_IF_ERROR(IsValidLimit(limit));

  // cgroup interface allows the range of 10 - 1000.
  return SetParamInt(KernelFiles::BlockIO::kWeight, limit * kWeightMultiplier);
}

StatusOr<uint32> BlockIoController::GetDefaultLimit() const {
  int32 weight = RETURN_IF_ERROR(GetParamInt(KernelFiles::BlockIO::kWeight));

  return weight/kWeightMultiplier;
}

StatusOr<string> BlockIoController::FormatWeightString(
    const BlockIoSpec::DeviceLimit &device_limit, int64 multiplier) const {
  if (!device_limit.has_device() || !device_limit.device().has_major() ||
      !device_limit.device().has_minor() ||
      !device_limit.has_limit()) {
    return Status(::util::error::INVALID_ARGUMENT,
                  Substitute("Incomplete device specified: $0",
                             device_limit.DebugString()));
  }
  return Substitute("$0:$1 $2", device_limit.device().major(),
                    device_limit.device().minor(),
                    device_limit.limit() * multiplier);
}

Status BlockIoController::UpdatePerDeviceLimit(
    const BlockIoSpec::DeviceLimitSet &limits_set) {
  for (const BlockIoSpec::DeviceLimit &limit : limits_set.device_limits()) {
    if (limit.has_limit()) {
      RETURN_IF_ERROR(IsValidLimit(limit.limit()));
    }
    const string device_limit_str =
        RETURN_IF_ERROR(FormatWeightString(limit, kWeightMultiplier));
    RETURN_IF_ERROR(SetParamString(KernelFiles::BlockIO::kPerDeviceWeight,
                                   device_limit_str));
  }
  return Status::OK;
}

Status BlockIoController::UpdateMaxLimit(
    const BlockIoSpec::MaxLimitSet &limits_set) {
  for (const BlockIoSpec::MaxLimit &max_limit : limits_set.max_limits()) {
    if (!max_limit.has_op_type() || !max_limit.has_limit_type()) {
      return Status(::util::error::INVALID_ARGUMENT,
                    Substitute("Incomplete device IO max limit specified: $0",
                               max_limit.DebugString()));
    }
    string path;
    if (max_limit.limit_type() == BlockIoSpec::BYTES_PER_SECOND) {
      path = (max_limit.op_type() == BlockIoSpec::READ) ?
          KernelFiles::BlockIO::kMaxReadBytesPerSecond :
          KernelFiles::BlockIO::kMaxWriteBytesPerSecond;
    } else {
      path = (max_limit.op_type() == BlockIoSpec::READ) ?
          KernelFiles::BlockIO::kMaxReadIoPerSecond :
          KernelFiles::BlockIO::kMaxWriteIoPerSecond;
    }
    for (const BlockIoSpec::DeviceLimit device : max_limit.limits()) {
      const string device_limit_str =
          RETURN_IF_ERROR(FormatWeightString(device, 1));
      RETURN_IF_ERROR(SetParamString(path, device_limit_str));
    }
  }
  return Status::OK;
}

Status BlockIoController::FillLimitSpec(
    google::protobuf::RepeatedPtrField<BlockIoSpec::DeviceLimit> *limits,
    const string &spec_file) const {
  FileLines limit_lines = RETURN_IF_ERROR(GetParamLines(spec_file));
  for (const StringPiece limit : limit_lines) {
    if (limit.empty()) continue;
    int major, minor;
    uint64 weight;
    // Ignore headers or malformed lines.
    if (sscanf(limit.data(), "%d:%d %lld", &major, &minor, &weight) != 3) {
      continue;
    }
    BlockIoSpec::DeviceLimit *device_limit = limits->Add();
    device_limit->mutable_device()->set_major(major);
    device_limit->mutable_device()->set_minor(minor);
    device_limit->set_limit(weight);
  }
    return Status::OK;
}

Status BlockIoController::FillThrottlingSpec(
    BlockIoSpec::MaxLimitSet *max_limit_set, const string &spec_file) const {
  BlockIoSpec::MaxLimit max_limit;
  RETURN_IF_ERROR(FillLimitSpec(max_limit.mutable_limits(), spec_file));
  if (max_limit.limits().size() != 0) {
    if (spec_file == KernelFiles::BlockIO::kMaxReadIoPerSecond ||
        spec_file == KernelFiles::BlockIO::kMaxWriteIoPerSecond) {
      max_limit.set_limit_type(BlockIoSpec::IO_PER_SECOND);
    } else {
      max_limit.set_limit_type(BlockIoSpec::BYTES_PER_SECOND);
    }
    if (spec_file == KernelFiles::BlockIO::kMaxReadIoPerSecond ||
        spec_file == KernelFiles::BlockIO::kMaxReadBytesPerSecond) {
      max_limit.set_op_type(BlockIoSpec::READ);
    } else {
      max_limit.set_op_type(BlockIoSpec::WRITE);
    }
    *max_limit_set->add_max_limits() = max_limit;
  }
  return Status::OK;
}

StatusOr<BlockIoSpec::MaxLimitSet> BlockIoController::GetMaxLimit() const {
  BlockIoSpec::MaxLimitSet max_limits;
  RETURN_IF_ERROR(FillThrottlingSpec(
      &max_limits, KernelFiles::BlockIO::kMaxReadIoPerSecond));
  RETURN_IF_ERROR(FillThrottlingSpec(
      &max_limits, KernelFiles::BlockIO::kMaxWriteIoPerSecond));
  RETURN_IF_ERROR(FillThrottlingSpec(
      &max_limits, KernelFiles::BlockIO::kMaxReadBytesPerSecond));
  RETURN_IF_ERROR(FillThrottlingSpec(
      &max_limits, KernelFiles::BlockIO::kMaxWriteBytesPerSecond));
  return max_limits;
}

StatusOr<BlockIoSpec::DeviceLimitSet>
BlockIoController::GetDeviceLimits() const {
  BlockIoSpec::DeviceLimitSet limits_set;
  RETURN_IF_ERROR(FillLimitSpec(limits_set.mutable_device_limits(),
                                KernelFiles::BlockIO::kPerDeviceWeight));
  // Adjust weight to be a fraction of available I/O limit.
  for (int i = 0; i < limits_set.device_limits_size(); ++i) {
    uint64 weight = limits_set.device_limits(i).limit();
    limits_set.mutable_device_limits(i)->set_limit(weight/kWeightMultiplier);
  }
  return limits_set;
}

}  // namespace lmctfy
}  // namespace containers
