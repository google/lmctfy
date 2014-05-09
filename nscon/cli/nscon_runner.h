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

#include <vector>

#include "util/task/statusor.h"

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
  //   0 on success and appropriate ::util::error::Code on failure.
  int Run(int argc, char **argv);

 private:
  void SetDefaultFlags() const;
  ::util::Status SetupOutput();
  ::util::StatusOr<string> InternalRun(
       int argc, char **argv, const ::std::vector<string> &user_command) const;

  // Closing either of these fds will possibly make the other one useless. Hence
  // if either is being passed anywhere outside of this class, then consider
  // dupping fds.
  FILE *nscon_stdout_;
  FILE *nscon_stderr_;


  DISALLOW_COPY_AND_ASSIGN(NsconRunner);
};

}  // namespace cli
}  // namespace nscon
}  // namespace containers

#endif  // PRODUCTION_CONTAINERS_NSCON_CLI_NSCON_RUNNER_H__
