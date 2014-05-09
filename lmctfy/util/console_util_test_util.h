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

#ifndef SRC_UTIL_CONSOLE_UTIL_TEST_UTIL_H_
#define SRC_UTIL_CONSOLE_UTIL_TEST_UTIL_H_

#include "lmctfy/util/console_util.h"

#include "gmock/gmock.h"

namespace containers {

class MockConsoleUtil : public ConsoleUtil {
 public:
  MockConsoleUtil() {}
  virtual ~MockConsoleUtil() {}

  MOCK_CONST_METHOD0(EnableDevPtsNamespaceSupport, ::util::Status());
};

}  // namespace containers

#endif  // SRC_UTIL_CONSOLE_UTIL_TEST_UTIL_H_
