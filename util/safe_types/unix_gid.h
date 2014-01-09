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

#ifndef UTIL_SAFE_TYPES_UNIX_GID_H_
#define UTIL_SAFE_TYPES_UNIX_GID_H_

#include <sys/types.h>
#include "base/integral_types.h"
#include "base/macros.h"
#include "util/intops/safe_int.h"  // IWYU pragma: export

namespace util {

// UnixGid
//
// It would probably be nice if UnixGid type == gid_t (with the extra safety
// of strong ints).  However, we might use some invalid gid_t values (e.g. -1)
// internally.
//
// Extra care needs to be taken when converting a gid_t to a UnixGid so very
// large uint32 values (values > max int32) don't get converted to unexpected
// values.
DEFINE_SAFE_INT_TYPE(UnixGid, int32, ::util_intops::LogFatalOnError);

// Need to make sure variables have the same size, so that we won't get
// invalid results upon conversion.
COMPILE_ASSERT(sizeof(gid_t) == sizeof(UnixGid::ValueType),
               gid_t_UnixGid_ValueType_size_mismatch);

// Constant GID values.
struct UnixGidValue {
 public:
  static UnixGid Root() { return UnixGid(0); }
  static UnixGid Invalid() { return UnixGid(-1); }
};

}  // namespace util

#endif  // UTIL_SAFE_TYPES_UNIX_GID_H_
