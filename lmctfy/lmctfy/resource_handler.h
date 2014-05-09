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

#ifndef SRC_RESOURCE_HANDLER_H_
#define SRC_RESOURCE_HANDLER_H_

#include <time.h>
#include <string>
using ::std::string;
#include <vector>

#include "base/callback.h"
#include "base/macros.h"
#include "lmctfy/general_resource_handler.h"
#include "include/config.pb.h"
#include "include/lmctfy.h"
#include "include/lmctfy.pb.h"
#include "util/safe_types/unix_gid.h"
#include "util/safe_types/unix_uid.h"
#include "util/task/statusor.h"
#include "util/task/status.h"

namespace containers {
namespace lmctfy {

class ResourceHandler;

// Factory for ResourceHandlers. For each ContainerApi instance there should only
// ever be one ResourceHandlerFactory per resource. Each container will get its
// own ResourceHandler for each resource. Thus the ResourceHandlerFactories
// implement any resource-specific global logic and the creation and
// initialization of the resource.
class ResourceHandlerFactory {
 public:
  virtual ~ResourceHandlerFactory() {}

  // Creates a Resource Handler for an existing container.
  //
  // Arguments:
  //   container_name: Absolute name of the container.
  // Return:
  //   StatusOr<ResourceHandler *>: Status of the operation. Iff OK, returns an
  //       instance of a handler for this factory's resource. Pointer is owned
  //       by the caller.
  virtual ::util::StatusOr<ResourceHandler *> Get(
      const string &container_name) = 0;

  // Creates a new Resource Handler with the specified spec. Only uses parts of
  // the spec that match the resource implemented.
  //
  // Arguments:
  //   container_name: Absolute name of the container.
  //   spec: Specification for the new ResourceHandler.
  // Return:
  //   StatusOr<ResourceHandler *>: Status of the operation. Iff OK, returns an
  //       instance of a handler for this factory's resource. Pointer is owned
  //       by the caller.
  virtual ::util::StatusOr<ResourceHandler *> Create(
      const string &container_name,
      const ContainerSpec &spec) = 0;

  // Initialize this resource handler on this machine. This setup is idempotent
  // and only needs to be done once at machine bootup.
  virtual ::util::Status InitMachine(const InitSpec &spec) = 0;

  // Returns the type or resource implemented by this ResourceHandlerFactory.
  ResourceType type() const { return type_; }

 protected:
  explicit ResourceHandlerFactory(ResourceType type) : type_(type) {}

 private:
  ResourceType type_;

  DISALLOW_COPY_AND_ASSIGN(ResourceHandlerFactory);
};

// A ResourceHandler is the resource-specific logic that exists with each
// container. Resources are things like CPU, memory, and network. Each resource
// implements its own Resource Handler and each Container that uses a resource
// will receive its own copy of the Resource Handler.
class ResourceHandler : public GeneralResourceHandler {
 public:
  virtual ~ResourceHandler() {}

  // Enters the specified TIDs into this Resource Handler.
  virtual ::util::Status Enter(const ::std::vector<pid_t> &tids) = 0;

  // Populates this resource's portion of the MachineSpec.
  virtual ::util::Status PopulateMachineSpec(MachineSpec *spec) const = 0;

 protected:
  ResourceHandler(const string &container_name, ResourceType type)
      : GeneralResourceHandler(container_name, type) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(ResourceHandler);
};

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_RESOURCE_HANDLER_H_
