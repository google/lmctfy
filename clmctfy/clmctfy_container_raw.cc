#include <unistd.h>
#include <vector>
using ::std::vector;

#include "clmctfy_container_raw.h"

#include "util/task/codes.pb.h"

#include "lmctfy.h"
#include "clmctfy_macros.h"
#include "clmctfy_container_struct.h"
#include "clmctfy_event_callback_wrapper.h"

using ::containers::lmctfy::Container;
using ::containers::lmctfy::ContainerApi;
using ::containers::lmctfy::InitSpec;
using ::containers::lmctfy::ContainerSpec;
using ::containers::lmctfy::RunSpec;
using ::containers::lmctfy::EventSpec;
using ::containers::lmctfy::ContainerStats;
using ::util::Status;
using ::util::StatusOr;
using ::util::internal::status_copy;
using ::util::internal::status_new;

#define STATUS_OK UTIL__ERROR__CODE__OK

int lmctfy_container_run_raw(struct container *container,
                             const int argc,
                             const char **argv,
                             const void *spec,
                             const size_t spec_size,
                             pid_t *tid,
                             struct status *s) {
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

int lmctfy_container_update_raw(struct container *container,
                            int policy,
                            const void *spec,
                            const size_t spec_size,
                            struct status *s) {
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

int lmctfy_container_register_notification_raw(struct container *container,
                                               lmctfy_event_callback_f callback,
                                               void *user_data,
                                               const void *spec,
                                               const size_t spec_size,
                                               notification_id_t *notif_id,
                                               struct status *s) {
  CHECK_NOTFAIL_OR_RETURN(s);
  CHECK_NOTNULL_OR_RETURN(s, container);
  CHECK_NOTNULL_OR_RETURN(s, container->container_);
  CHECK_NOTNULL_OR_RETURN(s, notif_id);
  CHECK_NOTNULL_OR_RETURN(s, callback);
  EventSpec event_spec;
  if (spec != NULL && spec_size > 0) {
    event_spec.ParseFromArray(spec, spec_size);
  }
  EventCallbackWrapper *cb =
      new EventCallbackWrapper(container, callback, user_data);

  // Container object does not take the ownership of the callback.
  StatusOr<Container::NotificationId> statusor_id =
      container->container_->RegisterNotification(event_spec, cb);

  if (!statusor_id.ok()) {
    delete cb;
    return status_copy(s, statusor_id.status());
  }
  notification_id_t nid = statusor_id.ValueOrDie();
  container->notif_map_[nid] = cb;
  *notif_id = nid;
  return STATUS_OK;
}


