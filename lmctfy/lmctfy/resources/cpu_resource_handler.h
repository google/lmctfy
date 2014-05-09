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

#ifndef SRC_RESOURCES_CPU_RESOURCE_HANDLER_H_
#define SRC_RESOURCES_CPU_RESOURCE_HANDLER_H_

#include <memory>
#include <string>
using ::std::string;

#include "base/macros.h"
#include "system_api/kernel_api.h"
#include "lmctfy/controllers/cpu_controller.h"
#include "lmctfy/controllers/cpuacct_controller.h"
#include "lmctfy/controllers/cpuset_controller.h"
#include "lmctfy/resources/cgroup_resource_handler.h"
#include "include/lmctfy.h"
#include "util/task/statusor.h"


namespace containers {

class InitSpec;

namespace lmctfy {

class ContainerSpec;
class CgroupFactory;
class ContainerStats;
class EventFdNotifications;
class ResourceHandler;

typedef ::system_api::KernelAPI KernelApi;


// Factory for CpuResourceHandlers.
//
// Class is thread-safe.
class CpuResourceHandlerFactory : public CgroupResourceHandlerFactory {
 public:
  // Create an instance of this factory. If the resource is not supported on
  // this machine a NOT_FOUND error is returned. The resource supports
  // functioning without support for cpuset as is typically the case in user
  // subcontainers. Does not take ownership of any argument.
  static ::util::StatusOr<CpuResourceHandlerFactory *> New(
      CgroupFactory *cgroup_factory, const KernelApi *kernel,
      EventFdNotifications *eventfd_notifications);

  // Takes ownership of all cpu related controller factories.
  // Does not own cgroup_factory or kernel. cpuset_controller_factory may be
  // null if not available.
  CpuResourceHandlerFactory(
      const CpuControllerFactory *cpu_controller_factory,
      const CpuAcctControllerFactory *cpuactt_controller_factory,
      const CpusetControllerFactory *cpuset_controller_factory,
      CgroupFactory *cgroup_factory,
      const KernelApi *kernel);
  virtual ~CpuResourceHandlerFactory() {}

 protected:
  virtual ::util::StatusOr<ResourceHandler *> GetResourceHandler(
      const string &container_name) const override;
  virtual ::util::StatusOr<ResourceHandler *> CreateResourceHandler(
      const string &container_name, const ContainerSpec &spec) const override;
  virtual ::util::Status InitMachine(const InitSpec &spec) override;

 private:
  // Controller factory for cpu cgroup controllers. Cpuset may be null if it is
  // not available.
  const ::std::unique_ptr<const CpuControllerFactory>
      cpu_controller_factory_;
  const ::std::unique_ptr<const CpuAcctControllerFactory>
      cpuacct_controller_factory_;
  const ::std::unique_ptr<const CpusetControllerFactory>
      cpuset_controller_factory_;

  friend class CpuResourceHandlerFactoryTest;

  DISALLOW_COPY_AND_ASSIGN(CpuResourceHandlerFactory);
};

// Resource handler for cpu. Handles latency and throughput, accounting and
// affinity.
//
// Class is thread-safe.
class CpuResourceHandler : public CgroupResourceHandler {
 public:
  // Does not own kernel. Takes ownership of cpu_controller, cpuacct_controller,
  // and cpuset_controller. cpuset_controller may be null if it is not
  // available.
  CpuResourceHandler(
      const string &container_name,
      const KernelApi *kernel,
      CpuController *cpu_controller,
      CpuAcctController *cpuacct_controller,
      CpusetController *cpuset_controller);
  virtual ~CpuResourceHandler() {}

  // Configure a newly created container with initial spec.
  virtual ::util::Status CreateOnlySetup(const ContainerSpec &spec);
  // Update a container config.
  virtual ::util::Status Update(const ContainerSpec &spec,
                                Container::UpdatePolicy policy);
  // Get Stats for an existing container.
  virtual ::util::Status Stats(Container::StatsType type,
                               ContainerStats *output) const;
  // Get Spec for the existing container.
  virtual ::util::Status Spec(ContainerSpec *spec) const;
  // Register for events of interest.
  virtual ::util::StatusOr<Container::NotificationId> RegisterNotification(
      const EventSpec &spec, Callback1< ::util::Status> *callback);

 private:
  // cpu controller manager latency and throughput.
  CpuController *cpu_controller_;
  CpuAcctController *cpuacct_controller_;
  CpusetController *cpuset_controller_;

  DISALLOW_COPY_AND_ASSIGN(CpuResourceHandler);
};

}  // namespace lmctfy
}  // namespace containers
#endif  // SRC_RESOURCES_CPU_RESOURCE_HANDLER_H_
