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

#include "lmctfy/controllers/device_controller.h"

#include "lmctfy/kernel_files.h"
#include "util/errors.h"
#include "strings/numbers.h"
#include "strings/substitute.h"
#include "strings/split.h"
#include "strings/util.h"

using ::strings::SkipEmpty;
using ::strings::Substitute;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {

Status DeviceController::SetRestrictions(
    const DeviceSpec::DeviceRestrictionsSet &rules) {
  for (const DeviceSpec::DeviceRestrictions &restriction :
       rules.restrictions()) {
    if (!restriction.has_type() ||
        !restriction.has_permission() ||
        !restriction.access_size() ||
        (restriction.access_size() > 3)) {
      // All of these are required fields for setting a restriction.
      return Status(::util::error::INVALID_ARGUMENT,
                    "Invalid device restriction specification.");
    }

    string type = "";
    if (restriction.type() == DeviceSpec::DEVICE_ALL) {
      type = "a";
    } else if (restriction.type() == DeviceSpec::DEVICE_CHAR) {
      type = "c";
    } else {
      type = "b";
    }

    string access = "";
    bool read = false;
    bool write = false;
    bool mknod = false;
    for (const auto accesstype : restriction.access()) {
      if (accesstype == DeviceSpec::READ) {
        read = true;
      } else if (accesstype == DeviceSpec::WRITE) {
        write = true;
      } else if (accesstype == DeviceSpec::MKNOD) {
        mknod = true;
      }
    }
    if (read) {
      access += "r";
    }
    if (write) {
      access += "w";
    }
    if (mknod) {
      access += "m";
    }

    string major = "*";
    if (restriction.has_major()) {
      major = SimpleItoa(restriction.major());
    }

    string minor = "*";
    if (restriction.has_minor()) {
      minor = SimpleItoa(restriction.minor());
    }

    string rule = Substitute("$0 $1:$2 $3", type, major, minor, access);
    string filename = "";
    if (restriction.permission() == DeviceSpec::ALLOW) {
      filename = KernelFiles::Device::kDevicesAllow;
    } else {
      filename = KernelFiles::Device::kDevicesDeny;
    }

    RETURN_IF_ERROR(SetParamString(filename, rule));
  }

  return Status::OK;
}

DeviceSpec::DeviceRestrictionsSet
DeviceController::GetAllDevicesDeniedRule() const {
  DeviceSpec::DeviceRestrictionsSet restriction_set;
  DeviceSpec::DeviceRestrictions *restriction =
      restriction_set.add_restrictions();
  restriction->set_type(DeviceSpec::DEVICE_ALL);
  restriction->add_access(DeviceSpec::READ);
  restriction->add_access(DeviceSpec::WRITE);
  restriction->add_access(DeviceSpec::MKNOD);
  restriction->set_permission(DeviceSpec::DENY);
  return restriction_set;
}

Status DeviceController::SetDeviceType(
    const string &rule, DeviceSpec::DeviceRestrictions *restriction) const {
  if (rule == "a") {
    restriction->set_type(DeviceSpec::DEVICE_ALL);
  } else if (rule == "b") {
    restriction->set_type(DeviceSpec::DEVICE_BLOCK);
  } else if (rule == "c") {
    restriction->set_type(DeviceSpec::DEVICE_CHAR);
  } else {
    return Status(::util::error::INTERNAL,
                  Substitute("Invalid device type $0", rule));
  }
  return Status::OK;
}

Status DeviceController::SetDeviceNumber(
    const string &rule, DeviceSpec::DeviceRestrictions *restriction) const {
  const vector<string> device_numbers = Split(rule, ":", SkipEmpty());
  if (device_numbers.size() != 2) {
    return Status(util::error::INTERNAL,
                  Substitute("Invalid device numbers $0", rule));
  }

  for (int i = 0; i < device_numbers.size(); ++i) {
    int64 device;
    if (SimpleAtoi(device_numbers[i], &device)) {
      if (i == 0) {
        restriction->set_major(device);
      } else {
        restriction->set_minor(device);
      }
    } else if (device_numbers[i] != "*") {
      return Status(util::error::INTERNAL,
                    Substitute("Invalid device number $0", rule));
    }
  }
  return Status::OK;
}

Status DeviceController::SetDeviceAccess(
    const string &rule, DeviceSpec::DeviceRestrictions *restriction) const {
  if (strcount(rule, 'm') == 1) {
    restriction->add_access(DeviceSpec::MKNOD);
  }
  if (strcount(rule, 'r') == 1) {
    restriction->add_access(DeviceSpec::READ);
  }
  if (strcount(rule, 'w') == 1) {
    restriction->add_access(DeviceSpec::WRITE);
  }

  if (restriction->access_size() < 1 || restriction->access_size() > 3 ||
      restriction->access_size() != rule.length()) {
    return Status(::util::error::INTERNAL,
                  Substitute("Invalid access type $0", rule));
  }

  return Status::OK;
}

StatusOr<DeviceSpec::DeviceRestrictionsSet> DeviceController::GetState() const {
  string rules = RETURN_IF_ERROR(GetParamString(
      KernelFiles::Device::kDevicesList));
  if (rules.empty()) {
    // All devices are denied.
    return GetAllDevicesDeniedRule();
  }
  DeviceSpec::DeviceRestrictionsSet restriction_set;
  for (StringPiece rule : Split(rules, "\n", SkipEmpty())) {
    if (rule.empty()) {
      continue;
    }
    DeviceSpec::DeviceRestrictions *restriction =
        restriction_set.add_restrictions();
    // Rules in the list are for allowed devices.
    restriction->set_permission(DeviceSpec::ALLOW);
    const vector<string> rule_parts = Split(rule, " ", SkipEmpty());
    if (rule_parts.size() != 3) {
      return Status(util::error::INTERNAL,
                    Substitute("Malformed device restriction rule $0", rule));
    }
    RETURN_IF_ERROR(SetDeviceType(rule_parts[0], restriction));
    RETURN_IF_ERROR(SetDeviceNumber(rule_parts[1], restriction));
    RETURN_IF_ERROR(SetDeviceAccess(rule_parts[2], restriction));
  }
  return restriction_set;
}

Status DeviceController::VerifyRestriction(
    const DeviceSpec::DeviceRestrictions &rule) const {
  if (!rule.has_permission() || !rule.has_type() ||
      rule.access_size() == 0 || rule.access_size() > 3) {
    return Status(util::error::INVALID_ARGUMENT,
                  "Malformed device restriction rule.");
  }
  return Status::OK;
}

}  // namespace lmctfy
}  // namespace containers
