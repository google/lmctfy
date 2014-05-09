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

#include "lmctfy/lmctfy_impl.h"

#include <sys/time.h>

#include <algorithm>
#include <memory>
#include <queue>
#include <set>
#include <utility>
#include <vector>

#include "gflags/gflags.h"
#include "base/integral_types.h"
#include "base/logging.h"
#include "util/eventfd_listener.h"
#include "file/base/file.h"
#include "file/base/path.h"
#include "lmctfy/active_notifications.h"
#include "lmctfy/cgroup_tasks_handler.h"
#include "lmctfy/controllers/cgroup_factory.h"
#include "lmctfy/controllers/eventfd_notifications.h"
#include "lmctfy/controllers/freezer_controller.h"
#include "lmctfy/controllers/freezer_controller_stub.h"
#include "lmctfy/controllers/job_controller.h"
#include "lmctfy/namespace_handler.h"
#include "lmctfy/tasks_handler.h"
#include "include/lmctfy.pb.h"
#include "util/safe_types/unix_gid.h"
#include "util/safe_types/unix_uid.h"
#include "util/errors.h"
#include "util/scoped_cleanup.h"
#include "strings/split.h"
#include "strings/substitute.h"
#include "thread/thread.h"
#include "thread/thread_options.h"
#include "util/gtl/stl_util.h"
#include "re2/re2.h"
#include "util/task/codes.pb.h"

DEFINE_int32(lmctfy_num_tries_for_unkillable, 3,
             "The number of times to try to kill a PID/TID before considering "
             "the PID/TID unkillable. See flag "
             "\"lmctfy_ms_delay_between_kills\" for the duration of the "
             "delay between tries.");

DEFINE_int32(lmctfy_ms_delay_between_kills, 250,
             "The number of milliseconds to wait between kill attempts.");


DEFINE_bool(lmctfy_use_namespaces,
            true,
            "Whether lmctfy uses namespaces.");

