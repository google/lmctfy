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

#include "file/base/helpers.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "util/gtl/lazy_static_ptr.h"

using ::std::string;
using ::util::Status;

namespace file {

static ::util::gtl::LazyStaticPtr<Options> file_options_defaults;
const Options &Defaults() {
  return *file_options_defaults;
}

// Read contents of a file to a string.
Status GetContents(StringPiece filename,
                   string* output,
                   const Options& ignored) {
  int fd = open(filename.ToString().c_str(), O_RDONLY);
  if (fd < 0) {
    return Status(::util::error::INTERNAL, "Failed to open file");
  }

  int64 kBufSize = 1 << 20;  // 1MB
  char* buf = new char[kBufSize];
  int64 nread = 0;

  // Read until there are no more bytes.
  while ((nread = read(fd, buf, kBufSize)) > 0) {
    output->append(buf, nread);
  }

  delete[] buf;
  close(fd);
  return Status::OK;
}

Status SetContents(StringPiece filename,
                   StringPiece content,
                   const Options& ignored) {
  // Remove the file if it exists.
  remove(filename.ToString().c_str());
  int fd = open(filename.ToString().c_str(), O_WRONLY | O_CREAT, S_IRWXU);
  if (fd < 0) {
    return Status(::util::error::INTERNAL, "Failed to recreate file");
  }

  const char *buf = content.data();
  int64 numToWrite = content.size();
  int64 numWritten = 0;

  // Write until there are no more bytes.
  while ((numWritten = write(fd, buf, numToWrite)) > 0) {
    numToWrite -= numWritten;
    if (numToWrite <= 0) break;
    buf += numWritten;
  }

  close(fd);
  return Status::OK;
}

}  // namespace file
