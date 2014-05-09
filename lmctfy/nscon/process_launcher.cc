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
#include <sys/apparmor.h>
#include <string.h>
#include <sys/mount.h>
#include <unistd.h>

#include <algorithm>

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
using ::system_api::ScopedFileCloser;
using ::util::ScopedCleanup;
using ::std::pair;
using ::std::unique_ptr;
using ::std::vector;
using ::strings::Substitute;
using ::util::error::INTERNAL;
using ::util::error::INVALID_ARGUMENT;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace nscon {

static char g_stack[1<<20] __attribute__((aligned(16)));

StatusOr<ProcessLauncher *> ProcessLauncher::New(NsUtil *ns_util) {
  return new ProcessLauncher(ns_util,
                             new IpcAgentFactory(),
                             new RunSpecConfigurator(ns_util));
}

struct CloneArgs {
  char **argv;
  int clone_flags;
  int console_fd;
  IpcAgent *sync_agent;
  const NsUtil *ns_util;
  const RunSpecConfigurator *runconfig;
  const RunSpec *run_spec;
  const vector<NsConfigurator *> *configurators;
  const NamespaceSpec *spec;
  IpcAgent *pid_notification_agent;
};

// Initialize the given clone_flags with supplied fields.
static void InitCloneArgs(CloneArgs *clone_args,
                          const vector<string> &argv,
                          int clone_flags,
                          int console_fd,
                          IpcAgent *sync_agent,
                          const NsUtil *ns_util,
                          const RunSpecConfigurator *runconfig,
                          const RunSpec *run_spec,
                          const vector<NsConfigurator *> *configs,
                          const NamespaceSpec *spec,
                          IpcAgent *pid_notification_agent) {
  memset(clone_args, 0, sizeof(CloneArgs));
  clone_args->clone_flags = clone_flags;

  clone_args->argv = new char*[argv.size() + 1];
  {
    int i = 0;
    for (const string &arg : argv) {
      clone_args->argv[i++] = strdup(arg.c_str());
    }
    clone_args->argv[i] = nullptr;
  }
  clone_args->console_fd = console_fd;
  clone_args->sync_agent = sync_agent;
  clone_args->ns_util = ns_util;
  clone_args->runconfig = runconfig;
  clone_args->run_spec = run_spec;

  clone_args->configurators = configs;
  clone_args->spec = spec;
  clone_args->pid_notification_agent = pid_notification_agent;
}

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

  Status status =
      ProcessLauncher::CloneFn(clone_args->argv,
                               clone_args->clone_flags,
                               clone_args->console_fd,
                               clone_args->sync_agent,
                               clone_args->ns_util,
                               clone_args->runconfig,
                               clone_args->run_spec,
                               clone_args->configurators,
                               clone_args->spec,
                               clone_args->pid_notification_agent);
  if (!status.ok()) {
    // Write error and signal parent.
    const string error = status.error_message();
    clone_args->sync_agent->WriteData(error).IgnoreError();
    clone_args->sync_agent->SignalParent().IgnoreError();
    GlobalLibcProcessApi()->_Exit(-1);
  }

  GlobalLibcProcessApi()->_Exit(0);
  return 0;  // Only to silence compiler.
}

Status ProcessLauncher::CloneFn(char **argv,
                                int clone_flags,
                                int console_fd,
                                IpcAgent *sync_agent,
                                const NsUtil *ns_util,
                                const RunSpecConfigurator *runconfig,
                                const RunSpec *run_spec,
                                const vector<NsConfigurator *> *configurators,
                                const NamespaceSpec *spec,
                                IpcAgent *pid_notification_agent) {
  CHECK(argv != nullptr);
  CHECK(argv[0] != nullptr);
  CHECK(sync_agent != nullptr);
  CHECK(ns_util != nullptr);
  CHECK(runconfig != nullptr);
  CHECK(run_spec != nullptr);

  // Proceed only on notification from parent.
  RETURN_IF_ERROR(sync_agent->ReadData());

  // Send our pid to parent if requested.
  if (pid_notification_agent != nullptr) {
    RETURN_IF_ERROR(pid_notification_agent->WriteData("pid"));
  }

  if (GlobalLibcProcessApi()->SetSid() < 0) {
    return Status(INTERNAL, Substitute("setsid failed. Error: $0",
                                       StrError(errno)));
  }

  if (configurators && spec) {
    for (NsConfigurator *nsconfig : *configurators) {
      RETURN_IF_ERROR(nsconfig->SetupInsideNamespace(*spec));
    }
  }

  // List of FDs we want to preserve even after exec.
  vector<int> fd_whitelist;
  if (console_fd > -1) {
    RETURN_IF_ERROR(ns_util->AttachToConsoleFd(console_fd));
    fd_whitelist.push_back(0);
    fd_whitelist.push_back(1);
    fd_whitelist.push_back(2);
  }

  RETURN_IF_ERROR(runconfig->Configure(*run_spec, fd_whitelist));

  GlobalLibcProcessApi()->Execve(argv[0], const_cast<char *const *>(argv),
                                 environ);
  return Status(INTERNAL,
                Substitute("execve($0) failed: $1", argv[0], StrError(errno)));
}

