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

#ifndef SRC_TASKS_HANDLER_MOCK_H_
#define SRC_TASKS_HANDLER_MOCK_H_

#include "lmctfy/tasks_handler.h"

#include "gmock/gmock.h"

namespace containers {
namespace lmctfy {

class MockTasksHandlerFactory : public TasksHandlerFactory {
 public:
  MOCK_CONST_METHOD2(
      Create, ::util::StatusOr<TasksHandler *>(const string &container_name,
                                               const ContainerSpec &spec));
  MOCK_CONST_METHOD1(Get, ::util::StatusOr<TasksHandler *>(
      const string &container_name));
  MOCK_CONST_METHOD1(Exists, bool(const string &container_name));
  MOCK_CONST_METHOD1(Detect, ::util::StatusOr<string>(pid_t tid));
};

typedef ::testing::NiceMock<MockTasksHandlerFactory>
    NiceMockTasksHandlerFactory;
typedef ::testing::StrictMock<MockTasksHandlerFactory>
    StrictMockTasksHandlerFactory;

class MockTasksHandler : public TasksHandler {
 public:
  explicit MockTasksHandler(const string &name) : TasksHandler(name) {}

  MOCK_METHOD0(Destroy, ::util::Status());
  MOCK_METHOD1(TrackTasks, ::util::Status(const ::std::vector<pid_t> &tids));
  MOCK_METHOD2(Delegate, ::util::Status(::util::UnixUid uid,
                                        ::util::UnixGid gid));
  MOCK_CONST_METHOD1(PopulateMachineSpec, ::util::Status(MachineSpec *spec));
  MOCK_CONST_METHOD1(ListSubcontainers,
                     ::util::StatusOr< ::std::vector<string>>(ListType type));
  MOCK_CONST_METHOD1(ListProcesses,
                     ::util::StatusOr<::std::vector<pid_t>>(ListType type));
  MOCK_CONST_METHOD1(ListThreads,
                     ::util::StatusOr<::std::vector<pid_t>>(ListType type));
};

typedef ::testing::NiceMock<MockTasksHandler> NiceMockTasksHandler;
typedef ::testing::StrictMock<MockTasksHandler> StrictMockTasksHandler;

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_TASKS_HANDLER_MOCK_H_
