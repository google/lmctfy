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

// The point of this class is to enable injecting and mocking out time-related
// libc calls. We define methods that (in production code) forward to the
// standard functions of time.h, but can be overridden using tools provided in
// libc_time_test_util.h for testing.

#ifndef SYSTEM_API_LIBC_TIME_API_H_
#define SYSTEM_API_LIBC_TIME_API_H_

#include <sys/time.h>
#include <time.h>

#include "base/macros.h"

namespace system_api {

// Allows mocking of libc's time-related APIs.
class LibcTimeApi {
 public:
  virtual ~LibcTimeApi() {}

  virtual char *CTimeR(const time_t *timep, char *buf) const = 0;
  virtual time_t Time(time_t *t) const = 0;
  virtual int GetTimeOfDay(struct timeval *time_value,
                           struct timezone *time_zone) const = 0;

  // The following functions could be added here if needed:
  // - localtime_r
  // - mktime
  // - sleep
  // - strptime
  // - usleep

 protected:
  LibcTimeApi() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(LibcTimeApi);
};

// Returns a singleton instance of the LibcTimeApi interface implementation.
const LibcTimeApi *GlobalLibcTimeApi();

}  // namespace system_api

#endif  // SYSTEM_API_LIBC_TIME_API_H_
