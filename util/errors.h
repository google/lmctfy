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

#ifndef UTIL_ERRORS_H_
#define UTIL_ERRORS_H_

#include <memory>

#include "base/logging.h"
#include "base/port.h"
#include "util/task/status.h"
#include "util/task/statusor.h"

// A set of convenience wrappers for operations that return Status and StatusOr.
//
//
// To propagate errors:
//   RETURN_IF_ERROR(method(args));
//   RETURN_IF_ERROR(method(args), &output);
//
// Examples:
//
// Status isEven(int n) {
//   if (n % 2 != 0) {
//     return Status(INVALID_ARGUMENT, Substitute("$0 is not even", n));
//   }
//
//   return Status::OK;
// }
//
// StatusOr<int> checkNumber(int n) {
//   RETURN_IF_ERROR(isEven(n));
//
//   return n;
// }
//
// Status validateNumber(int n) {
//   int value;
//   RETURN_IF_ERROR(checkNumber(n), &value);
//
//   printf("%d\n", value);
//
//   return Status::OK;
// }

namespace util {
namespace errors_internal {

// Returns OK.
inline ::util::Status ValueOrOk() {
  return ::util::Status::OK;
}

// Returns the specified value.
template<typename T>
inline T ValueOrOk(T t) {
  return t;
}

// No side effects for a status, so we just return the status.
inline ::util::Status PerformSideEffects(::util::Status s) {
  return s;
}

// Set the value if it was specified, then return the status.  Making T and U
// different type args allows for a StatusOr<T> to have its value stored in an
// out_ptr to const T.
template <typename T, typename U>
inline ::util::Status PerformSideEffects(::util::StatusOr<T> statusor,
                                         U *out_ptr) {
  if (PREDICT_TRUE(statusor.ok())) {
    *out_ptr = statusor.ValueOrDie();
  }
  return statusor.status();
}

// Set the value of the unique_ptr if it was specified, then return the status.
// Making T and U different type args allows for a StatusOr<T> to have its
// value stored in an out_ptr to const T.
template <typename T, typename U>
inline ::util::Status PerformSideEffects(::util::StatusOr<T *> statusor,
                                         ::std::unique_ptr<U> *out_ptr) {
  if (PREDICT_TRUE(statusor.ok())) {
    out_ptr->reset(statusor.ValueOrDie());
  }
  return statusor.status();
}

// Generically get a Status value from an argument expression (Status or
// StatusOr).  This could be made more public if we find utility in overloading
// it for other types.
inline ::util::Status ToStatus(const ::util::Status &status) {
    return status;
}
template<typename T>
inline ::util::Status ToStatus(const ::util::StatusOr<T> &status_or_t) {
    return status_or_t.status();
}

}  // namespace errors_internal
}  // namespace util

// Returns the status of the specified expression if it is not OK.
#define RETURN_IF_ERROR(...)                                                  \
  do {                                                                        \
    const ::util::Status _status =                                            \
        ::util::errors_internal::PerformSideEffects(__VA_ARGS__); \
    if (PREDICT_FALSE(!_status.ok())) return _status;                         \
  } while (0)

// CHECKs that an expression (producing a Status or a StatusOr) was OK.
#undef CHECK_OK  // defined by status.h, but only for Status, not StatusOr
#define CHECK_OK(expr)                                                   \
    CHECK(::util::errors_internal::ToStatus(expr).ok()) \

#endif  // UTIL_ERRORS_H_
