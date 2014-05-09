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

#ifndef UTIL_RESSET_H_
#define UTIL_RESSET_H_

#include <string>
#include <set>

using ::std::string;
using ::std::set;

namespace util {

// A helper class for dealing with sets of values (tasks, CPUs, memory
// nodes) in kernel interfaces.
class ResSet : public set<int> {
 public:
  ResSet & operator= (const set<int>& ints);
  // Helper functions for updating cpuset parameters
  bool AppendTaskSet(const string& path);
  void ReadTaskSet(const string& path);
  // Same as ReadTaskSet but ignores errors.
  void ReadTaskSetQuiet(const string& path);
  // Read a sequence of ranges (separated by 'sep' from a file)
  void ReadSet(const string& path, const char *sep);
  // ReadSet() but from a string instead of a file
  void ReadSetString(const string& buf, const char *sep);

  // Format a ResSet as comma-separated ranges, putting the result in "str"
  void Format(string *str) const;
};

// Typedef for explicitly indicating that a ResSet is being used to
// track threads.
typedef ResSet ThreadSet;

}  // namespace util

#endif  // UTIL_RESSET_H_
