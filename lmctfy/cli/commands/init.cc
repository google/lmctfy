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

#include "lmctfy/cli/commands/init.h"

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
#include "strings/stringpiece.h"
#include "util/task/codes.pb.h"
#include "util/task/statusor.h"

DECLARE_string(lmctfy_config);

using ::std::vector;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {
namespace cli {

// TODO(vmarmol): Pull out common components into a command_common.h

// Command to initialize containers.
Status InitContainers(const vector<string> &argv, const ContainerApi *lmctfy,
                      OutputMap *output) {
  // Args: init <container spec>
  if (argv.size() != 2 && argv.size() != 1) {
    return Status(::util::error::INVALID_ARGUMENT, "See help for options.");
  }

  // Ensure that a config file or a ASCII/binary proto was specified
  // (not both).
  if (FLAGS_lmctfy_config.empty() && argv.size() == 1) {
    return Status(
        ::util::error::INVALID_ARGUMENT,
        "Must specify a container config (via --lmctfy_config) or an "
        "ASCII/Binary config on the command line");
  } else if (!FLAGS_lmctfy_config.empty() && argv.size() == 2) {
    return Status(
        ::util::error::INVALID_ARGUMENT,
        "Can not specify both a container config and an ASCII/Binary config on "
        "the command line");
  }

  InitSpec spec;
  string config;

  // Read from file or command line.
  if (!FLAGS_lmctfy_config.empty()) {
    RETURN_IF_ERROR(::file::GetContents(FLAGS_lmctfy_config, &config,
                                        ::file::Defaults()));
  } else {
    config = argv[1];
  }

  // Try to parse the proto as both ASCII and binary.
  if (!::google::protobuf::TextFormat::ParseFromString(config, &spec) &&
      !spec.ParseFromString(config)) {
    return Status(::util::error::INVALID_ARGUMENT,
                  "Failed to parse the config");
  }

  // Initialize the machine.
  return ContainerApi::InitMachine(spec);
}

void RegisterInitCommand() {
  RegisterRootCommand(
      CMD("init",
          "Initialize lmctfy on this machine. Must be done before any "
          "containers are created. Only needs to be done once at boot. The "
          "init spec can be provided either on the command line or via a "
          "config file using the -c flag. The spec can be an ASCII or binary "
          "proto in either case.",
          "<spec proto in ASCII or binary mode>  | -c <config file>",
          CMD_TYPE_INIT, 0 /* min arguments */, 1 /* max arguments */,
          &InitContainers));
}

}  // namespace cli
}  // namespace lmctfy
}  // namespace containers
