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

#include "util/file_lines.h"

#include <memory>
#include <string>
#include <vector>

#include "base/callback.h"
#include "base/logging.h"
#include "system_api/libc_fs_api_test_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::std::string;
using ::std::unique_ptr;
using ::std::vector;
using ::testing::Contains;
using ::testing::DoAll;
using ::testing::Invoke;
using ::system_api::MockLibcFsApiOverride;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::StrEq;
using ::testing::StrictMock;
using ::testing::_;

namespace util {
namespace {

// Copy the first size bytes of the first line in expected_lines into s.
static vector<string> expected_lines;
void CopyString(char *s, int size, FILE *stream) {
  strncpy(s, expected_lines.front().c_str(), size);
  expected_lines.erase(expected_lines.begin());
}

static const char kFilePath[] = "/proc/lines";

class FileLinesTest : public ::testing::Test {
 public:
  static void TearDownTestCase() {
    // Gtest doesn't like that this static variable is never deleted.
    delete ::system_api::GlobalLibcFsApi();
  }

  // Expect the opening and closing of the specified file.
  void ExpectFileOpenClose(const string &file_path) {
    EXPECT_CALL(mock_libc_.Mock(), FOpen(StrEq(file_path), StrEq("r")))
        .WillOnce(Return(&lines_file_));
    EXPECT_CALL(mock_libc_.Mock(), FClose(&lines_file_))
        .WillOnce(Return(0));
  }

  // Expect lines_file_ to have the specified lines.
  void ExpectContents(const vector<string> &lines,
                      bool expect_null_terminator) {
    char tmp[0];
    auto &mock_fgets =
        EXPECT_CALL(mock_libc_.Mock(), FGetS(NotNull(), _, &lines_file_));

    // Expect each line and then EOF.
    expected_lines.clear();
    for (const string &line : lines) {
      expected_lines.push_back(line);
      mock_fgets.WillOnce(DoAll(Invoke(CopyString), Return(tmp)));
    }

    // Whether to expect the nulltermination.
    if (expect_null_terminator) {
      mock_fgets.WillRepeatedly(Return(nullptr));
    }
  }

 protected:
  FILE lines_file_;
  MockLibcFsApiOverride mock_libc_;
};

TEST_F(FileLinesTest, ForEachLoop) {
  ExpectFileOpenClose(kFilePath);
  ExpectContents({"1", "2", "3", "4"}, true);

  vector<string> output_lines;
  for (const auto &line : FileLines(kFilePath)) {
    output_lines.push_back(line.ToString());
  }

  ASSERT_EQ(4, output_lines.size());
  EXPECT_EQ("1", output_lines[0]);
  EXPECT_EQ("2", output_lines[1]);
  EXPECT_EQ("3", output_lines[2]);
  EXPECT_EQ("4", output_lines[3]);
}

TEST_F(FileLinesTest, ExplicitForLoop) {
  ExpectFileOpenClose(kFilePath);
  ExpectContents({"1", "2", "3", "4"}, true);

  vector<string> output_lines;
  FileLines lines(kFilePath);
  for (auto it = lines.begin(); it != lines.end(); ++it) {
    output_lines.push_back(it->ToString());
  }

  ASSERT_EQ(4, output_lines.size());
  EXPECT_EQ("1", output_lines[0]);
  EXPECT_EQ("2", output_lines[1]);
  EXPECT_EQ("3", output_lines[2]);
  EXPECT_EQ("4", output_lines[3]);
}

TEST_F(FileLinesTest, ExplicitForLoopPostIncrement) {
  ExpectFileOpenClose(kFilePath);
  ExpectContents({"1", "2", "3", "4"}, true);

  vector<string> output_lines;
  FileLines lines(kFilePath);
  for (auto it = lines.begin(); it != lines.end(); it++) {
    output_lines.push_back(it->ToString());
  }

  ASSERT_EQ(4, output_lines.size());
  EXPECT_EQ("1", output_lines[0]);
  EXPECT_EQ("2", output_lines[1]);
  EXPECT_EQ("3", output_lines[2]);
  EXPECT_EQ("4", output_lines[3]);
}

TEST_F(FileLinesTest, ConstExplicitForLoop) {
  ExpectFileOpenClose(kFilePath);
  ExpectContents({"1", "2", "3", "4"}, true);

  vector<string> output_lines;
  const FileLines lines(kFilePath);
  for (auto it = lines.cbegin(); it != lines.cend(); ++it) {
    output_lines.push_back(it->ToString());
  }

  ASSERT_EQ(4, output_lines.size());
  EXPECT_EQ("1", output_lines[0]);
  EXPECT_EQ("2", output_lines[1]);
  EXPECT_EQ("3", output_lines[2]);
  EXPECT_EQ("4", output_lines[3]);
}

TEST_F(FileLinesTest, DoesNotIterateOverAllLines) {
  ExpectFileOpenClose(kFilePath);
  ExpectContents({"1"}, false);

  for (const auto &line : FileLines(kFilePath)) {
    EXPECT_EQ("1", line.ToString());
    break;
  }
}

TEST_F(FileLinesTest, EmptyFile) {
  ExpectFileOpenClose(kFilePath);
  ExpectContents({}, true);

  bool has_lines = false;
  for (const auto &line : FileLines(kFilePath)) {
    LOG(INFO) << line.ToString();
    has_lines = true;
  }

  EXPECT_FALSE(has_lines);
}

TEST_F(FileLinesTest, OpenFileFails) {
  EXPECT_CALL(mock_libc_.Mock(), FOpen(StrEq(kFilePath), StrEq("r")))
      .WillOnce(Return(nullptr));

  bool has_lines = false;
  for (const auto &line : FileLines(kFilePath)) {
    LOG(INFO) << line.ToString();
    has_lines = true;
  }

  EXPECT_FALSE(has_lines);
}

TEST_F(FileLinesTest, CloseFileFails) {
  EXPECT_CALL(mock_libc_.Mock(), FOpen(StrEq(kFilePath), StrEq("r")))
      .WillOnce(Return(&lines_file_));
  EXPECT_CALL(mock_libc_.Mock(), FClose(&lines_file_))
      .WillRepeatedly(Return(-1));
  ExpectContents({"1", "2"}, true);

  vector<string> output_lines;
  for (const auto &line : FileLines(kFilePath)) {
    output_lines.push_back(line.ToString());
  }

  // A close error is ignored.
  ASSERT_EQ(2, output_lines.size());
  EXPECT_EQ("1", output_lines[0]);
  EXPECT_EQ("2", output_lines[1]);
}

}  // namespace
}  // namespace util