StatusOr<int> ProcessLauncher::GetConsoleFd(
    const RunSpec_Console &console) const {
  if (!console.has_slave_pty() || console.slave_pty().empty()) {
    return Status(INVALID_ARGUMENT, "Console must specify a slave_pty device.");
  }

  return ns_util_->OpenSlavePtyDevice(console.slave_pty());
}

StatusOr<pid_t> ProcessLauncher::CloneAndLaunch(
    const vector<string> &argv,
    const vector<int> &namespaces,
    const vector<NsConfigurator *> &configurators,
    const NamespaceSpec &spec,
    const RunSpec &run_spec,
    IpcAgent *pid_notification_agent) const {
  int console_fd = -1;
  unique_ptr<ScopedFileCloser> console_fd_closer;
  if (run_spec.has_console()) {
    // Open console in this context and pass it to the child process to be
    // attached to its stdin, stdout and stderr.
    console_fd = RETURN_IF_ERROR(GetConsoleFd(run_spec.console()));
    console_fd_closer.reset(new ScopedFileCloser(console_fd));
  }
  int clone_flags = SIGCHLD;
  for (int ns : namespaces) {
    clone_flags |= ns;
  }

  // We need synchronization between child and parent.
  IpcAgent *sync_agent = RETURN_IF_ERROR(ipc_agent_factory_->Create());
  ScopedCleanup sc(&IpcAgent::Destroy, sync_agent);

  CloneArgs clone_args;
  InitCloneArgs(&clone_args, argv, clone_flags, console_fd, sync_agent,
                ns_util_, run_spec_configurator_.get(), &run_spec,
                &configurators, &spec, pid_notification_agent);
  ScopedCloneArgsReleaser cargs_releaser(&clone_args);

  // We are ready to start the child. Here is the sequence of events from here
  // onwards:
  // - Child is cloned and waits on parent to finish its namespace setup from
  //   outside.
  // - Parent runs the namespace setup and on success signals child to continue.
  // - After signalling the child, parent waits for child to exec.
  // - Child does setup inside namespaces and execs init. If it encounters any
  //   errors, it writes the error message & signals the parent before dying.
  // - Parent waits till the child execs successfully (no signal from child). If
  //   child failed, it retrieves the error and reports it.

  pid_t child_pid =
      GlobalLibcProcessApi()->Clone(&CloneFnInvoker, &g_stack[sizeof(g_stack)],
                                    clone_flags,
                                    static_cast<void *>(&clone_args));
  if (child_pid < 0) {
    return Status(INTERNAL,
                  Substitute("clone() failed: $0", StrError(errno)));
  }

  // TODO(adityakali): Setup scoped process cleaner - it will kill the child
  // process and reap it if we encounter error below this point.

  // CloneFn will wait for us to run all the configurators first.
  for (NsConfigurator *nsconfig : configurators) {
    RETURN_IF_ERROR(nsconfig->SetupOutsideNamespace(spec, child_pid));
  }

  RETURN_IF_ERROR(sync_agent->WriteData("RESUME"));

  // Wait for child to execve(). If it fails anywhere, we will get the error
  // message. IpcAgent::WaitForChild() will return CANCELLED status when the
  // child implicitly terminates connection on successful exec().
  Status child_status = sync_agent->WaitForChild();
  if (child_status.CanonicalCode() == ::util::error::CANCELLED) {
    // No error message from child. Assume exec succeeded.
    return child_pid;
  }

  // If IpcAgent encountered some other error while waiting for child, return it
  // unchanged.
  RETURN_IF_ERROR(child_status);

  // Retrieve child error.
  pair<string, pid_t> child_info = RETURN_IF_ERROR(sync_agent->ReadData());
  return Status(INTERNAL, Substitute("Child error:: $0", child_info.first));
}

StatusOr<pid_t> ProcessLauncher::NewNsProcess(
    const vector<string> &argv,
    const vector<int> &namespaces,
    const vector<NsConfigurator *> &configurators,
    const NamespaceSpec &spec,
    const RunSpec &run_spec) const {
  return CloneAndLaunch(argv, namespaces, configurators, spec, run_spec,
                        nullptr);
}

