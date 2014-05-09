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

#ifndef SRC_CLI_COMMANDS_STATS_H_
#define SRC_CLI_COMMANDS_STATS_H_

#include <string>
using ::std::string;
#include <vector>

#include "lmctfy/cli/output_map.h"
#include "util/task/status.h"

namespace containers {
namespace lmctfy {

class ContainerApi;

namespace cli {

class OutputMap;

// Command to get summary stats for a container.
::util::Status StatsSummary(const ::std::vector<string> &argv,
                            const ContainerApi *lmctfy,
                            OutputMap *output);

// Command to get full stats for a container.
::util::Status StatsFull(const ::std::vector<string> &argv,
                         const ContainerApi *lmctfy,
                         OutputMap *output);

void RegisterStatsCommand();

}  // namespace cli
}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_CLI_COMMANDS_STATS_H_
