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

// Support for output of key-value pairs.

#include "lmctfy/cli/output_map.h"

#include <stdio.h>
#include <memory>
#include <string>
using ::std::string;

#include "base/logging.h"
#include "strings/escaping.h"
#include "util/gtl/lazy_static_ptr.h"
#include "re2/re2.h"

using ::std::make_pair;
using ::util::gtl::LazyStaticPtr;

// All keys must match this regex.
static const char kKeyStartCharRegex[] = "A-Za-z0-9";
static const char kKeyCharRegex[] = "-A-Za-z0-9_.";
// kRawKey can't be set by user and signifies that the value is raw.
static const char kRawKey[] = ".raw";

namespace containers {
namespace lmctfy {
namespace cli {

// Shortcut ctor to add 1 pair.
OutputMap::OutputMap(const string &key, const string &value) {
  Add(key, value);
}

// How many pairs in this set?
size_t OutputMap::NumPairs() const {
  return pairs_.size();
}

// Get a key.
const string &OutputMap::GetKey(size_t index) const {
  CHECK_LT(index, pairs_.size());
  return pairs_[index].first;
}

// Get a value.
const string &OutputMap::GetValue(size_t index) const {
  CHECK_LT(index, pairs_.size());
  return pairs_[index].second;
}

static const string MakeRegexSet(const string &chars) {
  return "[" + chars + "]";
}

// Add a string value.
OutputMap &OutputMap::Add(const string &key, const string &value) {
  string sanitized_key(key);
  if (RE2::GlobalReplace(&sanitized_key,
                         MakeRegexSet("^" + string(kKeyCharRegex)), "_") > 0) {
    LOG(WARNING) << "invalid key characters replaced: " << key;
  }

  CHECK(RE2::FullMatch(sanitized_key, MakeRegexSet(kKeyStartCharRegex) +
                        MakeRegexSet(kKeyCharRegex) + "*"))
      << "invalid key name: " << sanitized_key;

  pairs_.push_back(make_pair(sanitized_key, value));
  return *this;
}

// Add a bool value.
OutputMap &OutputMap::AddBool(const string &key, bool value) {
  return Add(key, value ? "yes" : "no");
}

OutputMap &OutputMap::AddRaw(const string &value) {
  pairs_.push_back(make_pair(kRawKey, value));
  return *this;
}

bool OutputMap::ContainsPair(const string &key, const string &value) const {
  for (const auto &pair : pairs_) {
    if (pair.first == key && pair.second == value) {
      return true;
    }
  }
  return false;
}

// Print all the pairs in this set.
void OutputMap::Print(FILE *out, Style style) const {
  switch (style) {
    case STYLE_PAIRS:
      PrintAll(out, PrintPair);
      break;
    case STYLE_VALUES:
      PrintAll(out, PrintValue);
      break;
    case STYLE_LONG:
      PrintAll(out, PrintLong);
      break;
    default:
      LOG(ERROR) << "unknown OutputMap style: " << style;
      break;
  }
}

void OutputMap::PrintAll(
    FILE *out,
    ::std::function<void(FILE *, PairVector::const_reference)> printer) const {
  for (const auto &pair : pairs_) {
    if (pair.first == kRawKey) {
      // Raw values are not affected by styles.
      fputs(pair.second.c_str(), out);
    } else {
      printer(out, pair);
    }
  }
}

void OutputMap::PrintPair(FILE *out, PairVector::const_reference pair) {
  fprintf(out, "%s=\"%s\"\n", pair.first.c_str(),
          strings::CEscape(pair.second).c_str());
}

void OutputMap::PrintValue(FILE *out, PairVector::const_reference pair) {
  fprintf(out, "%s\n", pair.second.c_str());
}

void OutputMap::PrintLong(FILE *out, PairVector::const_reference pair) {
  fprintf(out, "%-20s | %s\n\n", pair.first.c_str(), pair.second.c_str());
}

}  // namespace cli
}  // namespace lmctfy
}  // namespace containers
