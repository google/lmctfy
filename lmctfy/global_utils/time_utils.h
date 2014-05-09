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

// These are commonly used time queries that conveniently wrap the call to
// Walltime_Now.

#ifndef GLOBAL_UTILS_TIME_UTILS_H_
#define GLOBAL_UTILS_TIME_UTILS_H_

#include "base/macros.h"
#include "util/safe_types/time.h"

namespace util {

class TimeUtils {
 public:
  virtual ~TimeUtils() {}

  // Returns time from epoch in microseconds.
  virtual ::util::Microseconds MicrosecondsSinceEpoch() const = 0;

 protected:
  TimeUtils() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(TimeUtils);
};

// Returns a singleton instance of the GlobalTimeUtils interface implementation.
const TimeUtils *GlobalTimeUtils();

}  // namespace util

#endif  // GLOBAL_UTILS_TIME_UTILS_H_
