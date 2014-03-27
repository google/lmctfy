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

#include "lmctfy/controllers/freezer_controller.h"

#include "lmctfy/kernel_files.h"
#include "util/errors.h"
#include "strings/substitute.h"

using ::strings::Substitute;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {

const char kFrozen[] = "FROZEN";
const char kThawed[] = "THAWED";
const char kFreezing[] = "FREEZING";

bool FreezerController::IsHierarchicalFreezingSupported() const {
  StatusOr<int64> statusor =
      GetParamInt(KernelFiles::Freezer::kFreezerParentFreezing);
  if (!statusor.ok() &&
      statusor.status().CanonicalCode() == ::util::error::NOT_FOUND) {
    return false;
  }
  return true;
}

Status FreezerController::SafeToUpdate() const {
  if (!IsHierarchicalFreezingSupported() &&
      RETURN_IF_ERROR(GetSubcontainers()).size() > 0) {
    return {::util::error::FAILED_PRECONDITION,
          Substitute("Cgroup $0 has subcontainers and hierarchical freezing"
                     " is not supported.",
                     cgroup_name())};
  }
  return Status::OK;
}


Status FreezerController::Freeze() {
  RETURN_IF_ERROR(SafeToUpdate());
  return SetParamString(KernelFiles::Freezer::kFreezerState, kFrozen);
}

Status FreezerController::Unfreeze() {
  RETURN_IF_ERROR(SafeToUpdate());
  return SetParamString(KernelFiles::Freezer::kFreezerState, kThawed);
}

StatusOr<FreezerState> FreezerController::State() const {
  string state =
      RETURN_IF_ERROR(GetParamString(KernelFiles::Freezer::kFreezerState));
  if (state == kFrozen) {
    return FreezerState::FROZEN;
  } else if (state == kThawed) {
    return FreezerState::THAWED;
  } else if (state == kFreezing) {
    return FreezerState::FREEZING;
  }
  return Status(::util::error::INTERNAL,
                Substitute("Unrecognized freezer state \"$0\"", state));
}


}  // namespace lmctfy
}  // namespace containers
