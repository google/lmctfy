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

#include "lmctfy/resources/nscon_namespace_handler.h"

#include "base/integral_types.h"
#include "lmctfy/controllers/eventfd_notifications_mock.h"
#include "lmctfy/namespace_handler.h"
#include "lmctfy/resource_handler.h"
#include "lmctfy/tasks_handler_mock.h"
#include "lmctfy/util/console_util_test_util.h"
#include "include/lmctfy.pb.h"
#include "include/namespace_controller_mock.h"
#include "include/namespaces.pb.h"
#include "util/errors_test_util.h"
#include "util/file_lines_test_util.h"
#include "strings/substitute.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "util/task/codes.pb.h"
#include "util/task/status.h"

using ::util::FileLinesTestUtil;
using ::util::UnixGid;
using ::util::UnixUid;
using ::std::unique_ptr;
using ::std::vector;
using ::strings::Substitute;
using ::testing::AnyOf;
#include "util/testing/equals_initialized_proto.h"
using ::testing::EqualsInitializedProto;
using ::testing::IsEmpty;
using ::testing::NiceMock;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::StrictMock;
using ::testing::_;
using ::util::Status;
using ::util::StatusOr;
using ::util::error::INTERNAL;
using ::util::error::INVALID_ARGUMENT;
using ::util::error::NOT_FOUND;
using ::util::error::UNAVAILABLE;
using ::util::error::UNIMPLEMENTED;

namespace containers {
namespace lmctfy {

static const char kContainerName[] = "/test";
static const pid_t kInit = 2;

class NsconNamespaceHandlerFactoryTest : public ::testing::Test {
 public:
  void SetUp() override {
    mock_controller_factory_ =
        new nscon::StrictMockNamespaceControllerFactory();
    mock_tasks_handler_factory_.reset(new StrictMockTasksHandlerFactory());
    mock_console_util_ = new StrictMock<MockConsoleUtil>();
    factory_.reset(new NsconNamespaceHandlerFactory(
        mock_tasks_handler_factory_.get(),
        mock_controller_factory_,
        mock_console_util_));
  }

  // Expect the child to have the specified parent.
  void ExpectGetParentPid(pid_t child, pid_t parent) {
    file_lines_test_util_.ExpectFileLines(
        Substitute("/proc/$0/status", child),
        {"Name: cat", "State:  R (running)", Substitute("PPid: $0", parent)});
  }
  void ExpectGetParentPid(pid_t child, pid_t parent, int occurrences) {
    vector<vector<string>> lines;
    for (int i = 0; i < occurrences; ++i) {
      lines.push_back(
          {"Name: cat", "State:  R (running)", Substitute("PPid: $0", parent)});
    }
    file_lines_test_util_.ExpectFileLinesMulti(
        Substitute("/proc/$0/status", child), lines);
  }

  void ExpectGetProcesses(MockTasksHandler *handler,
                          const vector<pid_t> &pids) {
    EXPECT_CALL(*handler, ListProcesses(TasksHandler::ListType::SELF))
        .WillRepeatedly(Return(pids));
  }

  // TODO(vmarmol): This is terrible...refactor IsVirtualHost() and DetectInit()
  // into something we can inject into NsconNamespaceHandler.
  // Expect IsVirtualHost() and DetectInit() to be called and provide the
  // specified results.
  void ExpectIsVirtualHostAndDetectInit(
      const StatusOr<bool> &virtual_host_result,
      const StatusOr<pid_t> &detect_init_result) {
    auto &tasks_handler_factory_get_expectations =
        EXPECT_CALL(*mock_tasks_handler_factory_, Get(kContainerName));

    // IsVirtualHost() fails.
    if (!virtual_host_result.ok()) {
      tasks_handler_factory_get_expectations.WillOnce(
          Return(virtual_host_result.status()));
      return;
    }

    // Expectations for IsVirtualHost().
    {
      MockTasksHandler *handler = new StrictMockTasksHandler(kContainerName);
      tasks_handler_factory_get_expectations.WillOnce(Return(handler));
      EXPECT_CALL(*handler, ListProcesses(TasksHandler::ListType::SELF))
          .WillRepeatedly(Return(vector<pid_t>{23}));

      // Expect the container to be in n1 (if result is true) or n0 (if result
      // is false) and our thread to be in n0.
      EXPECT_CALL(*mock_controller_factory_, GetNamespaceId(23))
          .WillRepeatedly(Return(string("n1")));
      EXPECT_CALL(*mock_controller_factory_, GetNamespaceId(0)).WillRepeatedly(
          Return(string(virtual_host_result.ValueOrDie() ? "n0" : "n1")));

      // If we failed, don't continue.
      if (!virtual_host_result.ValueOrDie()) {
        return;
      }
    }

    // DetectInit() fails.
    if (!detect_init_result.ok()) {
      tasks_handler_factory_get_expectations.WillOnce(
          Return(detect_init_result.status()));
      return;
    }

    // Expectations for DetectInit().
    {
      MockTasksHandler *handler = new StrictMockTasksHandler(kContainerName);
      tasks_handler_factory_get_expectations.WillOnce(Return(handler));

      // Parent chain: {n1: kinit_pid} -> {n0: 89} | {n0: self}
      const pid_t init_pid = detect_init_result.ValueOrDie();
      ExpectGetProcesses(handler, {init_pid});
      ExpectGetParentPid(init_pid, 89, 2);
      EXPECT_CALL(*mock_tasks_handler_factory_, Detect(init_pid))
          .WillRepeatedly(Return(string(kContainerName)));
      EXPECT_CALL(*mock_controller_factory_, GetNamespaceId(AnyOf(0, 89)))
          .WillRepeatedly(Return(string("n0")));
      EXPECT_CALL(*mock_controller_factory_, GetNamespaceId(init_pid))
          .WillRepeatedly(Return(string("n1")));
    }
  }

  // Wrappers for testing private methods.

  StatusOr<bool> CallIsVirtualHost(const string &container_name) {
    return factory_->IsVirtualHost(container_name);
  }

  StatusOr<pid_t> CallGetParentPid(pid_t pid) {
    return factory_->GetParentPid(pid);
  }

  StatusOr<pid_t> CallDetectInit(const string &container_name) {
    return factory_->DetectInit(container_name);
  }

