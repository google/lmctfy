#include <stdlib.h>

#include "lmctfy-c.h"
#include "lmctfy.h"
#include "util/task/statusor.h"
#include "status-internal.h"
#include "lmctfy.pb.h"

using namespace ::containers::lmctfy;
using ::util::internal::status_new;
using ::util::Status;
using ::util::StatusOr;

struct container {
  Container *container_;
};

struct container_api {
  ContainerApi *container_api_;
};

struct status *lmctfy_init_machine_raw(const void *spec, const int spec_size) {
  InitSpec init_spec;
  init_spec.ParseFromArray(spec, spec_size);
  Status s = ContainerApi::InitMachine(init_spec);
  return status_new(s);
}

struct status *lmctfy_init_machine(const Containers__Lmctfy__InitSpec *spec) {
  size_t sz = containers__lmctfy__init_spec__get_packed_size(spec);
  void *buf = malloc(sz);
  containers__lmctfy__init_spec__pack(spec, (uint8_t *)buf);
  struct status *s = lmctfy_init_machine_raw(buf, sz);
  free(buf);
  return s;
}

struct status *lmctfy_new_container_api(struct container_api **api) {
  *api = new container_api();
  (*api)->container_api_ = NULL;
  StatusOr<ContainerApi *> statusor_container_api = ContainerApi::New();
  RETURN_IF_ERROR_PTR(statusor_container_api, &((*api)->container_api_));
  return &status_ok;
}

void lmctfy_release_container_api(struct container_api *api) {
  if (api != NULL) {
    if (api->container_api_ != NULL) {
      // TODO(monnand): delete api->container_api_?
    }
    delete api;
  }
}

