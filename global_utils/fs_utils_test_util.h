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

// Unit testing utilities for the MountUtils module.

#ifndef GLOBAL_UTILS_FS_UTILS_TEST_UTIL_H_
#define GLOBAL_UTILS_FS_UTILS_TEST_UTIL_H_

#include <string>

#include "global_utils/fs_utils.h"
#include "gmock/gmock.h"
#include "util/task/statusor.h"

namespace util {

class MockFsUtils : public FsUtils {
 public:
  MOCK_CONST_METHOD2(SafeEnsureDir, ::util::Status(const string &dirpath,
                                                   mode_t mode));
  MOCK_CONST_METHOD1(DirExists, ::util::Status(const string &dirpath));
  MOCK_CONST_METHOD1(FileExists, ::util::StatusOr<bool>(
      const string &filepath));
};

extern const FsUtils *GlobalFsUtils();

class MockFsUtilsOverride {
 public:
  ::testing::StrictMock<MockFsUtils> &Mock() {
    static ::testing::StrictMock<MockFsUtils> *ptr =
        (::testing::StrictMock<MockFsUtils> *)GlobalFsUtils();
    return *ptr;
  }
};

}  // namespace util

#endif  // GLOBAL_UTILS_FS_UTILS_TEST_UTIL_H_