 protected:
  StrictMock<MockConsoleUtil> *mock_console_util_;
  nscon::MockNamespaceControllerFactory *mock_controller_factory_;
  unique_ptr<MockTasksHandlerFactory> mock_tasks_handler_factory_;
  unique_ptr<NsconNamespaceHandlerFactory> factory_;
  FileLinesTestUtil file_lines_test_util_;
};

// Tests for Get().

TEST_F(NsconNamespaceHandlerFactoryTest, GetNamespaceHandlerRoot) {
  const string kRoot = "/";
  const pid_t kSystemInit = 1;

  nscon::MockNamespaceController *mock_controller =
      new nscon::StrictMockNamespaceController();
  EXPECT_CALL(*mock_controller_factory_, Get(kSystemInit))
      .WillRepeatedly(Return(mock_controller));
  EXPECT_CALL(*mock_controller, GetPid())
      .WillRepeatedly(Return(kSystemInit));

  StatusOr<NamespaceHandler *> statusor = factory_->GetNamespaceHandler(kRoot);
  ASSERT_OK(statusor);
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<NamespaceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_VIRTUALHOST, handler->type());
  EXPECT_EQ(kRoot, handler->container_name());
  EXPECT_EQ(kSystemInit, handler->GetInitPid());
}

TEST_F(NsconNamespaceHandlerFactoryTest, GetNamespaceHandlerSuccess) {
  ExpectIsVirtualHostAndDetectInit(true, kInit);

  nscon::MockNamespaceController *mock_controller =
      new nscon::StrictMockNamespaceController();
  EXPECT_CALL(*mock_controller_factory_, Get(kInit))
      .WillRepeatedly(Return(mock_controller));
  EXPECT_CALL(*mock_controller, GetPid())
      .WillRepeatedly(Return(kInit));

  StatusOr<NamespaceHandler *> statusor =
      factory_->GetNamespaceHandler(kContainerName);
  ASSERT_OK(statusor);
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<NamespaceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_VIRTUALHOST, handler->type());
  EXPECT_EQ(kContainerName, handler->container_name());
}

TEST_F(NsconNamespaceHandlerFactoryTest, GetNamespaceHandlerNotInVirtualHost) {
  ExpectIsVirtualHostAndDetectInit(false, kInit);

  EXPECT_ERROR_CODE(NOT_FOUND, factory_->GetNamespaceHandler(kContainerName));
}

TEST_F(NsconNamespaceHandlerFactoryTest,
       GetNamespaceHandlerIsVirtualHostFails) {
  ExpectIsVirtualHostAndDetectInit(Status::CANCELLED, kInit);

  EXPECT_EQ(Status::CANCELLED,
            factory_->GetNamespaceHandler(kContainerName).status());
}

TEST_F(NsconNamespaceHandlerFactoryTest, GetNamespaceHandlerDetectInitFails) {
  ExpectIsVirtualHostAndDetectInit(true, Status::CANCELLED);

  EXPECT_ERROR_CODE(::util::error::CANCELLED,
                    factory_->GetNamespaceHandler(kContainerName));
}

TEST_F(NsconNamespaceHandlerFactoryTest, GetNamespaceHandlerNoController) {
  ExpectIsVirtualHostAndDetectInit(true, kInit);
  EXPECT_CALL(*mock_controller_factory_, Get(kInit))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_ERROR_CODE(::util::error::CANCELLED,
                    factory_->GetNamespaceHandler(kContainerName));
}

// Tests for Create().

TEST_F(NsconNamespaceHandlerFactoryTest, CreateNamespaceHandlerSuccess) {
  ContainerSpec spec;
  spec.mutable_virtual_host();
  nscon::NamespaceSpec namespace_spec;
  namespace_spec.mutable_pid();
  namespace_spec.mutable_ipc();
  namespace_spec.mutable_mnt();
  namespace_spec.mutable_fs()->mutable_machine();
  namespace_spec.mutable_run_spec()->set_inherit_fds(true);
  // controller ownership transferred to namespace handler.
  nscon::MockNamespaceController *mock_controller =
      new nscon::StrictMockNamespaceController();
  EXPECT_CALL(*mock_controller_factory_,
              Create(EqualsInitializedProto(namespace_spec), IsEmpty()))
      .WillRepeatedly(Return(mock_controller));
  EXPECT_CALL(*mock_controller, GetPid())
      .WillRepeatedly(Return(kInit));

  StatusOr<NamespaceHandler *> statusor =
      factory_->CreateNamespaceHandler(kContainerName, spec, {});
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<NamespaceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_VIRTUALHOST, handler->type());
  EXPECT_EQ(kContainerName, handler->container_name());
}

TEST_F(NsconNamespaceHandlerFactoryTest, CreateNamespaceHandlerInvalidSpec) {
  ContainerSpec spec;
  EXPECT_ERROR_CODE(INVALID_ARGUMENT,
                    factory_->CreateNamespaceHandler(kContainerName, spec, {}));
}

TEST_F(NsconNamespaceHandlerFactoryTest, CreateNamespaceHandlerNoController) {
  ContainerSpec spec;
  spec.mutable_virtual_host();
  EXPECT_CALL(*mock_controller_factory_, Create(_, IsEmpty()))
      .WillRepeatedly(Return(Status::CANCELLED));
  EXPECT_ERROR_CODE(::util::error::CANCELLED,
                    factory_->CreateNamespaceHandler(kContainerName, spec, {}));
}

TEST_F(NsconNamespaceHandlerFactoryTest, CreateNamespaceHandlerHierarchical) {
  const string kChildName = "/parent/child";
  ContainerSpec spec;
  spec.mutable_virtual_host();
  EXPECT_ERROR_CODE(UNIMPLEMENTED,
                    factory_->CreateNamespaceHandler(kChildName, spec, {}));
}

TEST_F(NsconNamespaceHandlerFactoryTest, CreateNamespaceHandlerCustomInit) {
  ContainerSpec spec;
  VirtualHostSpec *vhost = spec.mutable_virtual_host();

  const vector<string> kInitArgv = { "/custom/init", "arg1", "arg2" };

  for (int i = 0; i < kInitArgv.size(); i++) {
    vhost->mutable_init()->add_init_argv(kInitArgv[i]);
  }

  // controller ownership transferred to namespace handler.
  nscon::MockNamespaceController *mock_controller =
      new nscon::StrictMockNamespaceController();
  EXPECT_CALL(*mock_controller_factory_, Create(_, kInitArgv))
      .WillRepeatedly(Return(mock_controller));
  EXPECT_CALL(*mock_controller, GetPid())
      .WillRepeatedly(Return(kInit));

  StatusOr<NamespaceHandler *> statusor =
      factory_->CreateNamespaceHandler(kContainerName, spec, {});
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<NamespaceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_VIRTUALHOST, handler->type());
  EXPECT_EQ(kContainerName, handler->container_name());
}

