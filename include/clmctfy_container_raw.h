#ifndef LMCTFY_INCLUDE_CLMCTFY_CONTAINER_RAW_H_
#define LMCTFY_INCLUDE_CLMCTFY_CONTAINER_RAW_H_

#include <unistd.h>
#include "clmctfy_status.h"
#include "clmctfy_container.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

struct container_api;
struct container;

// Run the specified command inside the container. Multiple instances of run
// can be active simultaneously. Processes MUST be reaped by the caller.
//
// Arguments:
//
//  - container: The container.
//  - argc: number of arguments (including the binary file path).
//  - argv: All arguments. The first element is the binary that will be executed
//    and must be an absolute path.
//  - spec: Serialized data (protobuf format) containing the specification.
//    Caller takes the ownership.
//  - spec_size: Size of the serialized data.
//  - tid: [output] On success, tid stores the PID of the command.
//  - s: [output] The status of the operations and an error message if the
//    status is not OK.
//
// Returns:
//
//  Returns the error code. 0 on success. When there's an error, the return code
//  is same as s->error_code when s is not NULL.
int lmctfy_container_run_raw(struct container *container,
                             const int argc,
                             const char **argv,
                             const void *spec,
                             const size_t spec_size,
                             pid_t *tid,
                             struct status *s);

// Updates the container according to the specification. The set of resource
// types being isolated cannot change during an Update. This means that a
// CONTAINER_UPDATE_POLICY_REPLACE must specify all the resources being isolated
// and a CONTAINER_UPDATE_POLICY_DIFF cannot specify any resource that is not
// already being isolated.
//
// Arguments:
//
//  - container
//  - policy: Update policy. Can be either CONTAINER_UPDATE_POLICY_DIFF, or
//    CONTAINER_UPDATE_POLICY_REPLACE.
//  - spec: Serialized data (protobuf format) containing the specification.
//    Caller takes the ownership.
//  - spec_size: Size of the serialized data.
//  - s: [output] The status of the operations and an error message if the
//    status is not OK.
//
// Returns:
//
//  Returns the error code. 0 on success. When there's an error, the return code
//  is same as s->error_code when s is not NULL.
int lmctfy_container_update_raw(struct container *container,
                                int policy,
                                const void *spec,
                                const size_t spec_size,
                                struct status *s);

// Register a notification for a specified container event. All notifications
// are unregistered when the container is destroyed.
//
// Arguments:
//  - container: The container.
//  - callback: The callback to run when the event is triggered. The caller
//    takes ownership of the callback which MUST be a repeatable callback.
//  - user_data: The pointer which will be passed to the callback function as
//    its last parameter. The caller takes the ownership.
//  - spec: Serialized data (protobuf format) containing the specification.
//    Caller takes the ownership.
//  - spec_size: Size of the serialized data.
//  - notif_id: [output] The ID for the notification. The ID is unique within
//    the current container_api instance. It will be used to unregister the
//    notification
//  - s: [output] The status of the operations and an error message if the
//    status is not OK.
//
// Returns:
//
//  Returns the error code. 0 on success. When there's an error, the return code
//  is same as s->error_code when s is not NULL.
int lmctfy_container_register_notification_raw(struct container *container,
                                               lmctfy_event_callback_f callback,
                                               void *user_data,
                                               const void *spec,
                                               const size_t spec_size,
                                               notification_id_t *notif_id,
                                               struct status *s);


#ifdef __cplusplus
}
#endif // __cplusplus

#endif // LMCTFY_INCLUDE_CLMCTFY_CONTAINER_RAW_H_
