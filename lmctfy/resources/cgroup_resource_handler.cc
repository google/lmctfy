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

#include "lmctfy/resources/cgroup_resource_handler.h"

#include <memory>
#include <string>
using ::std::string;
#include <utility>
#include <vector>

#include "base/logging.h"
#include "file/base/path.h"
#include "lmctfy/controllers/cgroup_controller.h"
#include "lmctfy/controllers/cgroup_factory.h"
#include "util/errors.h"
#include "strings/strcat.h"
#include "strings/substitute.h"
#include "util/gtl/stl_util.h"
#include "util/task/status.h"

using ::file::JoinPath;
using ::std::unique_ptr;
using ::strings::Substitute;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {

CgroupResourceHandlerFactory::CgroupResourceHandlerFactory(
    ResourceType resource_type, CgroupFactory *cgroup_factory,
    const KernelApi *kernel)
    : ResourceHandlerFactory(resource_type), kernel_(CHECK_NOTNULL(kernel)) {}

StatusOr<ResourceHandler *> CgroupResourceHandlerFactory::Get(
    const string &container_name) {
  return GetResourceHandler(container_name);
}

StatusOr<ResourceHandler *> CgroupResourceHandlerFactory::Create(
      const string &container_name,
      const ContainerSpec &spec) {
  // Create the ResourceHandler for the container.
  StatusOr<ResourceHandler *> statusor_handler =
      CreateResourceHandler(container_name, spec);
  if (!statusor_handler.ok()) {
    return statusor_handler.status();
  }
  unique_ptr<ResourceHandler> handler(statusor_handler.ValueOrDie());

  // Run the create action before applying the update.
  RETURN_IF_ERROR(handler->CreateResource(spec));

  // Prepare the container by doing a replace update.
  Status status = handler->Update(spec, Container::UPDATE_REPLACE);
  if (!status.ok()) {
    return status;
  }

  return handler.release();
}

CgroupResourceHandler::CgroupResourceHandler(
    const string &container_name,
    ResourceType resource_type,
    const KernelApi *kernel,
    const vector<CgroupController *> &controllers)
    : ResourceHandler(container_name, resource_type),
      kernel_(CHECK_NOTNULL(kernel)) {
  // Map all controllers by type.
  for (CgroupController *controller : controllers) {
    controllers_.push_back(controller);
  }
}

CgroupResourceHandler::~CgroupResourceHandler() {
  STLDeleteElements(&controllers_);
}

Status CgroupResourceHandler::CreateResource(const ContainerSpec &spec) {
  for (auto controller : controllers_) {
    // TODO(jonathanw): Remove the if and use default once we have handed
    // ownership of this to lmctfy.
    if (spec.has_children_limit()) {
      RETURN_IF_ERROR(controller->SetChildrenLimit(spec.children_limit()));
    }
  }
  return CreateOnlySetup(spec);
}

Status CgroupResourceHandler::Destroy() {
  Status status;

  // Destroy all controllers.
  for (auto it = controllers_.begin(); it != controllers_.end(); ++it) {
    status = (*it)->Destroy();
    if (!status.ok()) {
      // Erase the controllers we deleted already.
      controllers_.erase(controllers_.begin(), it);
      return status;
    }
  }

  controllers_.clear();
  delete this;
  return Status::OK;
}

Status CgroupResourceHandler::Enter(const vector<pid_t> &tids) {
  const char kAlreadyTrackedError[] = " some TIDs were tracked before this "
      "error, container may be left in an inconsistent state";

  // Enter all TIDs.
  Status status;
  bool some_tracked = false;
  for (pid_t tid : tids) {
    // Enter into all controllers.
    for (const auto controller : controllers_) {
      status = controller->Enter(tid);
      if (!status.ok()) {
        return Status(status.CanonicalCode(),
                      Substitute("$0$1", status.error_message(),
                                 some_tracked ? kAlreadyTrackedError : ""));
      }
      some_tracked = true;
    }
  }

  return Status::OK;
}

Status CgroupResourceHandler::Delegate(::util::UnixUid uid,
                                       ::util::UnixGid gid) {
  // Delegate all the controllers.
  for (const auto controller : controllers_) {
    RETURN_IF_ERROR(controller->Delegate(uid, gid));
  }

  return Status::OK;
}

Status CgroupResourceHandler::Adjust(
    Container::UpdatePolicy policy,
    ContainerSpec *update_spec) const {
  switch (policy) {
    case Container::UpdatePolicy::UPDATE_DIFF:
      return Status::OK;
    case Container::UpdatePolicy::UPDATE_REPLACE:
      RecursiveFillDefaults(update_spec);
      return Status::OK;
  }
  return Status(util::error::INVALID_ARGUMENT,
                StrCat("Unrecognised update policy specified: ", policy));
}

Status CgroupResourceHandler::Validate(const ContainerSpec &update_spec) const {
  ContainerSpec spec_after_update = update_spec;
  RETURN_IF_ERROR(Spec(&spec_after_update));
  return VerifyFullSpec(spec_after_update);
}

Status CgroupResourceHandler::Update(const ContainerSpec &spec,
                                     Container::UpdatePolicy policy) {
  ContainerSpec adjusted_spec = spec;
  RETURN_IF_ERROR(Adjust(policy, &adjusted_spec));
  RETURN_IF_ERROR(Validate(adjusted_spec));
  return DoUpdate(adjusted_spec);
}

Status CgroupResourceHandler::DoUpdate(const ContainerSpec &spec) {
  return Status(util::error::UNIMPLEMENTED, __func__);
}

void CgroupResourceHandler::RecursiveFillDefaults(ContainerSpec *spec) const {
}

Status CgroupResourceHandler::VerifyFullSpec(const ContainerSpec &spec) const {
  return Status(util::error::UNIMPLEMENTED, __func__);
}

Status CgroupResourceHandler::PopulateMachineSpec(MachineSpec *spec) const {
  for (const auto controller : controllers_) {
    RETURN_IF_ERROR(controller->PopulateMachineSpec(spec));
  }
  return Status::OK;
}

}  // namespace lmctfy
}  // namespace containers
