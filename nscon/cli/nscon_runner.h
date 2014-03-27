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

#ifndef PRODUCTION_CONTAINERS_NSCON_CLI_NSCON_RUNNER_H__
#define PRODUCTION_CONTAINERS_NSCON_CLI_NSCON_RUNNER_H__

#include "util/task/status.h"

namespace containers {
namespace nscon {
namespace cli {

// Prepares nscon for execution in a single-threaded environment and executes
// requested user operation.
// Class is thread hostile.
class NsconRunner {
 public:
  NsconRunner();

  ~NsconRunner();

  // Parses the input and executes the user requested operation.
  // Input:
  //   Unmodified command line arguments 'argv' and 'argc'.
  // Returns:
  //   INVALID_ARGUMENT if the user input is invalid.
  //   Result of the user requested operation otherwise.
  ::util::Status Run(int argc, char **argv) const;

 private:
  void SetDefaultFlags() const;

  DISALLOW_COPY_AND_ASSIGN(NsconRunner);
};

}  // namespace cli
}  // namespace nscon
}  // namespace containers

#endif  // PRODUCTION_CONTAINERS_NSCON_CLI_NSCON_RUNNER_H__
