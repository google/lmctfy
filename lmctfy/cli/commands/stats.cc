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

#include "lmctfy/cli/commands/stats.h"

#include <stdio.h>
#include <unistd.h>
#include <memory>
#include <string>
using ::std::string;
#include <vector>

#include "gflags/gflags.h"
#include "base/logging.h"
#include "google/protobuf/text_format.h"
#include "lmctfy/cli/command.h"
#include "include/lmctfy.h"
#include "include/lmctfy.pb.h"
#include "util/errors.h"
#include "util/task/statusor.h"

DECLARE_bool(lmctfy_binary);

using ::std::unique_ptr;
using ::std::vector;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {
namespace cli {

// Command to get stats for a container.
static Status StatsContainer(const vector<string> &argv,
                             const ContainerApi *lmctfy,
                             OutputMap *output,
                             Container::StatsType stats_type) {
  // Args: full|summary [<container name>]
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

  // Get the container stats;
  StatusOr<ContainerStats> statusor_stats = container->Stats(stats_type);
  if (!statusor_stats.ok()) {
    return statusor_stats.status();
  }

  // Output the stats as a proto in binary or ASCII format as specified.
  string stats_output;
  if (FLAGS_lmctfy_binary) {
    statusor_stats.ValueOrDie().SerializeToString(&stats_output);
  } else {
    ::google::protobuf::TextFormat::PrintToString(statusor_stats.ValueOrDie(),
                                        &stats_output);
  }
  output->AddRaw(stats_output);

  return Status::OK;
}

// Get summary stats.
Status StatsSummary(const vector<string> &argv, const ContainerApi *lmctfy,
                    OutputMap *output) {
  return StatsContainer(argv, lmctfy, output, Container::STATS_SUMMARY);
}

// Get full stats.
Status StatsFull(const vector<string> &argv, const ContainerApi *lmctfy,
                 OutputMap *output) {
  return StatsContainer(argv, lmctfy, output, Container::STATS_FULL);
}

void RegisterStatsCommand() {
  RegisterRootCommand(
      SUB("stats",
          "Get statistics about the specified container's usage of each "
          "resource.",
          "<stats type> [-b] [<container name>]", {
            CMD("summary",
                "Get summary statistics of a container's usage for each "
                "resource. If no container is specified, those of the calling "
                "process' container are listed. Statistics are output as a "
                "ContainerStats proto in ASCII format. If -b is specified they "
                "are output in binary form.",
                "[-b] [<container name>]",
                CMD_TYPE_GETTER,
                0,
                1,
                &StatsSummary),
            CMD("full",
                "Get full statistics of the specified container's usage for "
                "each resource. If no container is specified, those of the "
                "calling process' container are listed. Statistics are output "
                "as a ContainerStats proto in ASCII format. If -b is specified "
                "they are output in binary form.",
                "[-b] [<container name>]",
                CMD_TYPE_GETTER,
                0,
                1,
                &StatsFull)
          }));
}

}  // namespace cli
}  // namespace lmctfy
}  // namespace containers
