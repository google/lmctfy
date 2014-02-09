#ifndef LMCTFY_C_BINDING_STATUS_INTERNAL_H_
#define LMCTFY_C_BINDING_STATUS_INTERNAL_H_

#include "util/errors.h"
#include "clmctfy.h"
#include "util/task/status.h"
#include "util/task/statusor.h"
#include "lmctfy.h"

#include <unordered_map>

using ::containers::lmctfy::Container;
using ::containers::lmctfy::ContainerApi;

class EventCallbackWrapper : public Callback2<Container *, ::util::Status> {
 public:
  EventCallbackWrapper(struct container *c,
                       lmctfy_event_callback_f cb,
                       void *user_data) 
      : container_(c),
        callback_(cb),
        user_data_(user_data) { }
  virtual ~EventCallbackWrapper() {}
  virtual bool IsRepeatable() const { return true; }
  virtual void Run(Container *c, ::util::Status s);
 private:
  lmctfy_event_callback_f callback_;
  void *user_data_;
  struct container *container_;
  Container::NotificationId notif_id_;
  DISALLOW_COPY_AND_ASSIGN(EventCallbackWrapper);
};

struct container {
  Container *container_;
  // TODO(monnand): Make it thread-safe?
  ::std::unordered_map<notification_id_t, EventCallbackWrapper *> notif_map_;
};

struct container_api {
  ContainerApi *container_api_;
};

#define RETURN_IF_ERROR_PTR(s, ...)                                 \
    do {                                                            \
      const ::util::Status _status =                                \
        ::util::errors_internal::PerformSideEffects(__VA_ARGS__);   \
      if (PREDICT_FALSE(!_status.ok())) {                           \
        if (s != NULL) status_copy(s, _status);                     \
        return (int)_status.error_code();                           \
      }                                                             \
    } while (0)

#define CHECK_NOTFAIL_OR_RETURN(status) do {  \
  if ((status) != NULL) { \
    if ((status)->error_code != UTIL__ERROR__CODE__OK) {  \
      return (status)->error_code; \
    } \
  } \
} while(0)

#define CHECK_NOTNULL_OR_RETURN(status, ptr)  do {                    \
    if ((ptr) == NULL) {                                              \
      return status_new(status, UTIL__ERROR__CODE__INVALID_ARGUMENT,  \
                        "In function %s: %s cannot be null",          \
                        __func__, #ptr);                              \
    }                                                                 \
} while (0)

#define CHECK_POSITIVE_OR_RETURN(status, value)  do {                       \
    if ((value) <= 0) {                                                     \
      return status_new(status, UTIL__ERROR__CODE__INVALID_ARGUMENT,        \
                        "In function %s: %s=%d, but it should be positive", \
                        __func__, #value, value);                           \
    }                                                                       \
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
