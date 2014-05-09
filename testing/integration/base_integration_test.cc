#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <iostream>
#include <memory>
#include <vector>

#include "base/port.h"
#include "file/base/helpers.h"
#include "file/base/file.h"
#include "file/base/path.h"
#include "production/containers/gcontain/kernel_files.h"
#include "production/containers/public/gcontain.h"
#include "production/omlet/util/errors_test_util.h"
#include "production/omlet/util/file_lines.h"
#include "production/omlet/util/proc_mounts.h"
#include "strings/split.h"
#include "strings/substitute.h"
#include "testing/base/public/gmock.h"
#include "testing/base/public/gunit.h"
#include "util/task/codes.pb.h"
#include "util/task/status.h"
#include "util/task/statusor.h"

DECLARE_bool(gcontain_use_namespaces);
DECLARE_string(test_tmpdir);

using ::file::JoinPath;
using ::production_containers::gcontain::Container;
using ::production_containers::gcontain::ContainerSpec;
using ::production_containers::gcontain::GContain;
using ::production_containers::gcontain::KernelFiles;
using ::production_containers::gcontain::RunSpec;
using ::production_omlet::FileLines;
using ::production_omlet::ProcMountsData;
using ::std::unique_ptr;
using ::std::vector;
using ::strings::Split;
using ::strings::Substitute;
using ::testing::Contains;
using ::testing::EqualsInitializedProto;
using ::testing::Not;
using ::util::Status;
using ::util::StatusOr;

namespace production_containers {
namespace gcontain {
namespace {

class SubcontainersTest : public ::testing::Test {
 public:
  void SetUp() override {
    FLAGS_gcontain_use_namespaces = true;
    SetupContainers();
  }

  void TearDown() override {
    // Destroy the child test container if it still exists.
    if (child_ != nullptr) {
      DestroyContainer(move(child_));
    }
  }

  // Sets up 'gcontain_' and 'child_'
  void SetupContainers() {
    // Create the gContain instance.
    {
      StatusOr<GContain *> statusor = GContain::New();
      ASSERT_OK(statusor);
      gcontain_.reset(statusor.ValueOrDie());
    }

    // Create the child container.
    {
      StatusOr<Container *> statusor = gcontain_->Create(kName,
                                                         container_spec_);
      ASSERT_OK(statusor);
      child_.reset(statusor.ValueOrDie());
    }
  }

  Container* CreateContainer(const string &name, const ContainerSpec &spec) {
    // Create the child container.
    StatusOr<Container *> statusor = gcontain_->Create(name, spec);
    CHECK_OK(statusor) << "ERROR: " << statusor.status();
    return statusor.ValueOrDie();
  }

  void DestroyContainer(unique_ptr<Container> container) {
    CHECK_OK(gcontain_->Destroy(container.release()));
  }

  // Get the current container.
  virtual Container *GetSelf() const {
    StatusOr<Container *> statusor = gcontain_->Get(".");
    CHECK_OK(statusor) << "ERROR: " << statusor.status();
    return statusor.ValueOrDie();
  }

  // Get the name of the current container.
  virtual string DetectSelf() const {
    StatusOr<string> statusor = gcontain_->Detect();
    CHECK_OK(statusor) << "ERROR: " << statusor.status();
    return statusor.ValueOrDie();
  }

  // Get a list of the names of the subcontainers.
  virtual vector<string> GetSubcontainers(const Container &container,
                                          bool recursive) const {
    StatusOr<vector<Container *>> statusor = container.ListSubcontainers(
        recursive ? Container::LIST_RECURSIVE : Container::LIST_SELF);
    CHECK_OK(statusor) << "ERROR: " << statusor.status();

    // Get the names of the subcontainers and delete them.
    vector<string> subcontainers;
    for (const Container *subcontainer : statusor.ValueOrDie()) {
      subcontainers.emplace_back(subcontainer->name());
      delete subcontainer;
    }

    return subcontainers;
  }

 protected:
  const string kName = "test_sub";
  ContainerSpec container_spec_;

