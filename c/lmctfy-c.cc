#include <stdlib.h>

#include "lmctfy-c.h"
#include "lmctfy.h"
#include "util/task/statusor.h"
#include "status-internal.h"
#include "lmctfy.pb.h"

using namespace ::containers::lmctfy;
using ::util::internal::status_copy;
using ::util::Status;
using ::util::StatusOr;

struct container {
  Container *container_;
};

struct container_api {
  ContainerApi *container_api_;
};

int lmctfy_init_machine_raw(struct status *s, const void *spec, const int spec_size) {
  InitSpec init_spec;
  init_spec.ParseFromArray(spec, spec_size);
  Status v = ContainerApi::InitMachine(init_spec);
  if (s != NULL) {
    s->status_ = v;
  }
  return v.error_code();
}

int lmctfy_init_machine(struct status *s, const Containers__Lmctfy__InitSpec *spec) {
  size_t sz = containers__lmctfy__init_spec__get_packed_size(spec);
  void *buf = malloc(sz);
  int ret = 0;
  containers__lmctfy__init_spec__pack(spec, (uint8_t *)buf);
  ret = lmctfy_init_machine_raw(s, buf, sz);
  free(buf);
  return ret;
}

struct status *lmctfy_new_container_api(struct container_api **api) {
  *api = (struct container_api *)malloc(sizeof(struct container_api));
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
    free(api);
  }
}

struct status *lmctfy_container_api_get_container(const struct container_api *api,
                                                  struct container **container,
                                                  const char *container_name) {
  *container = (struct container *)malloc(sizeof(struct container));
  (*container)->container_ = NULL;

  StatusOr<Container *> statusor = api->container_api_->Get(container_name);
  RETURN_IF_ERROR_PTR(statusor, &((*container)->container_));
  return &status_ok;
}
