#ifndef LMCTFY_C_BINDING_STATUS_INTERNAL_H_
#define LMCTFY_C_BINDING_STATUS_INTERNAL_H_

#include "util/errors.h"
#include "clmctfy.h"
#include "util/task/status.h"
#include "util/task/statusor.h"
#include "lmctfy.h"

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

namespace containers {
namespace lmctfy {
namespace internal {

ContainerApi *lmctfy_container_api_strip(struct container_api *api);
Container *lmctfy_container_strip(struct container *container);

} // internal
} // lmctfy
} // containers

#endif // LMCTFY_C_BINDING_STATUS_INTERNAL_H_
