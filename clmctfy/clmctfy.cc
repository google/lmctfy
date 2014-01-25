#include <stdlib.h>

#include "clmctfy.h"
#include "lmctfy.h"
#include "util/task/statusor.h"
#include "status-internal.h"
#include "lmctfy.pb.h"
#include "codes.pb-c.h"

#define STATUS_OK UTIL__ERROR__CODE__OK

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
  return status_copy(s, v);
}

int lmctfy_init_machine(struct status *s, const Containers__Lmctfy__InitSpec *spec) {
  size_t sz = containers__lmctfy__init_spec__get_packed_size(spec);
  uint8_t *buf = new uint8_t[sz];
  int ret = 0;
  containers__lmctfy__init_spec__pack(spec, buf);
  ret = lmctfy_init_machine_raw(s, buf, sz);
  delete []buf;
  return ret;
}

int lmctfy_new_container_api(struct status *s, struct container_api **api) {
  *api = new container_api();
  (*api)->container_api_ = NULL;
  StatusOr<ContainerApi *> statusor_container_api = ContainerApi::New();
  RETURN_IF_ERROR_PTR(s, statusor_container_api, &((*api)->container_api_));
  return STATUS_OK;
}

void lmctfy_delete_container_api(struct container_api *api) {
  if (api != NULL) {
    if (api->container_api_ != NULL) {
      delete api->container_api_;
    }
    delete api;
  }
}

int lmctfy_container_api_get_container(struct status *s,
                                       struct container **c,
                                       const struct container_api *api,
                                       const char *container_name) {
  *c = new container();
  (*c)->container_ = NULL;

  StatusOr<Container *> statusor = api->container_api_->Get(container_name);
  RETURN_IF_ERROR_PTR(s, statusor, &((*c)->container_));
  return STATUS_OK;
}

int lmctfy_container_api_create_container_raw(struct status *s,
                                       struct container **c,
                                       struct container_api *api,
                                       const char *container_name,
                                       const void *spec,
                                       const int spec_size) {
  ContainerSpec container_spec;
  *c = new container();
  (*c)->container_ = NULL;
  container_spec.ParseFromArray(spec, spec_size);

  StatusOr<Container *> statusor = api->container_api_->Create(container_name, container_spec);
  RETURN_IF_ERROR_PTR(s, statusor, &((*c)->container_));
  return STATUS_OK;
}
