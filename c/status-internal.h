#ifndef LMCTFY_C_BINDING_STATUS_INTERNAL_H_
#define LMCTFY_C_BINDING_STATUS_INTERNAL_H_

#include "status-c.h"
#include "util/task/status.h"

namespace util {
namespace internal {

// We don't want users to instantiate the status structure.
struct status *status_new(Status &s);

} // namespace internal
} // namespace util

#endif // LMCTFY_C_BINDING_STATUS_INTERNAL_H_
