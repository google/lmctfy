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

// Defines a strong-int type for managing byte-dimensional values.
// Mostly copied from original bytes.h by thockin

#ifndef UTIL_BYTES_H_
#define UTIL_BYTES_H_

#include "base/integral_types.h"
#include "util/intops/safe_int.h"  // IWYU pragma: export

namespace util {

DEFINE_SAFE_INT_TYPE(Bytes, int64, ::util_intops::LogFatalOnError);
DEFINE_SAFE_INT_TYPE(Kilobytes, int64, ::util_intops::LogFatalOnError);
DEFINE_SAFE_INT_TYPE(Megabytes, int64, ::util_intops::LogFatalOnError);
DEFINE_SAFE_INT_TYPE(Gigabytes, int64, ::util_intops::LogFatalOnError);

inline Megabytes BytesToMegabytes(Bytes bytes) {
  if (bytes.value() < 0) {
    LOG(DFATAL) << "We shouldn't have a negative number of bytes";
    return Megabytes(0);
  }
  return Megabytes((bytes >> 20).value());
}

inline Bytes KilobytesToBytes(Kilobytes kilobytes) {
  if (kilobytes.value() < 0) {
    LOG(DFATAL) << "We shouldn't have a negative number of kilobytes";
    return Bytes(0);
  }
  return Bytes((kilobytes << 10).value());
}

inline Bytes MegabytesToBytes(Megabytes megabytes) {
  if (megabytes.value() < 0) {
    LOG(DFATAL) << "We shouldn't have a negative number of megabytes";
    return Bytes(0);
  }
  return Bytes((megabytes << 20).value());
}

inline Bytes GigabytesToBytes(Gigabytes gigabytes) {
  if (gigabytes.value() < 0) {
    LOG(DFATAL) << "We shouldn't have a negative number of gigabytes";
    return Bytes(0);
  }
  return Bytes((gigabytes << 30).value());
}

}  // namespace util

#endif  // UTIL_BYTES_H_
