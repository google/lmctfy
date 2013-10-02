// Copyright 2013 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef INCLUDE_LMCTFY_H_
#define INCLUDE_LMCTFY_H_

#include <time.h>
#include <string>
using ::std::string;
#include <vector>

#include "base/callback.h"
#include "base/macros.h"
#include "include/lmctfy.pb.h"
#include "strings/stringpiece.h"
#include "util/task/statusor.h"
#include "util/task/status.h"

namespace containers {
namespace lmctfy {

class Container;

// lmctfy: Base Containers Library.
//
// Facilitates the creation, management, monitoring, and interaction with
// containers.
//
// NOTE: This interface is under heavy development! Contact OWNERS before
// using.
//
// The container specifications can be found in:
//   //include/lmctfy.proto
//
// Container Naming Format:
//
// Container names mimic filesystem paths closely since they express a hierarchy
// of containers (i.e.: containers can be inside other containers, these are
// called subcontainers or children containers).
//
// Allowable characters for container names are:
// - Alpha numeric ([a-zA-Z]+, [0-9]+)
// - Underscores (_)
// - Dashes (-)
// - Periods (.)
//
// An absolute path is one that is defined from the root (/) container
// (i.e.: /sys/subcont). Container names can also be relative (i.e.: subcont).
// In general and unless otherwise specified, regular filesystem path rules
// apply.
//
// Examples:
//   /           : Root container
//   /sys        : the "sys" top level container
//   /sys/sub    : the "sub" container inside the "sys" top level container
//   .           : the current container
//   ./          : the current container
//   ..          : the parent of the current container
//   sub         : the "sub" subcontainer (child container) of the current
//                 container
//   ./sub       : the "sub" subcontainer (child container) of the current
//                 container
//   /sub        : the "sub" top level container
//   ../sibling  : the "sibling" child container of the parent container
//
// Containers exist on the whole machine and thus can be accessed from multiple
// processes and multiple threads in each process. At any given time, there may
// be Container objects for container /foo in different processes, and in
// different threads of the same process. All of these instances are
// synchronized such that mutable container operations are atomic.
//
// Note that since there are multiple Container objects in multiple processes, a
// container may be "deleted under you." Once a container is deleted, all of the
// operations on all Container objects that reference it will fail with
// NOT_FOUND. Calls to Info() will return not found as well.
//
// Tourist Threads:
// There may exist threads that are inside a container, but their thread-group
// leader (the thread whose TID is the same as the PID) is not inside the
// container. These threads shall be referred to as "tourist threads." This use
// is in general discouraged as it can lead to hard to manage edge cases. Some
// of these edge cases are described in the documentation below.
//
// Class is thread-safe.
class ContainerApi {
 public:
  // TODO(vmarmol): Move this to the "PowerUser API"
  // Initializes the machine to start being able to create containers. All
  // creations of ContainerApi objects will fail before this initialization is
  // complete. This should be called once during machine boot.
  //
  // Regular users do NOT need to call this.
  static ::util::Status InitMachine(const InitSpec &spec);

  // Returns a new instance of ContainerApi iff the status is OK. The returned
  // instance is thread-safe.
  static ::util::StatusOr<ContainerApi *> New();

  virtual ~ContainerApi() {}

  // Attach to an existing container. Get an object through which we can
  // interact with that container. If the container does not exist, an error is
  // returned.
  //
  // Multiple Get() operations on the same container (or a Create() and a Get())
  // return different Container object instances pointing to the same underlying
  // container. Any of these instances can be used to interact with the
  // container and certain interactions are synchronized (those that specify
  // it).
  //
  // Arguments:
  //   container_name: The name of the existing container. The container name
  //       format is outlined near the top of this file.
  // Return:
  //   StatusOr: OK iff the operation was successful. On success we populate an
  //       object for container interactions.
  virtual ::util::StatusOr<Container *> Get(
      StringPiece container_name) const = 0;

  // Create a new container from the provided specification. Get an object
  // through which we can interact with that container. If the container name
  // already exists, an error is returned.
  //
  // Arguments:
  //   container_name: The desired name for the new container. The container
  //       name format is outlined near the top of this file.
  //   spec: Container specification. Only resources that are specified will be
  //       included in the container. All those resources not specified will
  //       share their parent's limits.
  // Return:
  //   StatusOr: OK iff the operation was successful. On success we populate an
  //       object for container interactions.
  virtual ::util::StatusOr<Container *> Create(
      StringPiece container_name,
      const ContainerSpec &spec) const = 0;

