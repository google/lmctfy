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
// NamespaceController API Implementation.
//

#include "nscon/namespace_controller_impl.h"

#include <errno.h>
#include <sched.h>
#include <unistd.h>

#include "base/casts.h"
#include "gflags/gflags.h"
#include "google/protobuf/text_format.h"
#include "system_api/libc_process_api.h"
#include "strings/join.h"
#include "strings/substitute.h"

DEFINE_string(nscon_path, "/usr/local/bin/nscon", "Path to 'nscon' binary");
DEFINE_string(nsinit_path, "/usr/local/bin/nsinit", "Path to 'nsinit' binary");
// By default use uid/gid of nobody for the nsinit process.
// TODO(jnagal): Instead of using a flag, figure out the id for
// nobody and nogroup from /etc/passwd.
DEFINE_uint64(nsinit_uid, 65534, "User Id for nsinit");
DEFINE_uint64(nsinit_gid, 65534, "Group Id for nsinit");

using ::system_api::GlobalLibcProcessApi;
using ::std::unique_ptr;
using ::std::vector;
using ::strings::Join;
using ::strings::Substitute;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace nscon {

// Creates a new SubProcess.
static SubProcess *NewSubprocess() { return new SubProcess(); }

StatusOr<NamespaceControllerFactory *> NamespaceControllerFactory::New() {
  unique_ptr<NsHandleFactory> nshandle_factory(
      RETURN_IF_ERROR(NsHandleFactory::New()));
  unique_ptr<NsUtil> ns_util(RETURN_IF_ERROR(NsUtil::New()));
  return
      new NamespaceControllerFactoryImpl(nshandle_factory.release(),
                                         NewPermanentCallback(&NewSubprocess),
                                         ns_util.release());
}

//
// NamespaceControllerFactoryImpl methods.
//
StatusOr<NamespaceController *>
NamespaceControllerFactoryImpl::Get(pid_t pid) const {
  const NsHandle *nshandle = RETURN_IF_ERROR(nshandle_factory_->Get(pid));
  return new NamespaceControllerImpl(nshandle, subprocess_factory_.get());
}

StatusOr<NamespaceController *>
NamespaceControllerFactoryImpl::Get(const string &handlestr) const {
  const NsHandle *nshandle =
      RETURN_IF_ERROR(nshandle_factory_->Get(handlestr));
  return new NamespaceControllerImpl(nshandle, subprocess_factory_.get());
}

static const string SpecToStr(const NamespaceSpec &spec) {
  string spec_str;
  google::protobuf::TextFormat::Printer printer;
  printer.SetSingleLineMode(true);
  CHECK(printer.PrintToString(spec, &spec_str));
  return spec_str;
}

StatusOr<NamespaceController *>
NamespaceControllerFactoryImpl::Create(const NamespaceSpec &spec,
                                       const vector<string> &init_argv) const {
  // Setup subprocess so that we can read the stdout from nscon.
  unique_ptr<SubProcess> sp(subprocess_factory_->Run());
  sp->SetChannelAction(CHAN_STDOUT, ACTION_PIPE);
  sp->SetChannelAction(CHAN_STDERR, ACTION_PIPE);

  // Build nscon command with correct parameters. We must pass all known flags
  // to the binary.
  vector<string> argv;
  argv.push_back(FLAGS_nscon_path);  // program
  argv.push_back("create");  // nscon command
  argv.push_back(Substitute("--nsinit_path=$0", FLAGS_nsinit_path));
  argv.push_back(Substitute("--nsinit_uid=$0", FLAGS_nsinit_uid));
  argv.push_back(Substitute("--nsinit_gid=$0", FLAGS_nsinit_gid));
  // TODO(adityakali): The spec could get really huge. So consider passing it as
  // a binary or in a file to nscon.
  argv.push_back(SpecToStr(spec));

  if (!init_argv.empty()) {
    // Push init_argv to the end of argv after a --
    argv.push_back("--");
    argv.insert(argv.end(), init_argv.begin(), init_argv.end());
  }

  sp->SetArgv(argv);
  if (!sp->Start()) {
    return Status(::util::error::INTERNAL,
                  Substitute("'$0' failed:: ERROR: $1",
                             Join(argv, " "), sp->error_text()));
  }

  string nscon_stdout, nscon_stderr;
  // Communicate() return's the exit-code of the process.
  if (sp->Communicate(&nscon_stdout, &nscon_stderr) != 0) {
    return Status(
        ::util::error::INTERNAL,
        Substitute("'$0' failed:: ERROR(exit_code=$1): STDERR=[$2] STDOUT=[$3]",
                   Join(argv, " "), sp->exit_code(), nscon_stderr,
                   nscon_stdout));
  }

  // In case of success, 'nscon create' outputs the nshandle string.
  const NsHandle *nshandle =
      RETURN_IF_ERROR(nshandle_factory_->Get(nscon_stdout));
  return new NamespaceControllerImpl(nshandle, subprocess_factory_.get());
}

StatusOr<string> NamespaceControllerFactoryImpl::GetNamespaceId(
    pid_t pid) const {
  // Use the PID ns ID as the "namespace ID".
  return ns_util_->GetNamespaceId(pid, CLONE_NEWPID);
}

//
// NamespaceControllerImpl methods.
//
StatusOr<pid_t>
NamespaceControllerImpl::Run(const vector<string> &command) const {
  // Check if this nshandle is still valid
  if (!IsValid()) {
    return Status(::util::error::INTERNAL,
                  Substitute("Nshandle '$0' has become invalid.",
                             GetHandleString()));
  }

  // Setup subprocess so that we can read the stdout from nscon.
  unique_ptr<SubProcess> sp(subprocess_factory_->Run());
  sp->SetChannelAction(CHAN_STDOUT, ACTION_PIPE);
  sp->SetChannelAction(CHAN_STDERR, ACTION_PIPE);

  // Build nscon command with correct parameters.
  //   nscon run <nshandle> <command>
  vector<string> argv;
  argv.push_back(FLAGS_nscon_path);  // program
  argv.push_back("run");  // nscon command
  argv.push_back(GetHandleString());
  argv.push_back("--");
  argv.insert(argv.end(), command.begin(), command.end());

  sp->SetArgv(argv);
  if (!sp->Start()) {
    return Status(::util::error::INTERNAL,
                  Substitute("'$0' failed:: ERROR: $1",
                             Join(argv, " "), sp->error_text()));
  }

  string nscon_stdout, nscon_stderr;
  // Communicate() return's the exit-code of the process.
  if (sp->Communicate(&nscon_stdout, &nscon_stderr) != 0) {
    return Status(
        ::util::error::INTERNAL,
        Substitute("'$0' failed:: ERROR(exit_code=$1): STDERR=[$2] STDOUT=[$2]",
                   Join(argv, " "), sp->exit_code(), nscon_stderr,
                   nscon_stdout));
  }

  pid_t pid;
  if (!SimpleAtoi(nscon_stdout, &pid)) {
    return Status(::util::error::INTERNAL,
                   Substitute("Failed to parse pid from nscon stdout: $0",
                              nscon_stdout));
  }

  return pid;
}

Status NamespaceControllerImpl::Exec(const vector<string> &command) const {
  // Check if this nshandle is still valid
  if (!IsValid()) {
    return Status(
        ::util::error::INTERNAL,
        Substitute("Nshandle '$0' has become invalid.", GetHandleString()));
  }

  // Build nscon command with correct parameters.
  //   nscon exec <nshandle> <command>...
  vector<string> argv;
  argv.push_back(FLAGS_nscon_path);  // program
  argv.push_back("exec");  // nscon command
  argv.push_back(GetHandleString());
  argv.push_back("--");
  argv.insert(argv.end(), command.begin(), command.end());

  // Build a vector of C-compatible strings.
  vector<const char *> cargv;
  for (const string &s : argv) {
    cargv.push_back(s.c_str());
  }
  cargv.push_back(nullptr);

  GlobalLibcProcessApi()->Execve(
      cargv[0], const_cast<char *const *>(&cargv.front()), environ);
  return Status(::util::error::INTERNAL,
                Substitute("Exec failed with error: $0", strerror(errno)));
}

Status NamespaceControllerImpl::Update(const NamespaceSpec &spec) {
  // Check if this nshandle is still valid
  if (!IsValid()) {
    return Status(::util::error::INTERNAL,
                  Substitute("Nshandle '$0' has become invalid.",
                             GetHandleString()));
  }

  // Setup subprocess so that we can read the stdout from nscon.
  unique_ptr<SubProcess> sp(subprocess_factory_->Run());
  sp->SetChannelAction(CHAN_STDOUT, ACTION_PIPE);
  sp->SetChannelAction(CHAN_STDERR, ACTION_PIPE);

  // Build nscon command with correct parameters.
  //   nscon update <spec>
  vector<string> argv;
  argv.push_back(FLAGS_nscon_path);  // program
  argv.push_back("update");  // nscon command
  argv.push_back(GetHandleString());
  argv.push_back(SpecToStr(spec));

  sp->SetArgv(argv);
  if (!sp->Start()) {
    return Status(::util::error::INTERNAL,
                  Substitute("'$0' failed:: ERROR: $1",
                             Join(argv, " "), sp->error_text()));
  }

  string nscon_stdout, nscon_stderr;
  // Communicate() return's the exit-code of the process.
  if (sp->Communicate(&nscon_stdout, &nscon_stderr) != 0) {
    return Status(
        ::util::error::INTERNAL,
        Substitute("'$0' failed:: ERROR(exit_code=$1): STDERR=[$2] STDOUT=[$2]",
                   Join(argv, " "), sp->exit_code(), nscon_stderr,
                   nscon_stdout));
  }

  // In case of success, nscon doesn't output anything. Simply return OK here.
  return Status::OK;
}

Status NamespaceControllerImpl::Destroy() {
  // Check if this nshandle is still valid
  if (!IsValid()) {
    return Status(::util::error::INTERNAL,
                  Substitute("Nshandle '$0' has become invalid.",
                             GetHandleString()));
  }

  // Send SIGKILL to nsinit.
  pid_t nsinit_pid = GetPid();
  if (GlobalLibcProcessApi()->Kill(nsinit_pid, SIGKILL) != 0) {
    return Status(::util::error::INTERNAL,
                  Substitute("kill($0, SIGKILL) failed: $1",
                             nsinit_pid, strerror(errno)));
  }

  return Status::OK;
}

bool NamespaceControllerImpl::IsValid() const {
  return nshandle_->IsValid();
}

const string NamespaceControllerImpl::GetHandleString() const {
  return nshandle_->ToString();
}

pid_t NamespaceControllerImpl::GetPid() const {
  return nshandle_->ToPid();
}

}  // namespace nscon
}  // namespace containers
