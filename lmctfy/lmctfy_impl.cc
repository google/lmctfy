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
#include "lmctfy/controllers/job_controller.h"
#include "lmctfy/resources/cpu_resource_handler.h"
#include "lmctfy/resources/memory_resource_handler.h"
#include "lmctfy/resources/monitoring_resource_handler.h"
#include "lmctfy/tasks_handler.h"
#include "include/lmctfy.pb.h"
#include "util/errors.h"
#include "strings/split.h"
#include "strings/substitute.h"
#include "thread/thread.h"
#include "thread/thread_options.h"
#include "util/gtl/stl_util.h"
#include "util/process/subprocess.h"
#include "re2/re2.h"
#include "util/task/codes.pb.h"

DEFINE_int32(lmctfy_num_tries_for_unkillable, 3,
             "The number of times to try to kill a PID/TID before considering "
             "the PID/TID unkillable. See flag "
             "\"lmctfy_ms_delay_between_kills\" for the duration of the "
             "delay between tries.");

DEFINE_int32(lmctfy_ms_delay_between_kills, 250,
             "The number of milliseconds to wait between kill attempts.");

using ::util::EventfdListener;
using ::std::make_pair;
using ::std::map;
using ::std::queue;
using ::std::set;
using ::std::unique_ptr;
using ::std::vector;
using ::strings::Split;
using ::strings::Substitute;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {

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
  EXTRACT_RESOURCE(diskio, RESOURCE_DISKIO);
  EXTRACT_RESOURCE(network, RESOURCE_NETWORK);
  EXTRACT_RESOURCE(monitoring, RESOURCE_MONITORING);
  EXTRACT_RESOURCE(filesystem, RESOURCE_FILESYSTEM);

#undef EXTRACT_RESOURCE
}

// Creates a new SubProcess.
static SubProcess *NewSubprocess() { return new SubProcess(); }

// Appends the factory to output if the status is OK. Ignores those with status
// NOT_FOUND.
static Status AppendIfAvailable(
    const StatusOr<ResourceHandlerFactory *> &statusor,
    vector<ResourceHandlerFactory *> *output) {
  if (statusor.ok()) {
    output->push_back(statusor.ValueOrDie());
  } else if (statusor.status().error_code() != ::util::error::NOT_FOUND) {
    return statusor.status();
  }

  return Status::OK;
}

// Creates and returns factories for all supported ResourceHandlers.
static StatusOr<vector<ResourceHandlerFactory *>> CreateSupportedResources(
    CgroupFactory *cgroup_factory, const KernelApi *kernel,
    EventFdNotifications *eventfd_notifications) {
  // Create the available resource handlers.
  vector<ResourceHandlerFactory *> resource_factories;
  RETURN_IF_ERROR(
      AppendIfAvailable(CpuResourceHandlerFactory::New(cgroup_factory, kernel,
                                                       eventfd_notifications),
                        &resource_factories));
  RETURN_IF_ERROR(
      AppendIfAvailable(MemoryResourceHandlerFactory::New(
                            cgroup_factory, kernel, eventfd_notifications),
                        &resource_factories));
    RETURN_IF_ERROR(
      AppendIfAvailable(MonitoringResourceHandlerFactory::New(
                            cgroup_factory, kernel, eventfd_notifications),
                        &resource_factories));

  return resource_factories;
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
                                     eventfd_notifications),
        kernel);
  } else {
    return Status(::util::error::NOT_FOUND,
                  Substitute(
                      "lmctfy requires a canonical tasks cgroup hierarchy, "
                      "none were found"));
  }
}

