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

#include "lmctfy/cli/commands/create.h"

#include <memory>
#include <string>
using ::std::string;
#include <vector>

#include "gflags/gflags.h"
#include "base/logging.h"
#include "file/base/file.h"
#include "file/base/helpers.h"
#include "google/protobuf/text_format.h"
#include "lmctfy/cli/command.h"
#include "include/lmctfy.h"
#include "include/lmctfy.pb.h"
#include "util/errors.h"
#include "strings/substitute.h"
#include "strings/stringpiece.h"
#include "util/task/codes.pb.h"
#include "util/task/statusor.h"

DECLARE_string(lmctfy_config);

using ::std::unique_ptr;
using ::std::vector;
using ::strings::Substitute;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {
namespace cli {

// Command to create a container.
Status CreateContainer(const vector<string> &argv, const ContainerApi *lmctfy,
                       OutputMap *output) {
  // Args: create <container name> [<container spec>]
  if (argv.size() < 2 || argv.size() > 3) {
    return Status(::util::error::INVALID_ARGUMENT,
                  "See help for supported options.");
  }
  const string container_name = argv[1];

  // Ensure that a config file or a ASCII/binary proto was specified (not either
  // or both).
  if (FLAGS_lmctfy_config.empty() && argv.size() == 2) {
    return Status(
        ::util::error::INVALID_ARGUMENT,
        "Must specify a container config (via --lmctfy_config) or an "
        "ASCII/Binary config on the command line");
  } else if (!FLAGS_lmctfy_config.empty() && argv.size() == 3) {
    return Status(
        ::util::error::INVALID_ARGUMENT,
        "Can not specify both a container config and an ASCII/Binary config on "
        "the command line");
  }

  ContainerSpec spec;

  string config;

  // Read from file or from the command line.
  if (!FLAGS_lmctfy_config.empty()) {
    RETURN_IF_ERROR(::file::GetContents(FLAGS_lmctfy_config, &config,
                                        ::file::Defaults()));
  } else {
    config = argv[2];
  }

  // Try to parse the proto as both ASCII and binary.
  if (!::google::protobuf::TextFormat::ParseFromString(config, &spec) &&
      !spec.ParseFromString(config)) {
    return Status(::util::error::INVALID_ARGUMENT,
                  "Failed to parse the container config");
  }

  // Create the container and delete the handle on success.
  unique_ptr<Container> container(
      RETURN_IF_ERROR(lmctfy->Create(container_name, spec)));

  if (spec.has_virtual_host()) {
    const pid_t init_pid = RETURN_IF_ERROR(container->GetInitPid());
    output->Add("init_pid", SimpleItoa(init_pid));
  }

  return Status::OK;
}

void RegisterCreateCommand() {
  RegisterRootCommand(
      CMD("create",
          "Create a container from the spec. The spec is provided either on "
          "the command line or via a config file using the -c flag. The config "
          "can be an ASCII or binary proto in either case",
          "[-c <config file>] <container name> "
          "[<spec proto in ASCII or binary mode>]",
          CMD_TYPE_SETTER, 1, 2, &CreateContainer));
}

}  // namespace cli
}  // namespace lmctfy
}  // namespace containers
