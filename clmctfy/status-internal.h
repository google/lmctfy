#ifndef LMCTFY_C_BINDING_STATUS_INTERNAL_H_
#define LMCTFY_C_BINDING_STATUS_INTERNAL_H_

#include "util/errors.h"
#include "clmctfy.h"
#include "util/task/status.h"
#include "util/task/statusor.h"

#define RETURN_IF_ERROR_PTR(s, ...)                                 \
    do {                                                            \
      const ::util::Status _status =                                \
        ::util::errors_internal::PerformSideEffects(__VA_ARGS__);   \
      if (PREDICT_FALSE(!_status.ok())) {                           \
        if (s != NULL) status_copy(s, _status);                         \
        return (int)_status.error_code();                                \
      }                                                             \
    } while (0)

namespace util {
namespace internal {

int status_copy(struct status *dst, const Status &src);

} // namespace internal
} // namespace util

#endif // LMCTFY_C_BINDING_STATUS_INTERNAL_H_
