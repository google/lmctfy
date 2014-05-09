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

#ifndef SRC_RESOURCES_MEMORY_RESOURCE_HANDLER_H_
#define SRC_RESOURCES_MEMORY_RESOURCE_HANDLER_H_

#include <memory>
#include <string>
using ::std::string;

#include "base/macros.h"
#include "system_api/kernel_api.h"
#include "lmctfy/controllers/memory_controller.h"
#include "lmctfy/resources/cgroup_resource_handler.h"
#include "include/lmctfy.h"
#include "util/task/statusor.h"

namespace containers {
namespace lmctfy {

class CgroupFactory;
class ContainerSpec;
class ContainerStats;
class EventFdNotifications;
class ResourceHandler;

typedef ::system_api::KernelAPI KernelApi;


// Factory for MemoryResourceHandlers.
//
// Memory has a 1:1 mapping from container name to cgroup hierarchy.
//
// Class is thread-safe.
class MemoryResourceHandlerFactory : public CgroupResourceHandlerFactory {
 public:
  // Create an instance of this factory. If the resource is not supported on
  // this machine a NOT_FOUND error is returned. Does not take ownership of
  // any argument.
  static ::util::StatusOr<MemoryResourceHandlerFactory *> New(
      CgroupFactory *cgroup_factory, const KernelApi *kernel,
      EventFdNotifications *eventfd_notifications);

  // Takes ownership of memory_controller_factory. Does not own cgroup_factory
  // or kernel.
  MemoryResourceHandlerFactory(
      const MemoryControllerFactory *memory_controller_factory,
      CgroupFactory *cgroup_factory,
      const KernelApi *kernel);
  virtual ~MemoryResourceHandlerFactory() {}

 protected:
  virtual ::util::StatusOr<ResourceHandler *> GetResourceHandler(
      const string &container_name) const;
  virtual ::util::StatusOr<ResourceHandler *> CreateResourceHandler(
      const string &container_name, const ContainerSpec &spec) const;

 private:
  // Controller factory for memory cgroup controllers.
  const ::std::unique_ptr<const MemoryControllerFactory>
      memory_controller_factory_;

  friend class MemoryResourceHandlerFactoryTest;

  DISALLOW_COPY_AND_ASSIGN(MemoryResourceHandlerFactory);
};

// Resource handler for memory. Currently only does simple memory management
// used for subcontainers and only uses the memory cgroup hierarchy.
//
// Class is thread-safe.
class MemoryResourceHandler : public CgroupResourceHandler {
 public:
  // Does not own kernel. Takes ownership of memory_controller.
  MemoryResourceHandler(
      const string &container_name,
      const KernelApi *kernel,
      MemoryController *memory_controller);
  virtual ~MemoryResourceHandler() {}

  virtual ::util::Status CreateOnlySetup(const ContainerSpec &spec);
  virtual ::util::Status Update(const ContainerSpec &spec,
                                Container::UpdatePolicy policy);
  virtual ::util::Status Stats(Container::StatsType type,
                               ContainerStats *output) const;
  virtual ::util::Status Spec(ContainerSpec *spec) const;
  virtual ::util::StatusOr<Container::NotificationId> RegisterNotification(
      const EventSpec &spec, Callback1< ::util::Status> *callback);

 private:
  ::util::Status SetDirty(const MemorySpec_Dirty &dirty,
                          Container::UpdatePolicy policy);

  // Gets the dirty memory spec from the kernel and updates 'memory_spec'.
  ::util::Status GetDirtyMemorySpec(MemorySpec *memory_spec) const;

  // The Memory cgroup controller, it is owned by controllers.
  MemoryController *memory_controller_;

  DISALLOW_COPY_AND_ASSIGN(MemoryResourceHandler);
};

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_RESOURCES_MEMORY_RESOURCE_HANDLER_H_
