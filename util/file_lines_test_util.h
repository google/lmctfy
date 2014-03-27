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

#ifndef UTIL_FILE_LINES_TEST_UTIL_H_
#define UTIL_FILE_LINES_TEST_UTIL_H_

#include <memory>
#include <string>
#include <vector>

#include "system_api/libc_fs_api_test_util.h"

namespace util {

// Utility for testing FileLines-like file-line reading behavior.
//
// Mocks a file fopen() and fclose() with a series of line reads (fgets()) in
// between. Each line is provided along with an infinite number of EOF when
// those are exhausted.
//
// Example:
//
// FileLinesTestUtil test_util;
//
// test_util.ExpectFileLines("/tmp/names",
//                           {"bob", "john"})
//
// // Code being tested.
// vector<strings> users;
// for (const string &user : FileLines()) {
//   users.push_back(user);
// }
//
// ASSERT_EQ(2, users.size());
// EXPECT_EQ("bob", users[0]);
// EXPECT_EQ("john", users[1]);
//
// Similarly, this util can be used with any FileLines()-derived container that
// iterates over file lines and stored them under a type (e.g.: ProcMounts()).
//
// This class is thread-hostile as it stores the expectation in global state.
class FileLinesTestUtil {
 public:
  // Creates a global mock libc override, and takes ownership of it.
  FileLinesTestUtil();

  // Uses the specified mock libc override. Does not take ownership of
  // the passed in 'mock_libc'.
  explicit FileLinesTestUtil(::system_api::MockLibcFsApiOverride* mock_libc);

  // Deletes mock_libc_ if it is owned by this class.
  ~FileLinesTestUtil();

  // Expect the specified file to contain the specified lines (or set of lines,
  // one per invocation).
  void ExpectFileLines(const string &filename,
                       const ::std::vector<string> &lines);
  void ExpectFileLinesMulti(const string &filename,
                            const ::std::vector<::std::vector<string>> &lines);

 private:
  ::std::vector<::std::unique_ptr<FILE>> files_;
  ::system_api::MockLibcFsApiOverride *mock_libc_;
  bool own_mock_libc_;

  DISALLOW_COPY_AND_ASSIGN(FileLinesTestUtil);
};

}  // namespace util

#endif  // UTIL_FILE_LINES_TEST_UTIL_H_
