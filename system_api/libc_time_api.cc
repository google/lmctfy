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

#include "system_api/libc_time_api.h"

#include <sys/time.h>

namespace system_api {
namespace {

// The "real" implementation of the API.
class LibcTimeApiImpl : public LibcTimeApi {
 public:
  LibcTimeApiImpl() {}

  char *CTimeR(const time_t *timep, char *buf) const override {
    return ::ctime_r(timep, buf);
  }

  time_t Time(time_t *t) const override {
    return ::time(t);
  }

  int GetTimeOfDay(struct timeval *time_value,
                   struct timezone *time_zone) const override {
    return ::gettimeofday(time_value, time_zone);
  }
};

}  // namespace

// The default singleton instantiation.
const LibcTimeApi *GlobalLibcTimeApi() {
  static LibcTimeApi *api = new LibcTimeApiImpl();
  return api;
}

}  // namespace system_api
