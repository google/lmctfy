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

#ifndef PRODUCTION_CONTAINERS_NSCON_NS_UTIL_MOCK_H_
#define PRODUCTION_CONTAINERS_NSCON_NS_UTIL_MOCK_H_

#include "nscon/ns_util.h"

#include "gmock/gmock.h"

namespace containers {
namespace nscon {

class MockSavedNamespace : public SavedNamespace {
 public:
  MockSavedNamespace() : SavedNamespace(0, 0) {
    // Cancel the fd_closer_ as we don't want it to get executed at destruction
    // time.
    fd_closer_.Cancel();
  }

  MOCK_METHOD0(RestoreAndDelete, ::util::Status());

 private:
  DISALLOW_COPY_AND_ASSIGN(MockSavedNamespace);
};

class MockNsUtil : public NsUtil {
 public:
  MockNsUtil() : NsUtil(/* supported_namespaces */{}) {}

  virtual ~MockNsUtil() {}

  MOCK_CONST_METHOD2(AttachNamespaces,
                     ::util::Status(const ::std::vector<int>& namespaces,
                                    pid_t target));

  MOCK_CONST_METHOD1(UnshareNamespaces,
                     ::util::Status(const ::std::vector<int>& namespaces));

  MOCK_CONST_METHOD1(NsCloneFlagToName,
                     ::util::StatusOr<const char*>(int clone_flag));

  MOCK_CONST_METHOD1(GetUnsharedNamespaces,
                     ::util::StatusOr<const ::std::vector<int>>(pid_t pid));

  MOCK_CONST_METHOD1(IsNamespaceSupported, bool(int ns));

  MOCK_CONST_METHOD2(GetNamespaceId,
                     ::util::StatusOr<string>(pid_t pid, int ns));

  MOCK_CONST_METHOD1(SaveNamespace,
                    ::util::StatusOr<SavedNamespace *>(int ns));

  MOCK_CONST_METHOD1(CharacterDeviceFileExists,
                     ::util::Status(const string &path));

  MOCK_CONST_METHOD1(AttachToConsoleFd, ::util::Status(const int console_fd));

  MOCK_CONST_METHOD1(OpenSlavePtyDevice,
                     ::util::StatusOr<int>(const string &slave_pty));

  MOCK_CONST_METHOD0(GetOpenFDs, ::util::StatusOr<::std::vector<int>>());

 private:
  DISALLOW_COPY_AND_ASSIGN(MockNsUtil);
};

}  // namespace nscon
}  // namespace containers

#endif  // PRODUCTION_CONTAINERS_NSCON_NS_UTIL_MOCK_H_
