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

#ifndef PRODUCTION_CONTAINERS_NSCON_PROCESS_LAUNCHER_MOCK_H__
#define PRODUCTION_CONTAINERS_NSCON_PROCESS_LAUNCHER_MOCK_H__

#include "nscon/process_launcher.h"

#include "gmock/gmock.h"

namespace containers {
namespace nscon {

class MockProcessLauncher : public ProcessLauncher {
 public:
  MockProcessLauncher() : ProcessLauncher(nullptr, nullptr, nullptr) {}

  MOCK_CONST_METHOD4(NewNsProcessInTarget,
                     ::util::StatusOr<pid_t>(
                         const ::std::vector<string> &argv,
                         const ::std::vector<int> &namespaces,
                         pid_t ns_target,
                         const RunSpec &run_spec));

  MOCK_CONST_METHOD5(NewNsProcess,
                     ::util::StatusOr<pid_t>(
                         const ::std::vector<string> &argv,
                         const ::std::vector<int> &namespaces,
                         const ::std::vector<NsConfigurator *> &configurators,
                         const NamespaceSpec &spec,
                         const RunSpec &run_spec));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockProcessLauncher);
};

}  // namespace nscon
}  // namespace containers

#endif  // PRODUCTION_CONTAINERS_NSCON_PROCESS_LAUNCHER_MOCK_H__
