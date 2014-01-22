#ifndef LMCTFY_C_BINDING_LMCTFY_C_H_
#define LMCTFY_C_BINDING_LMCTFY_C_H_
#include "lmctfy.pb-c.h"
#include "status-c.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

struct container;
struct container_api;

// InitMachine
//
// Arguments:
//  - s: s will be used as output. It contains the error code/message.
//  - spec: Serialized data (protobuf format) containing the specification.
//  - spec_size: Size of the serialized data.
//
// Returns:
//  Returns the error code. 0 on success. The return code is same as
//  status_get_code(s).
int lmctfy_init_machine_raw(struct status *s, const void *spec, const int spec_size);

// InitMachine
//
// Arguments:
//  - s: s will be used as output. It contains the error code/message.
//  - spec: The specification.
//
// Returns:
//  Returns the error code. 0 on success. The return code is same as
//  status_get_code(s).
int lmctfy_init_machine(struct status *s, const Containers__Lmctfy__InitSpec *spec);

int lmctfy_new_container_api(struct status *s, struct container_api **api);
void lmctfy_release_container_api(struct container_api *api);

int lmctfy_container_api_get_container(
    struct status *s,
    const struct container_api *api,
    struct container **container,
    const char *container_name);

#ifdef __cplusplus
}
#endif // __cplusplus
#endif // LMCTFY_C_BINDING_LMCTFY_C_H_
