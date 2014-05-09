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

#ifndef INCLUDE_NAMESPACE_CONTROLLER_MOCK_H_
#define INCLUDE_NAMESPACE_CONTROLLER_MOCK_H_

#include "include/namespace_controller.h"
#include "gmock/gmock.h"

namespace containers {
namespace nscon {

class MockNamespaceControllerFactory : public NamespaceControllerFactory {
 public:
  virtual ~MockNamespaceControllerFactory() {}

  MOCK_CONST_METHOD1(Get,
                     ::util::StatusOr<NamespaceController *>(pid_t pid));
  MOCK_CONST_METHOD1(Get, ::util::StatusOr<NamespaceController *>(
                         const string &handlestr));
  MOCK_CONST_METHOD2(Create, ::util::StatusOr<NamespaceController *>(
                         const NamespaceSpec &spec,
                         const ::std::vector<string> &init_argv));
  MOCK_CONST_METHOD1(GetNamespaceId, ::util::StatusOr<string>(pid_t pid));
};

typedef ::testing::NiceMock<MockNamespaceControllerFactory>
NiceMockNamespaceControllerFactory;
typedef ::testing::StrictMock<MockNamespaceControllerFactory>
StrictMockNamespaceControllerFactory;

class MockNamespaceController : public NamespaceController {
 public:
  virtual ~MockNamespaceController() {}

  MOCK_CONST_METHOD2(Run, ::util::StatusOr<pid_t>(
      const ::std::vector<string> &command,
      const RunSpec &run_spec));
  MOCK_CONST_METHOD1(Exec,
                     ::util::Status(const ::std::vector<string> &command));
  MOCK_METHOD1(Update, ::util::Status(const NamespaceSpec &spec));
  MOCK_METHOD0(Destroy, ::util::Status());
  MOCK_CONST_METHOD0(IsValid, bool());
  MOCK_CONST_METHOD0(GetHandleString, const string());
  MOCK_CONST_METHOD0(GetPid, pid_t());
};

typedef ::testing::NiceMock<MockNamespaceController>
NiceMockNamespaceController;
typedef ::testing::StrictMock<MockNamespaceController>
StrictMockNamespaceController;

}  // namespace nscon
}  // namespace containers

#endif  // INCLUDE_NAMESPACE_CONTROLLER_MOCK_H_
