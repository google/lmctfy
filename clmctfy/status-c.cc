#include <string.h> // to use strdup

#include "clmctfy.h"
#include "clmctfy_internal.h"
#include "util/task/status.h"

using ::util::Status;

namespace util {
namespace internal {

int status_new(struct status *dst, int code, const char *message) {
  if (dst == NULL) {
    return code;
  }
  dst->error_code = code;
  if (message != NULL && code != 0) {
    dst->message = strdup(message);
  }
  return code;
}

int status_copy(struct status *dst, const Status &src) {
  if (dst == NULL) {
    return (int)src.error_code();
  }
  dst->error_code = src.error_code();
  if (!src.ok() && src.error_message() != "") {
    dst->message = strdup(src.error_message().c_str());
  }
  return (int)src.error_code();
}

} // namespace internal
} // namespace util

