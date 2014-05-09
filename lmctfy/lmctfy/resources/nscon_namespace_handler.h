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

#ifndef SRC_RESOURCES_NSCON_NAMESPACE_HANDLER_H_
#define SRC_RESOURCES_NSCON_NAMESPACE_HANDLER_H_


#include <memory>
#include <string>
using ::std::string;

#include "base/macros.h"
#include "base/logging.h"
#include "lmctfy/namespace_handler.h"
#include "lmctfy/tasks_handler.h"
#include "include/namespace_controller.h"
#include "include/lmctfy.h"
#include "util/task/statusor.h"

namespace containers {
class ConsoleUtil;

namespace lmctfy {

class NsconNamespaceHandlerFactory : public NamespaceHandlerFactory {
 public:
  // Takes ownership of namespace_controller_factory.
  // Does not own task_handlers_factory.
  NsconNamespaceHandlerFactory(
      const TasksHandlerFactory *tasks_handler_factory,
      const nscon::NamespaceControllerFactory *namespace_controller_factory,
      const ConsoleUtil *console_util)
      : tasks_handler_factory_(CHECK_NOTNULL(tasks_handler_factory)),
        namespace_controller_factory_(
            CHECK_NOTNULL(namespace_controller_factory)),
        console_util_(console_util) {}

  ~NsconNamespaceHandlerFactory() override {}

  ::util::StatusOr<NamespaceHandler *> GetNamespaceHandler(
      const string &container_name) const override;

  ::util::StatusOr<NamespaceHandler *> CreateNamespaceHandler(
      const string &container_name, const ContainerSpec &spec,
      const MachineSpec &machine_spec) override;

  ::util::Status InitMachine(const InitSpec &spec) override;

 private:
  // Checks whether the specified container is a VirtualHost.
  ::util::StatusOr<bool> IsVirtualHost(const string &container_name) const;

  // Gets the parent PID of the specified pid.
  ::util::StatusOr<pid_t> GetParentPid(pid_t pid) const;

  // Performs a crawl of the PID tree
  ::util::StatusOr<pid_t> CrawlTreeToFindInit(
      const string &container_name, const string &root_namespace,
      const TasksHandler &tasks_handler) const;

  // TODO(vmarmol): Have DetectInit() call IsVirtualHost() to handle non-Virtual
  // Host containers.
  // Finds the PID of the init in the specified container. container_name must
  // be a Virtual Host, otherwise DetectInit()'s behavior is undefined.
  ::util::StatusOr<pid_t> DetectInit(const string &container_name) const;

  const TasksHandlerFactory *tasks_handler_factory_;

  const ::std::unique_ptr<const nscon::NamespaceControllerFactory>
      namespace_controller_factory_;

  const ::std::unique_ptr<const ConsoleUtil> console_util_;

  friend class NsconNamespaceHandlerFactoryTest;

  DISALLOW_COPY_AND_ASSIGN(NsconNamespaceHandlerFactory);
};

class NsconNamespaceHandler : public NamespaceHandler {
 public:
  // Takes ownership of namespace_controller, borrows
  // namespace_controller_factory.
  NsconNamespaceHandler(
      const string &container_name,
      nscon::NamespaceController *namespace_controller,
      const nscon::NamespaceControllerFactory *namespace_controller_factory)
      : NamespaceHandler(container_name, RESOURCE_VIRTUALHOST),
        namespace_controller_(CHECK_NOTNULL(namespace_controller)),
        namespace_controller_factory_(
            CHECK_NOTNULL(namespace_controller_factory)) {}
  ~NsconNamespaceHandler() override {}

  ::util::Status CreateResource(const ContainerSpec &spec) override {
    return ::util::Status::OK;
  }

  ::util::Status Update(const ContainerSpec &spec,
                        Container::UpdatePolicy policy) override;
  ::util::Status Exec(const ::std::vector<string> &command) override;
  ::util::StatusOr<pid_t> Run(const ::std::vector<string> &command,
                              const RunSpec &spec) override;
  ::util::Status Stats(Container::StatsType type,
                       ContainerStats *output) const override;
  ::util::Status Spec(ContainerSpec *spec) const override;
  ::util::Status Destroy() override;
  ::util::Status Delegate(::util::UnixUid uid,
                          ::util::UnixGid gid) override;
  ::util::StatusOr<Container::NotificationId> RegisterNotification(
      const EventSpec &spec, Callback1< ::util::Status> *callback) override;
  pid_t GetInitPid() const override;
  ::util::StatusOr<bool> IsDifferentVirtualHost(
      const ::std::vector<pid_t> &tids) const override;

 private:
  ::std::unique_ptr<nscon::NamespaceController> namespace_controller_;
  const nscon::NamespaceControllerFactory *namespace_controller_factory_;

  DISALLOW_COPY_AND_ASSIGN(NsconNamespaceHandler);
};

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_RESOURCES_NSCON_NAMESPACE_HANDLER_H_
