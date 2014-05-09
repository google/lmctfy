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
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string>
#include <vector>

#include "base/casts.h"
#include "base/linux_syscall_support.h"
#include "base/logging.h"

using ::std::string;
using ::std::vector;

namespace {
int getdents(unsigned int fd, struct kernel_dirent* dirp, unsigned int count) {
  return syscall(__NR_getdents, fd, dirp, count);
}
}  // namespace

static const int kMaxNumChannels = 3;

static const int kErrorMsgMaxLen = 1024;

struct SubProcess::CommBuf {
  explicit CommBuf(int num_chan)
      : cmsg_buf(CMSG_SPACE(num_chan * sizeof(int)), 0),
        fds(num_chan, -1) {
  }
  int error_no = 0;
  char errmsg[kErrorMsgMaxLen] = {};
  vector<char> cmsg_buf;
  vector<int> fds;
};

SubProcess::SubProcess()
    : running_(false),
      use_session_(false),
      inherit_higher_fds_(false),
      exit_status_(0),
      comm_buf_(new CommBuf(SubProcess::NumOfChannels())),
      child_pipe_fds_(new int[SubProcess::NumOfChannels()]),
      parent_pipe_fds_(new int[SubProcess::NumOfChannels()]) {
  // By default all channels should be close on exec.
  for (int chan = CHAN_STDIN; chan <= CHAN_STDERR; ++chan) {
    actions_.push_back(ACTION_CLOSE);
    child_pipe_fds_[chan] = -1;
    parent_pipe_fds_[chan] = -1;
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

bool SubProcess::SetupChildToParentFds() {
  int pair[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) < 0) {
    error_text_ = string("socketpair() failed. Error: ") + strerror(errno);
    LOG(ERROR) << error_text_;
    exit_status_ = errno;
    return false;
  }
  parent_to_child_fd_ = pair[0];
  child_to_parent_fd_ = pair[1];
  return true;
}

// -----------------------------------------------------------------------------
int SubProcess::SendMessageToParent() {
  struct iovec iov[2] = { { 0 } };
  struct msghdr msgh = { 0, 0 };
  struct cmsghdr *cmsghp;

  // On Linux, we must transmit at least 1 byte of real data in
  // order to send ancillary data
  iov[0].iov_base = &comm_buf_->error_no;
  iov[0].iov_len = sizeof(comm_buf_->error_no);

  iov[1].iov_base = comm_buf_->errmsg;
  iov[1].iov_len = strlen(comm_buf_->errmsg) + 1;

  msgh.msg_iov = iov;
  msgh.msg_iovlen = 2;
  msgh.msg_name = nullptr;
  msgh.msg_namelen = 0;

  msgh.msg_control    = &comm_buf_->cmsg_buf[0];
  msgh.msg_controllen = sizeof(comm_buf_->cmsg_buf);

  cmsghp = CMSG_FIRSTHDR(&msgh);
  cmsghp->cmsg_level = SOL_SOCKET;
  cmsghp->cmsg_type = SCM_RIGHTS;


  cmsghp->cmsg_len = CMSG_LEN(0);
  int rc;
  do {
    rc = sendmsg(child_to_parent_fd_, &msgh, 0);
  } while (rc < 0 && errno == EINTR);

  if (rc< 0) {
    LOG(ERROR) << "Send message failed " << strerror(errno);
  }
  return rc;
}

void SubProcess::SendFatalError(int error_no, const string &error_msg) {
  comm_buf_->error_no = error_no;
  memcpy(comm_buf_->errmsg, error_msg.c_str(), error_msg.size());
  comm_buf_->errmsg[error_msg.size()] = '\0';
  SendMessageToParent();
  while (1) {
    exit(comm_buf_->error_no);
  }
}

void SubProcess::ShareFdsWithParent() {
  int rc = SendMessageToParent();
  if (rc < 0) {
    SendFatalError(errno, "Failed to share fds with parent");
  }
}

bool SubProcess::ReceiveMessageFromChild() {
  char cmsg_buf[CMSG_SPACE(kMaxNumChannels*sizeof(int))];
  ssize_t nr;
  struct iovec iov[2] = { { 0 } };
  struct msghdr msgh = { 0, 0 };
  struct cmsghdr *cmhp;

  memset(&iov, 0, sizeof(iov));
  memset(&msgh, 0, sizeof(msgh));

  iov[0].iov_base = reinterpret_cast<void *>(&comm_buf_->error_no);
  iov[0].iov_len  = sizeof(comm_buf_->error_no);

  iov[1].iov_base = reinterpret_cast<void *>(comm_buf_->errmsg);
  iov[1].iov_len  = strlen(comm_buf_->errmsg);

  msgh.msg_iov        = iov;
  msgh.msg_iovlen     = 2;

  msgh.msg_control    = cmsg_buf;
  msgh.msg_controllen = sizeof(cmsg_buf);

  msgh.msg_name = nullptr;
  msgh.msg_namelen = 0;

  do {
    nr = recvmsg(parent_to_child_fd_, &msgh, 0);
  } while (nr < 0 && (errno == EINTR || errno == EAGAIN));

  if (nr < 0) {
    exit_status_ = errno;
    error_text_ = string("recvmsg() failed. Error: ") + strerror(errno);
    LOG(ERROR) <<  error_text_;
    return false;
  } else if (nr == 0) {
    exit_status_ = EINVAL;
    error_text_ = string("Child Failed to send control message.");
    LOG(ERROR) <<  error_text_;
    return false;
  }
  return true;
}

// -----------------------------------------------------------------------------

bool SubProcess::SetupPipesForChannels() {
  for (int chan = CHAN_STDIN; chan <= CHAN_STDERR; ++chan) {
    if (actions_[chan] == ACTION_PIPE) {
      int channel_pair[2] = { 0 };
      if (socketpair(AF_UNIX, SOCK_STREAM, 0, channel_pair) < 0) {
        LOG(ERROR) << "Failed to create socket pair";
        return false;
      }
      // Record file handle for sending it to the parent
      child_pipe_fds_[chan] = channel_pair[1];
      parent_pipe_fds_[chan] = channel_pair[0];
      if (fcntl(parent_pipe_fds_[chan], F_SETFL, O_NONBLOCK) < 0) {
        LOG(ERROR) << "Failed to make parent pipe fd non blocking";
        return false;
      }
    }
  }
  return true;
}

void SubProcess::CloseNonChannelFds() {
  int proc_fd = open("/proc/self/fd", O_RDONLY, 0);
  if (proc_fd != -1) {
    // Scan /proc/self/fd looking for filehandles
    char buffer[sizeof(struct kernel_dirent)];
    int bytes;
    while ((bytes = getdents(proc_fd,
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
        if (fd <= NumOfChannels()) continue;
        if (fd == child_to_parent_fd_) continue;
        if (fd == proc_fd) continue;  // Don't close the directory handle
        while (close(fd) < 0 && errno == EINTR) {}
      }
    }
    close(proc_fd);
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
    SendFatalError(errno,
                   string("Failed to open /dev/null. Error: ") +
                   strerror(errno));
  }
  for (int chan = CHAN_STDIN; chan <= CHAN_STDERR; ++chan) {
    int dupfd = nullfd;
    if (actions_[chan] == ACTION_PIPE) {
      if (child_pipe_fds_[chan] < 0) {
        SendFatalError(EINVAL, "Pipe fd invalid for channel ");
      }
      dupfd = child_pipe_fds_[chan];
    } else if (actions_[chan] == ACTION_DUPPARENT) {
      dupfd = chan;
    }
    if (dup2(dupfd, chan) == -1) {
      SendFatalError(errno, string("Failed to dup. Error: ") + strerror(errno));
    }
  }

  // Close the higher fds if inheriting wasn't specified.
  if (!inherit_higher_fds_) {
    CloseNonChannelFds();
  }

  SendMessageToParent();

  shutdown(child_to_parent_fd_, SHUT_RDWR);
  while (close(child_to_parent_fd_) < 0 && errno == EINTR) {}
  child_to_parent_fd_ = -1;

  ExecChild();
  exit(1);
}

void SubProcess::ExecChild() {
  // Build a vector of C-compatible strings.
  vector<const char *> cargv;
  for (const string &s : argv_) {
    cargv.push_back(s.c_str());
  }
  cargv.push_back(nullptr);

  execvp(argv_[0].c_str(), const_cast<char *const *>(&cargv.front()));
}

bool SubProcess::Wait() {
  pid_t pid;
  if (running()) {
    int saved_errno = errno;
    int status;
    do {
      pid = waitpid(pid_, &status, 0);
    } while (pid < 0 && EINTR == errno);
    if (pid >= 0) {
      errno = saved_errno;
      exit_status_ = status;
      running_ = false;
      return true;
    } else if (pid < 0) {
      if (errno == ECHILD) {
        running_ = false;
      }
      LOG(ERROR) << "PID " << pid_ << ": Unexpected error from wait4(). Error: "
                 << strerror(errno);
      return false;
    }
  }
  return true;
}

void SubProcess::CloseAllPipeFds() {
  for (int chan = CHAN_STDIN; chan <= CHAN_STDERR; ++chan) {
    if (actions_[chan] == ACTION_PIPE) {
      shutdown(child_pipe_fds_[chan], SHUT_RDWR);
      shutdown(parent_pipe_fds_[chan], SHUT_RDWR);
      while (close(child_pipe_fds_[chan]) && errno == EINTR) {}
      while (close(parent_pipe_fds_[chan]) && errno == EINTR) {}
      child_pipe_fds_[chan] = -1;
      parent_pipe_fds_[chan] = -1;
    }
  }
}

void SubProcess::CloseChildPipeFds() {
  for (int chan = CHAN_STDIN; chan <= CHAN_STDERR; ++chan) {
    if (actions_[chan] == ACTION_PIPE) {
      while (close(child_pipe_fds_[chan]) && errno == EINTR) {}
      child_pipe_fds_[chan] = -1;
    }
  }
}

bool SubProcess::Start() {
  CHECK(!running_);

  if (!SetupChildToParentFds()) {
    return false;
  }

  if (!SetupPipesForChannels()) {
    exit_status_ = errno;
    error_text_ = "Failed to setup pipes.";
    return false;
  }

  BlockSignals();

  // Fork, exec.
  pid_t pid = fork();
  if (pid == 0) {
    // Child never returns;
    ChildFork();
  }

  UnblockSignals();

  if (!ReceiveMessageFromChild()) {
    CloseAllPipeFds();
    return false;
  }

  shutdown(parent_to_child_fd_, SHUT_RDWR);
  while (close(parent_to_child_fd_) < 0 && errno == EINTR) {}
  parent_to_child_fd_ = -1;

  if (comm_buf_->error_no != 0 || strlen(comm_buf_->errmsg) > 0) {
    exit_status_ = comm_buf_->error_no;
    error_text_ = comm_buf_->errmsg;
    CloseAllPipeFds();
    return false;
  }

  // The child is now running.
  pid_ = pid;
  running_ = true;
  CloseChildPipeFds();

  return true;
}

string SubProcess::error_text() const {
  return error_text_;
}

int SubProcess::exit_code() const {
  return WIFEXITED(exit_status_) ? WEXITSTATUS(exit_status_) : -1;
}

int SubProcess::NumOfChannels() {
  return CHAN_STDIN + kMaxNumChannels;
}

void SubProcess::MaybeAddFD(Channel channel,
                            string* output,
                            string** io_strings,
                            Channel* channels,
                            struct pollfd* fds,
                            int* descriptors_to_poll,
                            int* descriptors_left,
                            int16 events) {
  if (actions_[channel] != ACTION_PIPE) return;
  io_strings[*descriptors_to_poll] = output;
  channels[*descriptors_to_poll] = channel;
  fds[*descriptors_to_poll].fd = parent_pipe_fds_[channel];
  fds[*descriptors_to_poll].events = events;
  fds[*descriptors_to_poll].revents = 0;
  (*descriptors_to_poll)++;
  (*descriptors_left)++;
}

void SubProcess::Close(Channel chan) {
  int fd = parent_pipe_fds_[chan];
  parent_pipe_fds_[chan] = -1;

  if (fd < 0) {
    return;
  }

  // Not only do we close the file handle, but we also shut it down.
  // This is necessary in order to deal with file handles that might
  // have accidentally been leaked to another child in between the time
  // that we received the file handle and when we set the close-on-exec flag.
  shutdown(fd, SHUT_RDWR);

  if (close(fd) < 0) {
    LOG(ERROR) << "PID " << pid_ << ": Failed to close channel " << chan
               << " fd=" << fd << ". Error: " << strerror(errno);
  }
}

int SubProcess::Communicate(string* stdout_output,
                            string* stderr_output) {
  char buffer[4096];
  const int kMaxDescriptorsToPoll = kMaxNumChannels;
  int descriptors_left = 0;
  int descriptors_to_poll = 0;
  string* io_strings[kMaxDescriptorsToPoll];
  size_t io_written[kMaxDescriptorsToPoll];

  if (!running()) {
    return -1;
  }

  for (size_t i = 0; i < kMaxDescriptorsToPoll; ++i) {
    io_written[i] = 0;
  }
  Channel channels[kMaxDescriptorsToPoll];
  struct pollfd fds[kMaxDescriptorsToPoll];
  // Set up file descriptors to poll.
  MaybeAddFD(CHAN_STDOUT, stdout_output, io_strings, channels, fds,
             &descriptors_to_poll, &descriptors_left, POLLIN);
  MaybeAddFD(CHAN_STDERR, stderr_output, io_strings, channels, fds,
             &descriptors_to_poll, &descriptors_left, POLLIN);
  errno = EINTR;

  while (descriptors_left > 0) {
    int data_count = poll(fds, descriptors_to_poll, -1);
    if ((errno != EINTR) && (data_count <= 0)) {
      error_text_ = string("Error while polling - ") + strerror(errno);
      for (int i = 0; i < descriptors_to_poll; ++i) {
        Close(channels[i]);
      }
      break;
    }

    for (int i = 0; i < descriptors_to_poll; ++i) {
      if ((fds[i].revents & POLLIN) != 0) {
        int bytes = read(fds[i].fd, buffer, sizeof(buffer));
        if (bytes == 0) {
          // All data read
          fds[i].fd = -1;
          descriptors_left--;
          Close(channels[i]);
        } else if (bytes > 0) {
          if (io_strings[i] != NULL) {
            io_strings[i]->append(buffer, bytes);
          }
        } else if (errno != EINTR) {
          LOG(ERROR) << "PID " << pid_ << ": read failed. Error: "
                     << strerror(errno);
          fds[i].fd = -1;
          descriptors_left--;
          Close(channels[i]);
        }
      }
    }
  }
  if (!Wait()) {
    return -1;
  }
  return exit_code();
}
