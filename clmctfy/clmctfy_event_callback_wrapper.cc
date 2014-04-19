#include "clmctfy_event_callback_wrapper.h"

#include "util/task/status.h"
#include "lmctfy.h"
#include "clmctfy_status_internal.h"
#include "clmctfy_container_struct.h"

using ::containers::lmctfy::Container;
using ::util::Status;
using ::util::internal::status_copy;
using ::util::internal::status_new;

void EventCallbackWrapper::Run(Container *c, Status s) {
  if (callback_ == NULL) {
    return;
  }
  struct status sts;
  status_copy(&sts, s);
  if (c == NULL) {
    callback_(NULL, &sts, user_data_);
    if (sts.message != NULL) {
      free(sts.message);
    }
    return;
  }
  if (c != container_->container_) {
    char *oldmsg = sts.message;
    int olderrcode = sts.error_code;
    struct container ctnr;
    ctnr.container_ = c;
    // This should never happen.
    status_new(&sts, UTIL__ERROR__CODE__UNKNOWN,
               "Unknown container passed to the callback. "
               "(ErrorCode=%d, Message=\"%s\")", olderrcode,
               (oldmsg == NULL ? "" : oldmsg));
    callback_(&ctnr, &sts, user_data_);
    if (sts.message != NULL) {
      free(sts.message);
    }
    if (oldmsg != NULL) {
      free(oldmsg);
    }
    return;
  }
  callback_(container_, &sts, user_data_);
  if (sts.message != NULL) {
    free(sts.message);
  }
}

