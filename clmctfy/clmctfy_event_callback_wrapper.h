#ifndef LMCTFY_CLMCTFY_CLMCTFY_EVENT_CALLBACK_WRAPPER_H_
#define LMCTFY_CLMCTFY_CLMCTFY_EVENT_CALLBACK_WRAPPER_H_

#include "lmctfy.h"
#include "clmctfy_container.h"

class EventCallbackWrapper : public Callback2<::containers::lmctfy::Container *, ::util::Status> {
 public:
  EventCallbackWrapper(struct container *c,
                       lmctfy_event_callback_f cb,
                       void *user_data) 
      : container_(c),
        callback_(cb),
        user_data_(user_data) { }
  virtual ~EventCallbackWrapper() {}
  virtual bool IsRepeatable() const { return true; }
  virtual void Run(::containers::lmctfy::Container *c, ::util::Status s);
 private:
  lmctfy_event_callback_f callback_;
  void *user_data_;
  struct container *container_;
  ::containers::lmctfy::Container ::NotificationId notif_id_;
  DISALLOW_COPY_AND_ASSIGN(EventCallbackWrapper);
};

#endif // LMCTFY_CLMCTFY_CLMCTFY_EVENT_CALLBACK_WRAPPER_H_
