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

#ifndef SRC_LMCTFY_IMPL_H_
#define SRC_LMCTFY_IMPL_H_

#include <sys/types.h>
#include <map>
#include <memory>
#include <set>
#include <string>
using ::std::string;
#include <vector>

#include "base/callback.h"
#include "base/macros.h"
#include "base/thread_annotations.h"
#include "system_api/kernel_api.h"
#include "lmctfy/resource_handler.h"
#include "include/lmctfy.h"
#include "strings/stringpiece.h"
#include <memory>
using ::std::shared_ptr;
#include "util/task/statusor.h"

class SubProcess;

namespace containers {
namespace lmctfy {

class ActiveNotifications;
class CgroupFactory;
class ContainerInfo;
class ContainerSpec;
class ContainerStats;
class EventFdNotifications;
class InitSpec;
class LockHandler;
class LockHandlerFactory;
class TasksHandler;
class TasksHandlerFactory;

typedef ::system_api::KernelAPI KernelApi;
typedef ::std::map<ResourceType, ResourceHandlerFactory *> ResourceFactoryMap;
typedef ResultCallback<SubProcess *> SubProcessFactory;


// Implementation of util::containers::lmctfy::ContainerApi. All methods assume
// that the machine has already initialized (with the exception of
// InitMachine()). InitMachine() must be called after machine boot before any
// containers are created. Doing otherwise will likely fail as the resources are
// not initialized.
class ContainerApiImpl : public ContainerApi {
 public:
  // Exposed for use in testing.
  static ::util::StatusOr<ContainerApiImpl *> NewContainerApiImpl(
      CgroupFactory *cgroup_factory, const KernelApi *kernel);
  static ::util::Status InitMachineImpl(const KernelApi *kernel,
                                        const InitSpec &spec);

  // ContainerApiImpl takes ownership of all pointers except kernel.
  // ResourceHandlerFactories generate resource-specific ResourceHandlers.
  ContainerApiImpl(
      const LockHandlerFactory *lock_handler_factory,
      TasksHandlerFactory *tasks_handler_factory,
      const CgroupFactory *cgroup_factory,
      const ::std::vector<ResourceHandlerFactory *> &resource_factories,
      const KernelApi *kernel, ActiveNotifications *active_notifications,
      EventFdNotifications *eventfd_notifications);
  virtual ~ContainerApiImpl();

  // These methods are documented in //include/lmctfy.h
  virtual ::util::StatusOr<Container *> Get(StringPiece container_name) const;
  virtual ::util::StatusOr<Container *> Create(StringPiece container_name,
                                               const ContainerSpec &spec) const;
  virtual ::util::Status Destroy(Container *container) const;
  virtual ::util::StatusOr<string> Detect(pid_t tid) const;

  // Initialize lmctfy on this machine. This should only be called once at
  // machine boot and MUST be done before any container is returned. At this
  // point the cgroup hierarchies are all mounted, this is to do any
  // resource-specific initialization.
  //
  // Arguments:
  //   spec: Parameters that specify how to setup lmctfy on this machine.
  // Return:
  //   Status: OK iff the machine is setup and ready to use lmctfy.
  virtual ::util::Status InitMachine(const InitSpec &spec) const;

 private:
  // Resolves the container name to its absolute canonical form. For details
  // about this form look at the public lmctfy.proto.
  ::util::StatusOr<string> ResolveContainerName(
      StringPiece container_name) const;

  // Determines whether the specified container exists. Name must be resolved.
  bool Exists(const string &container_name) const;

  // Destroys the specified container and deletes it iff the destruction
  // succeeded. Returns OK in this case.
  ::util::Status DestroyDeleteContainer(Container *container) const;

  // Factory for each container's LockHandler.
  ::std::unique_ptr<const LockHandlerFactory> lock_handler_factory_;

  // Factory for TasksHandler in use.
  ::std::unique_ptr<TasksHandlerFactory> tasks_handler_factory_;

  // Factories for handlers of supported resources.
  ResourceFactoryMap resource_factories_;

  // Wrapper for all calls to the kernel.
  const KernelApi *kernel_;

  // Factory for creating SubProcess instances.
  ::std::unique_ptr<SubProcessFactory> subprocess_factory_;

  // Factory for cgroup paths. Auto-detects where the cgroups are located.
  ::std::unique_ptr<const CgroupFactory> cgroup_factory_;

  // Notifications currently active and in use.
  ::std::unique_ptr<ActiveNotifications> active_notifications_;

  // Eventfd-based notifications.
  ::std::unique_ptr<EventFdNotifications> eventfd_notifications_;

  friend class ContainerApiImplTest;

  DISALLOW_COPY_AND_ASSIGN(ContainerApiImpl);
};

// Implementation of util::containers::lmctfy::Container
class ContainerImpl : public Container {
 public:
  // Takes ownership of lock_handler and tasks_handler. Does not take
  // ownership of the resource factories, lmctfy, kernel API,
  // subprocess_factory, or active_notifications.
  ContainerImpl(const string &name,
                LockHandler *lock_handler,
                TasksHandler *tasks_handler,
                const ResourceFactoryMap &resource_factories,
                const ContainerApi *lmctfy,
                const KernelApi *kernel,
                SubProcessFactory *subprocess_factory,
                ActiveNotifications *active_notifications);
  virtual ~ContainerImpl();

  virtual ::util::Status Update(const ContainerSpec &spec, UpdatePolicy policy)
      LOCKS_EXCLUDED(*lock_handler_);

  // Destroys container and all resource handlers. Also kills all processes
  // inside this container. Returns OK iff successful. Does NOT delete the
  // object.
  virtual ::util::Status Destroy() LOCKS_EXCLUDED(*lock_handler_);

