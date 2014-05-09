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
  return ExpectFileLinesMulti(filename, {lines});
}

void FileLinesTestUtil::ExpectFileLinesMulti(
    const string &filename, const vector<vector<string>> &lines) {
  // Expect the open calls.
  auto &mock_fopen =
      EXPECT_CALL(mock_libc_->Mock(), FOpen(StrEq(filename), StrEq("r")));
  vector<FILE *> cfiles;
  for (int i = 0; i < lines.size(); ++i) {
    FILE *cfile = new FILE();
    mock_fopen.WillOnce(Return(cfile));
    cfiles.emplace_back(cfile);
    files_.emplace_back(cfile);
  }

  int i = 0;
  for (auto cfile : cfiles) {
    // Expect Close of the file
    EXPECT_CALL(mock_libc_->Mock(), FClose(cfile)).WillRepeatedly(Return(0));
    EXPECT_CALL(mock_libc_->Mock(), FError(cfile)).WillRepeatedly(Return(0));

    // Expect file to have the specified lines.
    char tmp[0];
    auto &mock_fgets =
        EXPECT_CALL(mock_libc_->Mock(), FGetS(NotNull(), _, cfile));

    // Expect each line and then EOF.
    for (const string &line : lines[i++]) {
      mock_fgets.WillOnce(DoAll(Invoke([line](char *s, int size, FILE *stream) {
                                  strncpy(s, line.c_str(), size);
                                }),
                                Return(tmp)));
    }
    mock_fgets.WillRepeatedly(Return(nullptr));
  }
}

}  // namespace util
