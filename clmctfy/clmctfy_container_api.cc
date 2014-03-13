#include "clmctfy_container_api.h"

#include <stdlib.h>
#include <string.h>

#include "util/task/statusor.h"
#include "lmctfy.pb.h"
#include "util/task/codes.pb-c.h"
#include "lmctfy.h"

#include "clmctfy_macros.h"
#include "clmctfy_container_struct.h"
#include "clmctfy_container_api_struct.h"

#define STATUS_OK UTIL__ERROR__CODE__OK

using ::containers::lmctfy::Container;
using ::containers::lmctfy::ContainerApi;
using ::containers::lmctfy::InitSpec;
using ::containers::lmctfy::ContainerSpec;
using ::util::internal::status_copy;
using ::util::internal::status_new;
using ::util::Status;
using ::util::StatusOr;

int lmctfy_init_machine_raw(const void *spec, const size_t spec_size, struct status *s) {
  InitSpec init_spec;
  CHECK_NOTFAIL_OR_RETURN(s);
  if (spec != NULL && spec_size > 0) {
    // XXX should we consider this as an error?
    init_spec.ParseFromArray(spec, spec_size);
  }
  Status v = ContainerApi::InitMachine(init_spec);
  return status_copy(s, v);
}

int lmctfy_init_machine(const Containers__Lmctfy__InitSpec *spec, struct status *s) {
  uint8_t *buf = NULL;
  size_t sz = 0;
  int ret = 0;

  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, spec);
  sz = containers__lmctfy__init_spec__get_packed_size(spec);
  if (sz > 0) {
    // TODO(monnand) Can we use alloca(3) here, so that we don't need to use heap?
    buf = new uint8_t[sz];
    containers__lmctfy__init_spec__pack(spec, buf);
  }
  ret = lmctfy_init_machine_raw(buf, sz, s);
  if (buf != NULL) {
    delete []buf;
  }
  return ret;
}

int lmctfy_new_container_api(struct container_api **api, struct status *s) {
  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, api);
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

#define COPY_CONTAINER_STRUCTURE(ctnr_ptr, ctnr_struct)  do {  \
  if ((ctnr_ptr) != NULL) { \
    (*(ctnr_struct)) = new container(); \
    (*(ctnr_struct))->container_ = ctnr_ptr;  \
  }   \
} while(0)

int lmctfy_container_api_get_container(const struct container_api *api,
                                       const char *container_name,
                                       struct container **c,
                                       struct status *s) {
  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, api);
  CHECK_NOTNULL_OR_RETURN(s, api->container_api_);
  CHECK_NOTNULL_OR_RETURN(s, c);
  CHECK_NOTNULL_OR_RETURN(s, container_name);
  CHECK_POSITIVE_OR_RETURN(s, strlen(container_name));
  Container *ctnr = NULL;

  StatusOr<Container *> statusor = api->container_api_->Get(container_name);
  RETURN_IF_ERROR_PTR(s, statusor, &ctnr);
  COPY_CONTAINER_STRUCTURE(ctnr, c);
  return STATUS_OK;
}

int lmctfy_container_api_create_container_raw(struct container_api *api,
                                       const char *container_name,
                                       const void *spec,
                                       const size_t spec_size,
                                       struct container **c,
                                       struct status *s) {
  CHECK_NOTNULL_OR_RETURN(s, api);
  CHECK_NOTNULL_OR_RETURN(s, api->container_api_);
  CHECK_NOTNULL_OR_RETURN(s, c);
  CHECK_NOTNULL_OR_RETURN(s, container_name);
  CHECK_POSITIVE_OR_RETURN(s, strlen(container_name));
  ContainerSpec container_spec;
  Container *ctnr = NULL;
  if (spec != NULL && spec_size > 0) {
    container_spec.ParseFromArray(spec, spec_size);
  }

  StatusOr<Container *> statusor = api->container_api_->Create(container_name, container_spec);
  RETURN_IF_ERROR_PTR(s, statusor, &ctnr);
  COPY_CONTAINER_STRUCTURE(ctnr, c);
  return STATUS_OK;
}

int lmctfy_container_api_create_container(struct container_api *api,
                                          const char *container_name,
                                          const Containers__Lmctfy__ContainerSpec *spec,
                                          struct container **c,
                                          struct status *s) {
  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, api);
  CHECK_NOTNULL_OR_RETURN(s, api->container_api_);
  CHECK_NOTNULL_OR_RETURN(s, c);
  CHECK_NOTNULL_OR_RETURN(s, spec);
  CHECK_NOTNULL_OR_RETURN(s, container_name);
  CHECK_POSITIVE_OR_RETURN(s, strlen(container_name));
  uint8_t *buf = NULL;
  size_t sz = 0;
  int ret = 0;
  sz = containers__lmctfy__container_spec__get_packed_size(spec);
  if (sz > 0) {
    buf = new uint8_t[sz];
  }
  containers__lmctfy__container_spec__pack(spec, buf);
  ret = lmctfy_container_api_create_container_raw(api, container_name, buf, sz, c, s);
  if (buf != NULL) {
    delete []buf;
  }
  return ret;
}

int lmctfy_container_api_destroy_container(struct container_api *api,
                                           struct container *c,
                                           struct status *s) {
  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, api);
  CHECK_NOTNULL_OR_RETURN(s, api->container_api_);
  int ret = STATUS_OK;
  if (c != NULL && c->container_ != NULL) {
    Status status = api->container_api_->Destroy(c->container_);
    ret = status_copy(s, status);
  }
  return ret;
}

int lmctfy_container_api_detect_container(struct container_api *api,
                                          pid_t pid,
                                          char **container_name,
                                          struct status *s) {
  int ret = STATUS_OK;
  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, api);
  CHECK_NOTNULL_OR_RETURN(s, api->container_api_);
  CHECK_NOTNULL_OR_RETURN(s, container_name);
  StatusOr<string> statusor = api->container_api_->Detect(pid);
  ret = status_copy(s, statusor.status());
  if (container_name != NULL && statusor.ok()) {
    *container_name = strdup(statusor.ValueOrDie().c_str());
  }
  return ret;
}
