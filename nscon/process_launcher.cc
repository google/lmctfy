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
// ProcessLauncher implementation.
//

#include "nscon/process_launcher.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <unistd.h>

#include "gflags/gflags.h"
#include "file/base/path.h"
#include "nscon/configurator/ns_configurator.h"
#include "nscon/ns_util.h"
#include "include/namespaces.pb.h"
#include "util/errors.h"
#include "util/scoped_cleanup.h"
#include "system_api/libc_fs_api.h"
#include "system_api/libc_process_api.h"
#include "strings/substitute.h"
#include "util/task/status.h"

using ::file::JoinPath;
using ::system_api::GlobalLibcFsApi;
using ::system_api::GlobalLibcProcessApi;
using ::util::ScopedCleanup;
using ::std::pair;
using ::std::unique_ptr;
using ::std::vector;
using ::strings::Substitute;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace nscon {

static char g_stack[1<<20] __attribute__((aligned(16)));

// TODO(adityakali): It is discouraged to subclass SubProcess. Try to find
// another alternative.
class NsSubProcess : public SubProcess {
 public:
  // Does not take ownership of |ipc_agent|.
  explicit NsSubProcess(IpcAgent *ipc_agent) : ipc_agent_(ipc_agent) {}
  virtual ~NsSubProcess() override {}

 private:
  void ExecChild() override {
    if (ipc_agent_) {
      // We don't really need to write our pid into the WriteData. The pid of
      // the sender is implicitly sent to the receiver. Also, getpid() here
      // returns the namespace-specific pid which is probably useless for
      // receiver.
      ipc_agent_->WriteData(SimpleItoa(getpid())).IgnoreError();
    }
    SubProcess::ExecChild();
  }

  IpcAgent *ipc_agent_;

  DISALLOW_COPY_AND_ASSIGN(NsSubProcess);
};

// Creates a new SubProcess.
static SubProcess *NewNsSubprocess(IpcAgent *ipc_agent) {
  return new NsSubProcess(ipc_agent);
}

StatusOr<ProcessLauncher *> ProcessLauncher::New(NsUtil *ns_util) {
  return new ProcessLauncher(NewPermanentCallback(&NewNsSubprocess), ns_util,
                             new IpcAgentFactory());
}

StatusOr<bool> ProcessLauncher::WaitForChildSuccess(pid_t child_pid) const {
  int status;
  do {
    int w = GlobalLibcProcessApi()->WaitPid(child_pid, &status, 0);
    if (w < 0) {
      return Status(::util::error::INTERNAL,
                    Substitute("waitpid($0): $1", child_pid, StrError(errno)));
    }
  } while (!WIFEXITED(status) && !WIFSIGNALED(status));

  if (WIFEXITED(status) && (WEXITSTATUS(status) == EXIT_SUCCESS)) {
    return true;
  }
  return false;
}

StatusOr<pid_t>
ProcessLauncher::ForkAndLaunch(const vector<string> &argv) const {
  IpcAgent *ipc_agent = RETURN_IF_ERROR(ipc_agent_factory_->Create());

  // Setup scoped cleaner for cleanly destroying the ipc_agent object.
  ScopedCleanup sc{&IpcAgent::Destroy, ipc_agent};

  // Run Launch in a separate child process.
  pid_t tmp_child = GlobalLibcProcessApi()->Fork();
  if (tmp_child < 0) {
    return Status(::util::error::INTERNAL,
                  Substitute("fork() failed; ERROR: $0", StrError(errno)));
  }

  if (tmp_child == 0) {
    unique_ptr<SubProcess> sp(subprocess_factory_->Run(ipc_agent));
    sp->SetArgv(argv);
    if (!sp->Start()) {
      // Nowhere to return Status if this fails. Failure is indicated by our
      // exit status.
      ipc_agent->WriteData(sp->error_text()).IgnoreError();
      GlobalLibcProcessApi()->_Exit(-1);
    }
    GlobalLibcProcessApi()->_Exit(0);
  }

  // Parent: Wait for child.
  bool success = RETURN_IF_ERROR(WaitForChildSuccess(tmp_child));

  pair<string, pid_t> child_info = RETURN_IF_ERROR(ipc_agent->ReadData());

  // If child exited successfully, then we return the obtained child_pid.
  if (success) {
    return child_info.second;
  }

  // child_error_text can potentially contain information about why the child
  // failed.
  return Status(::util::error::INTERNAL,
                Substitute("Child failed with error: $0", child_info.first));
}

struct CloneArgs {
  char **argv;
  bool remount_proc_sys_fs;
  IpcAgent *ipc_agent;
  const vector<NsConfigurator *> *configurators;
  const NamespaceSpec *spec;
  int pipefd[2];
};

struct ScopedCloneArgsReleaser : public ScopedCleanup {
  explicit ScopedCloneArgsReleaser(CloneArgs *cargs)
      : ScopedCleanup(&ScopedCloneArgsReleaser::Release, cargs) {}

  static void Release(CloneArgs *cargs) {
    int i = 0;
    if (!cargs->argv) return;
    while (cargs->argv[i]) {
      free(cargs->argv[i++]);
    }
    delete[] cargs->argv;
  }
};

static int CloneFnInvoker(void *arg) {
  struct CloneArgs *clone_args = static_cast<CloneArgs *>(arg);

  close(clone_args->pipefd[0]);

  Status status =
      ProcessLauncher::CloneFn(clone_args->argv,
                               clone_args->remount_proc_sys_fs,
                               clone_args->ipc_agent,
                               clone_args->configurators,
                               clone_args->spec);
  if (!status.ok()) {
    const string error = status.error_message();
    write(clone_args->pipefd[1], error.c_str(), error.length());
    GlobalLibcProcessApi()->_Exit(-1);
  }

  GlobalLibcProcessApi()->_Exit(0);
  return 0;  // Only to silence compiler.
}

Status ProcessLauncher::CloneFn(char **argv, bool remount_proc_sys_fs,
                                IpcAgent *ipc_agent,
                                const vector<NsConfigurator *> *configurators,
                                const NamespaceSpec *spec) {
  CHECK(argv != nullptr);
  CHECK(argv[0] != nullptr);
  CHECK(ipc_agent != nullptr);

  if (remount_proc_sys_fs) {
    // TODO(adityakali): Try to report appropriate failure if any of these
    // fails.
    GlobalLibcFsApi()->UMount2("/proc", MNT_DETACH);
    GlobalLibcFsApi()->Mount("proc", "/proc", "proc", 0, nullptr);
    GlobalLibcFsApi()->UMount2("/sys", MNT_DETACH);
    GlobalLibcFsApi()->Mount("sysfs", "/sys", "sysfs", 0, nullptr);
  }

  // Proceed only on notification from parent.
  RETURN_IF_ERROR(ipc_agent->ReadData());

  if (configurators && spec) {
    for (NsConfigurator *nsconfig : *configurators) {
      RETURN_IF_ERROR(nsconfig->SetupInsideNamespace(*spec));
    }
  }

  GlobalLibcProcessApi()->Execve(argv[0], const_cast<char *const *>(argv),
                                 environ);
  return Status(::util::error::INTERNAL,
                Substitute("execve($0) failed: $1", argv[0], StrError(errno)));
}

StatusOr<pid_t>
ProcessLauncher::CloneAndLaunch(const vector<string> &argv,
                                const vector<int> &namespaces,
                                const vector<NsConfigurator *> &configurators,
                                const NamespaceSpec &spec) const {
  int clone_flags = SIGCHLD;
  for (int ns : namespaces) {
    clone_flags |= ns;
  }

  // We need synchronization between child and parent.
  IpcAgent *ipc_agent = RETURN_IF_ERROR(ipc_agent_factory_->Create());
  ScopedCleanup sc(&IpcAgent::Destroy, ipc_agent);

  CloneArgs clone_args;
  memset(&clone_args, 0, sizeof(clone_args));
  ScopedCloneArgsReleaser cargs_releaser(&clone_args);

  if ((clone_flags & CLONE_NEWPID) && (clone_flags & CLONE_NEWNS)) {
    clone_args.remount_proc_sys_fs = true;
  }

  clone_args.argv = new char*[argv.size() + 1];
  {
    int i = 0;
    for (const string &arg : argv) {
      clone_args.argv[i++] = strdup(arg.c_str());
    }
    clone_args.argv[i] = nullptr;
  }

  clone_args.ipc_agent = ipc_agent;

  if (!configurators.empty()) {
    clone_args.configurators = &configurators;
    clone_args.spec = &spec;
  }

  // TODO(adityakali): See if we can use IpcAgent::Read() itself to detect
  // closing of connection by child and get errors.
  if (pipe2(clone_args.pipefd, O_CLOEXEC) < 0) {
    return Status(::util::error::INTERNAL,
                  Substitute("pipe2() failed: $0", StrError(errno)));
  }

  // We are ready to start the child. Here is the sequence of events from here
  // onwards:
  // - Child is cloned and waits on parent to finish its namespace setup from
  //   outside.
  // - Parent runs the namespace setup and on success signals child to continue.
  // - After signalling the child, parent waits for child to exec.
  // - Child does setup inside namespaces and execs init. If it encounters any
  //   errors, it sends the error message via pipe to parent and dies.
  // - Parent waits till the other end of the pipe is closed and reads the data
  //   off it. No data on pipe indicates that child exec'd successfully.

  pid_t child_pid =
      GlobalLibcProcessApi()->Clone(&CloneFnInvoker, &g_stack[sizeof(g_stack)],
                                    clone_flags,
                                    static_cast<void *>(&clone_args));
  if (child_pid < 0) {
    return Status(::util::error::INTERNAL,
                  Substitute("clone() failed: $0", StrError(errno)));
  }

  // TODO(adityakali): Setup scoped process cleaner - it will kill the child
  // process and reap it if we encounter error below this point.

  // CloneFn will wait for us to run all the configurators first.
  for (NsConfigurator *nsconfig : configurators) {
    RETURN_IF_ERROR(nsconfig->SetupOutsideNamespace(spec, child_pid));
  }

  RETURN_IF_ERROR(ipc_agent->WriteData("RESUME"));

  // Detect execve() error, if any.
  char buf[1024];
  memset(buf, 0, sizeof(buf));
  close(clone_args.pipefd[1]);
  int r = read(clone_args.pipefd[0], buf, sizeof(buf));
  if (r < 0) {
    return Status(::util::error::INTERNAL,
                  Substitute("read(pipe) failed: $0", StrError(errno)));
  } else if (r > 0) {
    // Got error message from child.
    return Status(::util::error::INTERNAL,
                  Substitute("ChildError: $0", buf));
  }

  // No error message from child. Assume exec succeeded.
  return child_pid;
}

StatusOr<pid_t> ProcessLauncher::Launch(const vector<string> &argv,
                                        const vector<int> &namespaces,
                                        pid_t ns_target) const {
  if (ns_target != 0) {
    // Switch namespaces.
    RETURN_IF_ERROR(ns_util_->AttachNamespaces(namespaces, ns_target));
    return ForkAndLaunch(argv);
  } else {
    NamespaceSpec spec;
    vector<NsConfigurator *> configurators;
    return CloneAndLaunch(argv, namespaces, configurators, spec);
  }
}

StatusOr<pid_t> ProcessLauncher::LaunchWithConfiguration(
      const ::std::vector<string> &argv,
      const ::std::vector<int> &namespaces,
      const ::std::vector<NsConfigurator *> configurators,
      const NamespaceSpec &spec) const {
  return CloneAndLaunch(argv, namespaces, configurators, spec);
}

}  // namespace nscon
}  // namespace containers
