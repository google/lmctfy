#ifndef LMCTFY_INCLUDE_CLMCTFY_CONTAINER_API_H_
#define LMCTFY_INCLUDE_CLMCTFY_CONTAINER_API_H_

#include <unistd.h>

#include "lmctfy.pb-c.h"
#include "clmctfy_status.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

struct container;
struct container_api;

// Initializes the machine to start being able to create containers.
//
// Arguments:
//  - spec: The specification. Caller owns the pointer.
//  - s: [output] The status of the operations and an error message if the
//    status is not OK.
//
// Returns:
//
//  Returns the error code. 0 on success. When there's an error, the return code
//  is same as s->error_code when s is not NULL.
int lmctfy_init_machine(const Containers__Lmctfy__InitSpec *spec, struct status *s);

// Create a new container_api.
//
// Arguments:
//  - api: [output] The address of a pointer to struct container_api. The
//    pointer of the container api will be stored in this address.
//  - s: [output] The status of the operations and an error message if the
//    status is not OK.
//
// Returns:
//
//  Returns the error code. 0 on success. When there's an error, the return code
//  is same as s->error_code when s is not NULL.
int lmctfy_new_container_api(struct container_api **api, struct status *s);

// Release the container api. 
//
// Arguments:
//
//  - api: The container api. The function takes the ownershp.
void lmctfy_delete_container_api(struct container_api *api);

// Get a container
//
// Arguments:
//
//  - api: A container api.
//  - container_name: the container name.
//  - s: [output] The status of the operations and an error message if the
//    status is not OK.
//  - container: [output] The address of a pointer to struct container. It will
//    be used to store the pointer to the container. The caller takes the
//    ownership.
//
// Returns:
//
//  Returns the error code. 0 on success. When there's an error, the return code
//  is same as s->error_code when s is not NULL.
int lmctfy_container_api_get_container(
    const struct container_api *api,
    const char *container_name,
    struct container **container,
    struct status *s);

// Create a container
//
// Arguments:
//
//  - api: A container api.
//  - container_name: the container name.
//  - spec: container specification. Caller owns the pointer.
//  - container: [output] The address of a pointer to struct container. It will
//    be used to store the newly created container.
//  - s: [output] The status of the operations and an error message if the
//    status is not OK.
//
// Returns:
//
//  Returns the error code. 0 on success. When there's an error, the return code
//  is same as s->error_code when s is not NULL.
int lmctfy_container_api_create_container(
    struct container_api *api,
    const char *container_name,
    const Containers__Lmctfy__ContainerSpec *spec,
    struct container **container,
    struct status *s);

// Destroy a container. The caller has to call lmctfy_delete_container after
// detroying a container. Otherwise, the memory occupied by the container
// structure will not be released.
//
// Arguments:
//
//  - api: A container api.
//  - container: The pointer to struct container. The pointer will become
//    invalid after a successful destroy().
//  - s: [output] The status of the operations and an error message if the
//    status is not OK.
//
// Returns:
//
//  Returns the error code. 0 on success. When there's an error, the return code
//  is same as s->error_code when s is not NULL.
int lmctfy_container_api_destroy_container(struct container_api *api,
                                           struct container *container,
                                           struct status *s);

// Detect what container the specified thread is in.
//
// Arguments:
//  - api: The container api.
//  - pid: The thread ID to check. 0 refers to self.
//  - container_name: [output] Will be used to store the container name.
//    It's the caller's responsibility to free() *container_name.
//  - s: [output] The status of the operations and an error message if the
//    status is not OK.
//
// Returns:
//
//  Returns the error code. 0 on success. When there's an error, the return code
//  is same as s->error_code when s is not NULL.
int lmctfy_container_api_detect_container(struct container_api *api,
                                          pid_t pid,
                                          char **container_name,
                                          struct status *s);


#ifdef __cplusplus
}
#endif // __cplusplus
#endif // LMCTFY_INCLUDE_CLMCTFY_CONTAINER_API_H_
