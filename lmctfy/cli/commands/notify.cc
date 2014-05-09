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

#include "lmctfy/cli/commands/notify.h"

#include <memory>
#include <string>
using ::std::string;
#include <vector>

#include "gflags/gflags.h"
#include "base/logging.h"
#include "base/notification.h"
#include "lmctfy/cli/command.h"
#include "include/lmctfy.h"
#include "include/lmctfy.pb.h"
#include "util/errors.h"
#include "strings/numbers.h"
#include "strings/substitute.h"
#include "util/task/codes.pb.h"
#include "util/task/statusor.h"

DECLARE_string(lmctfy_config);

using ::std::unique_ptr;
using ::std::vector;
using ::strings::Substitute;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {
namespace cli {

// TODO(vmarmol): Add the ability to stream notifications.

// Handle a notification by storing the status and notifying the waiting thread.
static void NotificationHandler(Notification *notification, Status *out_status,
                         Container *container, Status status) {
  *out_status = status;
  notification->Notify();
}

// Register and wait for the specified event in the specified container.
static Status RegisterNotification(const EventSpec &spec,
                                   const string &container_name,
                                   const ContainerApi *lmctfy,
                                   OutputMap *output) {
  // Ensure the container exists.
  unique_ptr<Container> container(
      RETURN_IF_ERROR(lmctfy->Get(container_name)));

  // Ask for the notification and wait for it to occur.
  Status status;
  Notification notification;
  RETURN_IF_ERROR(
      container->RegisterNotification(spec,
                                      NewPermanentCallback(&NotificationHandler,
                                      &notification, &status)));
  notification.WaitForNotification();

  output->Add("notification_status", Substitute("$0", status.error_code()));
  return status;
}

// Register and wait for an out of memory notification.
Status MemoryOomHandler(const vector<string> &argv, const ContainerApi *lmctfy,
                        OutputMap *output) {
  // Args: oom <container name>
  if (argv.size() != 2) {
    return Status(::util::error::INVALID_ARGUMENT,
                  "See help for supported options.");
  }
  const string container_name = argv[1];

  EventSpec spec;
  spec.mutable_oom();
  return RegisterNotification(spec, container_name, lmctfy, output);
}

// Register and wait for a memory usage threshold notification.
Status MemoryThresholdHandler(const vector<string> &argv,
                              const ContainerApi *lmctfy,
                              OutputMap *output) {
  // Args: threshold <container name> <threshold in bytes>
  if (argv.size() != 3) {
    return Status(::util::error::INVALID_ARGUMENT,
                  "See help for supported options.");
  }
  const string container_name = argv[1];
  uint64 threshold;
  if (!SimpleAtoi(argv[2], &threshold)) {
    return Status(
        ::util::error::INVALID_ARGUMENT,
        Substitute("Failed to parse a threshold from \"$0\"", argv[2]));
  }

  EventSpec spec;
  spec.mutable_memory_threshold()->set_usage(threshold);
  return RegisterNotification(spec, container_name, lmctfy, output);
}

void RegisterNotifyCommands() {
  RegisterRootCommand(SUB(
      "notify",
      "Register for and deliver a notification for the specified event. "
      "Exit after the notification occurs.",
      "<resource> <event> <container name> [<event arguments>]",
      {SUB("memory", "Register for and deliver a memory related notification.",
           "<event> <container name> [<event arguments>]",
           {CMD("oom",
                "Register for and deliver an out of memory notification. "
                "The notification is triggered when the container runs out of "
                "memory.",
                "<container name>", CMD_TYPE_SETTER, 1, 1, &MemoryOomHandler),
            CMD("threshold",
                "Register for and deliver a memory usage threshold "
                "notification. "
                "The notification is triggered when the memory usage goes "
                "above the specified threshold.",
                "<container name> <threshold in bytes>", CMD_TYPE_SETTER, 2, 2,
                &MemoryThresholdHandler)})}));
}

}  // namespace cli
}  // namespace lmctfy
}  // namespace containers
