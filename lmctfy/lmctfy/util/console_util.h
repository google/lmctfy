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

#ifndef SRC_UTIL_CONSOLE_UTIL_H_
#define SRC_UTIL_CONSOLE_UTIL_H_

#include "util/task/statusor.h"

struct stat;
namespace containers {

// This class exports some console specific utilities.
// This class is thread-safe.
class ConsoleUtil {
 public:
  ConsoleUtil();
  virtual ~ConsoleUtil() {}

  // Enables devpts namespace support if it is enabled in the kernel.
  // Kernel config option CONFIG_DEVPTS_MULTIPLE_INSTANCES=y is required for
  // devpts namespace support.
  // Returns:
  //    OK if kernel does not support devpts namespace or if devpts namespace
  //    support was enabled successfully.
  virtual ::util::Status EnableDevPtsNamespaceSupport() const;

 private:
  // 'newinstance' is needed to setup a new devpts namespace.
  // 5 is tty group.
  const string kDevPtsMountData;

  // Stats 'path' and returns the statbuf on success.
  ::util::StatusOr<struct stat> StatFile(const string &path) const;

  // Returns true if a bind mount from /dev/pts/ptmx to /dev/ptmx exists.
  bool DevPtsPtmxToDevPtsBindMountExists() const;

  DISALLOW_COPY_AND_ASSIGN(ConsoleUtil);
};

}  // namespace containers

#endif  // SRC_UTIL_CONSOLE_UTIL_H_
