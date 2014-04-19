#ifndef LMCTFY_CLMCTFY_CLMCTFY_STATUS_INTERNAL_H_
#define LMCTFY_CLMCTFY_CLMCTFY_STATUS_INTERNAL_H_

#include "clmctfy_status.h"
#include "util/task/status.h"

namespace util {
namespace internal {

int status_new(struct status *dst, int code, const char *fmt, ...);
int status_copy(struct status *dst, const Status &src);

} // namespace internal
} // namespace util

#endif // LMCTFY_CLMCTFY_CLMCTFY_STATUS_INTERNAL_H_ 
