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

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "util/resset.h"

#include "base/casts.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "base/strtoint.h"
#include "strings/split.h"
#include "strings/strip.h"
#include "re2/re2.h"

using ::std::vector;

namespace util {

// Useful functions/types for operating on sets of integers, as used
// for memory and cpu assignments and process listings.

// Read a sequence of ranges (separated by 'sep' from a file)
void ResSet::ReadSet(const string& path, const char *sep) {
  char buf[4096];
  int fd;
  size_t bytes;
  // Open the file and read the data into a list of ranges
  CHECK((fd = open(path.c_str(), O_RDONLY)) != -1) << "Couldn't open " << path;
  CHECK((bytes = read(fd, buf, sizeof(buf) - 1)) != -1);
  buf[bytes] = 0;
  close(fd);
  ReadSetString(buf, sep);
}

void ResSet::ReadSetString(const string& buf, const char *sep) {
  clear();
  vector<string> list =
      strings::Split(buf, strings::delimiter::AnyOf(sep), strings::SkipEmpty());
  for (int i = 0; i < list.size(); i++) {
    int first, last;
    string field = list[i];
    string laststr;
    if (field == "\n") continue;
    // Match against either a single integer, or a range
    CHECK(RE2::FullMatch(field, "(\\d+)(?:-(\\d+))?\\n?", &first, &laststr))
        << " Invalid field '" << field << "', length " << field.size()
        << " char " << static_cast<int>(field[0]);
    if (laststr.empty()) {
      last = first;
    } else {
      last = atoi32(laststr.c_str());
    }

    // Fill in the range (or single value, if first==last)
    while (first <= last) {
      insert(first++);
    }
  }
}
bool ResSet::AppendTaskSet(const string& path) {
  FILE *f = fopen(path.c_str(), "r");
  if (!f)
    return false;
  int pid;
  while (fscanf(f, "%d\n", &pid) == 1) {
    insert(pid);
  }
  fclose(f);
  return true;
}

void ResSet::ReadTaskSet(const string& path) {
  clear();
  CHECK(AppendTaskSet(path)) << "Couldn't open " << path;
}

void ResSet::ReadTaskSetQuiet(const string& path) {
  clear();
  AppendTaskSet(path);
}

ResSet& ResSet::operator= (const set<int>& ints) {
  set<int>::operator= (ints);
  return *this;
}

void ResSet::Format(string *str) const {
  const char *sep = "";

  int last = -2, first = -2;
  for (ResSet::const_iterator it = begin(); it != end(); ++it) {
    int node = *it;
    if (node != last + 1) {
      if (last >= 0) {
        if (first == last) {
          StringAppendF(str, "%s%d", sep, first);
        } else {
          StringAppendF(str, "%s%d-%d", sep, first, last);
        }
        sep = ",";
      }
      first = last = node;
    } else {
      last = node;
    }
  }
  if (last >= 0) {
    if (first == last) {
      StringAppendF(str, "%s%d", sep, first);
    } else {
      StringAppendF(str, "%s%d-%d", sep, first, last);
    }
  }
  StripTrailingWhitespace(str);
}

}  // namespace util
