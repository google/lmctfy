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

#include "lmctfy/resources/null_namespace_handler.h"

#include <memory>
#include <string>
using ::std::string;

#include "base/logging.h"
#include "lmctfy/namespace_handler.h"
#include "util/errors.h"
#include "strings/substitute.h"
#include "util/task/codes.pb.h"
#include "util/task/status.h"

using ::util::UnixUid;
using ::util::UnixGid;
using ::std::unique_ptr;
using ::std::vector;
using ::strings::Substitute;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {

StatusOr<NamespaceHandlerFactory *> NamespaceHandlerFactory::NewNull(
    const KernelApi *kernel) {
  return new NullNamespaceHandlerFactory(kernel);
}

static SubProcess *NewSubprocess() { return new SubProcess(); }

NullNamespaceHandlerFactory::NullNamespaceHandlerFactory(
    const KernelApi *kernel)
    : kernel_(CHECK_NOTNULL(kernel)),
      subprocess_factory_(NewPermanentCallback(&NewSubprocess)) {}

StatusOr<NamespaceHandler *> NullNamespaceHandlerFactory::GetNamespaceHandler(
    const string &container_name) const {
  if (container_name != "/") {
    return Status(::util::error::NOT_FOUND,
                  "Virtual host is not isolated for " + container_name);
  }
  return new NullNamespaceHandler(container_name,
                                  kernel_,
                                  subprocess_factory_.get());
}

StatusOr<NamespaceHandler *>
NullNamespaceHandlerFactory::CreateNamespaceHandler(
    const string &container_name, const ContainerSpec &spec,
    const MachineSpec &machine_spec) {
  return Status(
      ::util::error::UNIMPLEMENTED,
      "Namespace creation with NullNamespaceHandler is not supported.");
}

Status NullNamespaceHandlerFactory::InitMachine(const InitSpec &spec) {
  return Status::OK;
}

NullNamespaceHandler::NullNamespaceHandler(
    const string &container_name,
    const KernelApi *kernel,
    SubProcessFactory *subprocess_factory)
    : NamespaceHandler(container_name, RESOURCE_VIRTUALHOST),
      kernel_(CHECK_NOTNULL(kernel)),
      subprocess_factory_(CHECK_NOTNULL(subprocess_factory)) {}

Status NullNamespaceHandler::CreateResource(const ContainerSpec &spec) {
  return Status::OK;
}

Status NullNamespaceHandler::Update(const ContainerSpec &spec,
                                    Container::UpdatePolicy policy) {
  return Status::OK;
}

Status NullNamespaceHandler::Exec(const vector<string> &command) {
  if (command.empty()) {
    return Status(::util::error::INVALID_ARGUMENT, "Command must not be empty");
  }

  // TODO(kyurtsever) Verify if these are necessary.
  // Clear timers, since they are preserved across an exec*().
  kernel_->SetITimer(ITIMER_REAL, nullptr, nullptr);
  kernel_->SetITimer(ITIMER_VIRTUAL, nullptr, nullptr);
  kernel_->SetITimer(ITIMER_PROF, nullptr, nullptr);

  kernel_->Execvp(command[0], command);

  return Status(::util::error::INTERNAL,
                Substitute("Exec failed with: $0", StrError(errno)));
}

StatusOr<pid_t> NullNamespaceHandler::Run(const vector<string> &command,
                                          const RunSpec &spec) {
  if (command.empty()) {
    return Status(::util::error::INVALID_ARGUMENT,
                  "Command must not be empty");
  }

  if (spec.has_fd_policy() && spec.fd_policy() == RunSpec::UNKNOWN) {
    return Status(::util::error::INVALID_ARGUMENT,
                  "Invalid FD policy: UNKNOWN");
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

  // Start running the command.
  if (!sp->Start()) {
    return Status(::util::error::FAILED_PRECONDITION,
                  "Failed to start a thread to run the specified command");
  }

  return sp->pid();
}

Status NullNamespaceHandler::Stats(Container::StatsType type,
                                   ContainerStats *output) const {
  return Status::OK;
}

Status NullNamespaceHandler::Spec(ContainerSpec *spec) const {
  return Status::OK;
}

Status NullNamespaceHandler::Destroy() {
  delete this;
  return Status::OK;
}

Status NullNamespaceHandler::Delegate(UnixUid uid, UnixGid gid) {
  return Status::OK;
}

StatusOr<Container::NotificationId> NullNamespaceHandler::RegisterNotification(
    const EventSpec &spec, Callback1< ::util::Status> *callback) {
  // Not supported.
  return Status(::util::error::NOT_FOUND,
                "Notifications are not supported for namespaces.");
}

pid_t NullNamespaceHandler::GetInitPid() const {
  return 1;
}

::util::StatusOr<bool> NullNamespaceHandler::IsDifferentVirtualHost(
    const vector<pid_t> &tids) const {
  return false;
}

}  // namespace lmctfy
}  // namespace containers
