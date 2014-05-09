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

//
// NamespaceControllerCli
//
// The CLI is a wrapper for actual nscon binary. It provides the real
// implementations of Create(), Run() and Update() methods for the CLI.
//

#ifndef PRODUCTION_CONTAINERS_NSCON_NAMESPACE_CONTROLLER_CLI_H__
#define PRODUCTION_CONTAINERS_NSCON_NAMESPACE_CONTROLLER_CLI_H__

#include <memory>

#include "nscon/configurator/ns_configurator_factory.h"
#include "nscon/ns_handle.h"
#include "nscon/process_launcher.h"
#include "include/namespaces.pb.h"

namespace containers {
namespace nscon {

class NamespaceSpec;
class RunSpec;

class NamespaceControllerCli {
 public:
  static ::util::StatusOr<NamespaceControllerCli *> New();

  virtual ~NamespaceControllerCli() {}

  // Returns the namespace handle string for the newly created namespace jail.
  virtual ::util::StatusOr<const string> Create(
          const NamespaceSpec &spec,
          const ::std::vector<string> &init_argv) const;

  virtual ::util::StatusOr<pid_t> RunShellCommand(const string &nshandlestr,
                                                  const string &command,
                                                  const RunSpec &runspec) const;

  // Runs the given command directly (without the bash -c wrapper).
  virtual ::util::StatusOr<pid_t> Run(const string &nshandlestr,
                                      const ::std::vector<string> &commandv,
                                      const RunSpec &runspec) const;

  // Does not return on success. On error, returns the Status containing error
  // information.
  virtual ::util::Status Exec(const string &nshandlestr,
                              const ::std::vector<string> &commandv) const;

  virtual ::util::Status Update(const string &nshandlestr,
                                const NamespaceSpec &spec) const;

 protected:
  // Takes ownership of |nshandle_factory|, |ns_util|, |process_launcher| and
  // |config_factory|.
  explicit NamespaceControllerCli(NsHandleFactory *nshandle_factory,
                                  NsUtil *ns_util,
                                  ProcessLauncher *process_launcher,
                                  NsConfiguratorFactory *config_factory)
      : nshandle_factory_(nshandle_factory), ns_util_(ns_util),
        pl_(process_launcher), config_factory_(config_factory) {}

 private:
  ::util::StatusOr<int> GetChildExitStatus(pid_t child_pid) const;
  ::std::vector<int> GetNamespacesFromSpec(const NamespaceSpec &spec) const;

  ::std::unique_ptr<NsHandleFactory> nshandle_factory_;
  ::std::unique_ptr<NsUtil> ns_util_;
  ::std::unique_ptr<ProcessLauncher> pl_;
  ::std::unique_ptr<NsConfiguratorFactory> config_factory_;

  friend class NamespaceControllerCliTest;

  DISALLOW_COPY_AND_ASSIGN(NamespaceControllerCli);
};

}  // namespace nscon
}  // namespace containers

#endif  // PRODUCTION_CONTAINERS_NSCON_NAMESPACE_CONTROLLER_CLI_H__
