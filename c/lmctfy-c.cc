#include "lmctfy-c.h"
#include "lmctfy.h"
#include "status-internal.h"
#include "lmctfy.pb.h"

using ::containers::lmctfy::Container;
using ::containers::lmctfy::ContainerApi;
using ::util::internal::status_new;

struct container {
  Container *container_;
};

struct container_api {
  ContainerApi *container_api_;
};

struct status *lmctfy_init_machine_raw(void *spec, int spec_size) {
}

