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
//
// StatusOr<string> produceString(const string &str) {
//   if (str.empty())
//     return Status(INVALID_ARGUMENT, "string is empty");
//   return str + ": other";
// }
//
// Status validateInput(const string &str) {
//   string output = XRETURN_IF_ERROR(produceString(str));
//   printf("%s\n", string);
//
//   return Status::OK;
// }
//
// Alternatively, if you want to change error code to INTERNAL you might use
// this:
//
// Status validateInput(const string &str) {
//   string output = XRETURN_INTERNAL_IF_ERROR(produceString(str));
//   printf("%s\n", string);
//
//   return Status::OK;
// }
//
// Moreover, one can prepend some string to be more verbose.
//
// Status validateInput(const string &str) {
//   string output = XRETURN_INTERNAL_IF_ERROR(produceString(str),
//       "Could not create output string");
//   printf("%s\n", string);
//
//   return Status::OK;
// }
//
// Alternatively to CHECK one can user XVERIFY_OR_RETURN which returns
// INTERNAL error in case of problems and logs DFATAL.
//
// Status checkInput(const string &str) {
//   string output = XVERIFY_OR_RETURN(produceString(str),
//                                     "This shouldn't happen.");
//   printf("%s\n", string);
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

// If the boolean is false, DFATALs and returns an INTERNAL error.
#define VERIFY_OR_RETURN(invariant, message)                                  \
  do {                                                                        \
    if (!(invariant)) {                                                       \
      const auto &_message_tmp = message;                                     \
      LOG(DFATAL) << _message_tmp;                                            \
      return Status(::util::error::INTERNAL, StringPiece(_message_tmp));      \
    }                                                                         \
  } while (0)

// Macros defined below are working thanks to GCC extension.
#if defined(__GNUC__)

namespace util {
namespace errors_internal {

inline const ::util::Status &XToStatus(const ::util::Status &status) {
  return status;
}

template <typename T>
inline const ::util::Status &XToStatus(const ::util::StatusOr<T> &statusor) {
  return statusor.status();
}

template <typename... Args>
inline const ::util::Status XToStatus(const ::util::Status &status,
                                      const Args &... rest) {
  return ::util::Status(status.CanonicalCode(),
                        StrCat(rest..., ": ", status.ToString()));
}

template <typename T, typename... Args>
inline const ::util::Status XToStatus(const ::util::StatusOr<T> &statusor,
                                      const Args &... rest) {
  return ::util::Status(statusor.status().CanonicalCode(),
                        StrCat(rest..., ": ", statusor.status().ToString()));
}

template <typename... Args>
inline const ::util::Status XToInternalStatus(const ::util::Status &status) {
  return ::util::Status(::util::error::INTERNAL, status.ToString());
}

template <typename T, typename... Args>
inline const ::util::Status XToInternalStatus(
    const ::util::StatusOr<T> &statusor) {
  return ::util::Status(::util::error::INTERNAL, statusor.status().ToString());
}

template <typename... Args>
inline const ::util::Status XToInternalStatus(const ::util::Status &status,
                                              const Args &... rest) {
  return ::util::Status(::util::error::INTERNAL,
                        StrCat(rest..., ": ", status.ToString()));
}

template <typename T, typename... Args>
inline const ::util::Status XToInternalStatus(
    const ::util::StatusOr<T> &statusor, const Args &... rest) {
  return ::util::Status(::util::error::INTERNAL,
                        StrCat(rest..., ": ", statusor.status().ToString()));
}

inline void XToValue(const ::util::Status &status) {}

template <typename T>
inline T XToValue(const ::util::StatusOr<T> &statusor) {
  return statusor.ValueOrDie();
}

}  // namespace errors_internal
}  // namespace util

// Returns the status of the specified expression if it is not OK.
// Also evaluates to value in case of StatusOr (so that it can be assigned).
#define XRETURN_IF_ERROR(expr, ...)                                          \
  ({                                                                         \
    const auto _eval_expr = (expr);                                          \
    if (PREDICT_FALSE(!_eval_expr.ok())) {                                   \
      return ::util::errors_internal::XToStatus(_eval_expr,      \
                                                            ##__VA_ARGS__);  \
    }                                                                        \
    ::util::errors_internal::XToValue(_eval_expr);               \
  })

// Same as XRETURN_IF_ERROR but in case of error changes error code to INTERNAL.
#define XRETURN_INTERNAL_IF_ERROR(expr, ...)                                 \
  ({                                                                         \
    const auto _eval_expr = (expr);                                          \
    if (PREDICT_FALSE(!_eval_expr.ok())) {                                   \
      return ::util::errors_internal::XToInternalStatus(         \
          _eval_expr, ##__VA_ARGS__);                                        \
    }                                                                        \
    ::util::errors_internal::XToValue(_eval_expr);               \
  })

// Same as XRETURN_IF_ERROR but also logs a DFATAL in case of error.
#define XVERIFY_OR_RETURN(expr, ...)                                         \
  ({                                                                         \
    const auto _eval_expr = (expr);                                          \
    if (PREDICT_FALSE(!_eval_expr.ok())) {                                   \
      const ::util::Status _status =                                         \
          ::util::errors_internal::XToStatus(_eval_expr);        \
      return _status;                                                        \
    }                                                                        \
    ::util::errors_internal::XToValue(_eval_expr);               \
  })

#endif  // defined(__GNUC__)
#endif  // UTIL_ERRORS_H_