  unique_ptr<GContain> gcontain_;
  unique_ptr<Container> child_;
};

TEST_F(SubcontainersTest, CreateAndDestroy) {
  // Nothing to do since SetUp() is doing the Create/Destroy.
}

TEST_F(SubcontainersTest, GetAndDetectSelf) {
  // Get self.
  unique_ptr<Container> self;
  {
    StatusOr<Container *> statusor = gcontain_->Get(".");
    ASSERT_OK(statusor);
    self.reset(statusor.ValueOrDie());
  }

  // Detect what container we are in.
  string self_container_name;
  {
    StatusOr<string> statusor = gcontain_->Detect();
    ASSERT_OK(statusor);
    self_container_name = statusor.ValueOrDie();
  }

  EXPECT_EQ(self_container_name, self->name());
}

TEST_F(SubcontainersTest, ListSubcontainers) {
  // The container created in SetUp should exist under the parent in recursive
  // and non-recursive modes.
  unique_ptr<Container> parent(GetSelf());
  vector<string> subcontainers = GetSubcontainers(*parent, false);
  EXPECT_EQ(1, subcontainers.size());
  EXPECT_THAT(subcontainers, Contains(child_->name()));
  subcontainers = GetSubcontainers(*parent, true);
  EXPECT_EQ(1, subcontainers.size());
  EXPECT_THAT(subcontainers, Contains(child_->name()));

  // Create a subcontainer of the test container (e.g.: test_sub/grandchild).
  const string kGrandchildName = JoinPath(kName, "grandchild");
  unique_ptr<Container> grandchild;
  {
    StatusOr<Container *> statusor = gcontain_->Create(kGrandchildName,
                                                       container_spec_);
    ASSERT_OK(statusor);
    grandchild.reset(statusor.ValueOrDie());
  }

  // The new subcontainer should exist under the child.
  subcontainers = GetSubcontainers(*child_, false);
  EXPECT_EQ(1, subcontainers.size());
  EXPECT_THAT(subcontainers, Contains(grandchild->name()));
  subcontainers = GetSubcontainers(*child_, true);
  EXPECT_EQ(1, subcontainers.size());
  EXPECT_THAT(subcontainers, Contains(grandchild->name()));

  // It should exist under the parent too but only if we list recursively.
  subcontainers = GetSubcontainers(*parent, false);
  EXPECT_EQ(1, subcontainers.size());
  EXPECT_THAT(subcontainers, Contains(child_->name()));
  subcontainers = GetSubcontainers(*parent, true);
  EXPECT_EQ(2, subcontainers.size());
  EXPECT_THAT(subcontainers, Contains(child_->name()));
  EXPECT_THAT(subcontainers, Contains(grandchild->name()));
}

TEST_F(SubcontainersTest, Enter) {
  // Get the parent and ensure this is where we are.
  unique_ptr<Container> parent(GetSelf());
  ASSERT_EQ(parent->name(), DetectSelf());

  // Move into the child and make sure we're there.
  ASSERT_OK(child_->Enter(0));
  EXPECT_EQ(child_->name(), DetectSelf());

  // Move back to the parent and make sure we're there.
  ASSERT_OK(parent->Enter(0));
  EXPECT_EQ(parent->name(), DetectSelf());
}

class RunSubcontainersTest : public SubcontainersTest {
 public:
  RunSubcontainersTest()
      : output_file_(JoinPath(FLAGS_test_tmpdir, "output")) {}

  void TearDown() override {
    File::Delete(output_file_);

    SubcontainersTest::TearDown();
  }

  // Waits for the process with the specified PID to exit.
  void WaitForTermination(pid_t pid) {
    while (kill(pid, 0) == 0) {
      sleep(1);
    }
  }

  // Parse the output file (expected to be the output of "ls -l") and get the
  // lines of FDs.
  vector<string> ParseFDs(const string &file_path) {
    CHECK(File::Exists(file_path));
    vector<string> fds;
    for (StringPiece line : FileLines(file_path)) {
      if (!line.starts_with("total")) {
        vector<string> parts = Split(line.ToString(), " ");
        if (parts.size() > 8) {
          fds.emplace_back(parts[8]);
        }
      }
    }
    return fds;
  }

