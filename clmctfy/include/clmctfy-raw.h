#ifndef LMCTFY_C_BINDING_CLMCTFY_RAW_H_
#define LMCTFY_C_BINDING_CLMCTFY_RAW_H_

#include "clmctfy.h"
#include "lmctfy.pb-c.h"

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
int lmctfy_init_machine_raw(struct status *s, const void *spec, const int spec_size);

int lmctfy_container_api_create_container_raw(
    struct status *s,
    struct container **container,
    struct container_api *api,
    const char *container_name,
    const void *spec,
    const int spec_size);

#ifdef __cplusplus
}
#endif // __cplusplus
#endif // LMCTFY_C_BINDING_CLMCTFY_RAW_H_
