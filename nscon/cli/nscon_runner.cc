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

using ::std::unique_ptr;
using ::strings::Substitute;
using ::util::error::INTERNAL;
using ::util::error::FAILED_PRECONDITION;
using ::util::error::INVALID_ARGUMENT;
using ::util::Status;

namespace containers {
namespace nscon {
namespace cli {
namespace {
const char kThreads[] = "Threads:";
const char kProcStatus[] = "/proc/self/status";
}  // namespace

NsconRunner::NsconRunner() {}

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

Status NsconRunner::Run(int argc, char **argv) const {
  SetDefaultFlags();

  if (argc < 2) {
    return {INVALID_ARGUMENT, NsconCli::kNsconHelp};
  }

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

    ::google::ParseCommandLineFlags(&argc, &argv, true);

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

}  // namespace cli
}  // namespace nscon
}  // namespace containers