using ::util::EventfdListener;
using ::containers::InitSpec;
using ::util::UnixGid;
using ::util::UnixGidValue;
using ::util::UnixUid;
using ::util::UnixUidValue;
using ::util::ScopedCleanup;
using ::std::make_pair;
using ::std::map;
using ::std::move;
using ::std::queue;
using ::std::set;
using ::std::unique_ptr;
using ::std::vector;
using ::strings::Split;
using ::strings::Substitute;
using ::util::error::FAILED_PRECONDITION;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {

// Creates and returns factories for all supported ResourceHandlers. This is in
// a separate file to allow for custom resource handlers to be utilized at link
// time.
// Default ones are found in lmctfy_init.cc
extern StatusOr<vector<ResourceHandlerFactory *>> CreateSupportedResources(
    CgroupFactory *cgroup_factory, const KernelApi *kernel,
    EventFdNotifications *eventfd_notifications);

Status EnterInto(const vector<pid_t> &tids,
                 const vector<ResourceHandler *> &resource_handlers,
                 TasksHandler *tasks_handler,
                 FreezerController *freezer_controller) {
  for (const pid_t &tid : tids) {
    RETURN_IF_ERROR(freezer_controller->Enter(tid));
  }

  RETURN_IF_ERROR(tasks_handler->TrackTasks(tids));

  for (ResourceHandler *handler : resource_handlers) {
    RETURN_IF_ERROR(handler->Enter(tids));
  }

  return Status::OK;
}

// Enters the current TID into this containers and runs the callback.
// Any errors are returned through the result_status outparam.
// All arguments are borrowed. |action| doesn't have to be repeatable.
template <typename T>
void EnterAndDo(const vector<ResourceHandler *> *resource_handlers,
                TasksHandler *tasks_handler,
                FreezerController *freezer_controller,
                ResultCallback<StatusOr<T>> *action,
                StatusOr<T> *result_status) {
  // Enter into the container so that the command we start is run inside this
  // container.
  Status status = EnterInto({0}, *resource_handlers, tasks_handler,
                            freezer_controller);
  if (!status.ok()) {
    *result_status = status;
    return;
  }
  *result_status = action->Run();
}

// Creates a thread, enters it into these handlers and runs the callback.
// All arguments are borrowed. |action| doesn't have to be repeatable.
template <typename T>
StatusOr<T> EnterThreadAndDo(
    const vector<ResourceHandler *> &resource_handlers,
    TasksHandler *tasks_handler,
    FreezerController *freezer_controller,
    ResultCallback<StatusOr<T>> *action) {
  StatusOr<T> statusor;
  ::thread::Options options;
  options.set_joinable(true);
  ClosureThread thread(options,
                       "lmctfy-enter",
                       NewPermanentCallback(&EnterAndDo<T>,
                                            &resource_handlers,
                                            tasks_handler,
                                            freezer_controller,
                                            action,
                                            &statusor));
  thread.Start();
  thread.Join();

  return statusor;
}

template<typename T, typename U>
StatusOr<T *> GetHandler(const string &name, U resource_handler_factory) {
  // Try to progressively attach to the parent container's resource if the
  // child container's resource is not found. Stop at root regardless of
  // success.
  string container_name = name;
  while (true) {
    StatusOr<T *> statusor = resource_handler_factory(container_name);
    if (statusor.status().error_code() != ::util::error::NOT_FOUND ||
        container_name == "/") {
      return statusor;
    }
    container_name = file::Dirname(container_name).ToString();
  }
}

StatusOr<ResourceHandler *> GetResourceHandler(
    const string &name,
    ResourceHandlerFactory *resource_handler_factory) {
  return GetHandler<ResourceHandler>(
      name,
      [resource_handler_factory](const string &name) {
        return resource_handler_factory->Get(name);
      });
}

StatusOr<vector<ResourceHandler *>> GetResourceHandlersFor(
    const string &name,
    const ResourceFactoryMap &resource_factories) {
  vector<ResourceHandler *> handlers;
  ElementDeleter d(&handlers);

  // Attach to all factories.
  for (const auto &type_factory_pair : resource_factories) {
    handlers.push_back(
        RETURN_IF_ERROR(GetResourceHandler(name, type_factory_pair.second)));
  }

  // TODO(vmarmol): Use ScopedCleanup<DeleteElements> when that is available.
  vector<ResourceHandler *> output;
  output.swap(handlers);
  return output;
}

// Given a map<ResourceType, T> and a ContainerSpec, extract all of the T's that
// have the corresponding resource defined in the ContainerSpec.
template <typename T>
static void GetUsedResourceHandlers(
    const ContainerSpec &spec, const map<ResourceType, T> &resource_handlers,
    set<T> *output) {

#define EXTRACT_RESOURCE(resource_proto_name, resource_type) \
  if (spec.has_##resource_proto_name() &&                    \
      ((it = resource_handlers.find(resource_type)) !=       \
       resource_handlers.end())) {                           \
    output->insert(it->second);                              \
  }
  // TODO(vmarmol): Consider doing this through proto introspection.
  typename map<ResourceType, T>::const_iterator it;
  EXTRACT_RESOURCE(cpu, RESOURCE_CPU);
  EXTRACT_RESOURCE(memory, RESOURCE_MEMORY);
  EXTRACT_RESOURCE(blockio, RESOURCE_BLOCKIO);
  EXTRACT_RESOURCE(network, RESOURCE_NETWORK);
  EXTRACT_RESOURCE(monitoring, RESOURCE_MONITORING);
  EXTRACT_RESOURCE(filesystem, RESOURCE_FILESYSTEM);
  EXTRACT_RESOURCE(virtual_host, RESOURCE_VIRTUALHOST);
  EXTRACT_RESOURCE(device, RESOURCE_DEVICE);
#undef EXTRACT_RESOURCE
}

// TODO(vmarmol): Move this to a CgroupTasksHandler::New() like we've done with
// ResourceHandlers.
// Creates a factory for TasksHandler. It tries to use the job cgroup hierarchy
// if available, else it falls back to the freezer cgroup hierarchy.
static StatusOr<TasksHandlerFactory *> CreateTasksHandler(
    CgroupFactory *cgroup_factory, const KernelApi *kernel,
    EventFdNotifications *eventfd_notifications) {
  if (cgroup_factory->IsMounted(CGROUP_JOB)) {
    return new CgroupTasksHandlerFactory<JobController>(
        new JobControllerFactory(cgroup_factory, kernel, eventfd_notifications),
        kernel);
  } else if (cgroup_factory->IsMounted(CGROUP_FREEZER)) {
    return new CgroupTasksHandlerFactory<FreezerController>(
        new FreezerControllerFactory(cgroup_factory, kernel,
                                     eventfd_notifications, false),
        kernel);
  } else {
    return Status(::util::error::NOT_FOUND,
                  Substitute(
                      "lmctfy requires a canonical tasks cgroup hierarchy, "
                      "none were found"));
  }
}

// Takes ownership of cgroup_factory.
StatusOr<ContainerApiImpl *> ContainerApiImpl::NewContainerApiImpl(
    unique_ptr<CgroupFactory> cgroup_factory, const KernelApi *kernel) {

  // Create the notifications subsystem.
  unique_ptr<ActiveNotifications> active_notifications(
      new ActiveNotifications());
  unique_ptr<EventFdNotifications> eventfd_notifications(
      new EventFdNotifications(
          active_notifications.get(),
          new EventfdListener(*kernel, "lmctfy_eventfd_listener", nullptr,
                              false, 20)));

  // Create the resource handler factories.
  vector<ResourceHandlerFactory *> resource_factories;
  ElementDeleter d(&resource_factories);
  resource_factories =
      RETURN_IF_ERROR(CreateSupportedResources(cgroup_factory.get(), kernel,
                                              eventfd_notifications.get()));

  // Create the TasksHandlerFactory.
  unique_ptr<TasksHandlerFactory> tasks_handler_factory(
      RETURN_IF_ERROR(
          CreateTasksHandler(cgroup_factory.get(), kernel,
                             eventfd_notifications.get())));

  // TODO(vishnuk): Pass in a real FreezerControllerFactory once Creation,
  // deletion and other operations are handled for freezer.
  unique_ptr<FreezerControllerFactory> freezer_controller_factory;

  if (cgroup_factory->IsMounted(CGROUP_JOB) ||
      !cgroup_factory->IsMounted(CGROUP_FREEZER)) {
    // It is OK for a machine to not have Freezer initialized or supported.
    freezer_controller_factory.reset(new FreezerControllerFactoryStub());
  } else {
    freezer_controller_factory.reset(
        new FreezerControllerFactory(cgroup_factory.get(), kernel,
                                     eventfd_notifications.get()));
  }

  unique_ptr<NamespaceHandlerFactory> namespace_handler_factory;
  if (FLAGS_lmctfy_use_namespaces) {
      namespace_handler_factory.reset(RETURN_IF_ERROR(
          NamespaceHandlerFactory::New(tasks_handler_factory.get())));
  } else {
      namespace_handler_factory.reset(RETURN_IF_ERROR(
          NamespaceHandlerFactory::NewNull(kernel)));
  }

  // Release all deleters and create the ContainerApi instance.
  vector<ResourceHandlerFactory *> resources_to_use;
  resources_to_use.swap(resource_factories);
  return new ContainerApiImpl(
      tasks_handler_factory.release(), move(cgroup_factory), resources_to_use,
      kernel, active_notifications.release(),
      namespace_handler_factory.release(),
      eventfd_notifications.release(), move(freezer_controller_factory));
}

// New() assumes that all cgroups are already mounted and automatically detects
// these mounts. It also checks if the machine has already been initialized.
StatusOr<ContainerApi *> ContainerApi::New() {
  const KernelApi *kernel = ::system_api::GlobalKernelApi();

  // TODO(vmarmol): Check that the machine has been initialized.

  // Auto-detect mount points for the cgroup hierarchies.
  unique_ptr<CgroupFactory> cgroup_factory(
      RETURN_IF_ERROR(CgroupFactory::New(kernel)));

  return ContainerApiImpl::NewContainerApiImpl(move(cgroup_factory), kernel);
}

// InitMachine() is called at machine boot to mount all hierarchies needed by
// lmctfy.
Status ContainerApi::InitMachine(const InitSpec &spec) {
  // Mount all the specified cgroups.
  unique_ptr<const KernelApi> kernel_api(::system_api::GlobalKernelApi());
  unique_ptr<CgroupFactory> cgroup_factory(
      RETURN_IF_ERROR(CgroupFactory::New(kernel_api.get())));
  return ContainerApiImpl::InitMachineImpl(kernel_api.release(),
                                       move(cgroup_factory), spec);
}

// Does not take ownership of kernel.
Status ContainerApiImpl::InitMachineImpl(const KernelApi *kernel,
                                     unique_ptr<CgroupFactory> cgroup_factory,
                                     const InitSpec &spec) {
  unique_ptr<ContainerApiImpl> lmctfy(
      RETURN_IF_ERROR(
          ContainerApiImpl::NewContainerApiImpl(move(cgroup_factory), kernel)));

  // Init the machine. This initializes all the handlers.
  return lmctfy->InitMachine(spec);
}

ContainerApiImpl::ContainerApiImpl(
    TasksHandlerFactory *tasks_handler_factory,
    unique_ptr<CgroupFactory> cgroup_factory,
    const vector<ResourceHandlerFactory *> &resource_factories,
    const KernelApi *kernel, ActiveNotifications *active_notifications,
    NamespaceHandlerFactory *namespace_handler_factory,
    EventFdNotifications *eventfd_notifications,
    unique_ptr<FreezerControllerFactory> freezer_controller_factory)
    : tasks_handler_factory_(CHECK_NOTNULL(tasks_handler_factory)),
      kernel_(CHECK_NOTNULL(kernel)),
      cgroup_factory_(move(cgroup_factory)),
      active_notifications_(CHECK_NOTNULL(active_notifications)),
      namespace_handler_factory_(CHECK_NOTNULL(namespace_handler_factory)),
      eventfd_notifications_(CHECK_NOTNULL(eventfd_notifications)),
      freezer_controller_factory_(move(freezer_controller_factory)) {
  // Map each Resource Handler to the resource type
  for (const auto &handler : resource_factories) {
    resource_factories_[handler->type()] = handler;
  }
}

ContainerApiImpl::~ContainerApiImpl() { STLDeleteValues(&resource_factories_); }

StatusOr<Container *> ContainerApiImpl::Get(StringPiece container_name) const {
  // Resolve the container name.
  const string resolved_name =
      RETURN_IF_ERROR(ResolveContainerName(container_name));

  // Ensure it exists.
  if (!Exists(resolved_name)) {
    return Status(
        ::util::error::NOT_FOUND,
        Substitute("Can't get non-existent container \"$0\"", resolved_name));
  }

  unique_ptr<FreezerController> freezer_controller(
      RETURN_IF_ERROR(freezer_controller_factory_->Get(resolved_name)));

  // Get the tasks handler for this container.
  unique_ptr<TasksHandler> tasks_handler(
      RETURN_IF_ERROR(tasks_handler_factory_->Get(resolved_name)));

  return new ContainerImpl(
      resolved_name, tasks_handler.release(), resource_factories_, this,
      kernel_, namespace_handler_factory_.get(), active_notifications_.get(),
      move(freezer_controller));
}

namespace {

template<typename T>
Status DestroyOrDelete(T *destroyable) {
  Status status = destroyable->Destroy();
  if (!status.ok()) {
    delete destroyable;
  }
  return status;
}

template<typename T>
struct DestroyDeleter {
  void operator()(T *t) { DestroyOrDelete(t).IgnoreError(); }
};

template<typename T> using UniqueDestroyPtr = unique_ptr<T, DestroyDeleter<T>>;

StatusOr<NamespaceHandler *> CreateNamespaceHandlerWrapper(
    NamespaceHandlerFactory *namespace_handler_factory,
    const string *resolved_name,
    const ContainerSpec *spec,
    const MachineSpec *machine_spec) {
  return namespace_handler_factory->CreateNamespaceHandler(*resolved_name,
                                                           *spec,
                                                           *machine_spec);
}

}  // namespace

StatusOr<Container *> ContainerApiImpl::Create(StringPiece container_name,
                                           const ContainerSpec &spec) const {
  // TODO(vmarmol): Check reserved keywords.
  // Ensure name is specified.
  if (container_name.empty()) {
    return Status(::util::error::INVALID_ARGUMENT, "Container name is missing");
  }

  // Get which ResourceHandlerFactories are being used by the spec.
  set<ResourceHandlerFactory *> used_handler_factories;
  GetUsedResourceHandlers(spec, resource_factories_, &used_handler_factories);

  // Resolve the container name.
  const string resolved_name =
      RETURN_IF_ERROR(ResolveContainerName(container_name));

  // Ensure the container doesn't already exist.
  if (Exists(resolved_name)) {
    return Status(
        ::util::error::ALREADY_EXISTS,
        Substitute("Can't create existing container \"$0\"", resolved_name));
  }

  // Create Freezer Cgroup before creating the tasks handler since tasks handler
  // can use Freezer internally.
  UniqueDestroyPtr<FreezerController> freezer_controller(
      RETURN_IF_ERROR(freezer_controller_factory_->Create(resolved_name)));

  // Create the tasks handler for this container
  UniqueDestroyPtr<TasksHandler> tasks_handler(
      RETURN_IF_ERROR(tasks_handler_factory_->Create(resolved_name, spec)));

  // We will locally create the resource handlers we need.
  vector<UniqueDestroyPtr<ResourceHandler>> specified_resource_handlers;

  // Create resource handlers for this container.
  const string kParentName = file::Dirname(resolved_name).ToString();
  StatusOr<ResourceHandler *> statusor;
  for (auto type_handler_pair : resource_factories_) {
    // Don't create resources that were not specified.
    if (used_handler_factories.find(type_handler_pair.second) ==
        used_handler_factories.end()) {
      continue;
    }

    specified_resource_handlers.emplace_back(
        RETURN_IF_ERROR(type_handler_pair.second->Create(resolved_name, spec)));
  }

  // Delegate the container if it was specified.
  UnixUid uid(spec.has_owner() ? UnixUid(spec.owner())
                               : UnixUidValue::Invalid());
  UnixGid gid(spec.has_owner_group() ? UnixGid(spec.owner_group())
                                     : UnixGidValue::Invalid());
  if (uid != UnixUidValue::Invalid() || gid != UnixGidValue::Invalid()) {
    // Delegate freezer controller, tasks handler and each of the resources.
    RETURN_IF_ERROR(freezer_controller->Delegate(uid, gid));
    RETURN_IF_ERROR(tasks_handler->Delegate(uid, gid));
    for (const auto &handler : specified_resource_handlers) {
      RETURN_IF_ERROR(handler->Delegate(uid, gid));
    }
  }

  if (spec.has_virtual_host()) {
    vector<ResourceHandler *> all_resource_handlers =
        RETURN_IF_ERROR(GetResourceHandlersFor(resolved_name,
                                               resource_factories_));
    ElementDeleter d(&all_resource_handlers);

    // Setup the correct machine spec
    MachineSpec machine_spec;
    for (const auto &handler : all_resource_handlers) {
      RETURN_IF_ERROR(handler->PopulateMachineSpec(&machine_spec));
    }
    RETURN_IF_ERROR(freezer_controller->PopulateMachineSpec(&machine_spec));
    RETURN_IF_ERROR(tasks_handler->PopulateMachineSpec(&machine_spec));
    RETURN_IF_ERROR(cgroup_factory_->PopulateMachineSpec(&machine_spec));

    const MachineSpec *machine_spec_ptr = &machine_spec;
    unique_ptr<ResultCallback<StatusOr<NamespaceHandler *>>> action(
        NewPermanentCallback(&CreateNamespaceHandlerWrapper,
                             namespace_handler_factory_.get(),
                             &resolved_name,
                             &spec,
                             machine_spec_ptr));
    unique_ptr<NamespaceHandler> namespace_handler(
        RETURN_IF_ERROR(
            EnterThreadAndDo(
                all_resource_handlers,
                tasks_handler.get(),
                freezer_controller.get(),
                action.get())));
  }

  for (auto &handler : specified_resource_handlers) {
    delete handler.release();
  }
  specified_resource_handlers.clear();

  return StatusOr<Container *>(new ContainerImpl(
      resolved_name, tasks_handler.release(), resource_factories_, this,
      kernel_, namespace_handler_factory_.get(), active_notifications_.get(),
      move(unique_ptr<FreezerController>(freezer_controller.release()))));
}

Status ContainerApiImpl::Destroy(Container *container) const {
  // Get all subcontainers to destroy them.
  vector<Container *> subcontainers =
      RETURN_IF_ERROR(container->ListSubcontainers(Container::LIST_RECURSIVE));

  // Destroy the subcontainers
  // Subcontainers are sorted by container name so that the children of a
  // container are always after their parent. We iterate backwards so that all
  // children are destroyed before their parent.
  Status status;
  for (auto it = subcontainers.rbegin(); it != subcontainers.rend(); ++it) {
    status = DestroyDeleteContainer(*it);
    if (!status.ok()) {
      // Delete the remaining containers without destroying them.
      STLDeleteContainerPointers(it, subcontainers.rend());
      return status;
    }
  }

  return DestroyDeleteContainer(container);
}

Status ContainerApiImpl::DestroyDeleteContainer(Container *container) const {
  // Destroy all resources.
  RETURN_IF_ERROR(container->Destroy());

  delete container;
  return Status::OK;
}

bool ContainerApiImpl::Exists(const string &resolved_container_name) const {
  return tasks_handler_factory_->Exists(resolved_container_name);
}

StatusOr<string> ContainerApiImpl::Detect(pid_t tid) const {
  return tasks_handler_factory_->Detect(tid);
}

Status ContainerApiImpl::InitMachine(const InitSpec &spec) const {
  for (auto &mount : spec.cgroup_mount()) {
    RETURN_IF_ERROR(cgroup_factory_->Mount(mount));
  }

  // Initialize the resource handlers.
  for (auto type_handler_pair : resource_factories_) {
    RETURN_IF_ERROR(type_handler_pair.second->InitMachine(spec));
  }
  RETURN_IF_ERROR(namespace_handler_factory_->InitMachine(spec));
  return Status::OK;
}

StatusOr<string> ContainerApiImpl::ResolveContainerName(
    StringPiece container_name) const {
  string resolved_name(container_name.data(), container_name.size());

  // Detect invalid characters (not alpha numeric or _, -, ., and /).
  if (!RE2::FullMatch(resolved_name, "[a-zA-Z0-9_\\-./]+")) {
    return Status(::util::error::INVALID_ARGUMENT,
                  Substitute("Invalid characters in container name \"$0\"",
                             resolved_name));
  }

  // Make absolute.
  if (!::file::IsAbsolutePath(resolved_name)) {
    const string detected_name = RETURN_IF_ERROR(Detect(0));
    resolved_name = ::file::JoinPath(detected_name, resolved_name);
  }

  resolved_name = ::file::CleanPath(resolved_name);

  // Ensure that no part of the path starts with a non-alphanumeric character.
  if (RE2::PartialMatch(resolved_name, "/[^a-zA-Z0-9]")) {
    return Status(::util::error::INVALID_ARGUMENT,
                  Substitute(
                      "Part of the container name \"$0\" starts with a "
                      "non-alphanumeric character",
                      container_name));
  }

  return resolved_name;
}

ContainerImpl::ContainerImpl(const string &name, TasksHandler *tasks_handler,
                             const ResourceFactoryMap &resource_factories,
                             const ContainerApiImpl *lmctfy,
                             const KernelApi *kernel,
                             NamespaceHandlerFactory *namespace_handler_factory,
                             ActiveNotifications *active_notifications,
                             unique_ptr<FreezerController> freezer_controller)
    : Container(name),
      tasks_handler_(CHECK_NOTNULL(tasks_handler)),
      resource_factories_(resource_factories),
      lmctfy_(CHECK_NOTNULL(lmctfy)),
      kernel_(CHECK_NOTNULL(kernel)),
      namespace_handler_factory_(namespace_handler_factory),
      active_notifications_(active_notifications),
      freezer_controller_(move(freezer_controller)) {}

ContainerImpl::~ContainerImpl() {}

Status ContainerImpl::Update(const ContainerSpec &spec, UpdatePolicy policy) {
  RETURN_IF_ERROR(Exists());

  // Get all resources and map them by type.
  map<ResourceType, GeneralResourceHandler *> all_handlers;
  for (auto *handler : RETURN_IF_ERROR(GetGeneralResourceHandlers())) {
    all_handlers[handler->type()] = handler;
  }
  ValueDeleter d(&all_handlers);

  // Get resources used in the spec.
  set<GeneralResourceHandler *> used_handlers;
  GetUsedResourceHandlers(spec, all_handlers, &used_handlers);

  // We need to ensure that all used resources are being isolated. To do this we
  // count the resources that are being isolated and the number that are
  // isolated and being used.
  int isolated_count = 0;
  int isolated_and_used_count = 0;
  for (const auto &type_handler_pair : all_handlers) {
    if (name_ == type_handler_pair.second->container_name()) {
      isolated_count++;

      // Count the resources that are both existing and used.
      if (used_handlers.find(type_handler_pair.second) != used_handlers.end()) {
        isolated_and_used_count++;
      }
    }
  }

  // Ensure that all specified resources are also being isolated. If this is not
  // the case, a used resource would not be in the isolated_and_used set.
  if (isolated_and_used_count != used_handlers.size()) {
    return Status(
        ::util::error::INVALID_ARGUMENT,
        "Must not specify an update to a resource not being isolated");
  }

  // If this is an update, all isolated resources must also be used.
  if ((policy == Container::UPDATE_REPLACE) &&
      (isolated_count != used_handlers.size())) {
    return Status(
        ::util::error::INVALID_ARGUMENT,
        "A replace update must specify all resources being isolated.");
  }

  // Apply the update to all specified handlers.
  for (GeneralResourceHandler *handler : used_handlers) {
    RETURN_IF_ERROR(handler->Update(spec, policy));
  }

  return Status::OK;
}

Status ContainerImpl::Destroy() {
  RETURN_IF_ERROR(Exists());

  // Ensure the container is empty (no tasks).
  RETURN_IF_ERROR(KillAll());

  // Get and destroy all resources.
  vector<GeneralResourceHandler *> handlers =
      RETURN_IF_ERROR(GetGeneralResourceHandlers());
  ElementDeleter d(&handlers);
  while (!handlers.empty()) {
    GeneralResourceHandler *handler = handlers.back();

    // Only destroy the resources attached to this container.
    if (name_ == handler->container_name()) {
      // Destroy deletes the element on success, on failure ElementDeleter
      // does.
      RETURN_IF_ERROR(handler->Destroy());
    } else {
      delete handler;
    }

    handlers.pop_back();
  }

  // Destroy tasks handler. Destroy() deleted the pointer so just release it.
  RETURN_IF_ERROR(tasks_handler_->Destroy());
  tasks_handler_.release();

  // Destroy freezer after tasks handler. Destroy() deletes the pointer, so
  // release it.
  RETURN_IF_ERROR(freezer_controller_->Destroy());
  freezer_controller_.release();

  return Status::OK;
}

Status ContainerImpl::Enter(const vector<pid_t> &tids) {
  RETURN_IF_ERROR(Exists());

  unique_ptr<NamespaceHandler> namespace_handler(
      RETURN_IF_ERROR(GetNamespaceHandler(name_)));
  if (RETURN_IF_ERROR(namespace_handler->IsDifferentVirtualHost(tids))) {
    return Status(FAILED_PRECONDITION,
                  "Container in a different Virtual Host can't be entered.");
  }
  // Generate resource handlers and enter tids into them.
  vector<ResourceHandler *> handlers = RETURN_IF_ERROR(GetResourceHandlers());
  ElementDeleter d(&handlers);

  return EnterInto(tids, handlers, tasks_handler_.get(),
                   freezer_controller_.get());
}

StatusOr<NamespaceHandler *> ContainerImpl::GetNamespaceHandler(
    const string &name) const {
  return GetHandler<NamespaceHandler>(name, [this](const string &name) {
    return namespace_handler_factory_->GetNamespaceHandler(name);
  });
}

StatusOr<pid_t> ContainerImpl::RunInNamespace(const vector<string> *command,
                                              const RunSpec *spec) const {
  unique_ptr<NamespaceHandler> namespace_handler(
      RETURN_IF_ERROR(GetNamespaceHandler(name_)));
  return namespace_handler->Run(*command, *spec);
}

StatusOr<pid_t> ContainerImpl::Run(const vector<string> &command,
                                   const RunSpec &spec) {
  RETURN_IF_ERROR(Exists());

  // TODO(kyurtsever) Move these checks to NamespaceHandler.
  // Check usage.
  if (spec.has_fd_policy() && spec.fd_policy() == RunSpec::UNKNOWN) {
    return Status(::util::error::INVALID_ARGUMENT,
                  "Invalid FD policy: UNKNOWN");
  }
  if (command.empty()) {
    return Status(::util::error::INVALID_ARGUMENT, "Command must not be empty");
  }


  // Create a thread, enter the thread into this container, and run the
  // command. This is so that all accounting is properly done without having
  // to move the user's thread into this container or doing some work between
  // fork and exec.
  vector<ResourceHandler *> handlers = RETURN_IF_ERROR(GetResourceHandlers());
  ElementDeleter d(&handlers);
  unique_ptr<ResultCallback<StatusOr<pid_t>>> action(
      NewPermanentCallback(this,
                           &ContainerImpl::RunInNamespace,
                           &command,
                           &spec));
  return EnterThreadAndDo(handlers,
                          tasks_handler_.get(),
                          freezer_controller_.get(),
                          action.get());
}

StatusOr<ContainerSpec> ContainerImpl::Spec() const {
  RETURN_IF_ERROR(Exists());

  // TODO(vmarmol): Fill in the non-resource-specific parts of the spec.

  // Generate resource handlers.
  vector<GeneralResourceHandler *> handlers =
      RETURN_IF_ERROR(GetGeneralResourceHandlers());
  ElementDeleter d(&handlers);

  // Get the spec from each ResourceHandler attached to this container..
  ContainerSpec spec;
  for (GeneralResourceHandler *handler : handlers) {
    if (name_ == handler->container_name()) {
      RETURN_IF_ERROR(handler->Spec(&spec));
    }
  }

  return spec;
}

Status ContainerImpl::Exec(const vector<string> &command) {
  RETURN_IF_ERROR(Exists());

  // Verify args.
  if (command.empty()) {
    return Status(::util::error::INVALID_ARGUMENT, "No command provided");
  }

  vector<ResourceHandler *> handlers = RETURN_IF_ERROR(GetResourceHandlers());
  ElementDeleter d(&handlers);

  RETURN_IF_ERROR(EnterInto({0}, handlers, tasks_handler_.get(),
                            freezer_controller_.get()));

  unique_ptr<NamespaceHandler> namespace_handler(
      RETURN_IF_ERROR(GetNamespaceHandler(name_)));

  // Execute the command.
  RETURN_IF_ERROR(namespace_handler->Exec(command));
  // We shouldn't get here.
  return Status(::util::error::INTERNAL, "Exec failed for unknown reason.");
}

// Sort the containers by name ascending.
static bool CompareContainerByName(Container *container1,
                                   Container *container2) {
  return container1->name() < container2->name();
}

// Translate from Container::ListType to TasksHandler::ListPolicy.
TasksHandler::ListType ToTasksHandlerListType(Container::ListPolicy policy) {
  return policy == Container::LIST_SELF ? TasksHandler::ListType::SELF
                                        : TasksHandler::ListType::RECURSIVE;
}

StatusOr<vector<Container *>> ContainerImpl::ListSubcontainers(
    ListPolicy policy) const {
  RETURN_IF_ERROR(Exists());

  // Get all subcontainers.
  const vector<string> subcontainer_names = RETURN_IF_ERROR(
      tasks_handler_->ListSubcontainers(ToTasksHandlerListType(policy)));

  // Attach to the subcontainers.
  vector<Container *> subcontainers;
  ScopedCleanup cleanup([&subcontainers]() {
    STLDeleteElements(&subcontainers);
  });
  for (const string &subcontainer_name : subcontainer_names) {
    subcontainers.emplace_back(
        RETURN_IF_ERROR(lmctfy_->Get(subcontainer_name)));
  }

  cleanup.Cancel();
  sort(subcontainers.begin(), subcontainers.end(), CompareContainerByName);
  return subcontainers;
}

StatusOr<vector<pid_t>> ContainerImpl::ListThreads(ListPolicy policy) const {
  RETURN_IF_ERROR(Exists());
  return tasks_handler_->ListThreads(ToTasksHandlerListType(policy));
}

StatusOr<vector<pid_t>> ContainerImpl::ListProcesses(ListPolicy policy) const {
  RETURN_IF_ERROR(Exists());
  return tasks_handler_->ListProcesses(ToTasksHandlerListType(policy));
}

Status ContainerImpl::Pause() {
  Status status = freezer_controller_->Freeze();
  if (status.CanonicalCode() == ::util::error::NOT_FOUND) {
    // Freezer cgroup was not setup.
    return Status(FAILED_PRECONDITION,
                  "Pause is not supported on this machine");
  }
  return status;
}

Status ContainerImpl::Resume() {
  Status status = freezer_controller_->Unfreeze();
  if (status.CanonicalCode() == ::util::error::NOT_FOUND) {
    // Freezer cgroup was not setup.
    return Status(FAILED_PRECONDITION,
                  "Resume is not supported on this machine.");
  }
  return status;
}

StatusOr<ContainerStats> ContainerImpl::Stats(StatsType stats_type) const {
  RETURN_IF_ERROR(Exists());

  ContainerStats stats;

  // Get all resource handlers.
  vector<GeneralResourceHandler *> handlers =
      RETURN_IF_ERROR(GetGeneralResourceHandlers());
  ElementDeleter d(&handlers);

  // Get stats from each resource.
  for (GeneralResourceHandler *handler : handlers) {
    // Only get stats for the resources attached to this container.
    if (name_ == handler->container_name()) {
      RETURN_IF_ERROR(handler->Stats(stats_type, &stats));
    }
  }

  return stats;
}

void ContainerImpl::HandleNotification(
    std::shared_ptr<Container::EventCallback> callback, Status status) {
  callback->Run(this, status);
}

StatusOr<Container::NotificationId> ContainerImpl::RegisterNotification(
    const EventSpec &spec, Container::EventCallback *callback) {
  CHECK_NOTNULL(callback);
  callback->CheckIsRepeatable();

  // A shared pointer is used since this callback is wrapped in the
  // HandleNotification() permanent callback. It is deleted when that callback
  // is deleted.
  std::shared_ptr<Container::EventCallback> user_callback(callback);

  RETURN_IF_ERROR(Exists());

  // Get all resource handlers.
  vector<GeneralResourceHandler *> handlers =
      RETURN_IF_ERROR(GetGeneralResourceHandlers());
  ElementDeleter d(&handlers);

  // Register notification (only one notification is specified per request).
  StatusOr<Container::NotificationId> statusor;
  for (GeneralResourceHandler *handler : handlers) {
    statusor = handler->RegisterNotification(
        spec, NewPermanentCallback(this, &ContainerImpl::HandleNotification,
                                   user_callback));

    // Stop if there was no error (error_code() == OK) or if there was a
    // non-NOT_FOUND error. Only one notification can be registered so the first
    // OK result is taken. An error code of NOT_FOUND means the ResourceHandler
    // does not handle the specified event.
    if (statusor.status().error_code() != ::util::error::NOT_FOUND) {
      return statusor;
    }
  }

  return Status(
      ::util::error::INVALID_ARGUMENT,
      "Unable to register any notification for the specified EventSpec");
}

Status ContainerImpl::UnregisterNotification(NotificationId notification_id) {
  RETURN_IF_ERROR(Exists());

  // If remove failed, there is no such notification.
  if (!active_notifications_->Remove(notification_id)) {
    return Status(
        ::util::error::INVALID_ARGUMENT,
        Substitute("No registered notification with NotificationId \"$0\"",
                   notification_id));
  }

  return Status::OK;
}

// TODO(vmarmol): Use Pause()/Resume() when that is available.
Status ContainerImpl::KillAll() {
  RETURN_IF_ERROR(Exists());

  // Send a SIGKILL to all processes.
  RETURN_IF_ERROR(KillTasks(LIST_PROCESSES));

  // At this point all the processes in the container have been killed. Any
  // remaining threads are "tourist threads." Kill the tourist threads.
  RETURN_IF_ERROR(KillTasks(LIST_THREADS));

  return Status::OK;
}

StatusOr<pid_t> ContainerImpl::GetInitPid() const {
  unique_ptr<NamespaceHandler> namespace_handler(
      RETURN_IF_ERROR(GetNamespaceHandler(name_)));
  return namespace_handler->GetInitPid();
}

StatusOr<vector<GeneralResourceHandler *>>
ContainerImpl::GetGeneralResourceHandlers() const {
  unique_ptr<NamespaceHandler> namespace_handler(
      RETURN_IF_ERROR(GetNamespaceHandler(name_)));
  auto statusor = GetResourceHandlersFor(name_, resource_factories_);
  if (!statusor.ok()) {
    return statusor.status();
  }
  auto &resource_handlers = statusor.ValueOrDie();
  vector<GeneralResourceHandler *> general_resource_handlers(
      resource_handlers.begin(),
      resource_handlers.end());
  general_resource_handlers.push_back(namespace_handler.release());
  return general_resource_handlers;
}

StatusOr<vector<ResourceHandler *>> ContainerImpl::GetResourceHandlers() const {
  return GetResourceHandlersFor(name_, resource_factories_);
}

StatusOr<vector<pid_t>> ContainerImpl::ListProcessesOrThreads(
    ListType type) const {
  if (type == LIST_PROCESSES) {
    return tasks_handler_->ListProcesses(TasksHandler::ListType::SELF);
  }

  return tasks_handler_->ListThreads(TasksHandler::ListType::SELF);
}

Status ContainerImpl::KillTasks(ListType type) const {
  StatusOr<vector<pid_t>> statusor;

  // Send signal until there are no more PIDs/TIDs or until num_tries times.
  int32 num_tries = FLAGS_lmctfy_num_tries_for_unkillable;
  while (num_tries > 0) {
    statusor = ListProcessesOrThreads(type);
    if (!statusor.ok()) {
      return statusor.status();
    }

    // If no PIDs/TIDs, we are done.
    if (statusor.ValueOrDie().empty()) {
      return Status::OK;
    }

    for (pid_t pid : statusor.ValueOrDie()) {
      kernel_->Kill(pid);
    }

    --num_tries;
    kernel_->Usleep(FLAGS_lmctfy_ms_delay_between_kills * 1000);
  }

  // Ensure that no PIDs/TIDs remain.
  statusor = ListProcessesOrThreads(type);
  if (!statusor.ok()) {
    return statusor.status();
  } else if (!statusor.ValueOrDie().empty()) {
    return Status(
        ::util::error::FAILED_PRECONDITION,
        Substitute(
            "Expected container \"$0\" to have no $1, has $2. Some may "
            "be unkillable",
            name_, type == LIST_PROCESSES ? "processes" : "threads",
            statusor.ValueOrDie().size()));
  }

  return Status::OK;
}

Status ContainerImpl::Exists() const {
  if (!lmctfy_->Exists(name_)) {
    return Status(::util::error::NOT_FOUND,
                  Substitute("Container \"$0\" does not exist", name_));
  }

  return Status::OK;
}

}  // namespace lmctfy
}  // namespace containers
