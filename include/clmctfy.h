#ifndef LMCTFY_INCLUDE_CLMCTFY_H_
#define LMCTFY_INCLUDE_CLMCTFY_H_

#include <unistd.h>
#include <stdint.h>

#include "util/task/codes.pb-c.h"
#include "lmctfy.pb-c.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

enum {
  // Update only the specified fields.
  CONTAINER_UPDATE_POLICY_DIFF,

  // Replace the existing container with the new specification.
  CONTAINER_UPDATE_POLICY_REPLACE
};

enum {
  // Only output the information of this container.
  CONTAINER_LIST_POLICY_SELF,

  // Output the information of this container and all of its subcontainers and
  // their subcontainers.
  CONTAINER_LIST_POLICY_RECURSIVE
};

enum {
  // A summary of the statistics (see each resource's definition of summary).
  CONTAINER_STATS_TYPE_SUMMARY,

  // All available statistics.
  CONTAINER_STATS_TYPE_FULL
};

struct status {
  int error_code;

  // Null-terminated string allocated on heap.
  // Needs to be free()'ed.
  char *message;
};


struct container;
struct container_api;

// Callback used on an event notification.
//
// The container and status structure pointers are only valid within the
// callback.
//
// - container: The container that received the notification. It is an error
//    to delete it.
// - status: The status of the notification. If OK, then the event registered
//    occured. Otherwise, an error is reported in the status. Errors may
//    be caused by container deletion or unexpected registration errors.
//    it will be an error if the user call free(s->message);
// - user_data: 
typedef void (*lmctfy_event_callback_f)(struct container *container,
                                        const struct status *status,
                                        void *user_data);

typedef uint64_t notification_id_t;

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
int lmctfy_container_api_detect_container(struct container_api *api,
                                          pid_t pid,
                                          char **container_name,
                                          struct status *s);

// Release the memory used by the container structure. The refered container
// will not be affected.
//
// Arguments:
//
//  - container: The container. This pointer will be invalid after the call to
//    this function
void lmctfy_delete_container(struct container *container);

// Moves the specified threads into this container. Enter is atomic.
//
// Arguments:
//
//  - container: The container.
//  - tids: Array of thread IDs to move into the container. Caller takes the
//    ownership.
//  - tids_size: Number of thread IDs stored in tids. Caller takes the
//    ownership.
//  - s: [output] The status of the operations and an error message if the
//    status is not OK.
int lmctfy_container_enter(struct container *container,
                           const pid_t *tids,
                           const int tids_size,
                           struct status *s);

// Run the specified command inside the container. Multiple instances of run
// can be active simultaneously. Processes MUST be reaped by the caller.
//
// Arguments:
//
//  - container: The container.
//  - argc: number of arguments (including the binary file path).
//  - argv: All arguments. The first element is the binary that will be executed
//    and must be an absolute path.
//  - spec: The specification of the runtime environment to use for the
//    execution of the command.
//  - tid: [output] On success, tid stores the PID of the command.
//  - s: [output] The status of the operations and an error message if the
//    status is not OK.
int lmctfy_container_run(struct container *container,
                         const int argc,
                         const char **argv,
                         const Containers__Lmctfy__RunSpec *spec,
                         pid_t *tid,
                         struct status *s);

