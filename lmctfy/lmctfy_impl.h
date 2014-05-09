// Copyright 2014 Google Inc. All Rights Reserved.
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
#include "lmctfy/namespace_handler.h"
#include "lmctfy/resource_handler.h"
#include "include/lmctfy.h"
#include "strings/stringpiece.h"
#include "util/task/statusor.h"

namespace containers {

class InitSpec;

namespace lmctfy {

class ActiveNotifications;
class CgroupFactory;
class ContainerInfo;
class ContainerSpec;
class ContainerStats;
class EventFdNotifications;
class FreezerController;
class FreezerControllerFactory;
class LockHandler;
class LockHandlerFactory;
class TasksHandler;
class TasksHandlerFactory;

typedef ::system_api::KernelAPI KernelApi;
typedef ::std::map<ResourceType, ResourceHandlerFactory *> ResourceFactoryMap;

// Implementation of util::containers::lmctfy::ContainerApi. All methods assume
// that the machine has already initialized (with the exception of
// InitMachine()). InitMachine() must be called after machine boot before any
// containers are created. Doing otherwise will likely fail as the resources are
// not initialized.
class ContainerApiImpl : public ContainerApi {
 public:
  // Exposed for use in testing.
  static ::util::StatusOr<ContainerApiImpl *> NewContainerApiImpl(
      ::std::unique_ptr<CgroupFactory> cgroup_factory, const KernelApi *kernel);
  static ::util::Status InitMachineImpl(
      const KernelApi *kernel,
      ::std::unique_ptr<CgroupFactory> cgroup_factory,
      const InitSpec &spec);

  // ContainerApiImpl takes ownership of all pointers except kernel.
  // ResourceHandlerFactories generate resource-specific ResourceHandlers.
  // TODO(vishnuk): Take all arguments that are owned by ContainerApiImpl as
  // unique_ptrs.
  ContainerApiImpl(
      TasksHandlerFactory *tasks_handler_factory,
      ::std::unique_ptr<CgroupFactory> cgroup_factory,
      const ::std::vector<ResourceHandlerFactory *> &resource_factories,
      const KernelApi *kernel,
      ActiveNotifications *active_notifications,
      NamespaceHandlerFactory *namespace_handler_factory,
      EventFdNotifications *eventfd_notifications,
      ::std::unique_ptr<FreezerControllerFactory> freezer_controller_factory);

  ~ContainerApiImpl() override;

  // These methods are documented in //include/lmctfy.h
  ::util::StatusOr<Container *> Get(StringPiece container_name) const override;
  ::util::StatusOr<Container *> Create(
      StringPiece container_name, const ContainerSpec &spec) const override;
  ::util::Status Destroy(Container *container) const override;
  ::util::StatusOr<string> Detect(pid_t tid) const override;

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

  // Determines whether the specified container exists. Name must be resolved.
  virtual bool Exists(const string &container_name) const;

 private:
  // Resolves the container name to its absolute canonical form. For details
  // about this form look at the public lmctfy.proto.
  ::util::StatusOr<string> ResolveContainerName(
      StringPiece container_name) const;

  // Destroys the specified container and deletes it iff the destruction
  // succeeded. Returns OK in this case.
  ::util::Status DestroyDeleteContainer(Container *container) const;

  // Factory for TasksHandler in use.
  ::std::unique_ptr<TasksHandlerFactory> tasks_handler_factory_;

  // Factories for handlers of supported resources.
  ResourceFactoryMap resource_factories_;

  // Wrapper for all calls to the kernel.
  const KernelApi *kernel_;

  // Factory for cgroup paths. Auto-detects where the cgroups are located.
  ::std::unique_ptr<CgroupFactory> cgroup_factory_;

  // Notifications currently active and in use.
  ::std::unique_ptr<ActiveNotifications> active_notifications_;

  ::std::unique_ptr<NamespaceHandlerFactory> namespace_handler_factory_;

  // Eventfd-based notifications.
  ::std::unique_ptr<EventFdNotifications> eventfd_notifications_;

  ::std::unique_ptr<FreezerControllerFactory> freezer_controller_factory_;

  friend class ContainerApiImplTest;

