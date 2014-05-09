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

#include "file/memfile/inlinefile.h"

#include <stdio.h>
#include <memory>

using ::std::unique_ptr;
using ::std::string;

string GetInlineFilename(const ::string& data) {
  unique_ptr<char[]> buf(new char[4 << 10]);
  const string filename = tmpnam_r(buf.get());

  // Create file and write the contents.
  FILE *fp = fopen(filename.c_str(), "w+");
  if (fp == nullptr) {
    return "";
  }
  fprintf(fp, "%s", data.c_str());
  fclose(fp);

  return filename;
}
