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

  // Ignore existing keys.
  if (GetValueByKey(sanitized_key) != "") {
    DCHECK(false) << "key already exists: " << sanitized_key;
    return *this;
  }

  pairs_.push_back(make_pair(sanitized_key, value));
  return *this;
}

// Add a bool value.
OutputMap &OutputMap::AddBool(const string &key, bool value) {
  return Add(key, value ? "yes" : "no");
}

// Get the value for a key.
const string &OutputMap::GetValueByKey(const string &key) const {
  CHECK_NE(key, "");
  for (size_t i = 0; i < pairs_.size(); i++) {
    if (pairs_[i].first == key) {
      return pairs_[i].second;
    }
  }
  // Don't return reference-to-temporary!
  static LazyStaticPtr<string> empty_string;
  return *empty_string;
}

// Print all the pairs in this set.
void OutputMap::Print(FILE *out, Style style) const {
  switch (style) {
    case STYLE_PAIRS:
      PrintPairs(out);
      break;
    case STYLE_VALUES:
      PrintValues(out);
      break;
    case STYLE_LONG:
      PrintLong(out);
      break;
    default:
      LOG(ERROR) << "unknown OutputMap style: " << style;
      break;
  }
}

void OutputMap::PrintPairs(FILE *out) const {
  for (size_t i = 0; i < pairs_.size(); i++) {
    if (i > 0) {
      fprintf(out, " ");
    }
    fprintf(out, "%s=\"%s\"",
            pairs_[i].first.c_str(),
            strings::CEscape(pairs_[i].second).c_str());
  }
  fprintf(out, "\n");
}

void OutputMap::PrintValues(FILE *out) const {
  for (size_t i = 0; i < pairs_.size(); i++) {
    if (i > 0) {
      fprintf(out, " | ");
    }
    fprintf(out, "%s", pairs_[i].second.c_str());
  }
  fprintf(out, "\n");
}

void OutputMap::PrintLong(FILE *out) const {
  for (size_t i = 0; i < pairs_.size(); i++) {
    fprintf(out, "%-20s | %s\n",
            pairs_[i].first.c_str(), pairs_[i].second.c_str());
  }
  fprintf(out, "\n");
}

}  // namespace cli
}  // namespace lmctfy
}  // namespace containers
