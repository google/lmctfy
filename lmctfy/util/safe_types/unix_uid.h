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

#ifndef UTIL_SAFE_TYPES_UNIX_UID_H_
#define UTIL_SAFE_TYPES_UNIX_UID_H_

#include <sys/types.h>
#include "base/integral_types.h"
#include "base/macros.h"
#include "util/intops/strong_int.h"  // IWYU pragma: export

namespace util {

// UnixUid
//
// It would be nice if UnixUid type == uid_t (with the extra safety of strong
// ints).  However, invalid uid_t values (e.g. -1) are used internally.
//
// Extra care needs to be taken when converting a uid_t to a UnixUid so very
// large uint32 values (values > max int32) don't get converted to unexpected
// values.
DEFINE_STRONG_INT_TYPE(UnixUid, int32);

// Need to make sure variables have the same size, so that we won't get
// invalid results upon conversion.
COMPILE_ASSERT(sizeof(uid_t) == sizeof(UnixUid::ValueType),
               uid_t_UnixUid_ValueType_size_mismatch);

// Constant UID values.
struct UnixUidValue {
 public:
  static UnixUid Root() { return UnixUid(0); }
  static UnixUid Invalid() { return UnixUid(-1); }
};

}  // namespace util

#endif  // UTIL_SAFE_TYPES_UNIX_UID_H_
