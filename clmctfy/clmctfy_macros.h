#ifndef LMCTFY_CLMCTFY_CLMCTFY_MACROS_H_H
#define LMCTFY_CLMCTFY_CLMCTFY_MACROS_H_H

#include "util/errors.h"
#include "util/task/status.h"
#include "util/task/statusor.h"
#include "util/task/codes.pb-c.h"

#include "clmctfy_status.h"
#include "clmctfy_status_internal.h"

#define RETURN_IF_ERROR_PTR(s, expr, out)                         \
    do {                                                          \
      auto _expr_result = (expr);                                 \
      if (PREDICT_FALSE(!_expr_result.ok())) {                    \
        const ::util::Status _status = _expr_result.status();     \
        if (s != NULL) ::util::internal::status_copy(s, _status); \
        return (int)_status.error_code();                         \
      }                                                           \
      (*(out)) = _expr_result.ValueOrDie();                       \
    } while (0)

#define CHECK_NOTFAIL_OR_RETURN(status) do {  \
  if ((status) != NULL) { \
    if ((status)->error_code != UTIL__ERROR__CODE__OK) {  \
      return (status)->error_code; \
    } \
  } \
} while(0)

#define CHECK_NOTNULL_OR_RETURN(status, ptr)  do {                    \
    if ((ptr) == NULL) {                                              \
      return ::util::internal::status_new(status, UTIL__ERROR__CODE__INVALID_ARGUMENT,  \
                        "In function %s: %s cannot be null",          \
                        __func__, #ptr);                              \
    }                                                                 \
} while (0)

#define CHECK_POSITIVE_OR_RETURN(status, value)  do {                       \
    if ((value) <= 0) {                                                     \
      return ::util::internal::status_new(status, UTIL__ERROR__CODE__INVALID_ARGUMENT,        \
                        "In function %s: %s=%d, but it should be positive", \
                        __func__, #value, value);                           \
    }                                                                       \
} while (0)

#endif // LMCTFY_CLMCTFY_CLMCTFY_MACROS_H_H
