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

#include "lmctfy/cli/commands/util.h"

#include "gflags/gflags.h"
#include "file/base/file.h"
#include "file/base/helpers.h"
#include "google/protobuf/io/tokenizer.h"
#include "google/protobuf/text_format.h"
#include "util/errors.h"
#include "util/task/codes.pb.h"
#include "util/task/status.h"

DECLARE_string(lmctfy_config);

using ::util::Status;
using ::std::vector;
using ::util::error::INVALID_ARGUMENT;
using ::google::protobuf::TextFormat;
using ::google::protobuf::io::ErrorCollector;

class SilentErrorCollector : public ErrorCollector {
 public:
  void AddError(int line, int column, const string &message) override {}
};

namespace containers {
namespace lmctfy {
namespace cli {

Status GetSpecFromConfigOrInline(const vector<string> &argv,
                                 int inline_config_position,
                                 ContainerSpec *spec) {
  // Ensure that a config file or a ASCII/binary proto was specified (not either
  // or both).
  if (FLAGS_lmctfy_config.empty() && argv.size() <= inline_config_position) {
    return Status(
        INVALID_ARGUMENT,
        "Must specify a container config (via -c) or an "
        "ASCII/Binary config on the command line");
  }

  if (!FLAGS_lmctfy_config.empty() &&
      argv.size() > inline_config_position) {
    return Status(
        INVALID_ARGUMENT,
        "Can not specify both a container config and an ASCII/Binary config on "
        "the command line");
  }

  string config;
  // Read from file or from the command line.
  if (!FLAGS_lmctfy_config.empty()) {
    RETURN_IF_ERROR(::file::GetContents(FLAGS_lmctfy_config,
                                        &config,
                                        ::file::Defaults()));
  } else {
    config = argv[inline_config_position];
  }

  // Parsing from text format may fail if input is binary and we don't want to
  // log an error in such case.
  SilentErrorCollector silent_error_collector;
  TextFormat::Parser parser;
  parser.RecordErrorsTo(&silent_error_collector);
  // Try to parse the proto as both text format and binary.
  if (!parser.ParseFromString(config, spec) && !spec->ParseFromString(config)) {
    return Status(INVALID_ARGUMENT, "Failed to parse the container config");
  }
  return Status::OK;
}

}  // namespace cli
}  // namespace lmctfy
}  // namespace containers