// Takes ownership of cgroup_factory and kernel.
StatusOr<ContainerApiImpl *> ContainerApiImpl::NewContainerApiImpl(
    CgroupFactory *cgroup_factory, const KernelApi *kernel) {
  unique_ptr<CgroupFactory> cgroup_factory_deleter(cgroup_factory);
  unique_ptr<const KernelApi> kernel_deleter(kernel);

  // Create the notifications subsystem.
  unique_ptr<ActiveNotifications> active_notifications(
      new ActiveNotifications());
  unique_ptr<EventFdNotifications> eventfd_notifications(
      new EventFdNotifications(
          active_notifications.get(),
          new EventfdListener(*kernel, "lmctfy_eventfd_listener", nullptr,
                              false, 20)));

  // Create the resource handlers.
  vector<ResourceHandlerFactory *> resource_factories;
  ElementDeleter d(&resource_factories);
  RETURN_IF_ERROR(CreateSupportedResources(cgroup_factory, kernel,
                                           eventfd_notifications.get()),
                  &resource_factories);

  // Create the TasksHandler.
  unique_ptr<TasksHandlerFactory> tasks_handler_factory;
  RETURN_IF_ERROR(
      CreateTasksHandler(cgroup_factory, kernel, eventfd_notifications.get()),
      &tasks_handler_factory);

  // Relese all deleters and create the ContainerApi instance.
  cgroup_factory_deleter.release();
  kernel_deleter.release();
  vector<ResourceHandlerFactory *> resources_to_use;
  resources_to_use.swap(resource_factories);
  return new ContainerApiImpl(
      tasks_handler_factory.release(), cgroup_factory, resources_to_use, kernel,
      active_notifications.release(), eventfd_notifications.release());
}

// New() assumes that all cgroups are already mounted and automatically detects
// these mounts. It also checks if the machine has already been initialized.
StatusOr<ContainerApi *> ContainerApi::New() {
  const KernelApi *kernel = ::system_api::GlobalKernelApi();

  // TODO(vmarmol): Check that the machine has been initialized.

  // Auto-detect mount points for the cgroup hierarchies.
  CgroupFactory *cgroup_factory;
  RETURN_IF_ERROR(CgroupFactory::New(kernel), &cgroup_factory);

  return ContainerApiImpl::NewContainerApiImpl(cgroup_factory, kernel);
}

// InitMachine() is called at machine boot to mount all hierarchies needed by
// lmctfy.
Status ContainerApi::InitMachine(const InitSpec &spec) {
  return ContainerApiImpl::InitMachineImpl(::system_api::GlobalKernelApi(), spec);
}

// Does not take ownership of kernel.
Status ContainerApiImpl::InitMachineImpl(const KernelApi *kernel,
                                     const InitSpec &spec) {
  // Mount all the specified cgroups.
  unique_ptr<CgroupFactory> cgroup_factory;
  RETURN_IF_ERROR(CgroupFactory::New(kernel), &cgroup_factory);
  for (auto &mount : spec.cgroup_mount()) {
    RETURN_IF_ERROR(cgroup_factory->Mount(mount));
  }

  unique_ptr<ContainerApiImpl> lmctfy;
  RETURN_IF_ERROR(
      ContainerApiImpl::NewContainerApiImpl(cgroup_factory.release(), kernel),
      &lmctfy);

  // Init the machine. This initializes all the handlers.
  return lmctfy->InitMachine(spec);
}

ContainerApiImpl::ContainerApiImpl(
    TasksHandlerFactory *tasks_handler_factory,
    const CgroupFactory *cgroup_factory,
    const vector<ResourceHandlerFactory *> &resource_factories,
    const KernelApi *kernel, ActiveNotifications *active_notifications,
    EventFdNotifications *eventfd_notifications)
    : tasks_handler_factory_(CHECK_NOTNULL(tasks_handler_factory)),
      kernel_(CHECK_NOTNULL(kernel)),
      subprocess_factory_(NewPermanentCallback(&NewSubprocess)),
      cgroup_factory_(CHECK_NOTNULL(cgroup_factory)),
      active_notifications_(CHECK_NOTNULL(active_notifications)),
      eventfd_notifications_(CHECK_NOTNULL(eventfd_notifications)) {
  // Map each Resource Handler to the resource type
  for (const auto &handler : resource_factories) {
    resource_factories_[handler->type()] = handler;
  }
}

ContainerApiImpl::~ContainerApiImpl() { STLDeleteValues(&resource_factories_); }