TEST_F(NsconNamespaceHandlerFactoryTest, CreateNamespaceHandlerWithConsole) {
  ContainerSpec spec;
  spec.mutable_virtual_host()->mutable_init()->mutable_run_spec()
      ->mutable_console()->set_slave_pty("10");
  // controller ownership transferred to namespace handler.
  nscon::MockNamespaceController *mock_controller =
      new nscon::StrictMockNamespaceController();
  nscon::NamespaceSpec namespace_spec;
  namespace_spec.mutable_pid();
  namespace_spec.mutable_ipc();
  namespace_spec.mutable_mnt();
  namespace_spec.mutable_fs()->mutable_machine();
  namespace_spec.mutable_run_spec()->mutable_console()->set_slave_pty("10");
  namespace_spec.mutable_run_spec()->set_inherit_fds(true);
  EXPECT_CALL(*mock_controller_factory_,
              Create(EqualsInitializedProto(namespace_spec), _))
      .WillRepeatedly(Return(mock_controller));
  EXPECT_CALL(*mock_controller, GetPid())
      .WillRepeatedly(Return(kInit));

  StatusOr<NamespaceHandler *> statusor =
      factory_->CreateNamespaceHandler(kContainerName, spec, {});
  ASSERT_OK(statusor);
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<NamespaceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_VIRTUALHOST, handler->type());
  EXPECT_EQ(kContainerName, handler->container_name());
}

TEST_F(NsconNamespaceHandlerFactoryTest, CreateNamespaceHandlerWithRootfs) {
  ContainerSpec spec;
  spec.mutable_virtual_host();
  spec.mutable_filesystem()->set_rootfs("blah");
  // controller ownership transferred to namespace handler.
  nscon::MockNamespaceController *mock_controller =
      new nscon::StrictMockNamespaceController();
  nscon::NamespaceSpec namespace_spec;
  namespace_spec.mutable_pid();
  namespace_spec.mutable_ipc();
  namespace_spec.mutable_mnt();
  namespace_spec.mutable_fs()->mutable_machine();
  namespace_spec.mutable_fs()->set_rootfs_path(spec.filesystem().rootfs());
  namespace_spec.mutable_run_spec()->set_inherit_fds(true);
  EXPECT_CALL(*mock_controller_factory_,
              Create(EqualsInitializedProto(namespace_spec), _))
      .WillRepeatedly(Return(mock_controller));
  EXPECT_CALL(*mock_controller, GetPid())
      .WillRepeatedly(Return(kInit));

  StatusOr<NamespaceHandler *> statusor =
      factory_->CreateNamespaceHandler(kContainerName, spec, {});
  ASSERT_OK(statusor);
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<NamespaceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_VIRTUALHOST, handler->type());
  EXPECT_EQ(kContainerName, handler->container_name());
}

TEST_F(NsconNamespaceHandlerFactoryTest, CreateNamespaceHandlerWithMounts) {
  ContainerSpec spec;
  spec.mutable_virtual_host();
  auto mount = spec.mutable_filesystem()->mutable_mounts()->add_mount();
  mount->set_source("/a");
  mount->set_target("/b");
  mount->set_private_(true);
  mount = spec.mutable_filesystem()->mutable_mounts()->add_mount();
  mount->set_source("/c");
  mount->set_target("/d");
  mount->set_read_only(true);

  // controller ownership transferred to namespace handler.
  nscon::MockNamespaceController *mock_controller =
      new nscon::StrictMockNamespaceController();
  nscon::NamespaceSpec namespace_spec;
  namespace_spec.mutable_pid();
  namespace_spec.mutable_ipc();
  namespace_spec.mutable_mnt();
  namespace_spec.mutable_fs()->mutable_machine();
  namespace_spec.mutable_run_spec()->set_inherit_fds(true);
  namespace_spec.mutable_fs()->mutable_external_mounts()->CopyFrom(
      spec.filesystem().mounts());
  EXPECT_CALL(*mock_controller_factory_,
              Create(EqualsInitializedProto(namespace_spec), _))
      .WillRepeatedly(Return(mock_controller));
  EXPECT_CALL(*mock_controller, GetPid())
      .WillRepeatedly(Return(kInit));

  StatusOr<NamespaceHandler *> statusor =
      factory_->CreateNamespaceHandler(kContainerName, spec, {});
  ASSERT_OK(statusor);
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<NamespaceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_VIRTUALHOST, handler->type());
  EXPECT_EQ(kContainerName, handler->container_name());
}

TEST_F(NsconNamespaceHandlerFactoryTest, CreateNamespaceHandler_MachineSpec) {
  ContainerSpec spec;
  spec.mutable_virtual_host();
  nscon::NamespaceSpec namespace_spec;
  namespace_spec.mutable_pid();
  namespace_spec.mutable_ipc();
  namespace_spec.mutable_mnt();
  namespace_spec.mutable_run_spec()->set_inherit_fds(true);
  MachineSpec machine_spec;
  auto virt_root1 =
      machine_spec.mutable_virtual_root()->add_cgroup_virtual_root();
  virt_root1->set_root("/test_cpu");
  virt_root1->set_hierarchy(CGROUP_CPU);
  auto virt_root2 =
      machine_spec.mutable_virtual_root()->add_cgroup_virtual_root();
  virt_root2->set_root("/test_memory");
  virt_root2->set_hierarchy(CGROUP_MEMORY);

  namespace_spec.mutable_fs()->mutable_machine()->CopyFrom(machine_spec);
  // controller ownership transferred to namespace handler.
  nscon::MockNamespaceController *mock_controller =
      new nscon::StrictMockNamespaceController();
  EXPECT_CALL(*mock_controller_factory_,
              Create(EqualsInitializedProto(namespace_spec), IsEmpty()))
      .WillRepeatedly(Return(mock_controller));
  EXPECT_CALL(*mock_controller, GetPid())
      .WillRepeatedly(Return(kInit));

  StatusOr<NamespaceHandler *> statusor =
      factory_->CreateNamespaceHandler(kContainerName, spec, machine_spec);
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<NamespaceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_VIRTUALHOST, handler->type());
  EXPECT_EQ(kContainerName, handler->container_name());
}


// Tests for IsVirtualHost().
typedef NsconNamespaceHandlerFactoryTest IsVirtualHostTest;

TEST_F(IsVirtualHostTest, Root) {
  StatusOr<bool> statusor = CallIsVirtualHost("/");
  ASSERT_OK(statusor);
  EXPECT_FALSE(statusor.ValueOrDie());
}

