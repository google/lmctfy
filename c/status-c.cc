#include "status-c.h"
#include "status-internal.h"
#include "util/task/status.h"

using ::util::Status;

struct status {
  Status status_;
};

int status_is_ok(struct status *s) {
  if (s->status_.ok()) {
    return 1;
  }
  return 0;
}

int status_get_code(struct status *s) {
  return s->status_.error_code();
}

const char *status_get_message(struct status *s) {
  return s->status_.error_message().c_str();
}

void status_release(struct status *s) {
  if (s != NULL) {
    delete s;
  }
}

namespace util {
namespace internal {

// TODO(monnand): We may want to use a memory pool for status stucture.
struct status *status_new(Status &s) {
  struct status *ret = new status();
  ret->status_ = s;
  return ret;
}

} // namespace internal
} // namespace util

