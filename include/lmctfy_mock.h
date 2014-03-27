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

#ifndef INCLUDE_LMCTFY_MOCK_H_
#define INCLUDE_LMCTFY_MOCK_H_

#include "include/lmctfy.h"
#include "gmock/gmock.h"

namespace containers {
namespace lmctfy {

class MockContainerApi : public ContainerApi {
 public:
  virtual ~MockContainerApi() {}

  MOCK_CONST_METHOD1(Get,
                     ::util::StatusOr<Container *>(StringPiece container_name));
  MOCK_CONST_METHOD2(Create,
                     ::util::StatusOr<Container *>(StringPiece container_name,
                                                   const ContainerSpec &spec));
  MOCK_CONST_METHOD1(Destroy, ::util::Status(Container *container));
  MOCK_CONST_METHOD1(Detect, ::util::StatusOr<string>(pid_t pid));
};

typedef ::testing::NiceMock<MockContainerApi> NiceMockContainerApi;
typedef ::testing::StrictMock<MockContainerApi> StrictMockContainerApi;

class MockContainer : public Container {
 public:
  explicit MockContainer(const string &container_name)
      : Container(container_name) {}
  virtual ~MockContainer() {}

  MOCK_METHOD2(Update, ::util::Status(const ContainerSpec &spec,
                                      UpdatePolicy policy));
  MOCK_METHOD1(Enter, ::util::Status(const ::std::vector<pid_t> &pids));
  MOCK_METHOD2(Run,
               ::util::StatusOr<pid_t>(const ::std::vector<string> &command,
                                       const RunSpec &spec));
  MOCK_METHOD1(Exec, ::util::Status(const ::std::vector<string> &command));
  MOCK_CONST_METHOD0(Spec, ::util::StatusOr<ContainerSpec>());
  MOCK_CONST_METHOD1(
      ListSubcontainers,
      ::util::StatusOr< ::std::vector<Container *>>(ListPolicy policy));
  MOCK_CONST_METHOD1(
      ListThreads, ::util::StatusOr< ::std::vector<pid_t>>(ListPolicy policy));
  MOCK_CONST_METHOD1(ListProcesses, ::util::StatusOr< ::std::vector<pid_t>>(
                                        ListPolicy policy));
  MOCK_METHOD0(Pause, ::util::Status());
  MOCK_METHOD0(Resume, ::util::Status());
  MOCK_CONST_METHOD1(Stats, ::util::StatusOr<ContainerStats>(StatsType type));
  MOCK_METHOD2(RegisterNotification,
               ::util::StatusOr<NotificationId>(const EventSpec &spec,
                                                EventCallback *callback));
  MOCK_METHOD1(UnregisterNotification,
               ::util::Status(NotificationId notification_id));
  MOCK_METHOD0(KillAll, ::util::Status());
  MOCK_CONST_METHOD0(GetInitPid, ::util::StatusOr<pid_t>());
  MOCK_METHOD0(Destroy, ::util::Status());
};

typedef ::testing::NiceMock<MockContainer> NiceMockContainer;
typedef ::testing::StrictMock<MockContainer> StrictMockContainer;

}  // namespace lmctfy
}  // namespace containers

#endif  // INCLUDE_LMCTFY_MOCK_H_
