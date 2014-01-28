#ifndef LMCTFY_C_BINDING_STATUS_INTERNAL_H_
#define LMCTFY_C_BINDING_STATUS_INTERNAL_H_

#include "util/errors.h"
#include "clmctfy.h"
#include "util/task/status.h"
#include "util/task/statusor.h"
#include "lmctfy.h"

using ::containers::lmctfy::Container;
using ::containers::lmctfy::ContainerApi;

#ifdef __cplusplus
// XXX(monnand): Do we need to extern "C"?
extern "C" {
#endif // __cplusplus

struct container {
  Container *container_;
};

struct container_api {
  ContainerApi *container_api_;
};

#ifdef __cplusplus
}
#endif // __cplusplus

#define RETURN_IF_ERROR_PTR(s, ...)                                 \
    do {                                                            \
      const ::util::Status _status =                                \
        ::util::errors_internal::PerformSideEffects(__VA_ARGS__);   \
      if (PREDICT_FALSE(!_status.ok())) {                           \
        if (s != NULL) status_copy(s, _status);                         \
        return (int)_status.error_code();                                \
      }                                                             \
    } while (0)

#define CHECK_NOTNULL_OR_RETURN(status, ptr)  do { \
    if ((ptr) == NULL) {  \
      return status_new(status, UTIL__ERROR__CODE__INVALID_ARGUMENT, \
                        "In function %s: %s cannot be null",  \
                        __func__, #ptr);  \
    } \
} while (0)

#define CHECK_POSITIVE_OR_RETURN(status, value)  do { \
    if ((value) <= 0) {  \
      return status_new(status, UTIL__ERROR__CODE__INVALID_ARGUMENT, \
                        "In function %s: %s=%d, but it should be positive",  \
                        __func__, #value, value);  \
    } \
} while (0)

namespace util {
namespace internal {

int status_new(struct status *dst, int code, const char *fmt, ...);
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
