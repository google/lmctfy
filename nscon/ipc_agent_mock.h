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

#ifndef PRODUCTION_CONTAINERS_NSCON_IPC_AGENT_MOCK_H__
#define PRODUCTION_CONTAINERS_NSCON_IPC_AGENT_MOCK_H__

#include "nscon/ipc_agent.h"

#include "gmock/gmock.h"

namespace containers {
namespace nscon {

class MockIpcAgentFactory : public IpcAgentFactory {
 public:
  MockIpcAgentFactory() {}

  MOCK_CONST_METHOD0(Create, ::util::StatusOr<IpcAgent *>(void));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockIpcAgentFactory);
};

class MockIpcAgent : public IpcAgent {
 public:
  MockIpcAgent() : IpcAgent(-1, "", (const int[2]){-1, -1}) {}

  MOCK_METHOD1(WriteData, ::util::Status(const string &data));
  MOCK_METHOD0(ReadData, ::util::StatusOr<::std::pair<string, pid_t>>());
  MOCK_METHOD0(WaitForChild, ::util::Status());
  MOCK_METHOD0(SignalParent, ::util::Status());
  MOCK_METHOD0(Destroy, ::util::Status());

 private:
  DISALLOW_COPY_AND_ASSIGN(MockIpcAgent);
};

}  // namespace nscon
}  // namespace containers

#endif  // PRODUCTION_CONTAINERS_NSCON_IPC_AGENT_MOCK_H__
