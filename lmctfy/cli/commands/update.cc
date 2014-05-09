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

#include "lmctfy/cli/commands/update.h"

#include <memory>
#include <string>
using ::std::string;
#include <vector>

#include "lmctfy/cli/command.h"
#include "lmctfy/cli/commands/util.h"
#include "include/lmctfy.h"
#include "include/lmctfy.pb.h"
#include "util/errors.h"
#include "strings/strcat.h"
#include "strings/substitute.h"
#include "util/task/codes.pb.h"
#include "util/task/status.h"

using ::std::unique_ptr;
using ::std::vector;
using ::strings::Substitute;
using ::util::Status;

namespace containers {
namespace lmctfy {
namespace cli {

// Command to update a container.
Status UpdateContainer(Container::UpdatePolicy policy,
                       const vector<string> &argv,
                       const ContainerApi *lmctfy,
                       OutputMap *output) {
  // Args: replace/diff <container name> [<container spec>]
  if (!in_range(argv.size(), 2, 4)) {
    return Status(::util::error::INVALID_ARGUMENT,
                  "See help for supported options.");
  }
  const string container_name = argv[1];

  ContainerSpec spec;
  RETURN_IF_ERROR(GetSpecFromConfigOrInline(
      argv,
      2 /* expected position of inline config */,
      &spec));

  // Ensure the container exists.
  unique_ptr<Container> container(
      RETURN_IF_ERROR(lmctfy->Get(container_name)));

  return container->Update(spec, policy);
}

Status UpdateReplace(const vector<string> &argv,
                     const ContainerApi *lmctfy,
                     OutputMap *output) {
  return UpdateContainer(Container::UPDATE_REPLACE, argv, lmctfy, output);
}

Status UpdateDiff(const vector<string> &argv,
                  const ContainerApi *lmctfy,
                  OutputMap *output) {
  return UpdateContainer(Container::UPDATE_DIFF, argv, lmctfy, output);
}

void RegisterUpdateCommand() {
  static const char *kDescriptionTemplate =
      "Update a container from the spec. The spec is provided either on "
      "the command line or via a config file using the -c flag. The config "
      "can be an ASCII or binary proto in either case. "
      "$0 "
      "Note that repeated fields are always considered set.";
  static const string kReplaceDescription =
      Substitute(kDescriptionTemplate,
                 "The unset fields are filled with defaults.");
  static const string kDiffDescription =
                Substitute(kDescriptionTemplate,
                           "Only set fields are applied.");
  static const char *kArgumentsFormat =
      "[-c <config file>] <container name> "
      "[<spec proto in text or binary mode>]";
  static const string kUpdateArgumentsFormat =
      StrCat("<update policy> ", kArgumentsFormat);
  RegisterRootCommand(
      SUB("update",
          "Update a container from the spec.",
          kUpdateArgumentsFormat.c_str(), {
            CMD("replace",
                kReplaceDescription.c_str(),
                kArgumentsFormat,
                CMD_TYPE_SETTER,
                1,
                2,
                &UpdateReplace),
            CMD("diff",
                kDiffDescription.c_str(),
                kArgumentsFormat,
                CMD_TYPE_SETTER,
                1,
                2,
                &UpdateDiff)
          }));
}

}  // namespace cli
}  // namespace lmctfy
}  // namespace containers
