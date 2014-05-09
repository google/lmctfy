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

// This defines some convenience functions and operator overloading to make it
// easier to write more natural unit test code for CpuMask.

#ifndef UTIL_CPU_MASK_TEST_UTIL_H_
#define UTIL_CPU_MASK_TEST_UTIL_H_

#include <string>

#include "base/integral_types.h"
#include "util/cpu_mask.h"

namespace util {

// Equality operator. This is to support:
//   EXPECT_EQ(expected_int, set);
// Other interfaces are not provided in order to discourage abuse.
bool operator==(uint64 lhs, const CpuMask &rhs);

// Non equality operator. This is to support:
//   EXPECT_NE(expected_int, set);
// Other interfaces are not provided in order to discourage abuse.
bool operator!=(uint64 lhs, const CpuMask &rhs);

}  // namespace util

#endif  // UTIL_CPU_MASK_TEST_UTIL_H_
