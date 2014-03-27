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
// serves nscon as the namespace-aware process launcher (in place of the
// SubProcess API in google3 or Taskd's SubProcessLauncher class).
//

#ifndef PRODUCTION_CONTAINERS_NSCON_PROCESS_LAUNCHER_H__
#define PRODUCTION_CONTAINERS_NSCON_PROCESS_LAUNCHER_H__

#include <memory>
#include <vector>

#include "base/callback.h"
#include "nscon/ipc_agent.h"
#include "nscon/ns_util.h"
#include "util/process/subprocess.h"
#include "util/task/status.h"
#include "util/task/statusor.h"

namespace containers {
namespace nscon {

class NamespaceSpec;
class NsConfigurator;
typedef ResultCallback1<SubProcess *, IpcAgent *> SubProcessFactory;

// Launches processes in the specified set of namespaces.
class ProcessLauncher {
 public:
  // Does not take ownership ns_util.
  static ::util::StatusOr<ProcessLauncher *> New(NsUtil *ns_util);

  // This function is ran under the context of a cloned child process. It
  // synchronizes with the parent using ipc_agent and runs specified namespace
  // configurators. It finally execs the given command (argv).
  static ::util::Status CloneFn(
      char **argv, bool remount_proc_sys_fs, IpcAgent *ipc_agent,
      const ::std::vector<NsConfigurator *> *configurators,
      const NamespaceSpec *spec);

  virtual ~ProcessLauncher() {}

  // Launches a given command and arguments (specified as argv vector, similar
  // to that of the //util/process:subprocess class). The launched command
  // will be in a different namespace if namespaces were specified.
  // NOTE: This MUST be called from a single-threaded process. Otherwise the
  // calls to unshare()/setns() will fail.
  //
  // TODO(adityakali): With introduction of LaunchWithConfiguration(), we never
  // call Launch() to start a process in new namespaces. So, we should just
  // disallow passing ns_target as 0.
  //
  // Arguments:
  //   argv: Vector of command and its arguments to be executed.
  //   namespaces: List of namespaces (specified using clone-flags like
  //      CLONE_NEWIPC, CLONE_NEWPID, etc.) to unshare/setns.
  //   ns_target: PID of the process (typically INIT process) whose namespaces
  //      we want to attach to. PID 0 implies new namespaces will be created.
  // Return:
  //   StatusOr: Status of the operation. OK iff successful. On success, the PID
  //      of the launched command is returned.
  virtual ::util::StatusOr<pid_t> Launch(const ::std::vector<string> &argv,
                                         const ::std::vector<int> &namespaces,
                                         pid_t ns_target) const;

  virtual ::util::StatusOr<pid_t> LaunchWithConfiguration(
      const ::std::vector<string> &argv,
      const ::std::vector<int> &namespaces,
      const ::std::vector<NsConfigurator *> configurators,
      const NamespaceSpec &spec) const;

 protected:
  // Takes ownership of |subprocess_factory| and |ipc_agent_factory|.
  ProcessLauncher(SubProcessFactory *subprocess_factory, NsUtil *ns_util,
                  IpcAgentFactory *ipc_agent_factory)
      : subprocess_factory_(subprocess_factory), ns_util_(ns_util),
        ipc_agent_factory_(ipc_agent_factory) {}

 private:
  // Returns true if child exited successfully (exit code of 0). Returns false
  // if it terminated for any other reason.
  ::util::StatusOr<bool> WaitForChildSuccess(pid_t child_pid) const;
  // Internal function to launch user jobs correctly when PID-ns is switched.
  ::util::StatusOr<pid_t> ForkAndLaunch(const ::std::vector<string> &argv)
      const;

  // Uses clone(2) to start a new process in new set of specified namespaces.
  ::util::StatusOr<pid_t>
  CloneAndLaunch(const ::std::vector<string> &argv,
                 const ::std::vector<int> &namespaces,
                 const ::std::vector<NsConfigurator *> &configurators,
                 const NamespaceSpec &spec) const;

  ::std::unique_ptr<SubProcessFactory> subprocess_factory_;
  NsUtil *ns_util_;
  ::std::unique_ptr<IpcAgentFactory> ipc_agent_factory_;

  friend class ProcessLauncherTest;

  DISALLOW_COPY_AND_ASSIGN(ProcessLauncher);
};

}  // namespace nscon
}  // namespace containers

#endif  // PRODUCTION_CONTAINERS_NSCON_PROCESS_LAUNCHER_H__
