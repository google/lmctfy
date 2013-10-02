// Copyright 2013 Google Inc. All Rights Reserved.
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

// Implementation of the convenience functions and operator overloading to make
// it easier to write more natural unit test code for cpu_set_t.

#include "util/os/core/cpu_set.h"
#include "util/os/core/cpu_set_test_util.h"

using util_os_core::UInt64ToCpuSet;
using util_os_core::CpuSetTestEqual;


bool operator==(const cpu_set_t &lhs, const cpu_set_t &rhs) {
  return CpuSetTestEqual(lhs, rhs);
}

bool operator==(uint64 lhs, const cpu_set_t &rhs) {
  return CpuSetTestEqual(UInt64ToCpuSet(lhs), rhs);
}

bool operator!=(const cpu_set_t &lhs, const cpu_set_t &rhs) {
  return !CpuSetTestEqual(lhs, rhs);
}

bool operator!=(uint64 lhs, const cpu_set_t &rhs) {
  return !CpuSetTestEqual(UInt64ToCpuSet(lhs), rhs);
}