  string RunAndCaptureOutput(vector<string> argv, RunSpec run_spec) {
    string file_output;
    StatusOr<pid_t> statusor = child_->Run(argv, run_spec);
    CHECK_OK(statusor) << "ERROR: " << statusor.status();
    WaitForTermination(statusor.ValueOrDie());
    CHECK_OK(file::GetContents(output_file_, &file_output, file::Defaults()));
    return file_output;
  }

 protected:
  const string output_file_;
};

// Just make sure that running a command is working.
TEST_F(RunSubcontainersTest, Run_Default) {
  RunSpec run_spec;
  const string kOutput = "Hello World";
  const vector<string> kCommand = { "/bin/bash", "-c",
                                    Substitute("echo -n \"$0\" > $1", kOutput,
                                               output_file_) };
  EXPECT_EQ(kOutput, RunAndCaptureOutput(kCommand, run_spec));
}

// Make sure that when the parent creates a fd, the child can see it.
// This test is not working any longer after we switched to using nscon.
/*
TEST_F(RunSubcontainersTest, Run_FdPolicy_Inherit) {
  // Create the output file.
  const string kOutput = "Hello World";
  ASSERT_OK(file::SetContents(output_file_, kOutput, file::Defaults()));

  // Create a new fd in the parent.
  int new_fd = open(output_file_.c_str(), O_RDONLY);

  // Create a run spec with inherit set.
  RunSpec run_spec;
  run_spec.set_fd_policy(RunSpec::INHERIT);

  // Run a child that outputs its own file descriptors
  const vector<string> kCommand = {"/bin/bash", "-c",
                                   Substitute("ls -l /proc/self/fd/ > $0",
                                              output_file_)};

  // Make sure we grab the file output for debugging.
  string file_output = RunAndCaptureOutput(kCommand, run_spec);

  // Make sure the new fd created by the parent is in the child's fd.
  const vector<string> fds = ParseFDs(output_file_);
  EXPECT_NE(std::find(fds.begin(), fds.end(), SimpleItoa(new_fd)), fds.end())
      << "Expected the child process to have fd: " << new_fd
      << ". Child has the following fd output:\n" << file_output;
} */

// Make sure that when the parent creates a fd, the child doesn't see it.
TEST_F(RunSubcontainersTest, Run_FdPolicy_Detached) {
  // Create the output file.
  const string kOutput = "Hello World";
  ASSERT_OK(file::SetContents(output_file_, kOutput, file::Defaults()));

  // Get the parent and ensure this is where we are.
  unique_ptr<Container> parent(GetSelf());
  ASSERT_EQ(parent->name(), DetectSelf());

  // Create a new fd in the parent.
  int new_fd = open(output_file_.c_str(), O_RDONLY);

  // Create a run spec with inherit set.
  RunSpec run_spec;
  run_spec.set_fd_policy(RunSpec::DETACHED);

  // Run a child that outputs its own file descriptors
  const vector<string> kCommand = {"/bin/bash", "-c",
                                   Substitute("ls -l /proc/self/fd/ > $0",
                                              output_file_)};

  // Make sure we grab the file output for debugging.
  string file_output = RunAndCaptureOutput(kCommand, run_spec);

  // Make sure the new fd created by the parent is not in the child's fd.
  const vector<string> fds = ParseFDs(output_file_);
  std::cout << file_output << "\n";
  // Expect only four fds: stdin, stdout, stderr, and /proc/self/fd.
  // TODO(vishnuk): Figure out why detached has 5 fds with namespaces turned on.
  EXPECT_EQ(fds.size(), 4);
  EXPECT_EQ(std::find(fds.begin(), fds.end(), SimpleItoa(new_fd)), fds.end())
      << "Expected the child process to not have fd: " << new_fd
      << ". Child has the following fd output:\n" << file_output;
}

class FreezerTest : public SubcontainersTest {
 public:
  void TearDown() {
    SubcontainersTest::TearDown();
    // Freezer cleanup check.
    EXPECT_FALSE(::File::Exists(::file::JoinPath(freezer_mountpoint_, kName)));
    EXPECT_TRUE(::File::Exists(freezer_mountpoint_));
  }

