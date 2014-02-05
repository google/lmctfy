#ifndef LMCTFY_C_BINDING_LMCTFY_C_H_
#define LMCTFY_C_BINDING_LMCTFY_C_H_

#include <unistd.h>
#include "lmctfy.pb-c.h"
#include "util/task/codes.pb-c.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

enum {
  CONTAINER_UPDATE_POLICY_DIFF,
  CONTAINER_UPDATE_POLICY_REPLACE
};

struct status {
  int error_code;

  // Null-terminated string allocated on heap.
  // Needs to be free()'ed.
  char *message;
};

struct container;
struct container_api;

// Initializes the machine to start being able to create containers.
//
// Arguments:
//  - s: [output] s will be used as output.
//    If the original value of s->code is not
//    UTIL__ERROR__CODE__INVALID_ARGUMENT, then the function will return
//    immediately with value s->code. This will help users to propagete errors.
//    Otherwise, it contains the error code and message.
//  - spec: The specification.
//
// Returns:
//
//  Returns the error code. 0 on success. When there's an error, the return code
//  is same as s->error_code when s is not NULL.
int lmctfy_init_machine(struct status *s, const Containers__Lmctfy__InitSpec *spec);

// Create a new container_api.
//
// Arguments:
//  - s: [output] s will be used as output.
//    If the original value of s->code is not
//    UTIL__ERROR__CODE__INVALID_ARGUMENT, then the function will return
//    immediately with value s->code. This will help users to propagete errors.
//    Otherwise, it contains the error code and message.
//  - api: [output] The address of a pointer to struct container_api. The
//    pointer of the container api will be stored in this address.
//
// Returns:
//
//  Returns the error code. 0 on success. When there's an error, the return code
//  is same as s->error_code when s is not NULL.
int lmctfy_new_container_api(struct status *s, struct container_api **api);

// Release the container api. 
//
// Arguments:
//
//  - api: The container api. This pointer will be invalid after the call to
//  this function
void lmctfy_delete_container_api(struct container_api *api);

// Get a container
//
// Arguments:
//
//  - s: [output] s will be used as output.
//    If the original value of s->code is not
//    UTIL__ERROR__CODE__INVALID_ARGUMENT, then the function will return
//    immediately with value s->code. This will help users to propagete errors.
//    Otherwise, it contains the error code and message.
//  - container: [output] The address of a pointer to struct container. It will
//    be used to store the pointer to the container.
//  - api: A container api.
//  - container_name: the container name.
//
// Returns:
//
//  Returns the error code. 0 on success. When there's an error, the return code
//  is same as s->error_code when s is not NULL.
int lmctfy_container_api_get_container(
    struct status *s,
    struct container **container,
    const struct container_api *api,
    const char *container_name);

// Create a container
//
// Arguments:
//
//  - s: [output] s will be used as output.
//    If the original value of s->code is not
//    UTIL__ERROR__CODE__INVALID_ARGUMENT, then the function will return
//    immediately with value s->code. This will help users to propagete errors.
//    Otherwise, it contains the error code and message.
//  - container: [output] The address of a pointer to struct container. It will
//    be used to store the newly created container.
//  - api: A container api.
//  - container_name: the container name.
//  - spec: container specification.
//
// Returns:
//
//  Returns the error code. 0 on success. When there's an error, the return code
//  is same as s->error_code when s is not NULL.
int lmctfy_container_api_create_container(
    struct status *s,
    struct container **container,
    struct container_api *api,
    const char *container_name,
    const Containers__Lmctfy__ContainerSpec *spec);

// Destroy a container
//
// Arguments:
//
//  - s: [output] s will be used as output.
//    If the original value of s->code is not
//    UTIL__ERROR__CODE__INVALID_ARGUMENT, then the function will return
//    immediately with value s->code. This will help users to propagete errors.
//    Otherwise, it contains the error code and message.
//  - api: A container api.
//  - container: The pointer to struct container. The pointer will become
//    invalid after a success destroy().
//
// Returns:
//
//  Returns the error code. 0 on success. When there's an error, the return code
//  is same as s->error_code when s is not NULL.
int lmctfy_container_api_destroy_container(struct status *s,
                                           struct container_api *api,
                                           struct container *container);

int lmctfy_container_api_detect_container(struct status *s,
                                          char *container_name,
                                          const size_t n,
                                          struct container_api *api,
                                          pid_t pid);

// Release the memory used by the container structure. The refered container
// will not be affected.
//
// Arguments:
//
//  - container: The container.  This pointer will be invalid after the call to
//    this function
void lmctfy_delete_container(struct container *container);

int lmctfy_container_enter(struct status *s,
                           struct container *container,
                           const pid_t *tids,
                           const int n);

int lmctfy_container_exec(struct status *s,
                          struct container *container,
                          const int argc,
                          const char **argv);

int lmctfy_container_update(struct status *s,
                            struct container *container,
                            int policy,
                            const Containers__Lmctfy__ContainerSpec *spec);

#ifdef __cplusplus
}
#endif // __cplusplus
#endif // LMCTFY_C_BINDING_LMCTFY_C_H_
