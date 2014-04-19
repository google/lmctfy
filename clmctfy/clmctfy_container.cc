#include "clmctfy_container.h"

#include <vector>
#include <unordered_map>

#include "util/task/statusor.h"
#include "base/callback.h"
#include "base/macros.h"

#include "lmctfy.h"
#include "clmctfy_macros.h"
#include "clmctfy_container_struct.h"
#include "clmctfy_container_raw.h"
#include "clmctfy_event_callback_wrapper.h"

using ::containers::lmctfy::Container;
using ::containers::lmctfy::ContainerApi;
using ::containers::lmctfy::ContainerSpec;
using ::containers::lmctfy::RunSpec;
using ::containers::lmctfy::EventSpec;
using ::containers::lmctfy::ContainerStats;
using ::util::internal::status_copy;
using ::util::internal::status_new;
using ::util::Status;
using ::util::StatusOr;
using ::util::error::INVALID_ARGUMENT;
using ::std::vector;
using ::std::unordered_map;

#define STATUS_OK UTIL__ERROR__CODE__OK

int lmctfy_container_run(struct container *container,
                         const int argc,
                         const char **argv,
                         const Containers__Lmctfy__RunSpec *spec,
                         pid_t *tid,
                         struct status *s) {
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
  ret = lmctfy_container_run_raw(container, argc, argv, buf, sz, tid, s);
  if (buf != NULL) {
    delete []buf;
  }
  return ret;
}

int lmctfy_container_enter(struct container *container,
                           const pid_t *tids,
                           const int tids_size,
                           struct status *s) {
  int ret = STATUS_OK;
  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, container);
  CHECK_NOTNULL_OR_RETURN(s, container->container_);

  if (tids == NULL || tids_size <= 0) {
    return ret;
  }

  vector<pid_t> tids_v(tids_size);
  int i = 0;
  for (i = 0; i < tids_size; i++) {
    tids_v[i] = tids[i];
  }
  Status status = container->container_->Enter(tids_v);
  return status_copy(s, status);
}

int lmctfy_container_exec(struct container *container,
                          const int argc,
                          const char **argv,
                          struct status *s) {
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
      unordered_map<notification_id_t, EventCallbackWrapper *>::iterator iter;
      for (iter = container->notif_map_.begin();
           iter != container->notif_map_.end();
           iter++) {
        container->container_->UnregisterNotification(iter->first);
        delete iter->second;
        iter->second = NULL;
      }
      container->notif_map_.clear();
      delete container->container_;
    }
    delete container;
  }
}

int lmctfy_container_update(struct container *container,
                            int policy,
                            const Containers__Lmctfy__ContainerSpec *spec,
                            struct status *s) {
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
  ret = lmctfy_container_update_raw(container, policy, buf, sz, s);
  if (buf != NULL) {
    delete []buf;
  }
  return ret;
}

int lmctfy_container_spec(struct container *container,
                          Containers__Lmctfy__ContainerSpec **spec,
                          struct status *s) {
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

int lmctfy_container_list_subcontainers(struct container *c,
                                        int list_policy,
                                        struct container **subcontainers[],
                                        int *subcontainers_size, 
                                        struct status *s) {
  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, c);
  CHECK_NOTNULL_OR_RETURN(s, c->container_);
  CHECK_NOTNULL_OR_RETURN(s, subcontainers);
  CHECK_NOTNULL_OR_RETURN(s, subcontainers_size);

  *subcontainers_size = 0;
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
  *subcontainers_size = subcontainers_vector.size();

  vector<Container *>::const_iterator container_iter = subcontainers_vector.begin();
  for (container_iter = subcontainers_vector.begin(); container_iter != subcontainers_vector.end(); container_iter++) {
    struct container *ctnr = new container();
    ctnr->container_ = *container_iter;
    *subctnrs = ctnr;
    subctnrs++;
  }
  return STATUS_OK;
}