  // These methods are documented in //include/lmctfy.h
  virtual ::util::Status Enter(const ::std::vector<pid_t> &tids)
      LOCKS_EXCLUDED(*lock_handler_);
  virtual ::util::StatusOr<ContainerInfo> Info() const;
  virtual ::util::StatusOr<pid_t> Run(StringPiece command, FdPolicy policy)
      LOCKS_EXCLUDED(*lock_handler_);
  virtual ::util::StatusOr< ::std::vector<Container *>> ListSubcontainers(
      ListPolicy policy) const;
  virtual ::util::StatusOr< ::std::vector<pid_t>> ListThreads(
      ListPolicy policy) const;
  virtual ::util::StatusOr< ::std::vector<pid_t>> ListProcesses(
      ListPolicy policy) const;
  virtual ::util::Status Pause();
  virtual ::util::Status Resume();
  virtual ::util::StatusOr<ContainerStats> Stats(StatsType type) const;
  virtual ::util::StatusOr<NotificationId> RegisterNotification(
      const EventSpec &spec, EventCallback *callback);
  virtual ::util::Status UnregisterNotification(NotificationId notification_id);
  virtual ::util::Status KillAll() LOCKS_EXCLUDED(*lock_handler_);

  // Listable types for any given container.
  enum ListType {
    LIST_PROCESSES,
    LIST_THREADS
  };

 private:
  // Get the ResourceHandlers for the container. If the handler is not found for
  // this container, it tries to get one of the parent container's. i.e.: If
  // /sys/subcont is not found, it uses /sys (if that exists) or / (if /sys does
  // not exist).
  //
  // Return:
  //   StatusOr<vector<ResourceHandler *>>: The status of the action. Iff OK,
  //       vector of pointers to the new ResourceHandlers is returned. Caller
  //       owns all pointers.
  ::util::StatusOr< ::std::vector<ResourceHandler *>>
  GetResourceHandlers() const;

  // Lists processes or threads as specified in the list type. Can list
  // recursively if specified.
  //
  // Arguments:
  //   policy: The listing policy, whether to list self or recursive.
  //   type: The type of resource to list, either processes or threads.
  // Return:
  //   StatusOr<vector<pid_t>>: The status of the action. Iff OK, vector is
  //       populated with the PIDs/TIDs that were requested.
  ::util::StatusOr< ::std::vector<pid_t>> ListProcessesOrThreads(
      ListPolicy policy,
      ListType type) const;

  // Send a SIGKILL signal to the PIDs/TIDs in the container until the container
  // is empty or FLAGS_lmctfy_num_tries_for_unkillable attempts have been
  // made.
  //
  // Arguments:
  //   type: Whether to send the signal to the PIDs or TIDs in the container.
  // Return:
  //   Status: OK iff all PIDs/TIDs are now dead.
  ::util::Status KillTasks(ListType type) const;

  // Same as KillAll() but expecting the container lock to be held when called.
  ::util::Status KillAllUnlocked() EXCLUSIVE_LOCKS_REQUIRED(*lock_handler_);

  // Same as Enter() but expecting the container lock to be held when called.
  ::util::Status EnterUnlocked(const ::std::vector<pid_t> &tids)
      SHARED_LOCKS_REQUIRED(*lock_handler_);

  // Enters the current TID into this containers and starts the subprocess sp.
  // Any errors are returned through the status outparam.
  void EnterAndRun(SubProcess *sp, ::util::Status *status)
      SHARED_LOCKS_REQUIRED(*lock_handler_);

  // Matches the resource handlers specified in handlers_to_match given the
  // existing_handlers. This may involve creating and destroying resource
  // handlers.
  //
  // Modifies all_handlers to remove any destroyed resources and add any created
  // ones (by replacing their previous instances).
  //
  // Argument:
  //   handlers_to_match: The resource handlers we want to match.
  //   existing_handlers: The resource handlers currently being used.
  //   spec: The spec of the container.
  //   all_handlers: Map from resource handler type to resource handler for all
  //       resource handlers.
  // Return:
  //   Status: OK iff the operation was successful.
  ::util::Status MatchResourceHandlers(
      const ::std::set<ResourceHandler *> &handlers_to_match,
      const ::std::set<ResourceHandler *> &existing_handlers,
      const ContainerSpec &spec,
      ::std::map<ResourceType, ResourceHandler *> *all_handlers)
      EXCLUSIVE_LOCKS_REQUIRED(*lock_handler_);

  // Same as ListSubcontainers() but the output is not guaranteed to be sorted.
  ::util::StatusOr< ::std::vector<Container *>> ListSubcontainersUnsorted(
      ListPolicy policy) const;

  // Handles a notification event by calling the user's callback with this
  // container and the specified status as arguments.
  void HandleNotification(shared_ptr<Container::EventCallback> callback,
                          ::util::Status status);

  // Handler for the lock subsystem used by the container.
  ::std::unique_ptr<LockHandler> lock_handler_;

  // Handler for the tasks subsystem used by the container.
  ::std::unique_ptr<TasksHandler> tasks_handler_;

  // Factories for resource handlers.
  const ResourceFactoryMap resource_factories_;

  // ContainerApi access for creating other containers.
  const ContainerApi *lmctfy_;

  // Wrapper for all calls to the kernel.
  const KernelApi *kernel_;

  // Factory for creating SubProcess instances.
  SubProcessFactory *subprocess_factory_;

  // Notifications currently active and in use.
  ActiveNotifications *active_notifications_;

  friend class ContainerImplTest;

  DISALLOW_COPY_AND_ASSIGN(ContainerImpl);
};

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_LMCTFY_IMPL_H_
