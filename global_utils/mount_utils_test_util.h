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

#ifndef GLOBAL_UTILS_MOUNT_UTILS_TEST_UTIL_H_
#define GLOBAL_UTILS_MOUNT_UTILS_TEST_UTIL_H_

#include <set>
#include <string>
#include <vector>

#include "global_utils/mount_utils.h"
#include "gmock/gmock.h"
#include "util/safe_types/bytes.h"
#include "util/task/statusor.h"

namespace util {

class MockMountUtils : public MountUtils {
 public:
  MOCK_CONST_METHOD3(BindMount,
                     ::util::Status(const string &source,
                                    const string &target,
                                    const ::std::set<BindMountOpts> &opts));

  MOCK_CONST_METHOD1(GetMountInfo, ::util::StatusOr<MountObject>(
      const string &mountpoint));

  MOCK_CONST_METHOD3(MountDevice, ::util::Status(const string &device_file,
                                                 const string &mountpoint,
                                                 const Mode mode));

  MOCK_CONST_METHOD3(MountTmpfs,
                     ::util::Status(const string &visible_at,
                                    const Bytes size_bytes,
                                    const ::std::vector<string> &mount_opts));

  MOCK_CONST_METHOD1(Unmount, ::util::Status(const string &mountpoint));

  MOCK_CONST_METHOD1(UnmountRecursive, ::util::Status(const string &path));
};

extern const MountUtils *GlobalMountUtils();

class MockMountUtilsOverride {
 public:
  ::testing::StrictMock<MockMountUtils> &Mock() {
    static ::testing::StrictMock<MockMountUtils> *ptr =
        (::testing::StrictMock<MockMountUtils> *)GlobalMountUtils();
    return *ptr;
  }
};

}  // namespace util

#endif  // GLOBAL_UTILS_MOUNT_UTILS_TEST_UTIL_H_
