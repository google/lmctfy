#include <stdlib.h>

#include "clmctfy.h"
#include "clmctfy-raw.h"
#include "lmctfy.h"
#include "util/task/statusor.h"
#include "clmctfy_internal.h"
#include "lmctfy.pb.h"
#include "util/task/codes.pb-c.h"

#define STATUS_OK UTIL__ERROR__CODE__OK

using namespace ::containers::lmctfy;
using ::util::internal::status_copy;
using ::util::internal::status_new;
using ::util::Status;
using ::util::StatusOr;

int lmctfy_init_machine_raw(struct status *s, const void *spec, const size_t spec_size) {
  InitSpec init_spec;
  CHECK_NOTFAIL_OR_RETURN(s);
  if (spec != NULL && spec_size > 0) {
    // XXX should we consider this as an error?
    init_spec.ParseFromArray(spec, spec_size);
  }
  Status v = ContainerApi::InitMachine(init_spec);
  return status_copy(s, v);
}

int lmctfy_init_machine(struct status *s, const Containers__Lmctfy__InitSpec *spec) {
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
  ret = lmctfy_init_machine_raw(s, buf, sz);
  delete []buf;
  return ret;
}

int lmctfy_new_container_api(struct status *s, struct container_api **api) {
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

int lmctfy_container_api_get_container(struct status *s,
                                       struct container **c,
                                       const struct container_api *api,
                                       const char *container_name) {
  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, api);
  CHECK_NOTNULL_OR_RETURN(s, api->container_api_);
  CHECK_NOTNULL_OR_RETURN(s, c);
  Container *ctnr = NULL;

  StatusOr<Container *> statusor = api->container_api_->Get(container_name);
  RETURN_IF_ERROR_PTR(s, statusor, &ctnr);
  COPY_CONTAINER_STRUCTURE(ctnr, c);
  return STATUS_OK;
}

int lmctfy_container_api_create_container_raw(struct status *s,
                                       struct container **c,
                                       struct container_api *api,
                                       const char *container_name,
                                       const void *spec,
                                       const size_t spec_size) {
  CHECK_NOTNULL_OR_RETURN(s, api);
  CHECK_NOTNULL_OR_RETURN(s, api->container_api_);
  CHECK_NOTNULL_OR_RETURN(s, c);
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

int lmctfy_container_api_create_container(struct status *s,
                                          struct container **c,
                                          struct container_api *api,
                                          const char *container_name,
                                          const Containers__Lmctfy__ContainerSpec *spec) {
  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, api);
  CHECK_NOTNULL_OR_RETURN(s, api->container_api_);
  CHECK_NOTNULL_OR_RETURN(s, c);
  CHECK_NOTNULL_OR_RETURN(s, spec);
  uint8_t *buf = NULL;
  size_t sz = 0;
  int ret = 0;
  sz = containers__lmctfy__container_spec__get_packed_size(spec);
  if (sz > 0) {
    buf = new uint8_t[sz];
  }
  containers__lmctfy__container_spec__pack(spec, buf);
  ret = lmctfy_container_api_create_container_raw(s, c, api, container_name, buf, sz);
  delete []buf;
  return ret;
}

int lmctfy_container_api_destroy_container(struct status *s,
                                           struct container_api *api,
                                           struct container *c) {
  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, api);
  CHECK_NOTNULL_OR_RETURN(s, api->container_api_);
  int ret = STATUS_OK;
  if (c != NULL && c->container_ != NULL) {
    Status status = api->container_api_->Destroy(c->container_);
    ret = status_copy(s, status);
    delete c;
  }
  return ret;
}

int lmctfy_container_api_detect_container(struct status *s,
                                          char *container_name,
                                          const size_t n,
                                          struct container_api *api,
                                          pid_t pid) {
  int ret = STATUS_OK;
  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, api);
  CHECK_NOTNULL_OR_RETURN(s, api->container_api_);
  CHECK_NOTNULL_OR_RETURN(s, container_name);
  CHECK_POSITIVE_OR_RETURN(s, n);
  StatusOr<string> statusor = api->container_api_->Detect(pid);
  ret = status_copy(s, statusor.status());
  if (container_name != NULL && statusor.ok() && n > 0) {
    strncpy(container_name, statusor.ValueOrDie().c_str(), n);
  }
  return ret;
}

namespace containers {
namespace lmctfy {
namespace internal {

ContainerApi *lmctfy_container_api_strip(struct container_api *api) {
  return api->container_api_;
}

Container *lmctfy_container_strip(struct container *c) {
  return c->container_;
}

} // internal
} // lmctfy
} // containers