int lmctfy_container_list_threads(struct container *container,
                                  int list_policy,
                                  pid_t *threads[],
                                  int *threads_size,
                                  struct status *s) {
  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, container);
  CHECK_NOTNULL_OR_RETURN(s, container->container_);
  CHECK_NOTNULL_OR_RETURN(s, threads);
  CHECK_POSITIVE_OR_RETURN(s, threads_size);

  *threads = NULL;
  *threads_size = 0;
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

  StatusOr<vector<pid_t>> statusor_pids = container->container_->ListThreads(policy);
  if (!statusor_pids.ok()) {
    return status_copy(s, statusor_pids.status());
  }
  const vector<pid_t> &pids = statusor_pids.ValueOrDie();
  *threads_size = pids.size();
  if (*threads_size == 0) {
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

int lmctfy_container_list_processes(struct container *c,
                                    int list_policy,
                                    pid_t *processes[],
                                    int *processes_size,
                                    struct status *s) {
  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, c);
  CHECK_NOTNULL_OR_RETURN(s, c->container_);
  CHECK_NOTNULL_OR_RETURN(s, processes);
  CHECK_NOTNULL_OR_RETURN(s, processes_size);

  *processes = NULL;
  *processes_size = 0;
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
  *processes_size = pids.size();
  if (*processes_size == 0) {
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

int lmctfy_container_pause(struct container *container, struct status *s) {
  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, container);
  CHECK_NOTNULL_OR_RETURN(s, container->container_);

  Status status = container->container_->Pause();
  return status_copy(s, status);
}

int lmctfy_container_resume(struct container *container, struct status *s) {
  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, container);
  CHECK_NOTNULL_OR_RETURN(s, container->container_);

  Status status = container->container_->Resume();
  return status_copy(s, status);
}

int lmctfy_container_killall(struct container *container, struct status *s) {
  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, container);
  CHECK_NOTNULL_OR_RETURN(s, container->container_);

  Status status = container->container_->KillAll();
  return status_copy(s, status);
}

int lmctfy_container_stats(struct container *container,
                          int stats_type,
                          Containers__Lmctfy__ContainerStats **stats,
                          struct status *s) {
  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, container);
  CHECK_NOTNULL_OR_RETURN(s, container->container_);
  CHECK_NOTNULL_OR_RETURN(s, stats);

  int ret = STATUS_OK;
  uint8_t *buf = NULL;
  size_t sz;
  ret = lmctfy_container_stats_raw(container, stats_type, (void **)&buf, &sz, s);
  if (ret != STATUS_OK) {
    return ret;
  }
  *stats = containers__lmctfy__container_stats__unpack(NULL, sz, buf);
  free(buf);
  return ret;
  /*
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
  */
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

int lmctfy_container_register_notification(struct container *container,
                                           lmctfy_event_callback_f callback,
                                           void *user_data,
                                           Containers__Lmctfy__EventSpec *spec,
                                           notification_id_t *notif_id,
                                           struct status *s) {
  uint8_t *buf = NULL;
  size_t sz = 0;
  int ret = 0;

  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, spec);
  CHECK_NOTNULL_OR_RETURN(s, container);
  CHECK_NOTNULL_OR_RETURN(s, container->container_);
  CHECK_NOTNULL_OR_RETURN(s, notif_id);
  CHECK_NOTNULL_OR_RETURN(s, callback);
  sz = containers__lmctfy__event_spec__get_packed_size(spec);
  if (sz > 0) {
    buf = new uint8_t[sz];
    containers__lmctfy__event_spec__pack(spec, buf);
  }
  ret = lmctfy_container_register_notification_raw(container,
                                                   callback,
                                                   user_data,
                                                   buf,
                                                   sz,
                                                   notif_id,
                                                   s);
  if (buf != NULL) {
    delete []buf;
  }
  return ret;
}

int lmctfy_container_unregister_notification(struct container *container,
                                             const notification_id_t notif_id,
                                             struct status *s) {
  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, container);
  CHECK_NOTNULL_OR_RETURN(s, container->container_);
  unordered_map<notification_id_t, EventCallbackWrapper *>::iterator iter =
      container->notif_map_.find(notif_id);
  if (iter == container->notif_map_.end()) {
    return status_new(s, UTIL__ERROR__CODE__INVALID_ARGUMENT, "unknown notification id");
  }
  Status status =
      container->container_->UnregisterNotification(notif_id);
  if (!status.ok()) {
    return status_copy(s, status);
  }
  container->notif_map_.erase(iter);
  delete iter->second;
  return STATUS_OK;
}

