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

int lmctfy_container_run_raw(struct status *s,
                             pid_t *tid,
                             struct container *container,
                             const int argc,
                             const char **argv,
                             const void *spec,
                             const size_t spec_size) {
  RunSpec run_spec;
  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, container);
  CHECK_NOTNULL_OR_RETURN(s, container->container_);
  CHECK_NOTNULL_OR_RETURN(s, tid);
  CHECK_POSITIVE_OR_RETURN(s, argc);
  if (spec != NULL && spec_size > 0) {
    run_spec.ParseFromArray(spec, spec_size);
  }
  vector<string> cmds(argc);
  int i = 0;
  for (i = 0; i < argc; i++) {
    cmds[i] = argv[i];
  }
  StatusOr<pid_t> statusor = container->container_->Run(cmds, run_spec);
  RETURN_IF_ERROR_PTR(s, statusor, tid);
  return STATUS_OK;
}

int lmctfy_container_run(struct status *s,
                         pid_t *tid,
                         struct container *container,
                         const int argc,
                         const char **argv,
                         const Containers__Lmctfy__RunSpec *spec) {

  uint8_t *buf = NULL;
  size_t sz = 0;
  int ret = 0;

  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, spec);
  CHECK_NOTNULL_OR_RETURN(s, container);
  CHECK_NOTNULL_OR_RETURN(s, container->container_);
  sz = containers__lmctfy__run_spec__get_packed_size(spec);
  if (sz > 0) {
    buf = new uint8_t[sz];
    containers__lmctfy__run_spec__pack(spec, buf);
  }
  ret = lmctfy_container_run_raw(s, tid, container, argc, argv, buf, sz);
  if (buf != NULL) {
    delete []buf;
  }
  return ret;
}

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
  int ret = STATUS_OK;

  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, spec);
  CHECK_NOTNULL_OR_RETURN(s, container);
  CHECK_NOTNULL_OR_RETURN(s, container->container_);
  sz = containers__lmctfy__container_spec__get_packed_size(spec);
  if (sz > 0) {
    buf = new uint8_t[sz];
    containers__lmctfy__container_spec__pack(spec, buf);
  }
  ret = lmctfy_container_update_raw(s, container, policy, buf, sz);
  if (buf != NULL) {
    delete []buf;
  }
  return ret;
}

int lmctfy_container_spec(struct status *s,
                          struct container *container,
                          Containers__Lmctfy__ContainerSpec **spec) {
  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, container);
  CHECK_NOTNULL_OR_RETURN(s, container->container_);
  CHECK_NOTNULL_OR_RETURN(s, spec);

  int ret = STATUS_OK;
  uint8_t *buf = NULL;
  StatusOr<ContainerSpec> statusor_container_spec = container->container_->Spec();
  if (!statusor_container_spec.ok()) {
    return status_copy(s, statusor_container_spec.status());
  }

  const ContainerSpec container_spec = statusor_container_spec.ValueOrDie();
  int sz = container_spec.ByteSize();
  *spec = NULL;
  if (sz > 0) {
    buf = new uint8_t[sz];
    container_spec.SerializeToArray(buf, sz);
    *spec = containers__lmctfy__container_spec__unpack(NULL, sz, buf);
    delete []buf;
  }
  return ret;
}

int lmctfy_container_list_subcontainers(struct status *s,
                                        struct container **subcontainers[],
                                        int *nr_subcontainers,
                                        struct container *c,
                                        int list_policy) {
  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, c);
  CHECK_NOTNULL_OR_RETURN(s, c->container_);
  CHECK_NOTNULL_OR_RETURN(s, subcontainers);
  CHECK_NOTNULL_OR_RETURN(s, nr_subcontainers);

  *nr_subcontainers = 0;
  *subcontainers = NULL;
  Container::ListPolicy policy;

  switch (list_policy) {
    case CONTAINER_LIST_POLICY_SELF:
      policy = Container::LIST_SELF;
      break;
    case CONTAINER_LIST_POLICY_RECURSIVE:
      policy = Container::LIST_RECURSIVE;
      break;
    default:
      return status_new(s, UTIL__ERROR__CODE__INVALID_ARGUMENT, "Unknown list policy: %d", list_policy);
  }

  StatusOr< vector<Container *> > statusor_subcontainers = 
      c->container_->ListSubcontainers(policy);

  if (!statusor_subcontainers.ok()) {
    return status_copy(s, statusor_subcontainers.status());
  }

  const vector<Container *> &subcontainers_vector = statusor_subcontainers.ValueOrDie();
  if (subcontainers_vector.size() == 0) {
    return STATUS_OK;
  }
  struct container **subctnrs = (struct container **)malloc(sizeof(struct container *) * subcontainers_vector.size());
  if (subctnrs == NULL) {
    return status_new(s, UTIL__ERROR__CODE__RESOURCE_EXHAUSTED, "out of memory");
  }
  *subcontainers = subctnrs;
  *nr_subcontainers = subcontainers_vector.size();

  vector<Container *>::const_iterator container_iter = subcontainers_vector.begin();
  for (container_iter = subcontainers_vector.begin(); container_iter != subcontainers_vector.end(); container_iter++) {
    struct container *ctnr = new container();
    ctnr->container_ = *container_iter;
    *subctnrs = ctnr;
    subctnrs++;
  }
  return STATUS_OK;
}

