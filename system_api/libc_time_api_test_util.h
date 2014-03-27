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

#ifndef SYSTEM_API_LIBC_TIME_API_TEST_UTIL_H_
#define SYSTEM_API_LIBC_TIME_API_TEST_UTIL_H_

#include <time.h>

#include "base/macros.h"
#include "gmock/gmock.h"
#include "system_api/libc_time_api.h"

namespace system_api {

class MockLibcTimeApi : public LibcTimeApi {
 public:
  MockLibcTimeApi() {}

  virtual ~MockLibcTimeApi() {}

  MOCK_CONST_METHOD2(CTimeR, char *(const time_t *timep, char *buf));
  MOCK_CONST_METHOD1(Time, time_t(time_t *t));
  MOCK_CONST_METHOD2(GetTimeOfDay, int(struct timeval *time_value,
                                       struct timezone *time_zone));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockLibcTimeApi);
};

extern const LibcTimeApi *GlobalLibcTimeApi();

class MockLibcTimeApiOverride {
 public:
  ::testing::StrictMock<MockLibcTimeApi> &Mock() {
    static ::testing::StrictMock<MockLibcTimeApi> *ptr =
        (::testing::StrictMock<MockLibcTimeApi> *)GlobalLibcTimeApi();
    return *ptr;
  }
};

}  // namespace system_api

#endif  // SYSTEM_API_LIBC_TIME_API_TEST_UTIL_H_