// Execute the specified command inside the container.  This replaces the
// current process image with the specified command.  The PATH environment
// variable is used, and the existing environment is passed to the new
// process image unchanged.
//
// Arguments:
//
//  - container
//  - argc: number of arguments (including the binary file path).
//  - argv: All arguments. The first element is the binary that will be executed
//    and must be an absolute path.
//  - s: [output] The status of the operations and an error message if the
//    status is not OK.
int lmctfy_container_exec(struct container *container,
                          const int argc,
                          const char **argv,
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
//  - spec: The specification of the desired updates.
//  - s: [output] The status of the operations and an error message if the
//    status is not OK.
int lmctfy_container_update(struct container *container,
                            int policy,
                            const Containers__Lmctfy__ContainerSpec *spec,
                            struct status *s);

// Returns the resource isolation specification (ContainerSpec) of this
// container.
//
// Arguments:
//
//  - container: The container
//  - spec: [output] An address of a ContainerSpec pointer. *spec will points to
//    the ContainerSpec of the container. Caller takes the ownership. *spec can
//    be released by calling free().
//  - s: [output] The status of the operations and an error message if the
//    status is not OK.
int lmctfy_container_spec(struct container *container,
                          Containers__Lmctfy__ContainerSpec **spec,
                          struct status *s);

// List all subcontainers.
//
// Arguments:
//
//  - container: The container
//  - list_policy: CONTAINER_LIST_POLICY_SELF or CONTAINER_LIST_POLICY_RECURSIVE
//  - subcontainers: [output] The address of a pointer points to an array of
//    struct container pointers. The caller takes the onwership. On success, the
//    pointer will be assigned an address to an array of containers. The pointed 
//    array should be released with free(). The containers inside the array
//    should be deleted/destroyed individually.
//  - subcontainers_size: [output] The address of an integer used to store number
//    of subcontainers in the array.
//  - s: [output] The status of the operations and an error message if the
//    status is not OK.
int lmctfy_container_list_subcontainers(struct container *container,
                                        int list_policy,
                                        struct container **subcontainers[],
                                        int *subcontainers_size, 
                                        struct status *s);

// List all TIDs in the container.
//
// Arguments:
//  - container
//  - list_policy: List policy. 
//  - threads: [output] The address of a pointer points to an array of pid_t.
//    The caller takes the ownership. The array can be released with free().
//  - threads_size: [output] *threads_size is the number of TIDs stored in *threads.
//  - s: [output] The status of the operations and an error message if the
//    status is not OK.
int lmctfy_container_list_threads(struct container *container,
                                  int list_policy,
                                  pid_t *threads[],
                                  int *threads_size,
                                  struct status *s);

// Get all PIDs in this container.
//
// Arguments:
//  - container
//  - list_policy: List policy. 
//  - processes: [output] The address of a pointer points to an array of pid_t.
//    The caller takes the ownership. The array can be released with free().
//  - processes_size: [output] *processes_size is the number of PIDs stored in
//    *processes.
//  - s: [output] The status of the operations and an error message if the
//    status is not OK.
int lmctfy_container_list_processes(struct container *container,
                                    int list_policy,
                                    pid_t *processes[],
                                    int *processes_size,
                                    struct status *s);

// Atomically stops the execution of all threads inside the container and all
// subcontainers (recursively). All threads moved to a paused container will
// be paused as well (regardless of whether the PID is in the container). This
// guarantees to get all threads.
//
// Arguments:
//  - container: The container.
//  - s: [output] The status of the operations and an error message if the
//    status is not OK.
int lmctfy_container_pause(struct container *container,
                           struct status *s);

// Atomically resumes the execution of all threads inside the container and
// all subcontainers (recursively). All paused threads moved to a non-paused
// container will be resumed.
//
// Arguments:
//  - container: The container.
//  - s: [output] The status of the operations and an error message if the
//    status is not OK.
int lmctfy_container_resume(struct container *container,
                            struct status *s);

// Kills all processes running in the container. This operation is atomic and
// is synchronized with any mutable operations on this container.
//
// The operation sends a SIGKILL to all processes in the containers. Tourist
// threads are killed via SIGKILL after all processes have exited.
//
// Note that this operation can potentially take a long time (O(seconds)) if
// the processes in the container do not finish quickly. This operation also
// blocks all mutable container operations while it is in progress.
//
// Arguments:
//  - container: The container.
//  - s: [output] The status of the operations and an error message if the
//    status is not OK.
int lmctfy_container_killall(struct container *container,
                             struct status *s);

// Gets usage and state information for the container. Note that the snapshot
// is not atomic.
//
// Arguments:
//  - container: The container.
//  - stats_type: The type of statistics to output.
//  - stats: [output] Used to store a pointer points to the container's
//    statistics. The call takes the ownership of *stats, which should be
//    free()'ed.
//  - s: [output] The status of the operations and an error message if the
//    status is not OK.
int lmctfy_container_stats(struct container *container,
                          int stats_type,
                          Containers__Lmctfy__ContainerStats **stats,
                          struct status *s);

// Get the name of the container.
//
// Arguments:
//  - s: [output] The status of the operations and an error message if the
//    status is not OK.
//
// Return:
//  The container name. The caller does not take the ownership.
const char *lmctfy_container_name(struct container *container);

// Register a notification for a specified container event. All notifications
// are unregistered when the container is destroyed.
//
// Arguments:
//  - container: The container.
//  - callback: The callback to run when the event is triggered. The caller
//    takes ownership of the callback which MUST be a repeatable callback.
//  - user_data: The pointer which will be passed to the callback function as
//    its last parameter. The caller takes the ownership.
//  - spec: The specification for the event for which to register notifications.
//    The caller takes the ownership.
//  - notif_id: [output] The ID for the notification. The ID is unique within
//    the current container_api instance. It will be used to unregister the
//    notification
//  - s: [output] The status of the operations and an error message if the
//    status is not OK.
int lmctfy_container_register_notification(struct container *container,
                                           lmctfy_event_callback_f callback,
                                           void *user_data,
                                           Containers__Lmctfy__EventSpec *spec,
                                           notification_id_t *notif_id,
                                           struct status *s);

// Unregister (stop) the specified notification from being received.
//
// Arguments:
//  - container: The container.
//  - notif_id: The unique notification ID for the container
//    notification.
//  - s: [output] The status of the operations and an error message if the
//    status is not OK.
int lmctfy_container_unregister_notification(struct container *container,
                                             const notification_id_t notif_id,
                                             struct status *s);

#ifdef __cplusplus
}
#endif // __cplusplus
#endif // LMCTFY_INCLUDE_CLMCTFY_H_
