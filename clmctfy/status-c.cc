#include <string.h> // to use strdup
#include <stdarg.h>

#include "clmctfy.h"
#include "clmctfy_internal.h"
#include "util/task/status.h"

using ::util::Status;

namespace util {
namespace internal {

#define MAXLINE 4096

static int status_new_fmt(struct status *dst, int code, const char *fmt, va_list ap) {
  char buf[MAXLINE];
  if (dst == NULL) {
    return code;
  }
  dst->error_code = code;

  if (fmt == NULL || code == 0) {
    return code;
  }
  memset(buf, 0, MAXLINE);
  vsnprintf(buf, MAXLINE, fmt, ap);
  dst->error_code = code;
  dst->message = strdup(buf);
  return code;
}

int status_new(struct status *dst, int code, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  status_new_fmt(dst, code, fmt, ap);
  va_end(ap);
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