TEST_F(IsVirtualHostTest, Subcontainer) {
  StatusOr<bool> statusor = CallIsVirtualHost("/task/sub");
  ASSERT_OK(statusor);
  EXPECT_FALSE(statusor.ValueOrDie());
}

TEST_F(IsVirtualHostTest, NoProcesses) {
  // Expect no processes in the container.
  MockTasksHandler *handler = new StrictMockTasksHandler(kContainerName);
  EXPECT_CALL(*mock_tasks_handler_factory_, Get(kContainerName))
      .WillOnce(Return(handler));
  EXPECT_CALL(*handler, ListProcesses(TasksHandler::ListType::SELF))
      .WillRepeatedly(Return(vector<pid_t>()));
  EXPECT_CALL(*handler, ListProcesses(TasksHandler::ListType::RECURSIVE))
      .WillRepeatedly(Return(vector<pid_t>()));

  StatusOr<bool> statusor = CallIsVirtualHost(kContainerName);
  ASSERT_OK(statusor);
  EXPECT_FALSE(statusor.ValueOrDie());
}

TEST_F(IsVirtualHostTest, InNamespace) {
  // Expect 3 processes in the container.
  MockTasksHandler *handler = new StrictMockTasksHandler(kContainerName);
  EXPECT_CALL(*mock_tasks_handler_factory_, Get(kContainerName))
      .WillOnce(Return(handler));
  EXPECT_CALL(*handler, ListProcesses(TasksHandler::ListType::SELF))
      .WillRepeatedly(Return(vector<pid_t>{23, 24, 25}));

  // Expect the container to be in n1 and our thread to be in n0.
  EXPECT_CALL(*mock_controller_factory_, GetNamespaceId(AnyOf(23, 24, 25)))
      .WillRepeatedly(Return(string("n1")));
  EXPECT_CALL(*mock_controller_factory_, GetNamespaceId(0))
      .WillRepeatedly(Return(string("n0")));

  StatusOr<bool> statusor = CallIsVirtualHost(kContainerName);
  ASSERT_OK(statusor);
  EXPECT_TRUE(statusor.ValueOrDie());
}

TEST_F(IsVirtualHostTest, NotInNamespace) {
  // Expect 3 processes in the container.
  MockTasksHandler *handler = new StrictMockTasksHandler(kContainerName);
  EXPECT_CALL(*mock_tasks_handler_factory_, Get(kContainerName))
      .WillOnce(Return(handler));
  EXPECT_CALL(*handler, ListProcesses(TasksHandler::ListType::SELF))
      .WillRepeatedly(Return(vector<pid_t>{23, 24, 25}));

  // Expect all PIDs to be in n0.
  EXPECT_CALL(*mock_controller_factory_, GetNamespaceId(_))
      .WillRepeatedly(Return(string("n0")));

  StatusOr<bool> statusor = CallIsVirtualHost(kContainerName);
  ASSERT_OK(statusor);
  EXPECT_FALSE(statusor.ValueOrDie());
}

TEST_F(IsVirtualHostTest, NoDirectProcesses) {
  // Expect 3 processes in the container.
  MockTasksHandler *handler = new StrictMockTasksHandler(kContainerName);
  EXPECT_CALL(*mock_tasks_handler_factory_, Get(kContainerName))
      .WillOnce(Return(handler));
  EXPECT_CALL(*handler, ListProcesses(TasksHandler::ListType::SELF))
      .WillRepeatedly(Return(vector<pid_t>()));
  EXPECT_CALL(*handler, ListProcesses(TasksHandler::ListType::RECURSIVE))
      .WillRepeatedly(Return(vector<pid_t>{23, 24, 25}));

  // Expect the container to be in n1 and our thread to be in n0.
  EXPECT_CALL(*mock_controller_factory_, GetNamespaceId(AnyOf(23, 24, 25)))
      .WillRepeatedly(Return(string("n1")));
  EXPECT_CALL(*mock_controller_factory_, GetNamespaceId(0))
      .WillRepeatedly(Return(string("n0")));

  StatusOr<bool> statusor = CallIsVirtualHost(kContainerName);
  ASSERT_OK(statusor);
  EXPECT_TRUE(statusor.ValueOrDie());
}

TEST_F(IsVirtualHostTest, GetTasksHandlerFails) {
  EXPECT_CALL(*mock_tasks_handler_factory_, Get(kContainerName))
      .WillOnce(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, CallIsVirtualHost(kContainerName).status());
}

TEST_F(IsVirtualHostTest, ListProcessesFails) {
  MockTasksHandler *handler = new StrictMockTasksHandler(kContainerName);
  EXPECT_CALL(*mock_tasks_handler_factory_, Get(kContainerName))
      .WillOnce(Return(handler));
  EXPECT_CALL(*handler, ListProcesses(TasksHandler::ListType::SELF))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, CallIsVirtualHost(kContainerName).status());
}

TEST_F(IsVirtualHostTest, ListProcessesRecursiveFails) {
  MockTasksHandler *handler = new StrictMockTasksHandler(kContainerName);
  EXPECT_CALL(*mock_tasks_handler_factory_, Get(kContainerName))
      .WillOnce(Return(handler));
  EXPECT_CALL(*handler, ListProcesses(TasksHandler::ListType::SELF))
      .WillRepeatedly(Return(vector<pid_t>()));
  EXPECT_CALL(*handler, ListProcesses(TasksHandler::ListType::RECURSIVE))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, CallIsVirtualHost(kContainerName).status());
}

TEST_F(IsVirtualHostTest, GetNamespaceIdFails) {
  MockTasksHandler *handler = new StrictMockTasksHandler(kContainerName);
  EXPECT_CALL(*mock_tasks_handler_factory_, Get(kContainerName))
      .WillOnce(Return(handler));
  EXPECT_CALL(*handler, ListProcesses(TasksHandler::ListType::SELF))
      .WillRepeatedly(Return(vector<pid_t>{23, 24, 25}));

  EXPECT_CALL(*mock_controller_factory_, GetNamespaceId(_))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, CallIsVirtualHost(kContainerName).status());
}

// Tests for GetParentPid().
typedef NsconNamespaceHandlerFactoryTest GetParentPidTest;

static const pid_t kPid = 42;
static const char kProcStatus[] = "/proc/42/status";

TEST_F(GetParentPidTest, Success) {
  const pid_t kParent = 43;

  ExpectGetParentPid(kPid, kParent);

  StatusOr<pid_t> statusor = CallGetParentPid(kPid);
  ASSERT_OK(statusor);
  EXPECT_EQ(kParent, statusor.ValueOrDie());
}