  bool CanTestFreezer() {
    // Detect the type of subcontainer support this machine has.
    freezer_mountpoint_ = GetFreezerMountPoint();
    if (freezer_mountpoint_.empty()) {
      freezer_support_ = UNSUPPORTED;
      return false;
    } else {
      if (!::File::Exists(::file::JoinPath(freezer_mountpoint_, kName))) {
        return false;
      }
      if (::File::Exists(::file::JoinPath(
              freezer_mountpoint_, kName,
              KernelFiles::Freezer::kFreezerParentFreezing))) {
        freezer_support_ = SUPPORTED;
      } else {
        freezer_support_ = SUPPORTED_NON_HIERARCHY;
      }
      return true;
    }
  }

  string GetProcessState(pid_t pid) {
    string process_state_info;
    EXPECT_OK(::file::GetContents(
         Substitute("/proc/$0/stat", pid), &process_state_info,
         file::Defaults()));
    const vector<string> fields = ::strings::Split(process_state_info, " ");
    return fields[2];
  }

  void ExpectPidInGcontainListProcesses(pid_t process) {
    StatusOr<vector<pid_t>> statusor =
        child_->ListProcesses(Container::LIST_RECURSIVE);
    EXPECT_THAT(statusor.ValueOrDie(), ::testing::Contains(process));
  }

  void ExpectPidExistsInCgroup(pid_t process, const string &container_name) {
    const string freezer_tasks_path = ::file::JoinPath(freezer_mountpoint_,
                                                       container_name,
                                                       "tasks");
    bool found_pid = false;
    for (const StringPiece &line : FileLines(freezer_tasks_path)) {
      if (line.starts_with(SimpleItoa(process))) {
        found_pid = true;
        break;
      }
    }
    EXPECT_TRUE(found_pid);
  }

  void WaitTillFrozen(const string &container_name) {
    // Max number of times we check for freezer state to be frozen.
    const int kMaxRetry = 3;
    const string freezer_state_path =
        ::file::JoinPath(freezer_mountpoint_, container_name,
                         KernelFiles::Freezer::kFreezerState);
    int repeat = 0;
    while (1) {
      string state_info;
      CHECK_OK(::file::GetContents(freezer_state_path, &state_info,
                                   file::Defaults()));
      if (state_info.find("FROZEN") != string::npos) {
        return;
      }
      EXPECT_LT(++repeat, kMaxRetry)
          << "Timed out while waiting for container to freeze";
      sleep(1);
    }
  }

 protected:
  enum FreezerSupport {
    UNSUPPORTED,
    SUPPORTED_NON_HIERARCHY,
    SUPPORTED
  };

  string GetFreezerMountPoint() {
    for (const ProcMountsData &mount : ::production_omlet::ProcMounts()) {
      if (mount.type != "cgroup") {
        continue;
      }
      for (const string &opt : mount.options) {
        if (opt.find("freezer") == 0) {
          return mount.mountpoint;
        }
      }
    }
    return "";
  }

  pid_t RunCommandInContainer(const vector<string> &command,
                              Container *container) {
    StatusOr<pid_t> statusor =
        container->Run(command, ::production_containers::gcontain::RunSpec());
    CHECK_OK(statusor) << "ERROR: " << statusor.status();
    return statusor.ValueOrDie();
  }

