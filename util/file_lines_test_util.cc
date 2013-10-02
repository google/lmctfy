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

#include "util/file_lines_test_util.h"

#include "base/callback.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::std::string;
using ::std::vector;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::StrEq;
using ::testing::_;
using ::system_api::MockLibcFsApiOverride;

namespace util {

namespace file_lines_test_util_internal {

// Copy the first size bytes of the first line in expected_lines into s.
static vector<string> expected_lines;
static void CopyString(char *s, int size, FILE *stream) {
  strncpy(s, expected_lines.front().c_str(), size);
  expected_lines.erase(expected_lines.begin());
}

}  // namespace file_lines_test_util_internal

FileLinesTestUtil::FileLinesTestUtil()
    : mock_libc_(new MockLibcFsApiOverride()), own_mock_libc_(true) {}
FileLinesTestUtil::FileLinesTestUtil(MockLibcFsApiOverride *mock_libc)
    : mock_libc_(mock_libc), own_mock_libc_(false) {}
FileLinesTestUtil::~FileLinesTestUtil() {
  if (own_mock_libc_) {
    delete mock_libc_;
  }
}

void FileLinesTestUtil::ExpectFileLines(const string &filename,
                                        const vector<string> &lines) {
  // Expect Open/Close of the file
  EXPECT_CALL(mock_libc_->Mock(), FOpen(StrEq(filename), StrEq("r")))
      .WillRepeatedly(Return(&file_));
  EXPECT_CALL(mock_libc_->Mock(), FClose(&file_))
      .WillRepeatedly(Return(0));

  // Expect file to have the specified lines.
  char tmp[0];
  auto &mock_fgets =
      EXPECT_CALL(mock_libc_->Mock(), FGetS(NotNull(), _, &file_));

  // Expect each line and then EOF.
  for (const string &line : lines) {
    file_lines_test_util_internal::expected_lines.push_back(line);
    mock_fgets.WillOnce(
        DoAll(Invoke(file_lines_test_util_internal::CopyString), Return(tmp)));
  }
  mock_fgets.WillRepeatedly(Return(nullptr));
}

}  // namespace util
