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

// A utility class that supplies stdio FILE that accepts writes where the
// contents can be read back later. Similar to std::ostringstream in concept,
// but usable in contexts where you have a writer that expects a stdio FILE.

#ifndef UTIL_TESTING_PIPE_FILE_H_
#define UTIL_TESTING_PIPE_FILE_H_

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string>
#include "base/macros.h"

namespace util_testing {

class PipeFile {
 public:
  // Don't do any work that might fail in the ctor.
  PipeFile() : read_file_(NULL), write_file_(NULL) {
  }

  // Clean up.
  ~PipeFile() {
    if (read_file_) {
      fclose(read_file_);
    }
    if (write_file_) {
      fclose(write_file_);
    }
  }

  // Gets the write side FILE* of the pipe.
  FILE *GetWriteFile() const {
    return write_file_;
  }

  // Gets the entire contents of the pipe as a string.
  string GetContents() const {
    char buf[1024];
    string result;
    while (fgets(buf, sizeof(buf), read_file_) != NULL) {
      result += buf;
    }
    return result;
  }

  // Opens the pipe for reading and writing.
  bool Open() {
    int pipe_fds[2];
    FILE *read_file;
    FILE *write_file;

    if (pipe(pipe_fds) != 0) {
      return false;
    }
    fcntl(pipe_fds[0], F_SETFL, O_NONBLOCK);
    read_file = fdopen(pipe_fds[0], "r");
    if (read_file == NULL) {
      close(pipe_fds[0]);
      close(pipe_fds[1]);
      return false;
    }
    write_file = fdopen(pipe_fds[1], "w");
    if (write_file == NULL) {
      fclose(read_file);
      close(pipe_fds[1]);
      return false;
    }
    if (setvbuf(read_file, NULL, _IOLBF, 0) != 0) {
      fclose(read_file);
      fclose(write_file);
      return false;
    }
    if (setvbuf(write_file, NULL, _IOLBF, 0) != 0) {
      fclose(read_file);
      fclose(write_file);
      return false;
    }
    if (read_file_) {
      fclose(read_file_);
    }
    read_file_ = read_file;
    if (write_file_) {
      fclose(write_file_);
    }
    write_file_ = write_file;
    return true;
  }

 private:
  FILE *read_file_;
  FILE *write_file_;

  DISALLOW_COPY_AND_ASSIGN(PipeFile);
};

}  // namespace util_testing

#endif  // UTIL_TESTING_PIPE_FILE_H_
