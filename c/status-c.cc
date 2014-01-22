#include "status-c.h"
#include "status-internal.h"
#include "util/task/status.h"

using ::util::Status;

struct status status_ok = { Status::OK };

struct status *status_new_success() {
  struct status *ok = (struct status *)malloc(sizeof(struct status));
  ok->status_ = Status::OK;
  return ok;
}

struct status *status_new(int code, const char *msg) {
  struct status *s = (struct status *)malloc(sizeof(struct status));
  Status st((::util::error::Code)code, msg);
  s->status_ = st;
  return s;
}

int status_is_ok(const struct status *s) {
  if (s->status_.ok()) {
    return 1;
  }
  return 0;
}

int status_get_code(const struct status *s) {
  return s->status_.error_code();
}

const char *status_get_message(const struct status *s) {
  return s->status_.error_message().c_str();
}

void status_release(struct status *s) {
  if (s != NULL && s != &status_ok) {
    free(s);
  }
}

namespace util {
namespace internal {

// TODO(monnand): We may want to use a memory pool for status stucture.
struct status *status_copy(const Status &s) {
  struct status *ret = (struct status *)malloc(sizeof(struct status));
  ret->status_ = s;
  return ret;
}

} // namespace internal
} // namespace util

