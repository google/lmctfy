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

#ifndef PRODUCTION_CONTAINERS_NSCON_NS_HANDLE_MOCK_H__
#define PRODUCTION_CONTAINERS_NSCON_NS_HANDLE_MOCK_H__

#include "nscon/ns_handle.h"

#include "gmock/gmock.h"

namespace containers {
namespace nscon {

class MockNsHandleFactory : public NsHandleFactory {
 public:
  MockNsHandleFactory() : NsHandleFactory(nullptr) {}

  MOCK_CONST_METHOD1(Get, ::util::StatusOr<const NsHandle *>(pid_t pid));
  MOCK_CONST_METHOD1(
      Get, ::util::StatusOr<const NsHandle *>(const string &handlestr));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockNsHandleFactory);
};

class MockNsHandle : public NsHandle {
 public:
  MockNsHandle() : NsHandle(0, "", nullptr) {}

  MOCK_CONST_METHOD0(IsValid, bool(void));
  MOCK_CONST_METHOD0(ToString, const string(void));
  MOCK_CONST_METHOD0(ToPid, pid_t(void));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockNsHandle);
};

}  // namespace nscon
}  // namespace containers

#endif  // PRODUCTION_CONTAINERS_NSCON_NS_HANDLE_MOCK_H__

