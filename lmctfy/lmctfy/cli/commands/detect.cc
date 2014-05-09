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

#include "lmctfy/cli/commands/detect.h"

#include <unistd.h>
#include <string>
using ::std::string;
#include <vector>

#include "base/logging.h"
#include "lmctfy/cli/command.h"
#include "lmctfy/cli/output_map.h"
#include "include/lmctfy.h"
#include "util/errors.h"
#include "strings/numbers.h"
#include "strings/stringpiece.h"
#include "strings/substitute.h"
#include "util/task/codes.pb.h"
#include "util/task/statusor.h"

using ::std::vector;
using ::strings::Substitute;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {
namespace cli {

// Command to detect container of a TID.
Status DetectContainer(const vector<string> &argv, const ContainerApi *lmctfy,
                       OutputMap *output) {
  // Args: detect [<PID/TID>]
  if (argv.size() < 1 || argv.size() > 2) {
    return Status(::util::error::INVALID_ARGUMENT,
                  "See help for supported options.");
  }

  // Assume parent's PID if no PID/TID was specified.
  pid_t pid;
  if (argv.size() == 2) {
    if (!SimpleAtoi(argv[1], &pid)) {
      return Status(::util::error::INVALID_ARGUMENT,
                    Substitute("Could not read \"$0\" as a PID/TID", argv[1]));
    }
  } else {
    pid = getppid();
  }

  // Detect the container and output the result on success.
  string container_name = RETURN_IF_ERROR(lmctfy->Detect(pid));

  output->Add("name", container_name);
  return Status::OK;
}

void RegisterDetectCommand() {
  RegisterRootCommand(
      CMD("detect",
          "Detect in which container the specified PID/TID is running. If no "
          "PID/TID is specified, assume the calling process.",
          "[<PID/TID>]",
          CMD_TYPE_GETTER,
          0,
          1,
          &DetectContainer));
}

}  // namespace cli
}  // namespace lmctfy
}  // namespace containers
