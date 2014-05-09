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
// process_launcher.h
// ProcessLauncher takes care of launching processes started by nscon. This
// serves nscon as the namespace-aware process launcher.
//

#ifndef PRODUCTION_CONTAINERS_NSCON_PROCESS_LAUNCHER_H__
#define PRODUCTION_CONTAINERS_NSCON_PROCESS_LAUNCHER_H__

#include <memory>
#include <vector>

#include "base/callback.h"
#include "nscon/ipc_agent.h"
#include "nscon/ns_util.h"
#include "util/task/status.h"
#include "util/task/statusor.h"

namespace containers {
namespace nscon {

class NamespaceSpec;
class NsConfigurator;
class RunSpec;
class RunSpec_Console;

class RunSpecConfigurator {
 public:
  explicit RunSpecConfigurator(const NsUtil *ns_util) : ns_util_(ns_util) {}
  virtual ~RunSpecConfigurator() {}

  virtual ::util::Status Configure(
      const RunSpec &run_spec,
      const ::std::vector<int> &fd_whitelist) const;

 private:
  ::util::Status SetGroups(const RunSpec &run_spec) const;
  const ::util::StatusOr<::std::vector<int>> GetOpenFDs(pid_t pid) const;

  const NsUtil *ns_util_;

  DISALLOW_COPY_AND_ASSIGN(RunSpecConfigurator);
};

// Launches processes in the specified set of namespaces.
class ProcessLauncher {
 public:
  // Does not take ownership ns_util.
  static ::util::StatusOr<ProcessLauncher *> New(NsUtil *ns_util);

  // This function is ran under the context of a cloned child process. It
  // synchronizes with the parent using sync_agent and runs specified namespace
  // configurators. If pid_notification_agent is given, CloneFn will use it to
  // send its PID to the remote process. After configuring RunSpec, it finally
  // execs the given command (argv).
  static ::util::Status CloneFn(
      char **argv,
      int clone_flags,
      int console_fd,
      IpcAgent *sync_agent,
      const NsUtil *ns_util,
      const RunSpecConfigurator *runconfig,
      const RunSpec *run_spec,
      const ::std::vector<NsConfigurator *> *configurators,
      const NamespaceSpec *spec,
      IpcAgent *pid_notification_agent);

  virtual ~ProcessLauncher() {}

  // Launches a given command in a new set of specified namespaces (if any).
  // NOTE: This MUST be called from a single-threaded process. Otherwise the
  // calls to unshare()/setns() will fail.
  //
  // Arguments:
  //   argv: Vector of command and its arguments to be executed.
  //   namespaces: List of namespaces (specified using clone-flags like
  //      CLONE_NEWIPC, CLONE_NEWPID, etc.) to unshare/setns.
  //   ns_target: PID of the process (typically INIT process) whose namespaces
  //      we want to attach to.
  //   run_spec: RunSpec for the process to be started.
  // Return:
  //   StatusOr: Status of the operation. OK iff successful. On success, the PID
  //      of the launched command is returned.
  virtual ::util::StatusOr<pid_t> NewNsProcessInTarget(
      const ::std::vector<string> &argv,
      const ::std::vector<int> &namespaces,
      pid_t ns_target,
      const RunSpec &run_spec) const;

  // Launches given command with new set of specified namespaces. The newly
  // created namespaces can be configured with specified configurators.
  //
  // Arguments:
  //   argv: Vector of command and its arguments to be executed.
  //   namespaces: List of namespaces (specified using clone-flags like
  //      CLONE_NEWIPC, CLONE_NEWPID, etc.) to unshare/setns.
  //   configurators: List of configurators that will be run before exec-ing the
  //      given command.
  //   ns_spec: NamespaceSpec specifying configuration settings for the new
  //      namespaces.
  //   run_spec: RunSpec for the process to be started.
  // Return:
  //   StatusOr: Status of the operation. OK iff successful. On success, the PID
  //      of the launched command is returned.
  virtual ::util::StatusOr<pid_t> NewNsProcess(
      const ::std::vector<string> &argv,
      const ::std::vector<int> &namespaces,
      const ::std::vector<NsConfigurator *> &configurators,
      const NamespaceSpec &spec,
      const RunSpec &run_spec) const;

 protected:
  // Takes ownership of |ipc_agent_factory| and |run_spec_configurator|.
  ProcessLauncher(NsUtil *ns_util,
                  IpcAgentFactory *ipc_agent_factory,
                  RunSpecConfigurator *run_spec_configurator)
      : ns_util_(ns_util),
        ipc_agent_factory_(ipc_agent_factory),
        run_spec_configurator_(run_spec_configurator) {}

 private:
  // Implements the run_spec for the process.
  static ::util::Status ConfigureRunSpec(const RunSpec *run_spec,
                                         const NsUtil *ns_util);

  // Opens the console device and returns its FD. Returns error if Console
  // specification is invalid or if error is encountered while opening the
  // console device.
  ::util::StatusOr<int> GetConsoleFd(const RunSpec_Console &console) const;

  // Internal function that does actual Clone() and runs configurators.
  ::util::StatusOr<pid_t> CloneAndLaunch(
      const ::std::vector<string> &argv,
      const ::std::vector<int> &namespaces,
      const ::std::vector<NsConfigurator *> &configurators,
      const NamespaceSpec &spec,
      const RunSpec &run_spec,
      IpcAgent *pid_notification_agent) const;

  NsUtil *ns_util_;
  ::std::unique_ptr<IpcAgentFactory> ipc_agent_factory_;
  ::std::unique_ptr<RunSpecConfigurator> run_spec_configurator_;

  friend class ProcessLauncherTest;

  DISALLOW_COPY_AND_ASSIGN(ProcessLauncher);
};

}  // namespace nscon
}  // namespace containers

#endif  // PRODUCTION_CONTAINERS_NSCON_PROCESS_LAUNCHER_H__
