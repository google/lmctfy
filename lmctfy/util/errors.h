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
#include "strings/strcat.h"
#include "util/task/status.h"
#include "util/task/statusor.h"

// A set of convenience wrappers for operations that return Status and StatusOr.
//
// To catch and propagate errors:
//   RETURN_IF_ERROR(expression_returns_void(args));
//   int result = RETURN_IF_ERROR(expression_returns_int(args));
//
// In the event of an error, this macro will return a Status from the current
// function.  The returned Status has the same code and message as the
// failed expression.
//
// Additionally, the message string may be prefixed with additional information
// by passing more arguments to the macro.  These arguments are passed through
// to StrCat().
//   RETURN_IF_ERROR(expression, "Oh damn.  The ", module, "failed.");
//
// Alternatively, if you want to change error code to INTERNAL you might use
// this:
//
// TODO(thockin): Revamp these when moving away from X* names.
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

// Extract the value or return a Status (with a different error_code and
// prefixed error message).  There's no value for a Status, so we just return
// the status.
inline Status GetValueWithPrefix(Status s,
                                 ::util::error::Code error_code,
                                 const string &error_msg_prefix) {
  if (PREDICT_TRUE(s.ok())) return s;
  return Status(error_code, error_msg_prefix + s.ToString());
}

// Extract the value or return a Status (with a different error_code and
// prefixed error message).
template <typename T, typename U>
inline Status GetValueWithPrefix(StatusOr<T> statusor, U *out_ptr,
                                 ::util::error::Code error_code,
                                 const string &error_msg_prefix) {
  if (PREDICT_TRUE(statusor.ok())) {
    *out_ptr = statusor.ValueOrDie();
    return statusor.status();
  }
  return Status(error_code,
                error_msg_prefix + statusor.status().ToString());
}

// Same as above but allows to return different Status (with different
// error_code and prefixed error message). D is a custom deleter (if any).
template <typename T, typename U, typename D>
inline Status GetValueWithPrefix(StatusOr<T*> statusor,
                                 ::std::unique_ptr<U, D> *out_ptr,
                                 ::util::error::Code error_code,
                                 const string &error_msg_prefix) {
  if (PREDICT_TRUE(statusor.ok())) {
    out_ptr->reset(statusor.ValueOrDie());
    return statusor.status();
  }
  return Status(error_code,
                error_msg_prefix + statusor.status().ToString());
}

// Generically get a Status value from an argument expression (Status or
// StatusOr).  This could be made more public if we find utility in overloading
// it for other types.
inline Status ToStatus(const Status &status) {
    return status;
}
template<typename T>
inline Status ToStatus(const StatusOr<T> &status_or_t) {
    return status_or_t.status();
}

}  // namespace errors_internal
}  // namespace util

// Returns error with given code and message concatenated from prefix and
// internal error message the status of the specified expression if it is not
// OK.
#define RETURN_IF_ERROR_AND_ADD(...)                              \
  do {                                                            \
    const ::util::Status _status =                                \
        ::util::errors_internal::GetValueWithPrefix(__VA_ARGS__); \
    if (PREDICT_FALSE(!_status.ok())) return _status;             \
  } while (0)

// CHECKs that an expression (producing a Status or a StatusOr) was OK.  Use
// sparingly.  Prefer not to CHECK in production code.
#undef CHECK_OK  // defined by status.h, but only for Status, not StatusOr
#define CHECK_OK(expr) CHECK(::util::errors_internal::ToStatus(expr).ok())

// If the boolean is false, DFATALs and returns an INTERNAL error.
#define VERIFY_OR_RETURN(invariant, message)            \
  do {                                                  \
    if (!(invariant)) {                                 \
      const auto &_message_tmp = message;               \
      LOG(DFATAL) << _message_tmp;                      \
      return ::util::Status(::util::error::INTERNAL,    \
                            StringPiece(_message_tmp)); \
    }                                                   \
  } while (0)

// Macros defined below are working thanks to GCC extension.
#if defined(__GNUC__)

namespace util {
namespace errors_internal {

inline const Status XToStatus(const Status &status) {
  return status;
}

template <typename T>
inline const Status XToStatus(const StatusOr<T> &statusor) {
  return statusor.status();
}

template <typename... Args>
inline const Status XToStatus(const Status &status,
                              const Args &... rest) {
  return Status(status.CanonicalCode(),
                StrCat(rest..., ": ", status.ToString()));
}

template <typename T, typename... Args>
inline const Status XToStatus(const StatusOr<T> &statusor,
                              const Args &... rest) {
  return Status(statusor.status().CanonicalCode(),
                StrCat(rest..., ": ", statusor.status().ToString()));
}

template <typename... Args>
inline const Status XToInternalStatus(const Status &status) {
  return Status(::util::error::INTERNAL, status.ToString());
}

template <typename T, typename... Args>
inline const Status XToInternalStatus(const StatusOr<T> &statusor) {
  return Status(::util::error::INTERNAL, statusor.status().ToString());
}

template <typename... Args>
inline const Status XToInternalStatus(const Status &status,
                                      const Args &... rest) {
  return Status(::util::error::INTERNAL,
                StrCat(rest..., ": ", status.ToString()));
}

template <typename T, typename... Args>
inline const Status XToInternalStatus(const StatusOr<T> &statusor,
                                      const Args &... rest) {
  return Status(::util::error::INTERNAL,
                StrCat(rest..., ": ", statusor.status().ToString()));
}

inline void XToValue(Status *status) {}

template <typename T>
inline T XToValue(StatusOr<T> *statusor) {
  return statusor->ValueOrDie();
}

}  // namespace errors_internal
}  // namespace util

// Evaluates an expression which returns a Status or StatusOr value.  If the
// resulting status is not OK, returns the status.  Otherwise this is an
// expression-statement which evaluates to the value of the StatusOr (or void
// for a plain Status).  This can take additional arguments which are passed
// directly to StrCat() in the event of the expression returning an error.
//
// Example:
//   string s = RETURN_IF_ERROR(FunctionThatReturnsStatusOrString());
//
// Note to maintainers: It is *very* important that __VA_ARGS__ is evaluated
// no more than once.
#define RETURN_IF_ERROR(expr, ...)                                            \
  ({                                                                          \
    auto _expr_result = (expr);                                               \
    if (PREDICT_FALSE(!_expr_result.ok())) {                                  \
      return ::util::errors_internal::XToStatus(_expr_result, ##__VA_ARGS__); \
    }                                                                         \
    ::util::errors_internal::XToValue(&_expr_result);                         \
  })

// Same as RETURN_IF_ERROR but in case of error changes error code to INTERNAL.
#define XRETURN_INTERNAL_IF_ERROR(expr, ...)                            \
  ({                                                                    \
    auto _expr_result = (expr);                                         \
    if (PREDICT_FALSE(!_expr_result.ok())) {                            \
      return ::util::errors_internal::XToInternalStatus(_expr_result,   \
                                                        ##__VA_ARGS__); \
    }                                                                   \
    ::util::errors_internal::XToValue(&_expr_result);                   \
  })

#endif  // defined(__GNUC__)
#endif  // UTIL_ERRORS_H_