TEST_F(GetParentPidTest, NoParentPid) {
  file_lines_test_util_.ExpectFileLines(kProcStatus,
                                        {"Name: cat", "State:  R (running)"});

  EXPECT_ERROR_CODE(NOT_FOUND, CallGetParentPid(kPid));
}

TEST_F(GetParentPidTest, FailedToParsePid) {
  file_lines_test_util_.ExpectFileLines(
      kProcStatus, {"Name: cat", "State:  R (running)", "PPid: potato"});

  EXPECT_ERROR_CODE(INTERNAL, CallGetParentPid(kPid));
}

// Tests for DetectInit().
typedef NsconNamespaceHandlerFactoryTest DetectInitTest;

TEST_F(DetectInitTest, AtRoot) {
  StatusOr<pid_t> statusor = CallDetectInit("/");
  ASSERT_OK(statusor);
  EXPECT_EQ(1, statusor.ValueOrDie());
}

TEST_F(DetectInitTest, NoProcesses) {
  MockTasksHandler *handler = new StrictMockTasksHandler(kContainerName);
  EXPECT_CALL(*mock_tasks_handler_factory_, Get(kContainerName))
      .WillOnce(Return(handler));

  EXPECT_CALL(*handler, ListProcesses(_))
      .WillRepeatedly(Return(vector<pid_t>()));
  EXPECT_CALL(*mock_controller_factory_, GetNamespaceId(0))
      .WillRepeatedly(Return(string("n0")));

  EXPECT_ERROR_CODE(INVALID_ARGUMENT, CallDetectInit(kContainerName));
}

TEST_F(DetectInitTest, OnlyInit) {
  MockTasksHandler *handler = new StrictMockTasksHandler(kContainerName);
  EXPECT_CALL(*mock_tasks_handler_factory_, Get(kContainerName))
      .WillOnce(Return(handler));

  // Parent chain: {n1: kInit} -> {n0: 1} | {n0: self}
  ExpectGetProcesses(handler, {kInit});
  ExpectGetParentPid(kInit, 1, 2);
  EXPECT_CALL(*mock_tasks_handler_factory_, Detect(kInit))
      .WillRepeatedly(Return(string(kContainerName)));
  EXPECT_CALL(*mock_controller_factory_, GetNamespaceId(AnyOf(0, 1)))
      .WillRepeatedly(Return(string("n0")));
  EXPECT_CALL(*mock_controller_factory_, GetNamespaceId(kInit))
      .WillRepeatedly(Return(string("n1")));

  StatusOr<pid_t> statusor = CallDetectInit(kContainerName);
  ASSERT_OK(statusor);
  EXPECT_EQ(kInit, statusor.ValueOrDie());
}

TEST_F(DetectInitTest, MultipleHops) {
  MockTasksHandler *handler = new StrictMockTasksHandler(kContainerName);
  EXPECT_CALL(*mock_tasks_handler_factory_, Get(kContainerName))
      .WillOnce(Return(handler));

  // Parent chain: {n1: 42} -> {n1: kInit} -> {n0: 1} | {n0: self}
  ExpectGetProcesses(handler, {42, kInit});
  ExpectGetParentPid(42, kInit);
  ExpectGetParentPid(kInit, 1, 2);
  EXPECT_CALL(*mock_tasks_handler_factory_, Detect(kInit))
      .WillRepeatedly(Return(string(kContainerName)));
  EXPECT_CALL(*mock_controller_factory_, GetNamespaceId(AnyOf(0, 1)))
      .WillRepeatedly(Return(string("n0")));
  EXPECT_CALL(*mock_controller_factory_, GetNamespaceId(AnyOf(kInit, 42)))
      .WillRepeatedly(Return(string("n1")));

  StatusOr<pid_t> statusor = CallDetectInit(kContainerName);
  ASSERT_OK(statusor);
  EXPECT_EQ(kInit, statusor.ValueOrDie());
}

TEST_F(DetectInitTest, MultipleHopsAndMultipleNamespaces) {
  MockTasksHandler *handler = new StrictMockTasksHandler(kContainerName);
  EXPECT_CALL(*mock_tasks_handler_factory_, Get(kContainerName))
      .WillOnce(Return(handler));

  // Parent chain: {n2: 42} -> {n1: kInit} -> {n0: 1} | {n0: self}
  ExpectGetProcesses(handler, {42, kInit});
  ExpectGetParentPid(42, kInit);
  ExpectGetParentPid(kInit, 1, 2);
  EXPECT_CALL(*mock_tasks_handler_factory_, Detect(kInit))
      .WillRepeatedly(Return(string(kContainerName)));
  EXPECT_CALL(*mock_controller_factory_, GetNamespaceId(AnyOf(0, 1)))
      .WillRepeatedly(Return(string("n0")));
  EXPECT_CALL(*mock_controller_factory_, GetNamespaceId(kInit))
      .WillRepeatedly(Return(string("n1")));
  EXPECT_CALL(*mock_controller_factory_, GetNamespaceId(42))
      .WillRepeatedly(Return(string("n2")));

  StatusOr<pid_t> statusor = CallDetectInit(kContainerName);
  ASSERT_OK(statusor);
  EXPECT_EQ(kInit, statusor.ValueOrDie());
}

TEST_F(DetectInitTest, TaskDiesDuringCrawl_GetParentPid) {
  MockTasksHandler *handler = new StrictMockTasksHandler(kContainerName);
  EXPECT_CALL(*mock_tasks_handler_factory_, Get(kContainerName))
      .WillOnce(Return(handler));

  // Parent chain: {n1: 43} -> {n1: 42} -> {n1: kInit} -> {n0: 1} | {n0: self}
  //                           {n1: 44}  /
  EXPECT_CALL(*handler, ListProcesses(TasksHandler::ListType::SELF))
      .WillOnce(Return(vector<pid_t>{43, 42, kInit}))
      .WillOnce(Return(vector<pid_t>{44, kInit}));
  ExpectGetParentPid(43, 42);
  ExpectGetParentPid(44, kInit);
  ExpectGetParentPid(kInit, 1, 2);
  file_lines_test_util_.ExpectFileLines("/proc/42/status", {});
  EXPECT_CALL(*mock_tasks_handler_factory_, Detect(kInit))
      .WillRepeatedly(Return(string(kContainerName)));
  EXPECT_CALL(*mock_controller_factory_, GetNamespaceId(AnyOf(0, 1)))
      .WillRepeatedly(Return(string("n0")));
  EXPECT_CALL(*mock_controller_factory_,
              GetNamespaceId(AnyOf(kInit, 42, 43, 44)))
      .WillRepeatedly(Return(string("n1")));

  StatusOr<pid_t> statusor = CallDetectInit(kContainerName);
  ASSERT_OK(statusor);
  EXPECT_EQ(kInit, statusor.ValueOrDie());
}

