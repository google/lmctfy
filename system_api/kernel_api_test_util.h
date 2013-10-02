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

#ifndef SYSTEM_API_KERNEL_API_TEST_UTIL_H__
#define SYSTEM_API_KERNEL_API_TEST_UTIL_H__

#include "system_api/kernel_api.h"
#include "system_api/kernel_api_mock.h"
#include "gmock/gmock.h"

namespace system_api {

extern const KernelAPI *GlobalKernelApi();

class MockKernelApiOverride {
 public:
  ::testing::StrictMock<KernelAPIMock> &Mock() {
    static ::testing::StrictMock<KernelAPIMock> *ptr =
        (::testing::StrictMock<KernelAPIMock> *) GlobalKernelApi();
    return *ptr;
  }
};

}  // namespace system_api

#endif  // SYSTEM_API_KERNEL_API_TEST_UTIL_H__
