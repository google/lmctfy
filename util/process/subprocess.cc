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

#include "util/process/subprocess.h"

#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string>
#include <vector>

#include "base/linux_syscall_support.h"
#include "base/logging.h"

using ::std::string;
using ::std::vector;

int getdents(unsigned int fd, struct kernel_dirent* dirp, unsigned int count) {
  return syscall(__NR_getdents, fd, dirp, count);
}

SubProcess::SubProcess()
    : running_(false), use_session_(false), inherit_higher_fds_(false) {
  // By default all channels should be close on exec.
  for (int i = CHAN_STDIN; i <= CHAN_STDERR; ++i) {
    actions_.push_back(ACTION_CLOSE);
  }
}

SubProcess::~SubProcess() {}

void SubProcess::SetUseSession() {
  CHECK(!running_);
  use_session_ = true;
}

void SubProcess::SetInheritHigherFDs(bool value) {
  CHECK(!running_);
  inherit_higher_fds_ = value;
}

void SubProcess::SetChannelAction(Channel chan, ChannelAction action) {
  CHECK(!running_);
  CHECK_GE(chan, CHAN_STDIN);
  CHECK_LE(chan, CHAN_STDERR);
  actions_[chan] = action;
}

void SubProcess::SetArgv(const vector<string> &argv) {
  CHECK(argv_.empty());
  CHECK(!argv.empty());
  argv_ = argv;
}

void SubProcess::BlockSignals() {
  sigset_t blocked_signals;
  sigfillset(&blocked_signals);
  sigprocmask(SIG_BLOCK, &blocked_signals, &old_signals_);
}

void SubProcess::UnblockSignals() {
  sigprocmask(SIG_SETMASK, &old_signals_, nullptr);
}

void SubProcess::CloseNonChannelFds() {
  int fds = open("/proc/self/fd", O_RDONLY, 0);
  if (fds != -1) {
    // Scan /proc/self/fd looking for filehandles
    char buffer[sizeof(struct kernel_dirent)];
    int bytes;
    while ((bytes = getdents(fds,
                             reinterpret_cast<struct kernel_dirent*>(buffer),
                             sizeof(buffer))) > 0) {
      struct kernel_dirent *de;
      for (int offset = 0; offset < bytes; offset += de->d_reclen) {
        de = (struct kernel_dirent *)(buffer + offset);
        if (de->d_name[0] == '.') continue;

        // Simple atoi() loop since apparently calling atoi() in this
        // environment isn't safe
        int fd = 0;
        char *p = de->d_name;
        while (*p) {
          fd = fd * 10 + *p++ - '0';
        }
        if (fd <= CHAN_STDERR) continue;
        if (fd == fds) continue;  // Don't close the directory handle
        while (close(fd) < 0 && errno == EINTR) {
        }
      }
    }
    close(fds);
  }
}

void SubProcess::ChildFork() {
  UnblockSignals();

  // Create a new session if that was specified.
  if (use_session_) {
    setsid();
  }

  // Point stdin, stdout, and stderr to /dev/null unless the user specified to
  // dup to the parent's FDs.
  int nullfd = open("/dev/null", O_RDWR);
  if (nullfd == -1) {
    exit(1);
  }
  for (int i = CHAN_STDIN; i <= CHAN_STDERR; ++i) {
    int dupfd = nullfd;

    if (actions_[i] == ACTION_DUPPARENT) {
      dupfd = i;
    }
    if (dup2(dupfd, i) == -1) {
      exit(1);
    }
  }

  // Close the higher fds if inheriting wasn't specified.
  if (!inherit_higher_fds_) {
    CloseNonChannelFds();
  }

  // Build a vector of C-compatible strings.
  vector<const char *> cargv;
  for (const string &s : argv_) {
    cargv.push_back(s.c_str());
  }
  cargv.push_back(nullptr);

  execvp(argv_[0].c_str(), const_cast<char *const *>(&cargv.front()));
  exit(1);
}

bool SubProcess::Start() {
  CHECK(!running_);

  BlockSignals();

  // Fork, exec.
  pid_t pid = fork();
  if (pid == 0) {
    // Child never returns;
    ChildFork();
  }

  UnblockSignals();

  // The child is now running.
  pid_ = pid;
  running_ = true;

  return true;
}