TEST_F(DetectInitTest, TaskDiesDuringCrawl_GetNamespaceId) {
  MockTasksHandler *handler = new StrictMockTasksHandler(kContainerName);
  EXPECT_CALL(*mock_tasks_handler_factory_, Get(kContainerName))
      .WillOnce(Return(handler));

  // Parent chain: {n1: 43} -> {n1: 42} -> {n1: kInit} -> {n0: 1} | {n0: self}
  //                           {n1: 44}  /
  EXPECT_CALL(*handler, ListProcesses(TasksHandler::ListType::SELF))
      .WillOnce(Return(vector<pid_t>{43, 42, kInit}))
      .WillOnce(Return(vector<pid_t>{44, kInit}));
  ExpectGetParentPid(43, 42);
  ExpectGetParentPid(44, kInit);
  ExpectGetParentPid(kInit, 1, 2);
  EXPECT_CALL(*mock_tasks_handler_factory_, Detect(kInit))
      .WillRepeatedly(Return(string(kContainerName)));
  EXPECT_CALL(*mock_controller_factory_, GetNamespaceId(AnyOf(0, 1)))
      .WillRepeatedly(Return(string("n0")));
  EXPECT_CALL(*mock_controller_factory_, GetNamespaceId(AnyOf(kInit, 43, 44)))
      .WillRepeatedly(Return(string("n1")));
  EXPECT_CALL(*mock_controller_factory_, GetNamespaceId(42))
      .WillOnce(Return(Status(NOT_FOUND, "")));

  StatusOr<pid_t> statusor = CallDetectInit(kContainerName);
  ASSERT_OK(statusor);
  EXPECT_EQ(kInit, statusor.ValueOrDie());
}

TEST_F(DetectInitTest, VerificationFailsAfterInitFound_GetParentPid) {
  MockTasksHandler *handler = new StrictMockTasksHandler(kContainerName);
  EXPECT_CALL(*mock_tasks_handler_factory_, Get(kContainerName))
      .WillOnce(Return(handler));

  // Parent chain: {n1: 42} -> {n1: kInit} -> {n0: 1} | {n0: self}
  EXPECT_CALL(*handler, ListProcesses(TasksHandler::ListType::SELF))
      .WillRepeatedly(Return(vector<pid_t>{42, kInit}));
  ExpectGetParentPid(42, kInit, 2);
  // Fails the first verification (index 1) then succeeds second (index 3).
  const string kPPid = "PPid: 1";
  file_lines_test_util_.ExpectFileLinesMulti(
      Substitute("/proc/$0/status", kInit), {{kPPid}, {}, {kPPid}, {kPPid}});
  EXPECT_CALL(*mock_tasks_handler_factory_, Detect(kInit))
      .WillRepeatedly(Return(string(kContainerName)));
  EXPECT_CALL(*mock_controller_factory_, GetNamespaceId(AnyOf(0, 1)))
      .WillRepeatedly(Return(string("n0")));
  EXPECT_CALL(*mock_controller_factory_, GetNamespaceId(AnyOf(kInit, 42)))
      .WillRepeatedly(Return(string("n1")));

  StatusOr<pid_t> statusor = CallDetectInit(kContainerName);
  ASSERT_OK(statusor);
  EXPECT_EQ(kInit, statusor.ValueOrDie());
}

TEST_F(DetectInitTest, VerificationFailsAfterInitFound_DetectOtherContainer) {
  MockTasksHandler *handler = new StrictMockTasksHandler(kContainerName);
  EXPECT_CALL(*mock_tasks_handler_factory_, Get(kContainerName))
      .WillOnce(Return(handler));

  // Parent chain: {n1: 42} -> {n1: kInit} -> {n0: 1} | {n0: self}
  EXPECT_CALL(*handler, ListProcesses(TasksHandler::ListType::SELF))
      .WillRepeatedly(Return(vector<pid_t>{42, kInit}));
  ExpectGetParentPid(42, kInit, 2);
  ExpectGetParentPid(kInit, 1, 4);
  EXPECT_CALL(*mock_tasks_handler_factory_, Detect(kInit))
      .WillOnce(Return(string("/other/container")))
      .WillRepeatedly(Return(string(kContainerName)));
  EXPECT_CALL(*mock_controller_factory_, GetNamespaceId(AnyOf(0, 1)))
      .WillRepeatedly(Return(string("n0")));
  EXPECT_CALL(*mock_controller_factory_, GetNamespaceId(AnyOf(kInit, 42)))
      .WillRepeatedly(Return(string("n1")));

  StatusOr<pid_t> statusor = CallDetectInit(kContainerName);
  ASSERT_OK(statusor);
  EXPECT_EQ(kInit, statusor.ValueOrDie());
}

TEST_F(DetectInitTest, RunOutOfRetries) {
  MockTasksHandler *handler = new StrictMockTasksHandler(kContainerName);
  EXPECT_CALL(*mock_tasks_handler_factory_, Get(kContainerName))
      .WillOnce(Return(handler));

  // Make it seem like 42 is always killed when we go check it.
  // Parent chain: {n1: 43} -> {n1: 42} -> {n1: kInit} -> {n0: 1} | {n0: self}
  EXPECT_CALL(*handler, ListProcesses(TasksHandler::ListType::SELF))
      .WillRepeatedly(Return(vector<pid_t>{43, 42, kInit}));
  ExpectGetParentPid(43, 42, 10);
  EXPECT_CALL(*mock_controller_factory_, GetNamespaceId(AnyOf(0, 1)))
      .WillRepeatedly(Return(string("n0")));
  EXPECT_CALL(*mock_controller_factory_, GetNamespaceId(AnyOf(kInit, 43)))
      .WillRepeatedly(Return(string("n1")));
  EXPECT_CALL(*mock_controller_factory_, GetNamespaceId(42))
      .WillRepeatedly(Return(Status(NOT_FOUND, "")));

  EXPECT_ERROR_CODE(UNAVAILABLE, CallDetectInit(kContainerName));
}

TEST_F(DetectInitTest, GetTasksHandlerFails) {
  EXPECT_CALL(*mock_tasks_handler_factory_, Get(kContainerName))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, CallDetectInit(kContainerName).status());
}

