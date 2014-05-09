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

#include "lmctfy/cli/commands/enter.h"

#include <limits.h>
#include <unistd.h>
#include <memory>
#include <string>
using ::std::string;
#include <vector>

#include "gflags/gflags.h"
#include "base/logging.h"
#include "lmctfy/cli/command.h"
#include "include/lmctfy.h"
#include "util/errors.h"
#include "strings/numbers.h"
#include "strings/stringpiece.h"
#include "strings/substitute.h"
#include "util/task/codes.pb.h"
#include "util/task/statusor.h"

namespace containers {
namespace lmctfy {
namespace cli {
class OutputMap;
}  // namespace cli
}  // namespace lmctfy
}  // namespace containers

DECLARE_string(lmctfy_config);

using ::std::unique_ptr;
using ::std::vector;
using ::strings::Substitute;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {
namespace cli {

// Command to enter a TID into a container.
Status EnterContainer(const vector<string> &argv, const ContainerApi *lmctfy,
                      OutputMap *output) {
  // Args: enter <container> [<TIDs in a space-separated list>]
  if (argv.size() < 2) {
    return Status(::util::error::INVALID_ARGUMENT,
                  "See help for supported options.");
  }
  const string container_name = argv[1];

  vector<pid_t> tids;
  tids.reserve(argv.size());

  // If not TIDs, assume self.
  if (argv.size() == 2) {
    tids.push_back(getppid());
  } else {
    // Parse the TIDs from argv. Skip the command and container name.
    auto it = argv.begin();
    ++it;
    ++it;
    for (; it != argv.end(); ++it) {
      pid_t tid;
      if (!SimpleAtoi(*it, &tid)) {
        return Status(::util::error::FAILED_PRECONDITION,
                      Substitute("Could not parse TID \"$0\"", *it));
      }

      tids.push_back(tid);
    }
  }

  // Ensure the container exists.
  unique_ptr<Container> container(
      RETURN_IF_ERROR(lmctfy->Get(container_name)));

  return container->Enter(tids);
}

void RegisterEnterCommand() {
  RegisterRootCommand(
      CMD("enter",
          "Enter a set of TIDs into the specified container. If none "
          "specified, assume the PID of the parent process.",
          "<container name> [<space-separated list of TIDs>]",
          CMD_TYPE_SETTER,
          1,
          INT_MAX,
          &EnterContainer));
}

}  // namespace cli
}  // namespace lmctfy
}  // namespace containers
