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

#ifndef SRC_RESOURCES_NULL_NAMESPACE_HANDLER_H_
#define SRC_RESOURCES_NULL_NAMESPACE_HANDLER_H_

#include <memory>
#include <string>
using ::std::string;

#include "base/callback.h"
#include "base/macros.h"
#include "lmctfy/namespace_handler.h"
#include "util/process/subprocess.h"
#include "util/task/statusor.h"

typedef ResultCallback<SubProcess *> SubProcessFactory;

namespace containers {
namespace lmctfy {

class NullNamespaceHandlerFactory : public NamespaceHandlerFactory {
 public:
  // Borrows kernel.
  explicit NullNamespaceHandlerFactory(const KernelApi *kernel);
  virtual ~NullNamespaceHandlerFactory() {}

  ::util::StatusOr<NamespaceHandler *> GetNamespaceHandler(
      const string &container_name) const override;
  ::util::StatusOr<NamespaceHandler *> CreateNamespaceHandler(
      const string &container_name,
      const ContainerSpec &spec,
      const MachineSpec &machine_spec) override;
  ::util::Status InitMachine(const InitSpec &spec) override;

 private:
  const KernelApi *kernel_;
  const ::std::unique_ptr<SubProcessFactory> subprocess_factory_;

  DISALLOW_COPY_AND_ASSIGN(NullNamespaceHandlerFactory);
};

class NullNamespaceHandler : public NamespaceHandler {
 public:
  // Borrows kernel and subprocess_factory.
  NullNamespaceHandler(
      const string &container_name,
      const KernelApi *kernel,
      SubProcessFactory *subprocess_factory);
  virtual ~NullNamespaceHandler() {}

  ::util::Status CreateResource(const ContainerSpec &spec) override;
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
      const EventSpec &spec,
      Callback1< ::util::Status> *callback) override;
  pid_t GetInitPid() const;
  ::util::StatusOr<bool> IsDifferentVirtualHost(
      const ::std::vector<pid_t> &tids) const override;

 private:
  const KernelApi *kernel_;
  SubProcessFactory *subprocess_factory_;

  DISALLOW_COPY_AND_ASSIGN(NullNamespaceHandler);
};

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_RESOURCES_NULL_NAMESPACE_HANDLER_H_