  const vector<string> kCommand = {"/bin/sh",
                                   "-c", "while :; do sleep 0; done"};
  FreezerSupport freezer_support_;
  string freezer_mountpoint_;
};

// GContain initialization is tested as part of Subcontainers test.

TEST_F(FreezerTest, Works_No_Subcontainer) {
  // Setup Tests Freezer initialization.
  if (!CanTestFreezer()) {
    return;
  }
}

TEST_F(FreezerTest, Works_With_Subcontainer) {
  // Setup Tests Freezer initialization.
  if (!CanTestFreezer()) {
    return;
  }
  const string kSubcontainerName = ::file::JoinPath(kName, "sub_cont");

  unique_ptr<Container> sub_container(CreateContainer(kSubcontainerName,
                                                      container_spec_));
  // Check that freezer has been setup correctly.
  EXPECT_TRUE(::File::Exists(::file::JoinPath(freezer_mountpoint_,
                                              kSubcontainerName)));

  // Now destroy the sub container.
  DestroyContainer(move(sub_container));
  // Check that freezer has been destroyed correctly.
  EXPECT_FALSE(::File::Exists(::file::JoinPath(freezer_mountpoint_,
                                               kSubcontainerName)));
}

TEST_F(FreezerTest, Pause_Resume_Works_No_Subcontainer) {
  // Setup Tests Freezer initialization.
  if (!CanTestFreezer()) {
    return;
  }

  const pid_t pid = RunCommandInContainer(kCommand, child_.get());

  ExpectPidExistsInCgroup(pid, kName);
  ExpectPidInGcontainListProcesses(pid);

  EXPECT_OK(child_->Pause());
  WaitTillFrozen(kName);

  EXPECT_EQ("D", GetProcessState(pid));

  EXPECT_OK(child_->Resume());
  EXPECT_NE("D", GetProcessState(pid));
}

TEST_F(FreezerTest, Pause_Resume_Works_With_Subcontainer) {
  // Setup Tests Freezer initialization.
  if (!CanTestFreezer()) {
    return;
  }
  const string kSubcontainerName = ::file::JoinPath(kName, "sub_cont");

  unique_ptr<Container> sub_container(CreateContainer(kSubcontainerName,
                                                       container_spec_));
  // Check that freezer has been setup correctly.
  EXPECT_TRUE(::File::Exists(::file::JoinPath(freezer_mountpoint_,
                                              kSubcontainerName)));
  const pid_t pid = RunCommandInContainer(kCommand, sub_container.get());

  ExpectPidExistsInCgroup(pid, kSubcontainerName);
  ExpectPidInGcontainListProcesses(pid);

  EXPECT_OK(child_->Pause());
  WaitTillFrozen(kSubcontainerName);

  EXPECT_EQ("D", GetProcessState(pid));

  EXPECT_OK(child_->Resume());
  EXPECT_NE("D", GetProcessState(pid));
}

class VirtualHostTest : public RunSubcontainersTest {
 public:
  VirtualHostTest() {}

  void SetUp() override {
    FLAGS_gcontain_use_namespaces = true;
    container_spec_.mutable_virtual_host()->
        set_virtual_hostname(kVirtualHostname);
    SetupContainers();
  }

 protected:
  const string kVirtualHostname = "virt_host";
};

TEST_F(VirtualHostTest, CreateAndDestroy) {
  // Nothing to do since SetUp() is doing the Create/Destroy.
}

// Just make sure that running a command is working.
TEST_F(VirtualHostTest, Run_Default) {
  RunSpec run_spec;
  const string kOutput = "Hello World";
  const vector<string> kCommand = { "/bin/bash", "-c",
                                   Substitute("echo -n \"$0\" > $1", kOutput,
                                              output_file_) };
  string file_output;
  StatusOr<pid_t> statusor = child_->Run(kCommand, run_spec);
  ASSERT_OK(statusor);
  WaitForTermination(statusor.ValueOrDie());
  ASSERT_OK(file::GetContents(output_file_, &file_output, file::Defaults()));
  EXPECT_EQ(kOutput, file_output);
}

TEST_F(VirtualHostTest, CustomInitVerifyPid) {
  ContainerSpec init_spec;
  init_spec.mutable_virtual_host()->
        set_virtual_hostname("init_virt_host");
  const string kOutput = "Hello World";
  const vector<string> kCommand = { "/bin/bash", "-c",
                                   Substitute("echo -n $$$$ > $0",
                                              output_file_) };

  for (int i = 0; i < kCommand.size(); i++) {
    init_spec.mutable_virtual_host()->mutable_init()->
        add_init_argv(kCommand[i]);
  }

  unique_ptr<Container> init_cont(CreateContainer("init_cont", init_spec));

  string file_output;
  ASSERT_OK(file::GetContents(output_file_, &file_output, file::Defaults()));
  EXPECT_EQ(SimpleItoa(1), file_output);

  DestroyContainer(move(init_cont));
}


// TODO(vmarmol): Add tests for:
// - Multiple subcontainers
// - Delegation
// - ListPids
// - ListTids
// - GetAndDetectParent
// - Create container with absolute path.

}  // namespace
}  // namespace gcontain
}  // namespace production_containers