TEST_F(DetectInitTest, GetProcessesFails) {
  MockTasksHandler *handler = new StrictMockTasksHandler(kContainerName);
  EXPECT_CALL(*mock_tasks_handler_factory_, Get(kContainerName))
      .WillOnce(Return(handler));

  EXPECT_CALL(*handler, ListProcesses(TasksHandler::ListType::SELF))
      .WillRepeatedly(Return(Status::CANCELLED));
  EXPECT_CALL(*mock_controller_factory_, GetNamespaceId(0))
      .WillRepeatedly(Return(string("n0")));

  EXPECT_EQ(Status::CANCELLED, CallDetectInit(kContainerName).status());
}

TEST_F(DetectInitTest, GetParentPidFails) {
  MockTasksHandler *handler = new StrictMockTasksHandler(kContainerName);
  EXPECT_CALL(*mock_tasks_handler_factory_, Get(kContainerName))
      .WillOnce(Return(handler));

  // Parent chain: {n1: kInit} -> {n0: 1} | {n0: self}
  ExpectGetProcesses(handler, {kInit});
  vector<vector<string>> kMultipleEmpty(10, vector<string>());
  file_lines_test_util_.ExpectFileLinesMulti(
      Substitute("/proc/$0/status", kInit), kMultipleEmpty);
  EXPECT_CALL(*mock_controller_factory_, GetNamespaceId(0))
      .WillRepeatedly(Return(string("n0")));

  EXPECT_NOT_OK(CallDetectInit(kContainerName));
}

TEST_F(DetectInitTest, GetNamespaceIdSelfFails) {
  MockTasksHandler *handler = new StrictMockTasksHandler(kContainerName);
  EXPECT_CALL(*mock_tasks_handler_factory_, Get(kContainerName))
      .WillOnce(Return(handler));

  // Parent chain: {n1: kInit} -> {n0: 1} | {n0: self}
  ExpectGetProcesses(handler, {kInit});
  EXPECT_CALL(*mock_controller_factory_, GetNamespaceId(0))
      .WillRepeatedly(Return(Status::CANCELLED));
  EXPECT_CALL(*mock_controller_factory_, GetNamespaceId(1))
      .WillRepeatedly(Return(string("n0")));
  EXPECT_CALL(*mock_controller_factory_, GetNamespaceId(kInit))
      .WillRepeatedly(Return(string("n1")));

  EXPECT_EQ(Status::CANCELLED, CallDetectInit(kContainerName).status());
}

TEST_F(DetectInitTest, GetNamespaceIdOfProcessFails) {
  MockTasksHandler *handler = new StrictMockTasksHandler(kContainerName);
  EXPECT_CALL(*mock_tasks_handler_factory_, Get(kContainerName))
      .WillOnce(Return(handler));

  // Parent chain: {n1: kInit} -> {n0: 1} | {n0: self}
  ExpectGetProcesses(handler, {kInit});
  ExpectGetParentPid(kInit, 1);
  EXPECT_CALL(*mock_controller_factory_, GetNamespaceId(0))
      .WillRepeatedly(Return(string("n0")));
  EXPECT_CALL(*mock_controller_factory_, GetNamespaceId(1))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, CallDetectInit(kContainerName).status());
}

// Tests for InitMachine().

TEST_F(NsconNamespaceHandlerFactoryTest, InitMachineSuccess) {
  EXPECT_CALL(*mock_console_util_, EnableDevPtsNamespaceSupport())
      .WillOnce(Return(Status::OK));
  EXPECT_OK(factory_->InitMachine({}));
}

TEST_F(NsconNamespaceHandlerFactoryTest, InitMachineFailure) {
  EXPECT_CALL(*mock_console_util_, EnableDevPtsNamespaceSupport())
      .WillOnce(Return(Status::CANCELLED));
  EXPECT_NOT_OK(factory_->InitMachine({}));
}

class NsconNamespaceHandlerTest : public ::testing::Test {
 public:
  void SetUp() override {
    mock_namespace_controller_ = new nscon::StrictMockNamespaceController();
    EXPECT_CALL(*mock_namespace_controller_, GetPid())
        .WillRepeatedly(Return(kInit));
    mock_namespace_controller_factory_.reset(
        new nscon::StrictMockNamespaceControllerFactory());
    handler_.reset(
        new NsconNamespaceHandler(kContainerName, mock_namespace_controller_,
                                  mock_namespace_controller_factory_.get()));
  }

 protected:
  unique_ptr<NsconNamespaceHandler> handler_;
  nscon::MockNamespaceController *mock_namespace_controller_;
  unique_ptr<nscon::MockNamespaceControllerFactory>
      mock_namespace_controller_factory_;
};

TEST_F(NsconNamespaceHandlerTest, Spec) {
  ContainerSpec spec;
  EXPECT_OK(handler_->Spec(&spec));
  EXPECT_TRUE(spec.has_virtual_host());
}

TEST_F(NsconNamespaceHandlerTest, DestroySuccess) {
  EXPECT_CALL(*mock_namespace_controller_, Destroy())
      .WillRepeatedly(Return(Status::OK));

  EXPECT_OK(handler_->Destroy());
  // handler is deleted by Destroy().
  handler_.release();
}

TEST_F(NsconNamespaceHandlerTest, DestroyFailure) {
  EXPECT_CALL(*mock_namespace_controller_, Destroy())
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_ERROR_CODE(::util::error::CANCELLED, handler_->Destroy());
}

TEST_F(NsconNamespaceHandlerTest, CreateResource) {
  ContainerSpec spec;
  EXPECT_OK(handler_->CreateResource(spec));
}

TEST_F(NsconNamespaceHandlerTest, StatsSummary) {
  Container::StatsType type = Container::STATS_SUMMARY;
  ContainerStats stats;
  EXPECT_OK(handler_->Stats(type, &stats));
}

TEST_F(NsconNamespaceHandlerTest, StatsFull) {
  Container::StatsType type = Container::STATS_FULL;
  ContainerStats stats;
  EXPECT_OK(handler_->Stats(type, &stats));
}

TEST_F(NsconNamespaceHandlerTest, ExecSuccess) {
  const vector<string> kCommand = {"/bin/ls", "-l"};

  EXPECT_CALL(*mock_namespace_controller_, Exec(kCommand))
      .WillOnce(Return(Status::OK));

  EXPECT_ERROR_CODE(INTERNAL, handler_->Exec(kCommand));
}

TEST_F(NsconNamespaceHandlerTest, ExecEmpty) {
  const vector<string> kCommand = {};

  EXPECT_ERROR_CODE(INVALID_ARGUMENT, handler_->Exec(kCommand));
}

