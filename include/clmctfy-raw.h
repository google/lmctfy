#ifndef LMCTFY_C_BINDING_CLMCTFY_RAW_H_
#define LMCTFY_C_BINDING_CLMCTFY_RAW_H_

#include "lmctfy.pb-c.h"
#include "clmctfy.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// Initializes the machine to start being able to create containers.
//
// Arguments:
//  - s: [output] s will be used as output. It contains the error code/message.
//  - spec: Serialized data (protobuf format) containing the specification.
//  - spec_size: Size of the serialized data.
//
// Returns:
//  Returns the error code. 0 on success. The return code is same as
//  status_get_code(s).
int lmctfy_init_machine_raw(struct status *s, const void *spec, const size_t spec_size);

int lmctfy_container_api_create_container_raw(
    struct status *s,
    struct container **container,
    struct container_api *api,
    const char *container_name,
    const void *spec,
    const size_t spec_size);

int lmctfy_container_run_raw(struct status *s,
                             pid_t *tid,
                             struct container *container,
                             const int argc,
                             const char **argv,
                             const void *spec,
                             const size_t spec_size);

int lmctfy_container_update_raw(struct status *s,
                                struct container *container,
                                int policy,
                                const void *spec,
                                const size_t spec_size);

int lmctfy_container_register_notification_raw(struct status *s,
                                               notification_id_t *notif_id,
                                               struct container *container,
                                               lmctfy_event_callback_f callback,
                                               void *user_data,
                                               const void *spec,
                                               const size_t spec_size);

#ifdef __cplusplus
}
#endif // __cplusplus
#endif // LMCTFY_C_BINDING_CLMCTFY_RAW_H_
