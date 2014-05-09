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

#include "lmctfy/util/console_util.h"

#include <sys/mount.h>

#include "global_utils/fs_utils.h"
#include "global_utils/mount_utils.h"
#include "util/errors.h"
#include "util/file_lines.h"
#include "system_api/libc_fs_api.h"
#include "strings/split.h"
#include "strings/substitute.h"

using ::util::FileLines;
using ::util::GlobalFsUtils;
using ::util::GlobalMountUtils;
using ::system_api::GlobalLibcFsApi;
using ::strings::Substitute;
using ::util::Status;
using ::util::StatusOr;

namespace containers {

namespace {

static const char *kDevPtsPath = "/dev/pts";
static const char *kDevPtmxPath = "/dev/ptmx";
static const char *kDevPtsPtmxPath = "/dev/pts/ptmx";
// Default mode as recommended in devpts.txt kernel documentation file.
static const mode_t kDevPtsPtmxMode = 0666;
static const char *kDevPtsPtmxModeStr = "ptmxmode=666";
// This the standard configuration for devpts.
static const int kDevPtsMountFlags =
    (MS_NOEXEC | MS_NOSUID | MS_RELATIME);
static const char *kProcMountInfo = "/proc/1/mountinfo";
static const char *kDevPtsMountType = "devpts";

}  // namespace

ConsoleUtil::ConsoleUtil() :
    kDevPtsMountData(Substitute("newinstance,$0,mode=600,gid=5",
                                kDevPtsPtmxModeStr)) {
}

StatusOr<struct stat> ConsoleUtil::StatFile(const string &path) const {
  struct stat statbuf;
  if (GlobalLibcFsApi()->Stat(path.c_str(), &statbuf) < 0) {
    return Status(::util::error::INTERNAL,
                  Substitute("Failed to stat $0. Error: $1",
                             kDevPtsPtmxPath, StrError(errno)));
  }
  return statbuf;
}

bool ConsoleUtil::DevPtsPtmxToDevPtsBindMountExists() const {
  for (const auto &line : FileLines(kProcMountInfo)) {
    // Format is :
    // 70 17 0:11 /ptmx /dev/ptmx rw,nosuid,noexec,relatime - \
    // devpts devpts rw,mode=600,ptmxmode=666
    vector<string> elements = Split(line, " ", ::strings::SkipEmpty());
    if (elements.size() != 10) {
      // Ignore invalid lines
      continue;
    }
    if (elements[3] == "/ptmx" && elements[4] == kDevPtmxPath &&
        elements[7] == kDevPtsMountType &&
        elements[9].find(kDevPtsPtmxModeStr) != string::npos) {
      return true;
    }
  }
  return false;
}

Status ConsoleUtil::EnableDevPtsNamespaceSupport() const {
  // If /dev/pts/ does not exist nothing to do.
  // TODO(vishnuk): Change this to DirExists once DirExists is available in open
  // source.
  if (!RETURN_IF_ERROR(GlobalFsUtils()->FileExists(kDevPtsPath))) {
    return Status::OK;
  }
  // If /dev/ptmx does not exist nothing to do.
  if (!RETURN_IF_ERROR(GlobalFsUtils()->FileExists(kDevPtmxPath))) {
    return Status::OK;
  }
  // If /dev/pts/ptmx does not exist, kernel does not have devpts namespace
  // support configured. Nothing to do.
  if (!RETURN_IF_ERROR(GlobalFsUtils()->FileExists(kDevPtsPtmxPath))) {
    return Status::OK;
  }

  if ((RETURN_IF_ERROR(StatFile(kDevPtsPtmxPath)).st_mode & 0777) !=
      kDevPtsPtmxMode) {
    // Mount if needed.
    if (GlobalLibcFsApi()->Mount(kDevPtsMountType,
                                 kDevPtsPath,
                                 kDevPtsMountType,
                                 kDevPtsMountFlags,
                                 kDevPtsMountData.c_str()) < 0) {
      return Status(::util::error::INTERNAL,
                    Substitute("devpts mount($0) failed: $1", kDevPtsPath,
                               StrError(errno)));
    }
  }
  if (!DevPtsPtmxToDevPtsBindMountExists()) {
    RETURN_IF_ERROR(GlobalMountUtils()->BindMount(kDevPtsPtmxPath,
                                                  kDevPtmxPath,
                                                  {}));
  }
  return Status::OK;
}

}  // namespace containers
