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

#include "strings/stringpiece.h"

#include <string.h>
#include <algorithm>
using std::copy;
using std::max;
using std::min;
using std::reverse;
using std::sort;
using std::swap;
#include <climits>
#include <string>
using std::string;

#include "base/hash.h"
#include "base/logging.h"
#include "strings/memutil.h"
#include "util/gtl/stl_util.h"

#include <ext/hash_set>
namespace __gnu_cxx {

size_t hash<StringPiece>::operator()(StringPiece s) const {
  return HashTo32(s.data(), s.size());
}

}  // namespace __gnu_cxx


std::ostream& operator<<(std::ostream& o, StringPiece piece) {
  o.write(piece.data(), piece.size());
  return o;
}

// Out-of-line error path.
void StringPiece::LogFatalSizeTooBig(size_t size, const char* details) {
  LOG(FATAL) << "size too big: " << size << " details: " << details;
}

StringPiece::StringPiece(StringPiece x, stringpiece_ssize_type pos)
    : ptr_(x.ptr_ + pos), length_(x.length_ - pos) {
  DCHECK_LE(0, pos);
  DCHECK_LE(pos, x.length_);
}

StringPiece::StringPiece(StringPiece x,
                         stringpiece_ssize_type pos,
                         stringpiece_ssize_type len)
    : ptr_(x.ptr_ + pos), length_(std::min(len, x.length_ - pos)) {
  DCHECK_LE(0, pos);
  DCHECK_LE(pos, x.length_);
  DCHECK_GE(len, 0);
}

void StringPiece::CopyToString(string* target) const {
  STLAssignToString(target, ptr_, length_);
}

void StringPiece::AppendToString(string* target) const {
  STLAppendToString(target, ptr_, length_);
}

stringpiece_ssize_type StringPiece::copy(char* buf,
                                         size_type n,
                                         size_type pos) const {
  stringpiece_ssize_type ret = std::min(length_ - pos, n);
  memcpy(buf, ptr_ + pos, ret);
  return ret;
}

bool StringPiece::contains(StringPiece s) const {
  return find(s, 0) != npos;
}

stringpiece_ssize_type StringPiece::find(StringPiece s, size_type pos) const {
  if (length_ <= 0 || pos > static_cast<size_type>(length_)) {
    if (length_ == 0 && pos == 0 && s.length_ == 0) return 0;
    return npos;
  }
  const char *result = memmatch(ptr_ + pos, length_ - pos,
                                s.ptr_, s.length_);
  return result ? result - ptr_ : npos;
}

stringpiece_ssize_type StringPiece::find(char c, size_type pos) const {
  if (length_ <= 0 || pos >= static_cast<size_type>(length_)) {
    return npos;
  }
  const char* result = static_cast<const char*>(
      memchr(ptr_ + pos, c, length_ - pos));
  return result != NULL ? result - ptr_ : npos;
}

stringpiece_ssize_type StringPiece::rfind(StringPiece s, size_type pos) const {
  if (length_ < s.length_) return npos;
  const size_t ulen = length_;
  if (s.length_ == 0) return std::min(ulen, pos);

  const char* last = ptr_ + std::min(ulen - s.length_, pos) + s.length_;
  const char* result = std::find_end(ptr_, last, s.ptr_, s.ptr_ + s.length_);
  return result != last ? result - ptr_ : npos;
}

// Search range is [0..pos] inclusive.  If pos == npos, search everything.
stringpiece_ssize_type StringPiece::rfind(char c, size_type pos) const {
  // Note: memrchr() is not available on Windows.
  if (length_ <= 0) return npos;
  for (stringpiece_ssize_type i =
      std::min(pos, static_cast<size_type>(length_ - 1));
       i >= 0; --i) {
    if (ptr_[i] == c) {
      return i;
    }
  }
  return npos;
}

