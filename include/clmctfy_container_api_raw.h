#ifndef LMCTFY_INCLUDE_CLMCTFY_CONTAINER_API_RAW_H_
#define LMCTFY_INCLUDE_CLMCTFY_CONTAINER_API_RAW_H_

#include <stdlib.h>
#include "clmctfy_status.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

struct container;
struct container_api;

// Initializes the machine to start being able to create containers.
//
// Arguments:
//  - spec: Serialized data (protobuf format) containing the specification.
//    Caller takes the ownership.
//  - spec_size: Size of the serialized data.
//  - s: [output] s will be used as output. It contains the error code/message.
//
// Returns:
//  Returns the error code. 0 on success. The return code is same as
//  status_get_code(s).
int lmctfy_init_machine_raw(const void *spec, const size_t spec_size, struct status *s);

// Create a container
//
// Arguments:
//
//  - api: A container api.
//  - container_name: the container name.
//  - spec: Serialized data (protobuf format) containing the specification.
//    Caller takes the ownership.
//  - spec_size: Size of the serialized data.
//  - container: [output] The address of a pointer to struct container. It will
//    be used to store the newly created container.
//  - s: [output] The status of the operations and an error message if the
//    status is not OK.
//
// Returns:
//
//  Returns the error code. 0 on success. When there's an error, the return code
//  is same as s->error_code when s is not NULL.
int lmctfy_container_api_create_container_raw(
    struct container_api *api,
    const char *container_name,
    const void *spec,
    const size_t spec_size,
    struct container **container,
    struct status *s);


#ifdef __cplusplus
}
#endif // __cplusplus

#endif // LMCTFY_INCLUDE_CLMCTFY_CONTAINER_API_RAW_H_