  DISALLOW_COPY_AND_ASSIGN(ContainerApiImpl);
};

// Implementation of util::containers::lmctfy::Container
class ContainerImpl : public Container {
 public:
  // Takes ownership of tasks_handler. Does not take ownership of the resource
  // factories, lmctfy, kernel API or active_notifications.
  ContainerImpl(const string &name,
                TasksHandler *tasks_handler,
                const ResourceFactoryMap &resource_factories,
                const ContainerApiImpl *lmctfy,
                const KernelApi *kernel,
                NamespaceHandlerFactory *namespace_handler_factory_,
                ActiveNotifications *active_notifications,
                ::std::unique_ptr<FreezerController> freezer_controller);

  ~ContainerImpl() override;

  ::util::Status Update(const ContainerSpec &spec,
                        UpdatePolicy policy) override;

  // Destroys container and all resource handlers. Also kills all processes
  // inside this container. Returns OK iff successful. Does NOT delete the
  // object.
  ::util::Status Destroy() override;

  // These methods are documented in //include/lmctfy.h
  ::util::Status Enter(const ::std::vector<pid_t> &tids) override;
  ::util::StatusOr<ContainerSpec> Spec() const override;
  ::util::StatusOr<pid_t> Run(const ::std::vector<string> &command,
                                      const RunSpec &spec) override;
  ::util::Status Exec(const ::std::vector<string> &command) override;
  ::util::StatusOr< ::std::vector<Container *>> ListSubcontainers(
      ListPolicy policy) const override;
  ::util::StatusOr< ::std::vector<pid_t>> ListThreads(
      ListPolicy policy) const override;
  ::util::StatusOr< ::std::vector<pid_t>> ListProcesses(
      ListPolicy policy) const override;
  ::util::Status Pause() override;
  ::util::Status Resume() override;
  ::util::StatusOr<ContainerStats> Stats(StatsType type) const override;
  ::util::StatusOr<NotificationId> RegisterNotification(
      const EventSpec &spec, EventCallback *callback) override;
  ::util::Status UnregisterNotification(NotificationId notification_id)
      override;
  ::util::Status KillAll() override;
  ::util::StatusOr<pid_t> GetInitPid() const override;

  // Listable types for any given container.
  enum ListType {
    LIST_PROCESSES,
    LIST_THREADS
  };

 private:
  ::util::StatusOr<NamespaceHandler *> GetNamespaceHandler(
      const string &name) const;

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
  ::util::StatusOr< ::std::vector<GeneralResourceHandler *>>
  GetGeneralResourceHandlers() const;

  // Lists processes or threads as specified in the list type. Lists
  // non-recursively by default.
  //
  // Arguments:
  //   type: The type of resource to list, either processes or threads.
  // Return:
  //   StatusOr<vector<pid_t>>: The status of the action. Iff OK, vector is
  //       populated with the PIDs/TIDs that were requested.
  ::util::StatusOr< ::std::vector<pid_t>> ListProcessesOrThreads(
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

  // Runs the command using namespace handler.
  ::util::StatusOr<pid_t> RunInNamespace(const ::std::vector<string> *command,
                                         const RunSpec *spec) const;

  // Handles a notification event by calling the user's callback with this
  // container and the specified status as arguments.
  void HandleNotification(std::shared_ptr<Container::EventCallback> callback,
                          ::util::Status status);

  // Returns OK iff this container still exists. This is necessary since a
  // container can be deleted "under you" by another process or thread.
  ::util::Status Exists() const;

  // Handler for the tasks subsystem used by the container.
  ::std::unique_ptr<TasksHandler> tasks_handler_;

  // Factories for resource handlers.
  const ResourceFactoryMap resource_factories_;

  // ContainerApi access for creating other containers.
  const ContainerApiImpl *lmctfy_;

  // Wrapper for all calls to the kernel.
  const KernelApi *kernel_;

  NamespaceHandlerFactory *namespace_handler_factory_;

  // Notifications currently active and in use.
  ActiveNotifications *active_notifications_;

  ::std::unique_ptr<FreezerController> freezer_controller_;

  friend class ContainerImplTest;

  DISALLOW_COPY_AND_ASSIGN(ContainerImpl);
};

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_LMCTFY_IMPL_H_
