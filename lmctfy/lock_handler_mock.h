// Copyright 2013 Google Inc. All Rights Reserved.
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

#ifndef SRC_LOCK_HANDLER_MOCK_H_
#define SRC_LOCK_HANDLER_MOCK_H_

#include "lmctfy/lock_handler.h"

#include "gmock/gmock.h"

namespace containers {
namespace lmctfy {

class MockLockHandlerFactory : public LockHandlerFactory {
 public:
  virtual ~MockLockHandlerFactory() {}

  MOCK_CONST_METHOD1(Create, ::util::StatusOr<LockHandler *>(
      const string &container_name));
  MOCK_CONST_METHOD1(Get, ::util::StatusOr<LockHandler *>(
      const string &container_name));
  MOCK_CONST_METHOD1(InitMachine, ::util::Status(const InitSpec &spec));
};

typedef ::testing::StrictMock<MockLockHandlerFactory>
    StrictMockLockHandlerFactory;
typedef ::testing::NiceMock<MockLockHandlerFactory>
    NiceMockLockHandlerFactory;

class MockLockHandler : public LockHandler {
 public:
  virtual ~MockLockHandler() {}

  MOCK_METHOD0(Destroy, ::util::Status());
  MOCK_METHOD0(ExclusiveLock, ::util::Status());
  MOCK_METHOD0(SharedLock, ::util::Status());
  MOCK_METHOD0(Unlock, void());
};

typedef ::testing::StrictMock<MockLockHandler> StrictMockLockHandler;
typedef ::testing::NiceMock<MockLockHandler> NiceMockLockHandler;

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_LOCK_HANDLER_MOCK_H_
