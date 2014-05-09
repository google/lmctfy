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

// init_impl.cc
//
// Simple 'init' implementation that can act as a parent for all the processes
// in a namespace jail.
//

#include <err.h>
#include <getopt.h>
#include <grp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "base/integral_types.h"  // For uint64
#include "base/logging.h"  // For CHECK macro
#include "strings/numbers.h"  // For SimpleAtoi()

#ifndef PR_SET_NO_NEW_PRIVS
#define PR_SET_NO_NEW_PRIVS 38
#endif

struct InitOptions {
  uid_t uid;
  gid_t gid;
};

// Parse UID/GID value from the given string.
static uint32 ParseIdOrDie(const char *str) {
  int32 val;
  // Accept only if conversion was successful.
  CHECK(SimpleAtoi(str, &val)) << "'" << str << "' is not a valid number";
  return val;
}

void ParseInitOptions(int argc, char **argv, InitOptions *opts) {
  enum {
    OPT_UID_FOUND = 1,
    OPT_GID_FOUND = 2,
  };

  while (true) {
    static const char kShortOpts[] = "";
    static const struct option kLongOpts[] = {
            { "uid", required_argument, 0, OPT_UID_FOUND },
            { "gid", required_argument, 0, OPT_GID_FOUND },
            { 0, 0, 0, 0 },
    };

    int idx = 0;
    int opt = getopt_long(argc, argv, kShortOpts, kLongOpts, &idx);
    if (opt == -1) {
      break;
    }
    switch (opt) {
      case OPT_UID_FOUND:
        opts->uid = ParseIdOrDie(optarg);
        break;
      case OPT_GID_FOUND:
        opts->gid = ParseIdOrDie(optarg);
        break;
      default:
        break;
    }
  }
}

int InitImpl(int argc, char **argv) {
  InitOptions opts = { /* uid */ static_cast<uid_t>(-1),
                       /* gid */ static_cast<gid_t>(-1) };

  ParseInitOptions(argc, argv, &opts);

  // Drop all privileges on setuid.
  if (prctl(PR_SET_KEEPCAPS, 0, 0, 0, 0) < 0) {
    err(-1, "prctl(PR_SET_KEEPCAPS)");
  }

  // Ignore error here as we might already be the session leader.
  setsid();

  // Prevent children from becoming zombies.  Thus this program doesn't
  // need to wait() for children.
  if (signal(SIGCHLD, SIG_IGN) == SIG_ERR) {
    err(-1, "signal");
  }

  // Clear supplementary groups if we can.
  gid_t *groups = NULL;
  if ((setgroups(0, groups) < 0) && (errno != EPERM)) {
    err(-1, "setgroups");
  }

  if ((opts.gid != -1) && (setresgid(opts.gid, opts.gid, opts.gid) < 0)) {
    err(-1, "setresgid");
  }

  if ((opts.uid != -1) && (setresuid(opts.uid, opts.uid, opts.uid) < 0)) {
    err(-1, "setresuid");
  }

  // Disable ability to gain privileges.
  if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) {
    warn("prctl(PR_SET_NO_NEW_PRIVS)");
  }

  // Block all (possible) signals.
  sigset_t mask;
  sigfillset(&mask);
  if (sigprocmask(SIG_SETMASK, &mask, NULL) < 0) {
    err(-1, "sigprocmask");
  }

  // Close all fds. Note that this could be inaccurate if the caller had changed
  // RLIMIT_NOFILE after opening some FDs. But this scenario is unlikely and so
  // we will just live with it to keep code simple.
  for (int i = 0; i < getdtablesize(); ++i) {
    close(i);
  }

  // Suspend ourself.
  sigfillset(&mask);
  while (true) {
    sigsuspend(&mask);
  }

  return 0;
}
