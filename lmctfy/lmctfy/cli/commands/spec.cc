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

#include "lmctfy/cli/commands/spec.h"

#include <stdio.h>
#include <unistd.h>
#include <memory>
#include <string>
using ::std::string;
#include <vector>

#include "gflags/gflags.h"
#include "google/protobuf/text_format.h"
#include "lmctfy/cli/command.h"
#include "include/lmctfy.h"
#include "include/lmctfy.pb.h"
#include "util/errors.h"

DECLARE_bool(lmctfy_binary);

using ::std::unique_ptr;
using ::std::vector;
using ::util::Status;

namespace containers {
namespace lmctfy {
namespace cli {

// Command to get the ContainerSpec for a container.
Status SpecContainer(const vector<string> &argv,
                            const ContainerApi *lmctfy,
                            OutputMap *output) {
  // Args: spec [<container name>]
  if (argv.size() < 1 || argv.size() > 2) {
    return Status(::util::error::INVALID_ARGUMENT,
                  "See help for supported options.");
  }

  // Get container name.
  string container_name;
  if (argv.size() == 2) {
    container_name = argv[1];
  } else {
    // Detect parent's container.
    container_name = RETURN_IF_ERROR(lmctfy->Detect(getppid()));
  }

  // Ensure the container exists.
  unique_ptr<Container> container(
      RETURN_IF_ERROR(lmctfy->Get(container_name)));

  // Get the container spec;
  ContainerSpec spec = RETURN_IF_ERROR(container->Spec());

  // Output the stats as a proto in binary or ASCII format as specified.
  string spec_output;
  if (FLAGS_lmctfy_binary) {
    spec.SerializeToString(&spec_output);
  } else {
    ::google::protobuf::TextFormat::PrintToString(spec, &spec_output);
  }
  output->AddRaw(spec_output);

  return Status::OK;
}

void RegisterSpecCommand() {
  RegisterRootCommand(
      CMD("spec",
          "Get the resource isolation specification of the specified "
          "container. If no container is specified, the current one is "
          "assumed. The spec is output as a ContainerSpec proto in ASCII "
          "format. If -b is specified it is output in binary form.",
          "[-b] [<container name>]", CMD_TYPE_GETTER, 0, 1, &SpecContainer));
}

}  // namespace cli
}  // namespace lmctfy
}  // namespace containers
