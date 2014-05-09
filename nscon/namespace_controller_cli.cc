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

#include "nscon/namespace_controller_cli.h"

#include <errno.h>
#include <unistd.h>

#include "nscon/configurator/ns_configurator.h"
#include "nscon/process_launcher.h"
#include "util/errors.h"
#include "system_api/libc_process_api.h"
#include "strings/substitute.h"
#include "util/gtl/stl_util.h"

// These are defined in namespace_controller_impl.cc
DECLARE_string(nsinit_path);
DECLARE_uint64(nsinit_uid);
DECLARE_uint64(nsinit_gid);

namespace containers {
namespace nscon {

using ::system_api::GlobalLibcProcessApi;
using ::std::unique_ptr;
using ::strings::Substitute;
using ::util::Status;
using ::util::StatusOr;

StatusOr<NamespaceControllerCli *> NamespaceControllerCli::New() {
  unique_ptr<NsHandleFactory> nsh_factory(
          RETURN_IF_ERROR(NsHandleFactory::New()));
  unique_ptr<NsUtil> ns_util(RETURN_IF_ERROR(NsUtil::New()));
  unique_ptr<ProcessLauncher> process_launcher(
          RETURN_IF_ERROR(ProcessLauncher::New(ns_util.get())));
  unique_ptr<NsConfiguratorFactory> config_factory(
          RETURN_IF_ERROR(NsConfiguratorFactory::New(ns_util.get())));
  return new NamespaceControllerCli(nsh_factory.release(),
                                    ns_util.release(),
                                    process_launcher.release(),
                                    config_factory.release());
}

vector<int>
NamespaceControllerCli::GetNamespacesFromSpec(const NamespaceSpec &spec) const {
  vector<int> namespaces;
  if (spec.has_ipc()) {
    namespaces.push_back(CLONE_NEWIPC);
  }
  if (spec.has_uts()) {
    namespaces.push_back(CLONE_NEWUTS);
  }
  if (spec.has_pid()) {
    namespaces.push_back(CLONE_NEWPID);
  }
  if (spec.has_mnt()) {
    namespaces.push_back(CLONE_NEWNS);
  }
  if (spec.has_net()) {
    namespaces.push_back(CLONE_NEWNET);
  }
  if (spec.has_user()) {
    namespaces.push_back(CLONE_NEWUSER);
  }
  return namespaces;
}

StatusOr<const string>
NamespaceControllerCli::Create(const NamespaceSpec &spec,
                               const vector<string> &init_argv) const {
  vector<NsConfigurator*> configs;
  ElementDeleter d(&configs);

  // Its invalid to provide FilesystemSpec without mount-namespace.
  if (spec.has_fs() && !spec.has_mnt()) {
    return Status(::util::error::INVALID_ARGUMENT,
                  "FilesystemSpec needs Mount namespaces enabled.");
  }

  // Always get the FilesystemConfigurator first.
  if (spec.has_mnt()) {
    configs.push_back(
        RETURN_IF_ERROR(config_factory_->GetFilesystemConfigurator()));
  }

  // Get configurators for all the namespaces. Return error if kernel doesn't
  // support any of the namespaces.
  vector<int> namespaces = GetNamespacesFromSpec(spec);
  for (auto ns : namespaces) {
    if (!ns_util_->IsNamespaceSupported(ns)) {
      const char *nsname = RETURN_IF_ERROR(ns_util_->NsCloneFlagToName(ns));
      return Status(::util::error::INVALID_ARGUMENT,
                    Substitute("Namespace '$0' not supported.", nsname));
    }

    StatusOr<NsConfigurator *> statusor = config_factory_->Get(ns);
    if (statusor.ok()) {
      configs.push_back(statusor.ValueOrDie());
    } else if (statusor.status().error_code() == ::util::error::NOT_FOUND) {
      // Its OK if there is no configurator for a given namespace. We will
      // simply create the namespace and leave it at that.
      continue;
    } else {
      return statusor.status();
    }
  }

  // Always add MachineConfigurator last.
  configs.push_back(RETURN_IF_ERROR(config_factory_->GetMachineConfigurator()));

  vector<string> argv = init_argv;
  if (argv.empty()) {
    // Build nsinit command and use process launcher to start it.
    argv = { FLAGS_nsinit_path,
             Substitute("--uid=$0", FLAGS_nsinit_uid),
             Substitute("--gid=$0", FLAGS_nsinit_gid) };
  }

  pid_t init_pid =
      RETURN_IF_ERROR(pl_->NewNsProcess(argv, namespaces, configs, spec,
                                        spec.run_spec()));

  unique_ptr<const NsHandle> nshandle(
      RETURN_IF_ERROR(nshandle_factory_->Get(init_pid)));

  return nshandle->ToString();
}

StatusOr<pid_t>
NamespaceControllerCli::RunShellCommand(const string &nshandlestr,
                                        const string &command,
                                        const RunSpec &run_spec) const {
  unique_ptr<const NsHandle> nshandle(
      RETURN_IF_ERROR(nshandle_factory_->Get(nshandlestr)));
  pid_t ns_target = nshandle->ToPid();

  // Build shell command to execute.
  vector<string> argv;
  argv.push_back("/bin/bash");
  argv.push_back("-c");
  argv.push_back(command);

  vector<int> namespaces =
      RETURN_IF_ERROR(ns_util_->GetUnsharedNamespaces(ns_target));

  // Launch with namespaces in the target identified by nshandle.
  return pl_->NewNsProcessInTarget(argv, namespaces, ns_target, run_spec);
}

StatusOr<pid_t>
NamespaceControllerCli::Run(const string &nshandlestr,
                            const vector<string> &command,
                            const RunSpec &run_spec) const {
  if (command.empty()) {
    return Status(::util::error::INVALID_ARGUMENT, "Empty command");
  }

  unique_ptr<const NsHandle> nshandle(
      RETURN_IF_ERROR(nshandle_factory_->Get(nshandlestr)));
  pid_t ns_target = nshandle->ToPid();

  vector<int> namespaces =
      RETURN_IF_ERROR(ns_util_->GetUnsharedNamespaces(ns_target));

  // Launch with namespaces in the target identified by nshandle.
  return pl_->NewNsProcessInTarget(command, namespaces, ns_target, run_spec);
}

// TODO(adityakali): Re-use the code in ProcessLauncher::WaitForChildSuccess()
StatusOr<int>
NamespaceControllerCli::GetChildExitStatus(pid_t child_pid) const {
  int status;
  do {
    int w = GlobalLibcProcessApi()->WaitPid(child_pid, &status, 0);
    if (w < 0) {
      return Status(::util::error::INTERNAL,
                    Substitute("waitpid($0): $1", child_pid, strerror(errno)));
    }
  } while (!WIFEXITED(status) && !WIFSIGNALED(status));

  return WEXITSTATUS(status);
}

Status NamespaceControllerCli::Exec(const string &nshandlestr,
                                    const vector<string> &command) const {
  if (command.empty()) {
    return Status(::util::error::INVALID_ARGUMENT, "Empty command");
  }

  unique_ptr<const NsHandle> nshandle(
      RETURN_IF_ERROR(nshandle_factory_->Get(nshandlestr)));
  pid_t ns_target = nshandle->ToPid();

  vector<int> namespaces =
      RETURN_IF_ERROR(ns_util_->GetUnsharedNamespaces(ns_target));

  RETURN_IF_ERROR(ns_util_->AttachNamespaces(namespaces, ns_target));

  vector<const char *> cargv;
  for (const string &s : command) {
    cargv.push_back(s.c_str());
  }
  cargv.push_back(nullptr);

  // We need extra fork() to enter child PIDns correctly.
  bool need_extra_fork = false;
  for (auto ns : namespaces) {
    if (ns == CLONE_NEWPID) {
      need_extra_fork = true;
    }
  }

  pid_t pid = 0;
  if (need_extra_fork) {
    pid = GlobalLibcProcessApi()->Fork();
    if (pid < 0) {
      return Status(::util::error::INTERNAL,
                    Substitute("fork() failed: $0", strerror(errno)));
    }
  }

  if (pid == 0) {
    GlobalLibcProcessApi()->Execve(cargv[0],
                                   const_cast<char *const *>(&cargv.front()),
                                   environ);
    if (need_extra_fork) {
      fprintf(stderr, "execve(%s) failed: %s\n", cargv[0], strerror(errno));
      GlobalLibcProcessApi()->_Exit(-1);
    }
  }

  if (need_extra_fork) {
    // Wait for child to exit and exit with the same exit-status.
    GlobalLibcProcessApi()->_Exit(RETURN_IF_ERROR(GetChildExitStatus(pid)));
  }

  // Should not reach here on Execve success.
  return Status(::util::error::INTERNAL,
                Substitute("execve($0) failed: $1", cargv[0], strerror(errno)));
}

Status NamespaceControllerCli::Update(const string &nshandlestr,
                                      const NamespaceSpec &spec) const {
  unique_ptr<const NsHandle> nshandle(
      RETURN_IF_ERROR(nshandle_factory_->Get(nshandlestr)));
  pid_t nsinit_pid = nshandle->ToPid();
  vector<int> namespaces = GetNamespacesFromSpec(spec);

  // Return error if kernel doesn't support any of the above namespaces.
  for (auto ns : namespaces) {
    if (!ns_util_->IsNamespaceSupported(ns)) {
      const char *nsname = RETURN_IF_ERROR(ns_util_->NsCloneFlagToName(ns));
      return Status(::util::error::INVALID_ARGUMENT,
                    Substitute("Namespace '$0' not supported.", nsname));
    }
  }

  // Get configurators for all the namespaces.
  vector<NsConfigurator*> configs;
  ElementDeleter d(&configs);
  for (auto ns : namespaces) {
    configs.push_back(RETURN_IF_ERROR(config_factory_->Get(ns)));
  }

  for (auto config : configs) {
    unique_ptr<SavedNamespace> saved_ns(
        RETURN_IF_ERROR(ns_util_->SaveNamespace(config->ns())));
    RETURN_IF_ERROR(config->SetupOutsideNamespace(spec, nsinit_pid));
    RETURN_IF_ERROR(ns_util_->AttachNamespaces({ config->ns() }, nsinit_pid));
    RETURN_IF_ERROR(config->SetupInsideNamespace(spec));
    RETURN_IF_ERROR(saved_ns->RestoreAndDelete());
    saved_ns.release();
  }

  return Status::OK;
}

}  // namespace nscon
}  // namespace containers
