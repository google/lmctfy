#include "clmctfy_container_api_raw.h"

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

#define COPY_CONTAINER_STRUCTURE(ctnr_ptr, ctnr_struct)  do {  \
  if ((ctnr_ptr) != NULL) { \
    (*(ctnr_struct)) = new container(); \
    (*(ctnr_struct))->container_ = ctnr_ptr;  \
  }   \
} while(0)

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

