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

#ifndef UTIL_PROCESS_MOCK_SUBPROCESS_H__
#define UTIL_PROCESS_MOCK_SUBPROCESS_H__

#include "util/process/subprocess.h"
#include "gmock/gmock.h"

class MockSubProcess : public SubProcess {
 public:
  MOCK_CONST_METHOD0(pid, pid_t());
  MOCK_CONST_METHOD0(running, bool());
  MOCK_CONST_METHOD0(error_text, string());
  MOCK_CONST_METHOD0(exit_code, int());
  MOCK_METHOD0(SetUseSession, void());

  MOCK_METHOD2(SetChannelAction, void(Channel chan, ChannelAction action));
  MOCK_METHOD1(SetArgv, void(const ::std::vector<::std::string> &argv));
  MOCK_METHOD0(Start, bool());
  MOCK_METHOD0(Wait, bool());
  MOCK_METHOD2(Communicate, int(string* stdout_output, string* stderr_output));
};

#endif  // UTIL_PROCESS_MOCK_SUBPROCESS_H__