  // Destroys the container and all subcontainers (recursive). Also kills any
  // processes inside the containers being destroyed.
  //
  // Arguments:
  //   container: The container to destroy. Takes ownership (and deletes) the
  //       pointer on success.
  // Return:
  //   Status: OK iff the container was destroyed (and deleted). Otherwise, the
  //       container is not destroyed (or deleted) and the caller retains
  //       ownership.
  virtual ::util::Status Destroy(Container *container) const = 0;

  // Detect what container the specified thread is in.
  //
  // Arguments:
  //   tid: The thread ID to check. 0 refers to self.
  // Return:
  //   StatusOr: OK iff the container exists. On success we populate the name of
  //       the container in which the thread lives. The name is a full and
  //       absolute name as described by the container name format near the top
  //       of this file.
  virtual ::util::StatusOr<string> Detect(pid_t tid) const = 0;
  inline ::util::StatusOr<string> Detect() const { return Detect(0); }

 protected:
  ContainerApi() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(ContainerApi);
};

// A lmctfy Container.
//
// Allows direct interactions with the container and its properties. Containers
// are created and destroyed by the lmctfy library above.
//
// Class is thread-safe.
class Container {
 public:
  // Destructor does not destroy the underlying container. For that, use
  // ContainerApi's Destroy().
  virtual ~Container() {}

  // TODO(vmarmol): Change to "enum class" when that is supported. Here and
  // elsewhere.
  enum UpdatePolicy {
    // Update only the specified fields.
    UPDATE_DIFF,

    // Replace the existing container with the new specification.
    UPDATE_REPLACE
  };

  // Updates the container according to the specification.
  //
  // Arguments:
  //   spec: The specification of the desired updates.
  //   policy: If UPDATE_REPLACE updates the container to EXACTLY match the
  //       specification; if the isolated resources change, the respective
  //       resources are added or removed as specified. If UPDATE_DIFF, only
  //       makes the specified changes. i.e.: if only memory limit is specified,
  //       only that is updated.
  // Return:
  //   Status: Status of the operation. OK iff successful.
  virtual ::util::Status Update(const ContainerSpec &spec,
                                UpdatePolicy policy) = 0;

  // Moves the specified threads into this container. Enter is atomic.
  //
  // If Enter fails, the system may be left in an inconsistent state as the TIDs
  // may have been partially moved into the container.
  //
  // Arguments:
  //   tids: The Thread IDs to move into the container.
  //   tid: The Thread ID to move into the container.
  // Return:
  //   Status: Status of the operation. OK iff successful.
  virtual ::util::Status Enter(const ::std::vector<pid_t> &tids) = 0;
  inline ::util::Status Enter(pid_t tid) {
    return Enter(::std::vector<pid_t>(1, tid));
  }

  enum FdPolicy {
    // Run the command with all file descriptors inherited from the parent
    // process.
    FDS_INHERITED,

    // Run the command with all file descriptors detached from the parent
    // process (redirected to /dev/null).
    FDS_DETACHED,
  };

  // Run the specified command inside the container. Multiple instances of run
  // can be active simultaneously. Processes MUST be reaped by the caller.
  //
  // Arguments:
  //   command: The shell command to execute. It is executed using "/bin/sh -c".
  //   policy: If FDS_INHERITED, the command will run with all file descriptors
  //   shared with the parent process.  If FDS_DETACHED, the command will
  //   run with all file descriptors closed and STDIN/STDOUT redirected to
  //   /dev/null.
  // Return:
  //   StatusOr: Status of the operation. OK iff successful. On success, the
  //       PID of the command is returned.
  virtual ::util::StatusOr<pid_t> Run(StringPiece command, FdPolicy policy) = 0;

  // Returns basic information about the container with the specified name (if
  // it exists). The basic information returned is specified in the
  // ContainerInfo proto.
  //
  // Arguments:
  //   StatusOr: Status of the operation. On success we populate the container
  //       information.
  // Return:
  //   ContainerInfo: The populated container information.
  virtual ::util::StatusOr<ContainerInfo> Info() const = 0;

  // Policies on listing output
  enum ListPolicy {
    // Only output the information of this container.
    LIST_SELF,

    // Output the information of this container and all of its subcontainers and
    // their subcontainers.
    LIST_RECURSIVE
  };

  // Get all subcontainers in this container.
  //
  // Recursive operation is not atomic so results may be stale or inconsistent
  // depending on other container operations in the system. i.e.: if someone
  // creates a subcontainer after all subcontainers were examined.
  //
  // Arguments:
  //   policy: If LIST_SELF, only list this container's subcontainers. If
  //       LIST_RECURSIVE, recursively lists all subcontainers of the
  //       subcontainers as well.
  // Return:
  //   StatusOr: Status of the operation. OK iff successful. On success
  //       populates a list of subcontainers sorted by container names.
  virtual ::util::StatusOr< ::std::vector<Container *>> ListSubcontainers(
      ListPolicy policy) const = 0;

