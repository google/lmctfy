#ifndef LMCTFY_CLMCTFY_CLMCTFY_CONTAINER_STRUCT_H_
#define LMCTFY_CLMCTFY_CLMCTFY_CONTAINER_STRUCT_H_

#include <unordered_map>
#include "lmctfy.h"
#include "clmctfy_container.h"

class EventCallbackWrapper;

struct container {
  ::containers::lmctfy::Container *container_;
  // TODO(monnand): Make it thread-safe?
  ::std::unordered_map<notification_id_t, EventCallbackWrapper *> notif_map_;
};


#endif // LMCTFY_CLMCTFY_CLMCTFY_CONTAINER_STRUCT_H_