TEST_F(NsconNamespaceHandlerTest, ExecExecFails) {
  const vector<string> kCommand = {"/bin/ls", "-l"};

  EXPECT_CALL(*mock_namespace_controller_, Exec(kCommand))
      .WillOnce(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, handler_->Exec(kCommand));
}

TEST_F(NsconNamespaceHandlerTest, RunSuccess) {
  vector<string> command = { "ls", "-l"};
  RunSpec spec;
  nscon::RunSpec nscon_run_spec;
  EXPECT_CALL(*mock_namespace_controller_,
              Run(command, EqualsInitializedProto(nscon_run_spec)))
      .WillRepeatedly(Return(1));
  StatusOr<pid_t> statusor = handler_->Run(command, spec);
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ(1, statusor.ValueOrDie());
}

TEST_F(NsconNamespaceHandlerTest, RunSuccessWithRunSpec) {
  vector<string> command = { "ls", "-l"};
  RunSpec spec;
  spec.mutable_console()->set_slave_pty("10");
  nscon::RunSpec nscon_run_spec;
  nscon_run_spec.mutable_console()->set_slave_pty("10");
  EXPECT_CALL(*mock_namespace_controller_,
              Run(command, EqualsInitializedProto(nscon_run_spec)))
      .WillRepeatedly(Return(1));
  StatusOr<pid_t> statusor = handler_->Run(command, spec);
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ(1, statusor.ValueOrDie());
}

TEST_F(NsconNamespaceHandlerTest, RunEmptyCommand) {
  vector<string> command;
  RunSpec spec;
  EXPECT_ERROR_CODE(INVALID_ARGUMENT, handler_->Run(command, spec));
}

TEST_F(NsconNamespaceHandlerTest, RunControllerFailure) {
  vector<string> command = { "ls", "-l"};
  RunSpec spec;
  nscon::RunSpec nscon_run_spec;
  EXPECT_CALL(*mock_namespace_controller_,
              Run(command, EqualsInitializedProto(nscon_run_spec)))
      .WillRepeatedly(Return(Status::CANCELLED));
  EXPECT_ERROR_CODE(::util::error::CANCELLED,
                    handler_->Run(command, spec));
}

TEST_F(NsconNamespaceHandlerTest, UpdateDiff) {
  ContainerSpec spec;
  EXPECT_OK(handler_->Update(spec, Container::UPDATE_DIFF));
}

TEST_F(NsconNamespaceHandlerTest, UpdateDiff_WithVirtualHost) {
  ContainerSpec spec;
  spec.mutable_virtual_host();
  EXPECT_ERROR_CODE(UNIMPLEMENTED,
                    handler_->Update(spec, Container::UPDATE_DIFF));
}

TEST_F(NsconNamespaceHandlerTest, UpdateReplace) {
  ContainerSpec spec;
  EXPECT_OK(handler_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(NsconNamespaceHandlerTest, UpdateReplace_WithVirtualHost) {
  ContainerSpec spec;
  spec.mutable_virtual_host();
  EXPECT_ERROR_CODE(UNIMPLEMENTED,
                    handler_->Update(spec, Container::UPDATE_DIFF));
}

TEST_F(NsconNamespaceHandlerTest, DelegateNoOp) {
  const UnixUid kUid(42);
  const UnixGid kGid(42);
  EXPECT_OK(handler_->Delegate(kUid, kGid));
}

TEST_F(NsconNamespaceHandlerTest, NotificationsNotFound) {
  EventSpec spec;
  EXPECT_ERROR_CODE(NOT_FOUND, handler_->RegisterNotification(spec, nullptr));
}

TEST_F(NsconNamespaceHandlerTest, GetInitPid_Success) {
  EXPECT_CALL(*mock_namespace_controller_, GetPid())
      .WillOnce(Return(500));
  EXPECT_EQ(500, handler_->GetInitPid());
}

// Tests for IsDifferentVirtualHost().
typedef NsconNamespaceHandlerTest IsDifferentVirtualHostTest;

TEST_F(IsDifferentVirtualHostTest, NoTidsSpecified) {
  EXPECT_CALL(*mock_namespace_controller_factory_, GetNamespaceId(kInit))
      .WillRepeatedly(Return(string("n0")));

  StatusOr<bool> statusor = handler_->IsDifferentVirtualHost(vector<pid_t>());
  ASSERT_OK(statusor);
  EXPECT_FALSE(statusor.ValueOrDie());
}

TEST_F(IsDifferentVirtualHostTest, SameNamespace) {
  const vector<pid_t> kTids = {10, 20, 30};

  EXPECT_CALL(*mock_namespace_controller_factory_,
              GetNamespaceId(AnyOf(kInit, 10, 20, 30)))
      .WillRepeatedly(Return(string("n0")));

  StatusOr<bool> statusor = handler_->IsDifferentVirtualHost(kTids);
  ASSERT_OK(statusor);
  EXPECT_FALSE(statusor.ValueOrDie());
}

TEST_F(IsDifferentVirtualHostTest, DifferentNamespace) {
  const vector<pid_t> kTids = {10, 20, 30};

  // 20 is in a different namespace
  EXPECT_CALL(*mock_namespace_controller_factory_,
              GetNamespaceId(AnyOf(kInit, 10, 30)))
      .WillRepeatedly(Return(string("n0")));
  EXPECT_CALL(*mock_namespace_controller_factory_, GetNamespaceId(20))
      .WillRepeatedly(Return(string("n1")));

  StatusOr<bool> statusor = handler_->IsDifferentVirtualHost(kTids);
  ASSERT_OK(statusor);
  EXPECT_TRUE(statusor.ValueOrDie());
}

TEST_F(IsDifferentVirtualHostTest, GetNamespaceIdOfInitFails) {
  const vector<pid_t> kTids = {10, 20, 30};

  EXPECT_CALL(*mock_namespace_controller_factory_,
              GetNamespaceId(AnyOf(10, 20, 30)))
      .WillRepeatedly(Return(string("n0")));
  EXPECT_CALL(*mock_namespace_controller_factory_, GetNamespaceId(kInit))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED,
            handler_->IsDifferentVirtualHost(kTids).status());
}

TEST_F(IsDifferentVirtualHostTest, GetNamespaceIdOfTidFails) {
  const vector<pid_t> kTids = {10, 20, 30};

  EXPECT_CALL(*mock_namespace_controller_factory_,
              GetNamespaceId(AnyOf(kInit, 10, 30)))
      .WillRepeatedly(Return(string("n0")));
  EXPECT_CALL(*mock_namespace_controller_factory_, GetNamespaceId(20))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED,
            handler_->IsDifferentVirtualHost(kTids).status());
}

}  // namespace lmctfy
}  // namespace containers
