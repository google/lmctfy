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

#include "system_api/libc_fs_api_test_util.h"

#include "gmock/gmock.h"

using system_api::LibcFsApi;

namespace system_api {

const LibcFsApi *GlobalLibcFsApi() {
  static MockLibcFsApi *mock_api = new ::testing::StrictMock<MockLibcFsApi>();

  // gtest things this mock leaks so ignore it when it is used.
  ::testing::Mock::AllowLeak(mock_api);

  return mock_api;
}

}  // namespace system_api
