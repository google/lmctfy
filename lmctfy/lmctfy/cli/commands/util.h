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

#ifndef SRC_CLI_COMMANDS_UTIL_H_
#define SRC_CLI_COMMANDS_UTIL_H_

#include <vector>

#include "include/lmctfy.pb.h"
#include "util/task/status.h"

namespace containers {
namespace lmctfy {
namespace cli {

// Parses ContainerSpec definition either from argv[inline_config_position]
// or from command line flag (lmctfy_config). Tries parsing both from text
// format and from binary format. This function checks if exactly one config
// source is specified (positional argument or flag). If this is not true
// returns an error.
::util::Status GetSpecFromConfigOrInline(const ::std::vector<string> &argv,
                                         int inline_config_position,
                                         ContainerSpec *spec);

//  Returns if val is in range [min, max).
//  e.g. 2 is in range [2,6)
//       6 is NOT in range [2,6)
//       5 is in range [2,6)
//       range [2,2) is empty
//       range [3, 2) is empty
template <typename X, typename Y, typename Z>
bool in_range(X val, Y min, Z max) {
//  IMPLEMENTER NOTE: This template accepts three (possibly) different types
//  because otherwise comparing e.g. vector size (vector::size_type) and two
//  integers requires casting otherwise. It may be beneficial to add some type
//  checks to avoid narrowing conversions during comparisons but complexity
//  seemed too high given this is meant as drop-in replacement for handwritten
//  checks.
//  NOTE2: See http://www.cs.utexas.edu/~EWD/transcriptions/EWD08xx/EWD831.html
//  for why interval should be right half-open.
  return (min <= val) && (val < max);
}

}  // namespace cli
}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_CLI_COMMANDS_UTIL_H_
