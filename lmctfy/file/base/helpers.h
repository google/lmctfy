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

#ifndef FILE_BASE_HELPERS_H__
#define FILE_BASE_HELPERS_H__

#include <string>

#include "strings/stringpiece.h"
#include "util/task/status.h"

namespace file {

class Options {
};

const Options &Defaults();

// Read contents of a file to a string.
::util::Status GetContents(StringPiece filename,
                           ::std::string* output,
                           const Options& options);

::util::Status SetContents(StringPiece filename,
                           StringPiece content,
                           const Options& options);

}  // namespace file

#endif  // FILE_BASE_HELPERS_H__
