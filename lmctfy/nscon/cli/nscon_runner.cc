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

#include "nscon/cli/nscon_runner.h"

#include "gflags/gflags.h"
#include "nscon/namespace_controller_cli.h"
#include "nscon/cli/nscon_cli.h"
#include "util/errors.h"
#include "util/file_lines.h"
#include "strings/substitute.h"

DEFINE_int32(nscon_output_fd, -1,
             "File descriptor to which nscon will write its output."
             " Any non negative fd is considered to be valid.");

using ::std::unique_ptr;
using ::strings::Substitute;
using ::util::error::INTERNAL;
using ::util::error::FAILED_PRECONDITION;
using ::util::error::INVALID_ARGUMENT;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace nscon {
namespace cli {
namespace {
const char kThreads[] = "Threads:";
const char kProcStatus[] = "/proc/self/status";
}  // namespace

NsconRunner::NsconRunner() : nscon_stdout_(stdout), nscon_stderr_(stderr) {}

NsconRunner::~NsconRunner() {}

void NsconRunner::SetDefaultFlags() const {
  // Do not log non-error messages to a file in the CLI at all by default.
  FLAGS_minloglevel = FLAGS_stderrthreshold;

}

Status VerifyCurrentContextIsSingleThreaded() {
  // To catch regressions, ensure that we are indeed a single-threaded process.
  // We do this by reading the 'Threads: X' line from /proc/self/status file.
  uint32 num_threads = 0;
  for (const auto &line : ::util::FileLines(kProcStatus)) {
    if (line.starts_with(kThreads)) {
      StringPiece line_copy(line);
      line_copy.remove_prefix(strlen(kThreads));
      if (!SimpleAtoi(line_copy.ToString(), &num_threads)) {
        return {INTERNAL,
              Substitute("Cannot parse line from kProcStatus: $0", line)};
      }
      break;
    }
  }
  if (num_threads != 1) {
    return {FAILED_PRECONDITION,
          Substitute("Invalid number of threads associated with the current "
                     "process: $0.\n Nscon must run as a single threaded "
                     "process.", num_threads)};
  }
  return Status::OK;
}

Status NsconRunner::SetupOutput() {
  if (FLAGS_nscon_output_fd > -1) {
    FILE *out = fdopen(FLAGS_nscon_output_fd, "w");
    if (out == nullptr) {
      return Status(INTERNAL,
                    Substitute("fdopen failed nscon_output_fd: $0. Error: $1\n",
                               FLAGS_nscon_output_fd, StrError(errno).c_str()));
    }
    nscon_stdout_ = out;
    nscon_stderr_ = out;
  }
  return Status::OK;
}

StatusOr<string> NsconRunner::InternalRun(
    int argc, char **argv, const vector<string> &user_command) const {
  RETURN_IF_ERROR(VerifyCurrentContextIsSingleThreaded());

  unique_ptr<NamespaceControllerCli> namespace_controller_cli(
      RETURN_IF_ERROR(NamespaceControllerCli::New()));
  // Takes ownership of 'namespace_controller_cli'.
  unique_ptr<NsconCli> nscon_cli(
      new NsconCli(::std::move(namespace_controller_cli)));

  vector<string> nscon_args;
  nscon_args.insert(nscon_args.begin(), argv, argv + argc);

  return nscon_cli->HandleUserInput(nscon_args, user_command);
}

int NsconRunner::Run(int argc, char **argv) {
  SetDefaultFlags();

  // We don't want ParseCommandLineFlags to touch anything beyond '--'
  // separator. So extract out everything beyond '--' and update argc for
  // ParseCommandLineFlags.
  vector<string> user_command;
  {
    int orig_argc = argc;
    for (int i = 1; i < orig_argc; ++i) {
      if (!strcmp(argv[i], "--")) {
        argv[i] = NULL;
        argc = i;
        break;
      }
    }

    for (int i = argc + 1; i < orig_argc; ++i) {
      user_command.push_back(argv[i]);
    }
  }

  ::gflags::ParseCommandLineFlags(&argc, &argv, true);
  Status status = SetupOutput();
  if (!status.ok()) {
    fprintf(stderr, "%s\n", status.error_message().c_str());
    return status.error_code();
  }

  StatusOr<string> statusor = InternalRun(argc, argv, user_command);
  if (!statusor.ok()) {
    fprintf(nscon_stderr_, "%s\n", statusor.status().error_message().c_str());
    return statusor.status().error_code();
  }
  fprintf(nscon_stdout_, "%s\n", statusor.ValueOrDie().c_str());
  return 0;
}

}  // namespace cli
}  // namespace nscon
}  // namespace containers
