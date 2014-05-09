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

#ifndef SRC_NAMESPACE_HANDLER_MOCK_H_
#define SRC_NAMESPACE_HANDLER_MOCK_H_

#include "lmctfy/namespace_handler.h"

#include "gmock/gmock.h"

namespace containers {
namespace lmctfy {

class MockNamespaceHandlerFactory : public NamespaceHandlerFactory {
 public:
  MockNamespaceHandlerFactory() {}
  virtual ~MockNamespaceHandlerFactory() {}

  MOCK_CONST_METHOD1(
      GetNamespaceHandler,
      ::util::StatusOr<NamespaceHandler *>(const string &container_name));
  MOCK_METHOD3(
      CreateNamespaceHandler,
      ::util::StatusOr<NamespaceHandler *>(const string &container_name,
                                           const ContainerSpec &spec,
                                           const MachineSpec &machine_spec));
  MOCK_METHOD1(InitMachine, ::util::Status(const InitSpec &spec));
};

typedef ::testing::NiceMock<MockNamespaceHandlerFactory>
    NiceMockNamespaceHandlerFactory;
typedef ::testing::StrictMock<MockNamespaceHandlerFactory>
    StrictMockNamespaceHandlerFactory;

class MockNamespaceHandler : public NamespaceHandler {
 public:
  MockNamespaceHandler(const string &container_name, ResourceType type)
      : NamespaceHandler(container_name, type) {}
  virtual ~MockNamespaceHandler() {}

  MOCK_METHOD1(Exec, ::util::Status(const ::std::vector<string> &command));
  MOCK_METHOD2(Run,
               ::util::StatusOr<pid_t>(const ::std::vector<string> &command,
                                       const RunSpec &spec));
  MOCK_METHOD2(Update, ::util::Status(const ContainerSpec &spec,
                                      Container::UpdatePolicy policy));
  MOCK_METHOD0(Destroy, ::util::Status());
  MOCK_METHOD1(CreateResource, ::util::Status(const ContainerSpec &spec));
  MOCK_METHOD1(Enter, ::util::Status(const ::std::vector<pid_t> &tids));
  MOCK_METHOD2(Delegate, ::util::Status(::util::UnixUid uid,
                                        ::util::UnixGid gid));
  MOCK_CONST_METHOD2(Stats, ::util::Status(Container::StatsType type,
                                           ContainerStats *output));
  MOCK_CONST_METHOD1(Spec, ::util::Status(ContainerSpec *spec));
  MOCK_METHOD2(RegisterNotification,
               ::util::StatusOr<Container::NotificationId>(
                   const EventSpec &spec,
                   Callback1< ::util::Status> *callback));
  MOCK_CONST_METHOD0(GetInitPid, pid_t());
  MOCK_CONST_METHOD1(IsDifferentVirtualHost,
                     ::util::StatusOr<bool>(const ::std::vector<pid_t> &tids));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockNamespaceHandler);
};

typedef ::testing::NiceMock<MockNamespaceHandler> NiceMockNamespaceHandler;
typedef ::testing::StrictMock<MockNamespaceHandler> StrictMockNamespaceHandler;

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_NAMESPACE_HANDLER_MOCK_H_