StatusOr<Container *> ContainerApiImpl::Get(StringPiece container_name) const {
  // Resolve the container name.
  const string resolved_name =
      XRETURN_IF_ERROR(ResolveContainerName(container_name));

  // Ensure it exists.
  if (!Exists(resolved_name)) {
    return Status(
        ::util::error::NOT_FOUND,
        Substitute("Can't get non-existent container \"$0\"", resolved_name));
  }

  // Get the tasks handler for this container.
  unique_ptr<TasksHandler> tasks_handler(
      XRETURN_IF_ERROR(tasks_handler_factory_->Get(resolved_name)));

  return new ContainerImpl(
      resolved_name, tasks_handler.release(), resource_factories_, this,
      kernel_, subprocess_factory_.get(), active_notifications_.get());
}

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
      XRETURN_IF_ERROR(ResolveContainerName(container_name));

  // Ensure the container doesn't already exist.
  if (Exists(resolved_name)) {
    return Status(
        ::util::error::ALREADY_EXISTS,
        Substitute("Can't create existing container \"$0\"", resolved_name));
  }

  // Create the tasks handler for this container
  unique_ptr<TasksHandler> tasks_handler(
      XRETURN_IF_ERROR(tasks_handler_factory_->Create(resolved_name, spec)));

  // We will locally create the resource handlers we need.
  vector<ResourceHandler *> resource_handlers;
  ElementDeleter d(&resource_handlers);

  // Create resource handlers for this container.
  const string kParentName = file::Dirname(resolved_name).ToString();
  StatusOr<ResourceHandler *> statusor;
  for (auto type_handler_pair : resource_factories_) {
    // Don't create resources that were not specified.
    if (used_handler_factories.find(type_handler_pair.second) ==
        used_handler_factories.end()) {
      continue;
    }

    statusor = type_handler_pair.second->Create(resolved_name, spec);
    if (!statusor.ok()) {
      // Destroy the resource handlers we already created.
      for (auto it = resource_handlers.begin(); it != resource_handlers.end();
           ++it) {
        // Only destroy the resources we just created, not those we attached to
        // one of our ancestors.
        if (resolved_name == (*it)->container_name()) {
          // A successful destroy deletes the handler so remove it from
          // resource_handlers since that will delete all its elements..
          if ((*it)->Destroy().ok()) {
            it = resource_handlers.erase(it);
            --it;
          }
        }
      }
      return statusor.status();
    }

    resource_handlers.push_back(statusor.ValueOrDie());
  }

  return StatusOr<Container *>(new ContainerImpl(
      resolved_name, tasks_handler.release(), resource_factories_, this,
      kernel_, subprocess_factory_.get(), active_notifications_.get()));
}

Status ContainerApiImpl::Destroy(Container *container) const {
  // Get all subcontainers to destroy them.
  vector<Container *> subcontainers =
      XRETURN_IF_ERROR(container->ListSubcontainers(Container::LIST_RECURSIVE));

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
  // Initialize the resource handlers.
  for (auto type_handler_pair : resource_factories_) {
    RETURN_IF_ERROR(type_handler_pair.second->InitMachine(spec));
  }

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
    const string detected_name = XRETURN_IF_ERROR(Detect(0));
    resolved_name = ::file::JoinPath(detected_name, resolved_name);
  }

  resolved_name = ::File::CleanPath(resolved_name);

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
                             SubProcessFactory *subprocess_factory,
                             ActiveNotifications *active_notifications)
    : Container(name),
      tasks_handler_(CHECK_NOTNULL(tasks_handler)),
      resource_factories_(resource_factories),
      lmctfy_(CHECK_NOTNULL(lmctfy)),
      kernel_(CHECK_NOTNULL(kernel)),
      subprocess_factory_(subprocess_factory),
      active_notifications_(active_notifications) {}

ContainerImpl::~ContainerImpl() {}

Status ContainerImpl::Update(const ContainerSpec &spec, UpdatePolicy policy) {
  RETURN_IF_ERROR(Exists());

  // Get all resources and map them by type.
  map<ResourceType, ResourceHandler *> all_handlers;
  for (ResourceHandler *handler : XRETURN_IF_ERROR(GetResourceHandlers())) {
    all_handlers[handler->type()] = handler;
  }
  ValueDeleter d(&all_handlers);

  // Get resources used in the spec.
  set<ResourceHandler *> used_handlers;
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
  for (ResourceHandler *handler : used_handlers) {
    RETURN_IF_ERROR(handler->Update(spec, policy));
  }

  return Status::OK;
}