  // Get all TIDs in this container.
  //
  // Recursive operation is not atomic so results may be stale or inconsistent
  // depending on other container operations in the system. i.e.: if someone
  // adds a process after the container was examined.
  //
  // Arguments:
  //   recursive: Whether to recursively list the TIDs of the subcontainer's
  //       subcontainers.
  // Return:
  //   StatusOr: Status of the operation. OK iff successful. On success
  //       populates a list of TIDs.
  virtual ::util::StatusOr< ::std::vector<pid_t>> ListThreads(
      ListPolicy policy) const = 0;

  // Get all PIDs in this container.
  //
  // Recursive operation is not atomic so results may be stale or inconsistent
  // depending on other container operations in the system. i.e.: if someone
  // adds a process after the container was examined.
  //
  // Arguments:
  //   recursive: Whether to recursively list the PIDs of the subcontainer's
  //       subcontainers.
  // Return:
  //   StatusOr: Status of the operation. OK iff successful. On success
  //       populates a list of PIDs.
  virtual ::util::StatusOr< ::std::vector<pid_t>> ListProcesses(
      ListPolicy policy) const = 0;

  // Atomically stops the execution of all threads inside the container and all
  // subcontainers (recursively). All threads moved to a paused container will
  // be paused as well (regardless of whether the PID is in the container). This
  // guarantees to get all threads.
  //
  // Return:
  //   Status: Status of the operation. OK iff successful.
  virtual ::util::Status Pause() = 0;

  // Atomically resumes the execution of all threads inside the container and
  // all subcontainers (recursively). All paused threads moved to a non-paused
  // container will be resumed.
  //
  // Return:
  //   Status: Status of the operation. OK iff successful.
  virtual ::util::Status Resume() = 0;

  // Type of stats to output
  enum StatsType {
    // A summary of the statistics (see each resource's definition of summary).
    STATS_SUMMARY,

    // All available statistics.
    STATS_FULL
  };

  // Gets usage and state information for the container. Note that the snapshot
  // is not atomic.
  //
  // Arguments:
  //   type: The type of statistics to output.
  //   output: A snapshot of the container's statistics.
  // Return:
  //   StatusOr: Status of the operation. On success we populate the container
  //       statistics.
  virtual ::util::StatusOr<ContainerStats> Stats(StatsType type) const = 0;

  // Unique IDs for registered notifications.
  typedef uint64 NotificationId;

  // Callback used on an event notification.
  //   container: The container that received the notification. It is an error
  //       to delete it.
  //   status: The status of the notification. If OK, then the event registered
  //       occured. Otherwise, an error is reported in the status. Errors may
  //       be caused by container deletion or unexpected registration errors.
  typedef Callback2<Container *, ::util::Status> EventCallback;

  // Register a notification for a specified container event. All notifications
  // are unregistered when the container is destroyed.
  //
  // Arguments:
  //   spec: The specification for the event for which to register
  //       notifications.
  //   callback: The callback to run when the event is triggered. The caller
  //       takes ownership of the callback which MUST be a repeatable callback.
  // Return:
  //   StatusOr: Status of the operation. OK iff successful and a unique ID for
  //       the notification is provided. The ID is unique within the current
  //       ContainerApi instance.
  virtual ::util::StatusOr<NotificationId> RegisterNotification(
      const EventSpec &spec, EventCallback *callback) = 0;

  // Unregister (stop) the specified notification from being received.
  //
  // Arguments:
  //   notification_id: The unique notification ID for the container
  //       notification.
  // Return:
  //   StatusOr: Status of the operation. OK iff successful.
  virtual ::util::Status UnregisterNotification(NotificationId event_id) = 0;

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
  // Return:
  //   Status: Status of the operation. OK iff successful.
  virtual ::util::Status KillAll() = 0;

  // Gets the name of the container.
  //
  // Return:
  //   string: The resolved absolute name of this container as outlined in the
  //       container name format near the top of this file.
  const string &name() const { return name_; }

 protected:
  explicit Container(const string &container_name) : name_(container_name) {}

  // Destroy the Container. This is for internal use only. Users should use
  // ContainerApi::Destroy() instead.
  //
  // Return:
  //   Status: Status of the operation. OK iff successful.
  virtual ::util::Status Destroy() = 0;

  // The name of the container
  string name_;

 private:
  friend class ContainerApiImpl;

  DISALLOW_COPY_AND_ASSIGN(Container);
};

}  // namespace lmctfy
}  // namespace containers

#endif  // INCLUDE_LMCTFY_H_
