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
    controllers_[controller->type()] = controller;
  }
}

CgroupResourceHandler::~CgroupResourceHandler() {
  STLDeleteValues(&controllers_);
}

Status CgroupResourceHandler::Create(const ContainerSpec &spec) {
  return Status::OK;
}

Status CgroupResourceHandler::Destroy() {
  Status status;

  // Destroy all controllers.
  for (auto it = controllers_.begin(); it != controllers_.end(); ++it) {
    status = it->second->Destroy();
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
    for (const auto &type_controller_pair : controllers_) {
      status = type_controller_pair.second->Enter(tid);
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

}  // namespace lmctfy
}  // namespace containers
