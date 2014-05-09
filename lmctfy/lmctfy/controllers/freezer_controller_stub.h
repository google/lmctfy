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

#ifndef SRC_CONTROLLERS_FREEZER_CONTROLLER_STUB_H_
#define SRC_CONTROLLERS_FREEZER_CONTROLLER_STUB_H_

#include "lmctfy/controllers/freezer_controller.h"

namespace containers {
namespace lmctfy {

class FreezerControllerStub : public FreezerController {
 public:
  explicit FreezerControllerStub(const string &cgroup_path)
      : FreezerController(
            "", cgroup_path, false,
            reinterpret_cast<KernelApi *>(0xABABABAB),
            reinterpret_cast<EventFdNotifications *>(0xABABABAB)) {}

  ::util::Status Destroy() override final {
    delete this;
    return ::util::Status::OK;
  }

  ::util::Status Enter(pid_t tid) override final {
    return ::util::Status::OK;
  }

  ::util::Status Delegate(::util::UnixUid uid,
                          ::util::UnixGid gid) override final {
    return ::util::Status::OK;
  }

  ::util::Status SetChildrenLimit(int64 limit) override final {
    LOG(DFATAL) << "Stub does not expect this method to be called.";
    return ::util::Status::OK;
  }

  ::util::StatusOr< ::std::vector<pid_t>> GetThreads() const override final {
    LOG(DFATAL) << "Stub does not expect this method to be called.";
    return {};
  }

  ::util::StatusOr< ::std::vector<pid_t>> GetProcesses() const override final {
    LOG(DFATAL) << "Stub does not expect this method to be called.";
    return {};
  }

  ::util::StatusOr< ::std::vector<string>> GetSubcontainers() const override
  final {
    LOG(DFATAL) << "Stub does not expect this method to be called.";
    return {};
  }

  ::util::StatusOr<int64> GetChildrenLimit() const override final {
    LOG(DFATAL) << "Stub does not expect this method to be called.";
    return 0;
  }

  ::util::Status EnableCloneChildren() override final {
    LOG(DFATAL) << "Stub does not expect this method to be called.";
    return ::util::Status::OK;
  }

  ::util::Status DisableCloneChildren() override final {
    LOG(DFATAL) << "Stub does not expect this method to be called.";
    return ::util::Status::OK;
  }

  ::util::Status Freeze() override final {
    return ::util::Status(::util::error::FAILED_PRECONDITION,
                          "Freezer support unavailable.");
  }

  ::util::Status Unfreeze() override final {
    return ::util::Status(::util::error::FAILED_PRECONDITION,
                          "Freezer support unavailable.");
  }

  ::util::StatusOr<FreezerState> State() const override final {
    return FreezerState::UNKNOWN;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(FreezerControllerStub);
};

class FreezerControllerFactoryStub : public FreezerControllerFactory {
 public:
  FreezerControllerFactoryStub()
      : FreezerControllerFactory(
            reinterpret_cast<CgroupFactory *>(0xABABABAB),
            reinterpret_cast<KernelApi *>(0xABABABAB),
            reinterpret_cast<EventFdNotifications *>(0xABABABAB),
            false) {}

  ::util::StatusOr<FreezerController *> Get(
       const string &hierarchy_path) const override final {
    return new FreezerControllerStub(hierarchy_path);
  }

  ::util::StatusOr<FreezerController *> Create(
       const string &hierarchy_path) const override final {
    return new FreezerControllerStub(hierarchy_path);
  }

  bool Exists(const string &hierarchy_path) const {
    LOG(DFATAL) << "Stub does not expect this method to be called.";
    return false;
  }

  string HierarchyName() const {
    return "freezer";
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(FreezerControllerFactoryStub);
};

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_CONTROLLERS_FREEZER_CONTROLLER_STUB_H_