// For each character in characters_wanted, sets the index corresponding
// to the ASCII code of that character to 1 in table.  This is used by
// the find_.*_of methods below to tell whether or not a character is in
// the lookup table in constant time.
// The argument `table' must be an array that is large enough to hold all
// the possible values of an unsigned char.  Thus it should be be declared
// as follows:
//   bool table[UCHAR_MAX + 1]
static inline void BuildLookupTable(StringPiece characters_wanted,
                                    bool* table) {
  const stringpiece_ssize_type length = characters_wanted.length();
  const char* const data = characters_wanted.data();
  for (stringpiece_ssize_type i = 0; i < length; ++i) {
    table[static_cast<unsigned char>(data[i])] = true;
  }
}

stringpiece_ssize_type StringPiece::find_first_of(StringPiece s,
                                                  size_type pos) const {
  if (length_ <= 0 || s.length_ <= 0) {
    return npos;
  }
  // Avoid the cost of BuildLookupTable() for a single-character search.
  if (s.length_ == 1) return find_first_of(s.ptr_[0], pos);

  bool lookup[UCHAR_MAX + 1] = { false };
  BuildLookupTable(s, lookup);
  for (stringpiece_ssize_type i = pos; i < length_; ++i) {
    if (lookup[static_cast<unsigned char>(ptr_[i])]) {
      return i;
    }
  }
  return npos;
}

stringpiece_ssize_type StringPiece::find_first_not_of(StringPiece s,
                                                      size_type pos) const {
  if (length_ <= 0) return npos;
  if (s.length_ <= 0) return 0;
  // Avoid the cost of BuildLookupTable() for a single-character search.
  if (s.length_ == 1) return find_first_not_of(s.ptr_[0], pos);

  bool lookup[UCHAR_MAX + 1] = { false };
  BuildLookupTable(s, lookup);
  for (stringpiece_ssize_type i = pos; i < length_; ++i) {
    if (!lookup[static_cast<unsigned char>(ptr_[i])]) {
      return i;
    }
  }
  return npos;
}

stringpiece_ssize_type StringPiece::find_first_not_of(char c,
                                                      size_type pos) const {
  if (length_ <= 0) return npos;

  for (; pos < static_cast<size_type>(length_); ++pos) {
    if (ptr_[pos] != c) {
      return pos;
    }
  }
  return npos;
}

stringpiece_ssize_type StringPiece::find_last_of(StringPiece s,
                                                 size_type pos) const {
  if (length_ <= 0 || s.length_ <= 0) return npos;
  // Avoid the cost of BuildLookupTable() for a single-character search.
  if (s.length_ == 1) return find_last_of(s.ptr_[0], pos);

  bool lookup[UCHAR_MAX + 1] = { false };
  BuildLookupTable(s, lookup);
  for (stringpiece_ssize_type i =
       std::min(pos, static_cast<size_type>(length_ - 1)); i >= 0; --i) {
    if (lookup[static_cast<unsigned char>(ptr_[i])]) {
      return i;
    }
  }
  return npos;
}

stringpiece_ssize_type StringPiece::find_last_not_of(StringPiece s,
                                                     size_type pos) const {
  if (length_ <= 0) return npos;

  stringpiece_ssize_type i = std::min(pos, static_cast<size_type>(length_ - 1));
  if (s.length_ <= 0) return i;

  // Avoid the cost of BuildLookupTable() for a single-character search.
  if (s.length_ == 1) return find_last_not_of(s.ptr_[0], pos);

  bool lookup[UCHAR_MAX + 1] = { false };
  BuildLookupTable(s, lookup);
  for (; i >= 0; --i) {
    if (!lookup[static_cast<unsigned char>(ptr_[i])]) {
      return i;
    }
  }
  return npos;
}

stringpiece_ssize_type StringPiece::find_last_not_of(char c,
                                                     size_type pos) const {
  if (length_ <= 0) return npos;

  for (stringpiece_ssize_type i =
       std::min(pos, static_cast<size_type>(length_ - 1)); i >= 0; --i) {
    if (ptr_[i] != c) {
      return i;
    }
  }
  return npos;
}

StringPiece StringPiece::substr(size_type pos, size_type n) const {
  if (pos > length_) pos = length_;
  if (n > length_ - pos) n = length_ - pos;
  return StringPiece(ptr_ + pos, n);
}

const StringPiece::size_type StringPiece::npos = size_type(-1);

