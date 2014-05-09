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

#include "strings/stringpiece.h"

namespace util {
namespace file_lines_internal {

bool FileLinesParseToStringPiece(const char *parsed_line, StringPiece *data) {
  data->set(parsed_line);
  return true;
}

}  // namespace file_lines_internal
}  // namespace util
