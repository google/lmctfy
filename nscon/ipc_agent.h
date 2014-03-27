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

//
// ipc_agent.h
// Interface for IpcAgent class that provides minimal functionality to transfer
// some data (like a pid) across processes in different namespaces.
//
#ifndef PRODUCTION_CONTAINERS_NSCON_IPC_AGENT_H__
#define PRODUCTION_CONTAINERS_NSCON_IPC_AGENT_H__

#include <utility>

#include "util/task/status.h"
#include "util/task/statusor.h"

namespace containers {
namespace nscon {

class IpcAgent;

class IpcAgentFactory {
 public:
  IpcAgentFactory() {}

  virtual ~IpcAgentFactory() {}

  // Returns an initialized instance of the IpcAgent.
  virtual ::util::StatusOr<IpcAgent *> Create() const;

 private:
  DISALLOW_COPY_AND_ASSIGN(IpcAgentFactory);
};

//
// IpcAgent
// This class provides interface to send data (like a pid) between
// same/different processes (typically between parent and child). The
// communicating processes may belong to different namespaces.
//
// Typical Usage:
//    IpcAgent *ipc_agent = RETURN_IF_ERROR(ipc_agent_factory->Create());
//    ... pid = fork() ...
//    PARENT                                  CHILD
//    statusor = ipc_agent->ReadData()  ...
//    <parent blocked>
//                                            ipc_agent->WriteData(data)
//    <parent unblocked>                      ...
//    uint32 data = statusor.ValueOrDie()
//    RETURN_IF_ERROR(ipc_agent_->Destroy());
//
// This interface is not responsible for providing synchronization between the
// callers. For example, if parent waits on ReadUint32() and child exits before
// sending any data, then the parent may block forever.
//
// The initial implementation of this class uses unix-domain sockets for IPC.
//
class IpcAgent {
 public:
  virtual ~IpcAgent() {}

  // WriteData is safe to be called between fork() and exec().
  virtual ::util::Status WriteData(const string &data) = 0;
  // Returns the data read and PID of the sender.
  virtual ::util::StatusOr<::std::pair<string, pid_t>> ReadData() = 0;
  // Takes ownership of this object and releases it. The object is in undefined
  // state if this function returns error.
  virtual ::util::Status Destroy() = 0;

 protected:
  IpcAgent() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(IpcAgent);
};

}  // namespace nscon
}  // namespace containers

#endif  // PRODUCTION_CONTAINERS_NSCON_IPC_AGENT_H__
