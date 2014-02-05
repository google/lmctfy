#include "clmctfy.h"
#include "lmctfy.h"
#include "clmctfy_internal.h"
#include "util/task/statusor.h"
#include <vector>

using namespace ::containers::lmctfy;
using ::util::internal::status_copy;
using ::util::internal::status_new;
using ::util::Status;
using ::util::StatusOr;
using ::util::error::INVALID_ARGUMENT;
using ::std::vector;

#define STATUS_OK UTIL__ERROR__CODE__OK

int lmctfy_container_enter(struct status *s,
                           struct container *container,
                           const pid_t *tids,
                           const int n) {
  int ret = STATUS_OK;
  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, container);
  CHECK_NOTNULL_OR_RETURN(s, container->container_);

  if (tids == NULL || n <= 0) {
    return ret;
  }

  vector<pid_t> tids_v(n);
  int i = 0;
  for (i = 0; i < n; i++) {
    tids_v[i] = tids[i];
  }
  Status status = container->container_->Enter(tids_v);
  return status_copy(s, status);
}

int lmctfy_container_exec(struct status *s,
                          struct container *container,
                          const int argc,
                          const char **argv) {
  int ret = STATUS_OK;
  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, container);
  CHECK_NOTNULL_OR_RETURN(s, container->container_);
  CHECK_POSITIVE_OR_RETURN(s, argc);

  vector<string> cmds(argc);
  int i = 0;
  for (i = 0; i < argc; i++) {
    cmds[i] = argv[i];
  }

  Status status = container->container_->Exec(cmds);
  return status_copy(s, status);
}

void lmctfy_delete_container(struct container *container) {
  if (container != NULL) {
    if (container->container_ != NULL) {
      delete container->container_;
    }
    delete container;
  }
}

int lmctfy_container_update_raw(struct status *s,
                                struct container *container,
                                int policy,
                                const void *spec,
                                const size_t spec_size) {
  ContainerSpec container_spec;
  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, container);
  CHECK_NOTNULL_OR_RETURN(s, container->container_);
  if (spec != NULL && spec_size > 0) {
    container_spec.ParseFromArray(spec, spec_size);
  }
  Container::UpdatePolicy p;
  switch (policy) {
  case CONTAINER_UPDATE_POLICY_DIFF:
    p = Container::UPDATE_DIFF;
    break;
  case CONTAINER_UPDATE_POLICY_REPLACE:
    p = Container::UPDATE_REPLACE;
    break;
  default:
    return status_new(s, UTIL__ERROR__CODE__INVALID_ARGUMENT,
                      "Unknown update policy: %d", policy);
  }
  Status status = container->container_->Update(container_spec, p);
  return status_copy(s, status);
}

int lmctfy_container_update(struct status *s,
                            struct container *container,
                            int policy,
                            const Containers__Lmctfy__ContainerSpec *spec) {

  uint8_t *buf = NULL;
  size_t sz = 0;
  int ret = 0;

  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, spec);
  CHECK_NOTNULL_OR_RETURN(s, container);
  sz = containers__lmctfy__container_spec__get_packed_size(spec);
  if (sz > 0) {
    buf = new uint8_t[sz];
    containers__lmctfy__container_spec__pack(spec, buf);
  }
  ret = lmctfy_container_update_raw(s, container, policy, buf, sz);
  delete []buf;
  return ret;
}
