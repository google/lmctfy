#include "clmctfy.h"
#include "lmctfy.h"
#include "clmctfy_internal.h"
#include "util/task/statusor.h"
#include "base/callback.h"
#include "base/macros.h"
#include <vector>
#include <unordered_map>

using namespace ::containers::lmctfy;
using ::util::internal::status_copy;
using ::util::internal::status_new;
using ::util::Status;
using ::util::StatusOr;
using ::util::error::INVALID_ARGUMENT;
using ::std::vector;
using ::std::unordered_map;

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

  const ContainerSpec &container_spec = statusor_container_spec.ValueOrDie();
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

int lmctfy_container_list_threads(struct status *s,
                                  pid_t *threads[],
                                  int *nr_threads,
                                  struct container *c,
                                  int list_policy) {
  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, c);
  CHECK_NOTNULL_OR_RETURN(s, c->container_);
  CHECK_NOTNULL_OR_RETURN(s, threads);
  CHECK_NOTNULL_OR_RETURN(s, nr_threads);

  *threads = NULL;
  *nr_threads = 0;
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

  StatusOr<vector<pid_t>> statusor_pids = c->container_->ListThreads(policy);
  if (!statusor_pids.ok()) {
    return status_copy(s, statusor_pids.status());
  }
  const vector<pid_t> &pids = statusor_pids.ValueOrDie();
  *nr_threads = pids.size();
  if (*nr_threads == 0) {
    return STATUS_OK;
  }

  pid_t *ptr = (pid_t *)malloc(sizeof(pid_t) * pids.size());
  *threads = ptr;
  vector<pid_t>::const_iterator iter;
  for (iter = pids.begin(); iter != pids.end(); iter++, ptr++) {
    *ptr = *iter;
  }
  return STATUS_OK;
}

int lmctfy_container_list_processes(struct status *s,
                                    pid_t *processes[],
                                    int *nr_processes,
                                    struct container *c,
                                    int list_policy) {
  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, c);
  CHECK_NOTNULL_OR_RETURN(s, c->container_);
  CHECK_NOTNULL_OR_RETURN(s, processes);
  CHECK_NOTNULL_OR_RETURN(s, nr_processes);

  *processes = NULL;
  *nr_processes = 0;
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

  StatusOr<vector<pid_t>> statusor_pids = c->container_->ListProcesses(policy);
  if (!statusor_pids.ok()) {
    return status_copy(s, statusor_pids.status());
  }
  const vector<pid_t> &pids = statusor_pids.ValueOrDie();
  *nr_processes = pids.size();
  if (*nr_processes == 0) {
    return STATUS_OK;
  }

  pid_t *ptr = (pid_t *)malloc(sizeof(pid_t) * pids.size());
  *processes = ptr;
  vector<pid_t>::const_iterator iter;
  for (iter = pids.begin(); iter != pids.end(); iter++, ptr++) {
    *ptr = *iter;
  }
  return STATUS_OK;
}

int lmctfy_container_pause(struct status *s,
                           struct container *container) {
  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, container);
  CHECK_NOTNULL_OR_RETURN(s, container->container_);

  Status status = container->container_->Pause();
  return status_copy(s, status);
}

int lmctfy_container_resume(struct status *s,
                           struct container *container) {
  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, container);
  CHECK_NOTNULL_OR_RETURN(s, container->container_);

  Status status = container->container_->Resume();
  return status_copy(s, status);
}

int lmctfy_container_killall(struct status *s,
                           struct container *container) {
  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, container);
  CHECK_NOTNULL_OR_RETURN(s, container->container_);

  Status status = container->container_->KillAll();
  return status_copy(s, status);
}

int lmctfy_container_stats(struct status *s,
                          struct container *container,
                          int stats_type,
                          Containers__Lmctfy__ContainerStats **stats) {
  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, container);
  CHECK_NOTNULL_OR_RETURN(s, container->container_);
  CHECK_NOTNULL_OR_RETURN(s, stats);

  int ret = STATUS_OK;
  uint8_t *buf = NULL;
  Container::StatsType type;
  switch (stats_type) {
    case CONTAINER_STATS_TYPE_SUMMARY:
      type = Container::STATS_SUMMARY;
      break;
    case CONTAINER_STATS_TYPE_FULL:
      type = Container::STATS_FULL;
      break;
    default:
      return status_new(s, UTIL__ERROR__CODE__INVALID_ARGUMENT, "Unknown stats type: %d", stats_type);
  }
  StatusOr<ContainerStats> statusor_container_stats = container->container_->Stats(type);
  if (!statusor_container_stats.ok()) {
    return status_copy(s, statusor_container_stats.status());
  }

  const ContainerStats &container_stats = statusor_container_stats.ValueOrDie();
  int sz = container_stats.ByteSize();
  *stats = NULL;
  if (sz > 0) {
    buf = new uint8_t[sz];
    container_stats.SerializeToArray(buf, sz);
    *stats = containers__lmctfy__container_stats__unpack(NULL, sz, buf);
    delete []buf;
  }
  return ret;
}

const char *lmctfy_container_name(struct container *container) {
  if (container == NULL) {
    return NULL;
  }
  if (container->container_ == NULL) {
    return NULL;
  }
  return container->container_->name().c_str();
}

inline void EventCallbackWrapper::Run(Container *c, Status s) {
  if (callback_ == NULL) {
    return;
  }
  struct container ctnr;
  struct status sts;
  ctnr.container_ = c;
  status_copy(&sts, s);

  if (c == NULL) {
    callback_(NULL, &sts);
  } else {
    callback_(&ctnr, &sts);
  }
  if (sts.message != NULL) {
    free(sts.message);
  }
}

int lmctfy_container_register_notification_raw(struct status *s,
                                               notification_id_t *notif_id,
                                               struct container *container,
                                               lmctfy_event_callback_f callback,
                                               const void *spec,
                                               const size_t spec_size) {
  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, container);
  CHECK_NOTNULL_OR_RETURN(s, container->container_);
  EventSpec event_spec;
  if (spec != NULL && spec_size > 0) {
    event_spec.ParseFromArray(spec, spec_size);
  }
  EventCallbackWrapper *cb = new EventCallbackWrapper(callback);

  // Container object does not take the ownership of the callback.
  StatusOr<Container::NotificationId> statusor_id = container->container_->RegisterNotification(event_spec, cb);

  if (!statusor_id.ok()) {
    delete cb;
    return status_copy(s, statusor_id.status());
  }
  notification_id_t nid = statusor_id.ValueOrDie();
  container->notif_map_[nid] = cb;
  *notif_id = nid;
  return STATUS_OK;
}


int lmctfy_container_unregister_notification_raw(struct status *s,
                                                 struct container *container,
                                                 const notification_id_t notif_id) {
  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, container);
  CHECK_NOTNULL_OR_RETURN(s, container->container_);
  unordered_map<notification_id_t, EventCallbackWrapper *>::iterator iter =
      container->notif_map_.find(notif_id);
  if (iter == container->notif_map_.end()) {
    return status_new(s, UTIL__ERROR__CODE__INVALID_ARGUMENT, "unknown notification id");
  }
  Status status = container->container_->UnregisterNotification(notif_id);
  if (!status.ok()) {
    return status_copy(s, status);
  }
  container->notif_map_.erase(iter);
  delete iter->second;
  return STATUS_OK;
}