Status ContainerImpl::Destroy() {
  RETURN_IF_ERROR(Exists());

  // Ensure the container is empty (no tasks).
  RETURN_IF_ERROR(KillAll());

  // Get and destroy all resources.
  vector<ResourceHandler *> handlers = XRETURN_IF_ERROR(GetResourceHandlers());
  ElementDeleter d(&handlers);
  while (!handlers.empty()) {
    ResourceHandler *handler = handlers.back();

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

  return Status::OK;
}

Status ContainerImpl::Enter(const vector<pid_t> &tids) {
  RETURN_IF_ERROR(Exists());

  Status status = tasks_handler_->TrackTasks(tids);
  if (!status.ok()) {
    return status;
  }

  // Generate resource handlers and enter tids into them.
  StatusOr<vector<ResourceHandler *>> statusor = GetResourceHandlers();
  if (!statusor.ok()) {
    return statusor.status();
  }
  vector<ResourceHandler *> handlers = statusor.ValueOrDie();
  ElementDeleter d(&handlers);

  for (ResourceHandler *handler : handlers) {
    status = handler->Enter(tids);
    if (!status.ok()) {
      return status;
    }
  }

  return Status::OK;
}

void ContainerImpl::EnterAndRun(SubProcess *sp, Status *status) {
  // Enter into the container so that the command we start is run inside this
  // container.
  *status = Enter(vector<pid_t>(1, kernel_->GetTID()));
  if (!status->ok()) {
    return;
  }

  // Start running the command.
  if (!sp->Start()) {
    *status = Status(::util::error::FAILED_PRECONDITION,
                     "Failed to start a thread to run the specified command");
    return;
  }

  *status = Status::OK;
}

StatusOr<pid_t> ContainerImpl::Run(const vector<string> &command,
                                   const RunSpec &spec) {
  RETURN_IF_ERROR(Exists());

  // Check usage.
  if (spec.has_fd_policy() && spec.fd_policy() == RunSpec::UNKNOWN) {
    return Status(::util::error::INVALID_ARGUMENT,
                  "Invalid FD policy: UNKNOWN");
  }
  if (command.empty()) {
    return Status(::util::error::INVALID_ARGUMENT, "Command must not be empty");
  }

  Status status;
  unique_ptr<SubProcess> sp(subprocess_factory_->Run());

  // Get ready to run the command.
  sp->SetArgv(command);

  if (!spec.has_fd_policy() || spec.fd_policy() == RunSpec::INHERIT) {
    // Retain all file descriptors.
    sp->SetChannelAction(CHAN_STDIN, ACTION_DUPPARENT);
    sp->SetChannelAction(CHAN_STDOUT, ACTION_DUPPARENT);
    sp->SetChannelAction(CHAN_STDERR, ACTION_DUPPARENT);
    sp->SetInheritHigherFDs(true);
  } else {
    // Start the command in a new session.
    sp->SetUseSession();
  }

  // Create a thread, enter the thread into this container, and run the
  // command. This is so that all accounting is properly done without having
  // to move the user's thread into this container or doing some work between
  // fork and exec.
  ::thread::Options options;
  options.set_joinable(true);
  ClosureThread thread(options, "lmctfy-enter",
                       NewPermanentCallback(this, &ContainerImpl::EnterAndRun,
                                            sp.get(), &status));

  thread.Start();
  thread.Join();

  // Ensure that EnterAndRun() succeeded.
  RETURN_IF_ERROR(status);

  return sp->pid();
}

StatusOr<ContainerSpec> ContainerImpl::Spec() const {
  RETURN_IF_ERROR(Exists());

  // Generate resource handlers.
  vector<ResourceHandler *> handlers = XRETURN_IF_ERROR(GetResourceHandlers());
  ElementDeleter d(&handlers);

  // Get the spec from each ResourceHandler attached to this container..
  ContainerSpec spec;
  for (ResourceHandler *handler : handlers) {
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

  // Enter into the container so that the command we start is run inside this
  // container.
  RETURN_IF_ERROR(Enter({0}));

  // Clear timers, since they are preserved across an exec*().
  kernel_->SetITimer(ITIMER_REAL, nullptr, nullptr);
  kernel_->SetITimer(ITIMER_VIRTUAL, nullptr, nullptr);
  kernel_->SetITimer(ITIMER_PROF, nullptr, nullptr);
  // Execute the command.
  kernel_->Execvp(command[0], command);

  // This only happens on error.
  return Status(::util::error::INTERNAL,
                Substitute("Exec failed with: $0", strerror(errno)));
}

// Sort the containers by name ascending.
static bool CompareContainerByName(Container *container1,
                                   Container *container2) {
  return container1->name() < container2->name();
}

StatusOr<vector<Container *>> ContainerImpl::ListSubcontainers(
    ListPolicy policy) const {
  RETURN_IF_ERROR(Exists());

  // Sort the containers by name.
  vector<Container *> containers =
      XRETURN_IF_ERROR(ListSubcontainersUnsorted(policy));
  sort(containers.begin(), containers.end(), CompareContainerByName);
  return containers;
}

StatusOr<vector<Container *>> ContainerImpl::ListSubcontainersUnsorted(
    ListPolicy policy) const {
  Status status;

  // Keep a separate list of subcontainers so we can delete them in the case of
  // an error.
  vector<Container *> subcontainers;
  ElementDeleter d(&subcontainers);

  if (policy == Container::LIST_SELF) {
    // Get the subcontainer names.
    StatusOr<vector<string>> statusor = tasks_handler_->ListSubcontainers();
    if (!statusor.ok()) {
      return statusor.status();
    }

    // Attach to those containers.
    for (const string &subcontainer_name : statusor.ValueOrDie()) {
      subcontainers.push_back(
          XRETURN_IF_ERROR(lmctfy_->Get(subcontainer_name)));
    }
  } else {
    queue<const Container *> to_check;

    // While there are subcontainers to check.
    to_check.push(this);
    while (!to_check.empty()) {
      // Get this container's subcontainers.
      StatusOr<vector<Container *>> statusor =
          to_check.front()->ListSubcontainers(Container::LIST_SELF);
      if (!statusor.ok()) {
        return statusor;
      }

      // Add new subcontainers to the queue to be checked.
      for (const Container *subcontainer : statusor.ValueOrDie()) {
        to_check.push(subcontainer);
      }
      subcontainers.insert(subcontainers.end(), statusor.ValueOrDie().begin(),
                           statusor.ValueOrDie().end());

      to_check.pop();
    }
  }

  // TODO(vmarmol): Use ScopedCleanup<DeleterElements> when that is available.
  // Add to output and don't delete the created containers since the operation
  // was successful.
  vector<Container *> output;
  output.swap(subcontainers);
  subcontainers.clear();

  return output;
}

StatusOr<vector<pid_t>> ContainerImpl::ListThreads(ListPolicy policy) const {
  return ListProcessesOrThreads(policy, ContainerImpl::LIST_THREADS);
}

StatusOr<vector<pid_t>> ContainerImpl::ListProcesses(ListPolicy policy) const {
  return ListProcessesOrThreads(policy, ContainerImpl::LIST_PROCESSES);
}

Status ContainerImpl::Pause() {
  // TODO(vmarmol): Implement.
  LOG(DFATAL) << "Unimplemented";
  return Status(::util::error::UNIMPLEMENTED, "Unimplemented");
}

Status ContainerImpl::Resume() {
  // TODO(vmarmol): Implement.
  LOG(DFATAL) << "Unimplemented";
  return Status(::util::error::UNIMPLEMENTED, "Unimplemented");
}

StatusOr<ContainerStats> ContainerImpl::Stats(StatsType stats_type) const {
  RETURN_IF_ERROR(Exists());

  ContainerStats stats;

  // Get all resource handlers.
  vector<ResourceHandler *> handlers = XRETURN_IF_ERROR(GetResourceHandlers());
  ElementDeleter d(&handlers);

  // Get stats from each resource.
  for (ResourceHandler *handler : handlers) {
    // Only get stats for the resources attached to this container.
    if (name_ == handler->container_name()) {
      RETURN_IF_ERROR(handler->Stats(stats_type, &stats));
    }
  }

  return stats;
}

void ContainerImpl::HandleNotification(
    shared_ptr<Container::EventCallback> callback, Status status) {
  callback->Run(this, status);
}

StatusOr<Container::NotificationId> ContainerImpl::RegisterNotification(
    const EventSpec &spec, Container::EventCallback *callback) {
  CHECK_NOTNULL(callback);
  callback->CheckIsRepeatable();

  // A shared pointer is used since this callback is wrapped in the
  // HandleNotification() permanent callback. It is deleted when that callback
  // is deleted.
  shared_ptr<Container::EventCallback> user_callback(callback);

  RETURN_IF_ERROR(Exists());

  // Get all resource handlers.
  vector<ResourceHandler *> handlers;
  RETURN_IF_ERROR(GetResourceHandlers(), &handlers);
  ElementDeleter d(&handlers);

  // Register notification (only one notification is specified per request).
  StatusOr<Container::NotificationId> statusor;
  for (ResourceHandler *handler : handlers) {
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

StatusOr<vector<ResourceHandler *>> ContainerImpl::GetResourceHandlers() const {
  vector<ResourceHandler *> handlers;
  StatusOr<ResourceHandler *> statusor;
  ElementDeleter d(&handlers);

  // Attach to all factories.
  for (const auto &type_factory_pair : resource_factories_) {
    // Try to progressively attach to the parent container's resource if the
    // child container's resource is not found.
    string name = name_;
    bool has_parent = true;
    do {
      has_parent = (name != "/");
      statusor = type_factory_pair.second->Get(name);

      // Stop searching if it was found. If there was an error or if we reached
      // the root and got any error, return the error. We only continue looking
      // when non-root containers were not found.
      if (statusor.ok()) {
        break;
      } else if ((statusor.status().error_code() != ::util::error::NOT_FOUND) ||
                 !has_parent) {
        return statusor.status();
      }

      // This resource was not found in this container, try to use the parent
      // (if there is one).
      name = file::Dirname(name).ToString();
    } while (has_parent);

    handlers.push_back(statusor.ValueOrDie());
  }

  // TODO(vmarmol): Use ScopedCleanup<DeleteElements> when that is available.
  vector<ResourceHandler *> output;
  output.swap(handlers);
  return output;
}

StatusOr<vector<pid_t>> ContainerImpl::ListProcessesOrThreads(
    ListPolicy policy, ListType type) const {
  RETURN_IF_ERROR(Exists());

  // Get the processes/threads of this container.
  vector<pid_t> output;
  if (type == ContainerImpl::LIST_PROCESSES) {
    output = XRETURN_IF_ERROR(tasks_handler_->ListProcesses());
  } else {
    output = XRETURN_IF_ERROR(tasks_handler_->ListThreads());
  }

  // Get the processes/threads of subcontainers if asked.
  if (policy == Container::LIST_RECURSIVE) {
    // Get all subcontainers.
    StatusOr<vector<Container *>> statusor_containers =
        ListSubcontainers(Container::LIST_RECURSIVE);
    if (!statusor_containers.ok()) {
      return statusor_containers.status();
    }

    // Get processes/threads list from each of the subcontainers
    StatusOr<vector<pid_t>> statusor;
    set<pid_t> unique_pids(output.begin(), output.end());
    for (const Container *subcontainer : statusor_containers.ValueOrDie()) {
      if (type == ContainerImpl::LIST_PROCESSES) {
        statusor = subcontainer->ListProcesses(Container::LIST_SELF);
      } else {
        statusor = subcontainer->ListThreads(Container::LIST_SELF);
      }
      if (!statusor.ok()) {
        return statusor.status();
      }

      // Aggregate PIDs/TIDS uniquely. Although TasksHandler guarantees that no
      // PID/TID will be in two containers at the same time, our queries to the
      // handler are not atomic so PIDs/TIDs may have moved since the last
      // query.
      unique_pids.insert(statusor.ValueOrDie().begin(),
                         statusor.ValueOrDie().end());
    }

    output.assign(unique_pids.begin(), unique_pids.end());
  }

  return output;
}

Status ContainerImpl::KillTasks(ListType type) const {
  StatusOr<vector<pid_t>> statusor;

  // Send signal until there are no more PIDs/TIDs or until num_tries times.
  int32 num_tries = FLAGS_lmctfy_num_tries_for_unkillable;
  while (num_tries > 0) {
    statusor = ListProcessesOrThreads(LIST_SELF, type);
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
  statusor = ListProcessesOrThreads(LIST_SELF, type);
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
