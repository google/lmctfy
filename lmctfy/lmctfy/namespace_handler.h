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

#ifndef SRC_NAMESPACE_HANDLER_H_
#define SRC_NAMESPACE_HANDLER_H_

#include <vector>

#include "base/macros.h"
#include "system_api/kernel_api.h"
#include "lmctfy/resource_handler.h"
#include "lmctfy/tasks_handler.h"
#include "include/lmctfy.pb.h"
#include "util/task/status.h"
#include "util/task/statusor.h"

typedef ::system_api::KernelAPI KernelApi;

namespace containers {
namespace lmctfy {

class NamespaceHandler;

// Factory of NamespaceHandlers.
//
// Class is thread-safe.
class NamespaceHandlerFactory {
 public:
  static ::util::StatusOr<NamespaceHandlerFactory *> New(
      const TasksHandlerFactory *tasks_handler_factory);
  static ::util::StatusOr<NamespaceHandlerFactory *> NewNull(
      const KernelApi *kernel);


  virtual ~NamespaceHandlerFactory() {}

  // Creates a Namespace Handler for an existing container.
  //
  // Arguments:
  //   container_name: Absolute name of the container.
  // Return:
  //   StatusOr<NamespaceHandler *>: Status of the operation. Iff OK, returns an
  //       instance of a handler for this factory. Pointer is owned by the
  //       caller.
  ::util::StatusOr<GeneralResourceHandler *> Get(
      const string &container_name) {
    return GetNamespaceHandler(container_name);
  }
  virtual ::util::StatusOr<NamespaceHandler *>
      GetNamespaceHandler(const string &container_name) const = 0;

  // Creates a new Namespace Handler with the specified spec. Only uses parts of
  // the spec necessary for namespaces.
  //
  // Arguments:
  //   container_name: Absolute name of the container.
  //   spec: Specification for the new NamespaceHandler.
  // Return:
  //   StatusOr<NamespaceHandler *>: Status of the operation. Iff OK, returns an
  //       instance of a handler for this factory. Pointer is owned by the
  //       caller.
  ::util::StatusOr<GeneralResourceHandler *> Create(
      const string &container_name,
      const ContainerSpec &spec) {
     return CreateNamespaceHandler(container_name, spec, {});
  }
  virtual ::util::StatusOr<NamespaceHandler *> CreateNamespaceHandler(
      const string &container_name,
      const ContainerSpec &spec,
      const MachineSpec &machine_spec) = 0;

  // Initialize this handler on this machine. This setup is idempotent and only
  // needs to be done once at machine bootup.
  virtual ::util::Status InitMachine(const InitSpec &spec) = 0;

 protected:
  NamespaceHandlerFactory() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(NamespaceHandlerFactory);
};

// Handles namespaces in a particular container. It also behaves like a
// ResourceHandler for the namespace resource.
//
// Class is thread-safe.
class NamespaceHandler : public GeneralResourceHandler {
 public:
  virtual ~NamespaceHandler() {}

  // Exec the current process into the specified command inside the namespaces.
  //
  // Arguments:
  //   command: The program to execute. The first argument is exec'd.
  // Return:
  //   Status: Status of the operation, iff failure. If this call succeeds, it
  //       never returns.
  virtual ::util::Status Exec(const ::std::vector<string> &command) = 0;

  // Run the specified command inside the namespaces.
  //
  // Arguments:
  //   command: The command to execute with its arguments. The first element is
  //       the binary that will be executed and must be an absolute path.
  //   spec: The specification of the runtime environment to use for the
  //       execution of the command.
  // Return:
  //   StatusOr: Status of the operation. OK iff successful. On success, the
  //       PID of the command is returned.
  virtual ::util::StatusOr<pid_t> Run(const ::std::vector<string> &command,
                                      const RunSpec &spec) = 0;

  // Gets the PID of the init process in this namespace.
  //
  // Return:
  //   pid_t: The PID of the init process.
  virtual pid_t GetInitPid() const = 0;

  // Tell if any of |tids| is running in a different Virtual Host than the one
  // managed by this handler.
  virtual ::util::StatusOr<bool> IsDifferentVirtualHost(
      const ::std::vector<pid_t> &tids) const = 0;

 protected:
  NamespaceHandler(const string &container_name, ResourceType type)
      : GeneralResourceHandler(container_name, type) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(NamespaceHandler);
};

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_NAMESPACE_HANDLER_H_