StatusOr<pid_t> ProcessLauncher::NewNsProcessInTarget(
    const vector<string> &argv,
    const ::std::vector<int> &namespaces,
    pid_t ns_target,
    const RunSpec &run_spec) const {
  if (ns_target <= 0) {
    return Status(INVALID_ARGUMENT,
                  Substitute("Invalid ns_target PID '$0'.", ns_target));
  }

  // First switch namespaces.
  RETURN_IF_ERROR(ns_util_->AttachNamespaces(namespaces, ns_target));

  // We need two IpcAgent objects:
  // 1st (err_agent) for communication with our temporary child. This is where
  //     we will get the error information (if any).
  // 2nd (pid_notification_agent) for communication with our grandchild. This
  //     will be used to read its pid. We read from this IpcAgent object only
  //     if there were no errors.
  // Also setup scoped cleaner for cleanly destroying these objects.
  IpcAgent *err_agent = RETURN_IF_ERROR(ipc_agent_factory_->Create());
  ScopedCleanup sc1{&IpcAgent::Destroy, err_agent};
  IpcAgent *pid_notification_agent =
      RETURN_IF_ERROR(ipc_agent_factory_->Create());
  ScopedCleanup sc2{&IpcAgent::Destroy, pid_notification_agent};

  // Run Launch in a separate child process.
  pid_t tmp_child = GlobalLibcProcessApi()->Fork();
  if (tmp_child < 0) {
    return Status(INTERNAL,
                  Substitute("fork() failed; ERROR: $0", StrError(errno)));
  }

  if (tmp_child == 0) {
    NamespaceSpec spec;
    StatusOr<pid_t> statusor = CloneAndLaunch(argv, {}, {}, spec, run_spec,
                                              pid_notification_agent);
    if (!statusor.ok()) {
      // Send the error message via err_agent and indicate failure using our
      // exit status.
      err_agent->WriteData(statusor.status().error_message()).IgnoreError();
      err_agent->SignalParent().IgnoreError();
      GlobalLibcProcessApi()->_Exit(-1);
    }
    GlobalLibcProcessApi()->_Exit(0);
  }

  Status child_status = err_agent->WaitForChild();
  if (child_status.CanonicalCode() == ::util::error::CANCELLED) {
    // No error message from child. Assume exec succeeded. Retrieve grandchild's
    // PID and return it.
    pair<string, pid_t> pid_info =
        RETURN_IF_ERROR(pid_notification_agent->ReadData());
    return pid_info.second;
  }

  // If IpcAgent encountered some other error while waiting for child, return it
  // unchanged.
  RETURN_IF_ERROR(child_status);

  // Something went wrong while execing the process. Retrieve the error
  // information and relay it back.
  pair<string, pid_t> err_info = RETURN_IF_ERROR(err_agent->ReadData());
  return Status(INTERNAL,
                Substitute("Error starting process in target namespace:: $0",
                           err_info.first));
}

Status RunSpecConfigurator::Configure(const RunSpec &run_spec,
                                      const vector<int> &fd_whitelist) const {
  RETURN_IF_ERROR(SetGroups(run_spec));

  if (run_spec.has_gid()) {
    gid_t gid = run_spec.gid();
    if (GlobalLibcProcessApi()->SetResGid(gid, gid, gid) < 0) {
      return Status(INTERNAL, Substitute("setresgid($0,$1,$2): $3",
                                         gid, gid, gid, StrError(errno)));
    }
  }

  if (run_spec.has_uid()) {
    uid_t uid = run_spec.uid();
    if (GlobalLibcProcessApi()->SetResUid(uid, uid, uid) < 0) {
      return Status(INTERNAL, Substitute("setresuid($0,$1,$2): $3",
                                         uid, uid, uid, StrError(errno)));
    }
  }

  if (!run_spec.has_inherit_fds() || !run_spec.inherit_fds()) {
    vector<int> fd_list = RETURN_IF_ERROR(ns_util_->GetOpenFDs());
    // We can't close() all FDs since we may end up closing FDs opened by
    // IpcAgent and we won't be able to communicate with our parent. So just
    // mark all the FDs with FD_CLOEXEC flag and let them get closed by
    // execve().
    for (auto fd : fd_list) {
      // Skip closing the FD if it is in the whitelist.
      if (::std::find(fd_whitelist.begin(), fd_whitelist.end(), fd) !=
          fd_whitelist.end()) {
        continue;
      }

      // Don't error-out in case of fcntl() error. fcntl() usually may fail with
      // EBADF for some FDs in fd_list which are now closed. So proceed either
      // way.
      GlobalLibcFsApi()->FCntl(fd, F_SETFD, FD_CLOEXEC);
    }
  }

  // Apply AppArmor profile if present.
  if (run_spec.has_apparmor_profile()) {
    if (aa_change_onexec(run_spec.apparmor_profile().c_str())
        == -1) {
      return Status(INTERNAL, strerror(errno));
    }
  }

  // TODO(adityakali): drop privileges.
  return Status::OK;
}

Status RunSpecConfigurator::SetGroups(const RunSpec &run_spec) const {
  size_t groups_size = run_spec.groups_size();
  unique_ptr<gid_t[]> groups(new gid_t[groups_size]());

  uint32 i = 0;
  for (auto grp : run_spec.groups()) {
    groups.get()[i++] = grp;
  }

  if (GlobalLibcProcessApi()->SetGroups(groups_size, groups.get()) < 0) {
    return Status(INTERNAL, Substitute("setgroups(): $0", StrError(errno)));
  }

  return Status::OK;
}

}  // namespace nscon
}  // namespace containers
