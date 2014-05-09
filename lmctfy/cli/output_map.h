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

// Support for output of key-value pairs.  This is the fundamental 'result'
// structure throughout lmctfy.
//
// The Rules:
//   1) A key can be any token matching the regex: [A-Za-z0-9][-A-Za-z0-9_.]*
//   2) A value can be any string. Values are C-style escaped when printed.

#ifndef SRC_CLI_OUTPUT_MAP_H_
#define SRC_CLI_OUTPUT_MAP_H_

#include <stddef.h>
#include <stdio.h>
#include <functional>
#include <string>
using ::std::string;
#include <utility>
#include <vector>

namespace containers {
namespace lmctfy {
namespace cli {


// A OutputMap is a set of keyed values.
//
// Class is thread-compatible.
class OutputMap {
 public:
  // TODO(vmarmol): Make into enum class when that is available.
  // This enum determines how the Print() method will format the output.
  enum Style {
    STYLE_PAIRS,
    STYLE_VALUES,
    STYLE_LONG,
  };

  // Default constructor.
  OutputMap() {}

  // Shortcut constructor to add one pair.
  OutputMap(const string &key, const string &value);

  // Default copy and assign is okay.

  // Gets the number of pairs in the set.
  size_t NumPairs() const;

  // Gets the key at an index.
  const string &GetKey(size_t index) const;

  // Gets the value at an index.
  const string &GetValue(size_t index) const;

  // Adds a key and value. Duplicate keys are checked in debug mode and ignored
  // in optimized mode. Returns a reference to self so that callers can chain
  // calls to Add.
  // For example:
  //   output_map.Add("k1", "v1").Add("k2", "v2").Add("k3", "v3");
  OutputMap &Add(const string &key, const string &value);

  // Adds a key and bool value. Duplicate keys are checked in debug mode and
  // ignored in optimized mode. Returns a reference to self so that callers can
  // chain calls to AddBool.
  // For example:
  //   output_map.AddBool("k1", "v1").AddBool("k2", "v2").AddBool("k3", "v3");
  OutputMap &AddBool(const string &key, bool value);

  // Adds a raw value. It is always printed directly as passed.
  OutputMap &AddRaw(const string &value);

  // Returns if OutputMap contains |key|, |value| pair.
  bool ContainsPair(const string &key, const string &value) const;

  // Prints all the pairs in this set in the order they were added.
  void Print(FILE *out, Style style) const;

 private:
  typedef ::std::vector< ::std::pair<string, string> > PairVector;
  PairVector pairs_;

  // Prints the data in specific styles.
  void PrintAll(
      FILE *out,
      ::std::function<void(FILE *, PairVector::const_reference)> printer) const;
  static void PrintPair(FILE *out, PairVector::const_reference pair);
  static void PrintValue(FILE *out, PairVector::const_reference pair);
  static void PrintLong(FILE *out, PairVector::const_reference pair);
};

}  // namespace cli
}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_CLI_OUTPUT_MAP_H_
