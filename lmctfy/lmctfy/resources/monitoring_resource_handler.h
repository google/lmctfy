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

#ifndef SRC_RESOURCES_MONITORING_RESOURCE_HANDLER_H_
#define SRC_RESOURCES_MONITORING_RESOURCE_HANDLER_H_

#include <memory>

#include "lmctfy/controllers/perf_controller.h"
#include "lmctfy/resources/cgroup_resource_handler.h"

namespace containers {
namespace lmctfy {

class CgroupFactory;
class EventFdNotifications;

typedef ::system_api::KernelAPI KernelApi;

class MonitoringResourceHandlerFactory : public CgroupResourceHandlerFactory {
 public:
  // Create an instance of this factory. If the resource is not supported on
  // this machine a NOT_FOUND error is returned. Does not take ownership of
  // any argument.
  static ::util::StatusOr<MonitoringResourceHandlerFactory *> New(
      CgroupFactory *cgroup_factory, const KernelApi *kernel,
      EventFdNotifications *eventfd_notifications);

  // Takes ownership of perf_controller_factory. Does not own cgroup_factory or
  // kernel.
  MonitoringResourceHandlerFactory(
      const PerfControllerFactory *perf_controller_factory,
      CgroupFactory *cgroup_factory, const KernelApi *kernel);
  virtual ~MonitoringResourceHandlerFactory() {}

 protected:
  virtual ::util::StatusOr<ResourceHandler *> GetResourceHandler(
      const string &container_name) const;
  virtual ::util::StatusOr<ResourceHandler *> CreateResourceHandler(
      const string &container_name, const ContainerSpec &spec) const;

 private:
  const ::std::unique_ptr<const PerfControllerFactory>
      perf_controller_factory_;

  friend class MonitoringResourceHandlerFactoryTest;

  DISALLOW_COPY_AND_ASSIGN(MonitoringResourceHandlerFactory);
};

class MonitoringResourceHandler : public CgroupResourceHandler {
 public:
  // Does not own kernel. Takes ownership of perf_controller.
  MonitoringResourceHandler(
      const string &container_name,
      const KernelApi *kernel,
      PerfController *perf_controller);
  virtual ~MonitoringResourceHandler() {}

  virtual ::util::Status Update(const ContainerSpec &spec,
                                Container::UpdatePolicy policy);
  virtual ::util::Status Stats(Container::StatsType type,
                               ContainerStats *output) const;
  virtual ::util::Status Spec(ContainerSpec *spec) const;
  virtual ::util::StatusOr<Container::NotificationId> RegisterNotification(
      const EventSpec &spec, Callback1< ::util::Status> *callback);

 private:
  DISALLOW_COPY_AND_ASSIGN(MonitoringResourceHandler);
};

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_RESOURCES_MONITORING_RESOURCE_HANDLER_H_
