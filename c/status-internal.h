#ifndef LMCTFY_C_BINDING_STATUS_INTERNAL_H_
#define LMCTFY_C_BINDING_STATUS_INTERNAL_H_

#include "util/errors.h"
#include "status-c.h"
#include "util/task/status.h"
#include "util/task/statusor.h"

struct status {
  ::util::Status status_;
};

extern struct status status_ok;

#define RETURN_IF_ERROR_PTR(...)                                    \
    do {                                                            \
      const ::util::Status _status =                                \
        ::util::errors_internal::PerformSideEffects(__VA_ARGS__);   \
      if (PREDICT_FALSE(!_status.ok())) return status_new(_status); \
    } while (0)

namespace util {
namespace internal {

// We don't want users to instantiate the status structure.
struct status *status_new(const Status &s);

} // namespace internal
} // namespace util

#endif // LMCTFY_C_BINDING_STATUS_INTERNAL_H_
