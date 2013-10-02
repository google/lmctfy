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

#ifndef UTIL_TESTING_EQUALS_INITIALIZED_PROTO_H__
#define UTIL_TESTING_EQUALS_INITIALIZED_PROTO_H__

#include <string>

#include "gmock/gmock.h"

namespace testing {

MATCHER_P(EqualsInitializedProto, expected_proto, "") {
  ::std::string expected_str;
  expected_proto.SerializeToString(&expected_str);
  ::std::string actual_str;
  arg.SerializeToString(&actual_str);
  return expected_str == actual_str;
}

}  // namespace testing

#endif  // UTIL_TESTING_EQUALS_INITIALIZED_PROTO_H__
