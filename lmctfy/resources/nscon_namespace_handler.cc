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

#include "lmctfy/resources/nscon_namespace_handler.h"

#include <memory>
#include <string>
using ::std::string;

#include "file/base/path.h"
#include "lmctfy/resource_handler.h"
#include "lmctfy/namespace_handler.h"
#include "nscon/namespace_controller_impl.h"
#include "nscon/ns_handle.h"
#include "include/lmctfy.pb.h"
#include "include/namespace_controller.h"
#include "include/namespaces.pb.h"
#include "lmctfy/util/console_util.h"
#include "util/errors.h"
#include "util/file_lines.h"
#include "strings/substitute.h"
#include "util/task/codes.pb.h"
#include "util/task/status.h"

using ::file::Dirname;
using ::containers::ConsoleUtil;
using ::containers::nscon::NamespaceController;
using ::containers::nscon::NamespaceControllerFactory;
using ::util::FileLines;
using ::util::UnixGid;
using ::util::UnixUid;
using ::std::unique_ptr;
using ::std::vector;
using ::strings::Substitute;
using ::util::error::INTERNAL;
using ::util::error::INVALID_ARGUMENT;
using ::util::error::NOT_FOUND;
using ::util::error::UNAVAILABLE;
using ::util::error::UNIMPLEMENTED;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {

StatusOr<NamespaceHandlerFactory *> NamespaceHandlerFactory::New(
    const TasksHandlerFactory *tasks_handler_factory) {
  NamespaceControllerFactory *namespace_controller_factory =
      RETURN_IF_ERROR(NamespaceControllerFactory::New());
  return new NsconNamespaceHandlerFactory(tasks_handler_factory,
                                          namespace_controller_factory,
                                          new ConsoleUtil());
}

StatusOr<NamespaceHandler *> NsconNamespaceHandlerFactory::GetNamespaceHandler(
    const string &container_name) const {
  // Ensure we only Get() Virtual Hosts and root.
  if (container_name != "/") {
    const bool is_virtual_host = RETURN_IF_ERROR(IsVirtualHost(container_name));
    if (!is_virtual_host) {
      return Status(NOT_FOUND, Substitute(
                                   "There is no NamespaceHandler for non "
                                   "Virtual Host container \"$0\"",
                                   container_name));
    }
  }

  const pid_t init_pid = RETURN_IF_ERROR(DetectInit(container_name));
  NamespaceController *namespace_controller =
      RETURN_IF_ERROR(namespace_controller_factory_->Get(init_pid));
  return new NsconNamespaceHandler(container_name, namespace_controller,
                                   namespace_controller_factory_.get());
}

StatusOr<NamespaceHandler *>
NsconNamespaceHandlerFactory::CreateNamespaceHandler(
    const string &container_name, const ContainerSpec &spec,
    const MachineSpec &machine_spec) {
  if (!spec.has_virtual_host()) {
    return Status(INVALID_ARGUMENT,
                  Substitute("No virtual host spec for container \"$0\"",
                             container_name));
  }
  // Enabling hierarchical namespaces is not yet supported.
  if (file::Dirname(container_name) != "/") {
    return Status(UNIMPLEMENTED,
                  "Nested Virtual Hosts are not yet supported.");
  }

  // If virtual host is specified, enable pid, ipc, and mount namespaces.
  // TODO(jnagal): Add support for other namespaces.
  nscon::NamespaceSpec namespace_spec;
  namespace_spec.mutable_pid();
  namespace_spec.mutable_ipc();
  namespace_spec.mutable_mnt();
  // Inherit all fds to preserve stdin, stdout and stderr.
  namespace_spec.mutable_run_spec()->set_inherit_fds(true);
  if (spec.virtual_host().has_network()) {
    namespace_spec.mutable_net()->CopyFrom(spec.virtual_host().network());
  }
  if (spec.virtual_host().has_init() &&
      spec.virtual_host().init().has_run_spec() &&
      spec.virtual_host().init().run_spec().has_console() &&
      spec.virtual_host().init().run_spec().console().has_slave_pty()) {
    namespace_spec.mutable_run_spec()->mutable_console()->set_slave_pty(
        spec.virtual_host().init().run_spec().console().slave_pty());
  }
  if (spec.has_filesystem()) {
    if (spec.filesystem().has_rootfs()) {
      namespace_spec.mutable_fs()->set_rootfs_path(spec.filesystem().rootfs());
    }
    if (spec.filesystem().has_mounts()) {
      namespace_spec.mutable_fs()->mutable_external_mounts()->CopyFrom(
          spec.filesystem().mounts());
    }
  }

  vector<string> init_argv(spec.virtual_host().init().init_argv().begin(),
                           spec.virtual_host().init().init_argv().end());

  namespace_spec.mutable_fs()->mutable_machine()->CopyFrom(machine_spec);

  unique_ptr<NamespaceController> namespace_controller(
      RETURN_IF_ERROR(namespace_controller_factory_->Create(namespace_spec,
                                                            init_argv)));
  return new NsconNamespaceHandler(container_name,
                                   namespace_controller.release(),
                                   namespace_controller_factory_.get());
}

// Gets a list of processes directly in the container, or in its subcontainers
// if none were found directly in it.
static StatusOr<vector<pid_t>> GetProcesses(const TasksHandler &handler) {
  StatusOr<vector<pid_t>> statusor =
      RETURN_IF_ERROR(handler.ListProcesses(TasksHandler::ListType::SELF));

  // If none were found directly in the container, try its subcontainers.
  if (statusor.ValueOrDie().empty()) {
    statusor = RETURN_IF_ERROR(
        handler.ListProcesses(TasksHandler::ListType::RECURSIVE));
  }

  return statusor;
}

// VirtualHosts are defined as top-level containers with namespaces. Root is not
// considered a VirtualHost. We assume that we are the only ones creating
// namespaces.
StatusOr<bool> NsconNamespaceHandlerFactory::IsVirtualHost(
    const string &container_name) const {
  // VirtualHosts are only top-level containers, not at root or subcontainers.
  if (container_name == "/" || container_name.find("/", 1) != string::npos) {
    return false;
  }

  // Get PIDs in the container.
  unique_ptr<const TasksHandler> tasks_handler(
      RETURN_IF_ERROR(tasks_handler_factory_->Get(container_name)));
  const vector<pid_t> pids = RETURN_IF_ERROR(GetProcesses(*tasks_handler));

  // If empty, there are no processes so thus not a VirtualHost (since
  // at least an init must exist in one).
  if (pids.empty()) {
    return false;
  }

  // Get the namespace ID of the current PID and a PID inside the container.
  // If they match, then the container is not a VirtualHost.
  const string container_namespace = RETURN_IF_ERROR(
      namespace_controller_factory_->GetNamespaceId(pids.front()));
  const string root_namespace =
      RETURN_IF_ERROR(namespace_controller_factory_->GetNamespaceId(0));
  return root_namespace != container_namespace;
}

// TODO(vmarmol): Use /proc/<pid>/stat instead.
// Get the parent PID of the specified PID. Returns NOT_FOUND if the parent was
// not found (may mean the specified PID no longer exists).
StatusOr<pid_t> NsconNamespaceHandlerFactory::GetParentPid(pid_t pid) const {
  static const char kPPid[] = "PPid:";
  const string proc_status = Substitute("/proc/$0/status", pid);

  // Go through /proc/<pid>/status to find the parent's PID.
  for (StringPiece line : FileLines(proc_status)) {
    if (line.starts_with(kPPid)) {
      StringPiece line_copy(line);
      line_copy.remove_prefix(strlen(kPPid));

      pid_t ppid;
      if (!SimpleAtoi(line_copy.ToString(), &ppid)) {
        return Status(INTERNAL,
                      Substitute("Cannot parse PID from line '$0' from '$1'",
                                 line, proc_status));
      }
      return ppid;
    }
  }

  return Status(NOT_FOUND,
                Substitute("Failed to find $0 in $1", kPPid, proc_status));
}

// This function returns NOT_FOUND whenever the crawl should be retried. This is
// usually due to a transient condition like PID dead.
StatusOr<pid_t> NsconNamespaceHandlerFactory::CrawlTreeToFindInit(
    const string &container_name, const string &root_namespace,
    const TasksHandler &tasks_handler) const {
  const vector<pid_t> pids = RETURN_IF_ERROR(GetProcesses(tasks_handler));
  if (pids.empty()) {
    return Status(INVALID_ARGUMENT,
                  "Container is not a VirtualHost, can't detect init. It may "
                  "have already died.");
  }

  pid_t previous = -1;
  pid_t current = pids.front();
  string current_namespace;
  while (current_namespace != root_namespace) {
    previous = current;

    // Get parent's PID. Returns NOT_FOUND if the PID died.
    current = RETURN_IF_ERROR(GetParentPid(current));

    // Get namespace ID. Returns NOT_FOUND if the PID died.
    current_namespace =
        RETURN_IF_ERROR(namespace_controller_factory_->GetNamespaceId(current));
  }

  // At this point current PID is the parent of the init in the namespace.
  const pid_t init_pid = previous;

  // There are a couple of cases wherein we may detect an init incorrectly so
  // lets verify that this is actually init by ensuring it is in the container
  // and that its parent is still the same one from the crawl (its PID may have
  // gotten reused).
  const pid_t init_parent = RETURN_IF_ERROR(GetParentPid(init_pid));
  if (init_parent != current) {
    // A crawl was racing with PID death, signal to retry.
    return Status(NOT_FOUND, Substitute(
                                 "Falsely detected $0 as init, but its parent "
                                 "was re-used during the crawl",
                                 init_pid));
  }
  const string init_container =
      RETURN_IF_ERROR(tasks_handler_factory_->Detect(init_pid));
  if (init_container != container_name) {
    // A crawl was racing with PID death, signal to retry.
    return Status(NOT_FOUND, Substitute(
                                 "Falsely detected $0 as init, it is container "
                                 "$1 and not in the target container $2",
                                 init_pid, init_container, container_name));
  }

  return init_pid;
}

// TODO(vmarmol): Improve this, the current implementation has some rare races.
// This function detects the init PID in a container by assuming that all PIDs
// in a VirtualHost container form a tree around init. Thus, by crawling the PID
// tree from child to parent, we'll get to init. We recognize it is init by
// looking for the last PID that is not in the root namespace. This function
// should only be called from the root namespace.
StatusOr<pid_t> NsconNamespaceHandlerFactory::DetectInit(
    const string &container_name) const {
  // init is 1 in the root.
  if (container_name == "/") {
    return 1;
  }

  unique_ptr<const TasksHandler> tasks_handler(
      RETURN_IF_ERROR(tasks_handler_factory_->Get(container_name)));

  const string root_namespace =
      RETURN_IF_ERROR(namespace_controller_factory_->GetNamespaceId(0));

  // Since any PID on our tree may die during the crawl, we retry the crawl in
  // those cases.
  static const int kMaxTries = 10;
  for (int i = 0; i < kMaxTries; ++i) {
    StatusOr<pid_t> statusor =
        CrawlTreeToFindInit(container_name, root_namespace, *tasks_handler);
    if (statusor.status().CanonicalCode() == NOT_FOUND) {
      // Some PID died during the crawl, we may need to restart the crawl.
      continue;
    } else if (!statusor.ok()) {
      return statusor.status();
    }

    return statusor.ValueOrDie();
  }

  return Status(UNAVAILABLE, "Ran out of tries while detecting init");
}

Status NsconNamespaceHandlerFactory::InitMachine(const InitSpec &spec) {
  // Setup devpts namespace support.
  return console_util_->EnableDevPtsNamespaceSupport();
}

Status NsconNamespaceHandler::Update(const ContainerSpec &spec,
                                     Container::UpdatePolicy policy) {
  // TODO(jnagal): Namespaces cannot be updated yet.
  if (spec.has_virtual_host()) {
    return Status(UNIMPLEMENTED,
                  "Updating VirtualHost is not currently implemented");
  }

  // Return success as there is nothing to do for the supported namespaces.
  return Status::OK;
}

Status NsconNamespaceHandler::Exec(const vector<string> &command) {
  if (command.empty()) {
    return Status(INVALID_ARGUMENT, "Command must not be empty");
  }

  RETURN_IF_ERROR(namespace_controller_->Exec(command));
  return Status(INTERNAL, "Exec failed");
}

StatusOr<pid_t> NsconNamespaceHandler::Run(const vector<string> &command,
                                          const RunSpec &spec) {
  if (command.empty()) {
    return Status(INVALID_ARGUMENT, "Command must not be empty");
  }

  // We need to convert lmctfy::RunSpec to nscon::RunSpec.
  nscon::RunSpec nscon_run_spec;
  if (spec.has_console() && spec.console().has_slave_pty()) {
    nscon_run_spec.mutable_console()->set_slave_pty(spec.console().slave_pty());
  }

  if (spec.has_fd_policy() && (spec.fd_policy() == RunSpec::INHERIT)) {
    nscon_run_spec.set_inherit_fds(true);
  }

  if (spec.has_apparmor_profile()) {
    nscon_run_spec.set_apparmor_profile(spec.apparmor_profile());
  }

  // TODO(adityakali): Set uid & gid in nscon_run_spec.
  return namespace_controller_->Run(command, nscon_run_spec);
}

Status NsconNamespaceHandler::Stats(Container::StatsType type,
                                   ContainerStats *output) const {
  return Status::OK;
}

Status NsconNamespaceHandler::Spec(ContainerSpec *spec) const {
  // Existence of namespace handler implies pid namespace is enabled.
  // Calling IsValid() on namespace controller might be misleading if the
  // process used to Get() the namespace is gone.
  spec->mutable_virtual_host();
  return Status::OK;
}

Status NsconNamespaceHandler::Destroy() {
  RETURN_IF_ERROR(namespace_controller_->Destroy());

  delete this;
  return Status::OK;
}

Status NsconNamespaceHandler::Delegate(UnixUid uid, UnixGid gid) {
  return Status::OK;
}

StatusOr<Container::NotificationId> NsconNamespaceHandler::RegisterNotification(
    const EventSpec &spec, Callback1< ::util::Status> *callback) {
  // Not supported.
  return Status(NOT_FOUND,
                "Notifications are not supported for namespaces.");
}

pid_t NsconNamespaceHandler::GetInitPid() const {
  return namespace_controller_->GetPid();
}

::util::StatusOr<bool> NsconNamespaceHandler::IsDifferentVirtualHost(
    const vector<pid_t> &tids) const {
  const string current_namespace = RETURN_IF_ERROR(
      namespace_controller_factory_->GetNamespaceId(GetInitPid()));

  // Ensure all the TIDs are in the same VirtualHost as the namespace's init.
  for (const pid_t tid : tids) {
    const string namespace_id =
        RETURN_IF_ERROR(namespace_controller_factory_->GetNamespaceId(tid));

    if (current_namespace != namespace_id) {
      return true;
    }
  }

  return false;
}

}  // namespace lmctfy
}  // namespace containers
