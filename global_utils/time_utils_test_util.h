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

// Unit testing utilities for the TimeUtils module.

#ifndef GLOBAL_UTILS_TIME_UTILS_TEST_UTIL_H_
#define GLOBAL_UTILS_TIME_UTILS_TEST_UTIL_H_

#include "global_utils/time_utils.h"
#include "gmock/gmock.h"

namespace util {

class MockTimeUtils : public TimeUtils {
 public:
  MOCK_CONST_METHOD0(MicrosecondsSinceEpoch, ::util::Microseconds());
};

extern const TimeUtils *GlobalTimeUtils();

class MockTimeUtilsOverride {
 public:
  ::testing::StrictMock<MockTimeUtils> &Mock() {
    static ::testing::StrictMock<MockTimeUtils> *ptr =
        (::testing::StrictMock<MockTimeUtils> *)GlobalTimeUtils();
    return *ptr;
  }
};

}  // namespace util

#endif  // GLOBAL_UTILS_TEST_UTIL_H_
