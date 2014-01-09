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

#ifndef UTIL_PROCESS_SUBPROCESS_H__
#define UTIL_PROCESS_SUBPROCESS_H__

#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <string>
#include <vector>

enum Channel {
  CHAN_STDIN = STDIN_FILENO,
  CHAN_STDOUT = STDOUT_FILENO,
  CHAN_STDERR = STDERR_FILENO,
};

// How the channel is handled.
enum ChannelAction {
  ACTION_CLOSE,
  ACTION_DUPPARENT
};

// Utility for running other processes.
//
// Class is thread-compatible.
class SubProcess {
 public:
  SubProcess();
  virtual ~SubProcess();

  virtual inline pid_t pid() const { return pid_; }
  virtual bool running() const { return running_; }

  // Sets the child as a group leader.
  virtual void SetUseSession();

  // Whether to inherit the parent's "higher" (not stdin, stdout, stderr) file
  // descriptors.
  void SetInheritHigherFDs(bool value);

  // How to handle standard input/output channels in the new process.
  virtual void SetChannelAction(Channel chan, ChannelAction action);

  // Set up a program and argument list for execution. The first argument is the
  // program that will be executed.
  virtual void SetArgv(const ::std::vector<::std::string> &argv);

  // Starts process.
  virtual bool Start();

 private:
  void BlockSignals();
  void UnblockSignals();
  void CloseNonChannelFds();
  void ChildFork();

  bool running_;
  bool use_session_;
  bool inherit_higher_fds_;

  pid_t pid_;
  ::std::vector<::std::string> argv_;
  sigset_t old_signals_;
  ::std::vector<ChannelAction> actions_;
};

#endif  // UTIL_PROCESS_SUBPROCESS_H__
