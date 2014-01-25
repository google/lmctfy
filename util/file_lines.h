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

// This file defines two constructs:
// 1 - FileLines: Convenience class for iterating over the lines of a file.
// 2 - TypedFileLines: Extensible class that allows the creation of custom
//     iterators over line-based data in files.
//
//
// 1 - FileLines:
//
// Iterate over the lines in a file. Each line is represented by a StringPiece.
// Reading is done one line at a time. Each line has a default (and
// customizable) limit of 4KB.
//
// Example foreach usage:
//
// for (const auto &line : FileLines("/proc/mounts")) {
//   LOG(INFO) << "Mount: " << line;
// }
//
// Also works with regular iterators:
//
// FileLines lines("/proc/mounts");
// for (auto &it = lines.begin(); it != lines.end(); ++it) {
//   LOG(INFO) << "Mount: " << line;
// }
//
// If the 4KB buffer is not to your liking, this file also defined a
// StringFileLines<buffer_size_in_bytes> you can use the same as FileLines.
//
//
// 2 - TypedFileLines:
//
// Through this class we can create a typed iterator over line-based data in an
// underlying file.
//
// For example, assume a file with lines in the format:
//
// <name> <e-mail>
//
// We can create a typed iterator over these elements by the following use of
// TypedFileLines:
//
// class UserData {
//  public:
//   string name;
//   string email;
// };
//
// bool ParseUserData(const char *line, UserData *data) {
//   vector<string> elements = Split(line, " ");
//   if (elements.size() != 2) {
//     return false;
//   }
//   data->name = elements[0];
//   data->email = elements[0];
//   return true;
// }
//
// typedef TypedFileLines<UserData, ParseUserData> Users;
//
// or explicitly subclass (useful for custom construction):
//
// class Users : public TypedFileLines<UserData, ParseUserData> {
//  public:
//   explicit Users(const string &company)
//       : TypedFileLines<UserData, ParseUserData>(JoinPath(company, "users")) {
//   }
//   ~Users() {}
// };
//
// Users can now we used the same way as FileLines above.
// FileLines is implemented in terms of TypedFileLines.
//
// The parsing function takes the line that was read as well as a pointer to the
// data being output. The return of the function determines whether to use the
// parsed output, if the return is false the line is skipped when providing
// output to the user.

#ifndef UTIL_FILE_LINES_H_
#define UTIL_FILE_LINES_H_

#include <memory>
#include <string>
#include <vector>

#include "base/integral_types.h"
#include "base/logging.h"
#include "base/macros.h"
#include "strings/stringpiece.h"
#include "system_api/libc_fs_api.h"

namespace util {

// Iterator of the lines of a given file. A copy of an iterator instance
// advances the original iterator. For a new iterator, call begin() again. These
// iterators do NOT outlive the factory that creates them.
//
// Default buffer size is 4KB.
//
// Class is thread-hostile. All copied instances of the iterator share the same
// file pointer and any of those advance the file pointer for all copies.
template <typename DataType,
          bool (*ParseFunction)(const char *, DataType *),
          size_t buffer_size = size_t(4 << 10)>
class TypedFileLinesIterator {
 public:
  // Initializes the iterator to the first line in the file at filepath, or to
  // the the past-the-end iterator if cfile is nullptr. Does not take ownership
  // of cfile.
  explicit TypedFileLinesIterator(FILE *cfile) : cfile_(cfile) {
    if (cfile != nullptr) {
      line_.reset(new char[buffer_size]);

      // Get first line.
      ReadNextLine();
    }
  }

  // Copy constructor, required to support proper standard iterator semantics.
  TypedFileLinesIterator(const TypedFileLinesIterator &other)
      : data_(other.data_), cfile_(other.cfile_) {
    // Only allocate the line buffer if it is not an end() iterator.
    if (cfile_ != nullptr) {
      line_.reset(new char[buffer_size]);
    }
  }

  virtual ~TypedFileLinesIterator() {}

  // Pre-increment operator overload, required to support proper standard
  // iterator semantics. Advances the iterator forward.
  //
  // Precondition: TypedFileLinesIterator is not equal to end().
  // Postcondition: If end of the file is reached, *this == end() otherwise
  //     *(*this) is the current line.
  TypedFileLinesIterator &operator++() {
    ReadNextLine();

    return *this;
  }

  // Post-increment operator overload, required to support proper standard
  // iterator semantics. Advances the iterator forward.
  //
  // Precondition: TypedFileLinesIterator is not equal to end().
  // Postcondition: If end of the file is reached, *this == end() otherwise
  //     *(*this) is the current line.
  TypedFileLinesIterator operator++(int unused) {
    TypedFileLinesIterator tmp = *this;
    ++(*this);
    return tmp;
  }

  // Return a reference to the current line.
  //
  // Requires: *this != end()
  const DataType &operator*() const {
    return data_;
  }

  // Return a pointer to the current line.
  //
  // Requires: *this != end()
  const DataType *operator->() const {
    return &data_;
  }

  // Equality comparison.
  // We only really want to differentiate between wildly different pointers as
  // well as past-the-end pointers.
  bool Equals(const TypedFileLinesIterator &other) const {
    return cfile_ == other.cfile_;
  }

 private:
  // Reads one line from the file.
  void ReadNextLine() {
    CHECK(cfile_ != nullptr) << "Can't increment the past-the-end iterator";

    // Try to parse lines until one is deemed usable by the parse function or we
    // reach the end of file.
    do {
      if (::system_api::GlobalLibcFsApi()->FGetS(line_.get(), buffer_size,
                                                 cfile_) == nullptr) {
        // No more lines, make into end() iterator
        cfile_ = nullptr;
        line_.reset();
        return;
      }

      // Parse the DataType from the line.
    } while (!ParseFunction(line_.get(), &data_) && cfile_ != nullptr);
  }

  // The underlying data from the parsed file line.
  DataType data_;

  // Pointer to the FILE being read. This is shared amongs all iterators copied
  // from the original iterator returned by begin().
  FILE *cfile_;

  // Buffer for the current line being read.
  ::std::unique_ptr<char[]> line_;
};

template <typename DataType,
          bool (*ParseFunction)(const char *, DataType *),
          size_t buffer_size>
bool operator==(
    const TypedFileLinesIterator<DataType, ParseFunction, buffer_size> &left,
    const TypedFileLinesIterator<DataType, ParseFunction, buffer_size> &right) {
  return left.Equals(right);
}

template <typename DataType,
          bool (*ParseFunction)(const char *, DataType *),
          size_t buffer_size>
bool operator!=(
    const TypedFileLinesIterator<DataType, ParseFunction, buffer_size> &left,
    const TypedFileLinesIterator<DataType, ParseFunction, buffer_size> &right) {
  return !left.Equals(right);
}

// Iterates over the lines of a file.
//
// begin() will open the file. It will be closed when TypedFileLines is
// destroyed. Each call to begin() gets their own opened file. We can do better
// if there is a usecase, we have not found one.
//
// Class is thread-compatible.
template <typename DataType,
          bool (*ParseFunction)(const char *, DataType *),
          size_t buffer_size = size_t(4 << 10)>
class TypedFileLines {
 public:
  typedef TypedFileLinesIterator<DataType, ParseFunction, buffer_size> iterator;
  typedef const TypedFileLinesIterator<DataType, ParseFunction, buffer_size>
      const_iterator;

  // TODO(vmarmol): Remove when we fix the open source StatusOr.
  // No-op default constructor.
  TypedFileLines() {}

  // Creates an object that will iterate over the lines in the file at the
  // specified file_path.
  explicit TypedFileLines(const ::std::string& file_path)
      : file_path_(file_path) {}
  virtual ~TypedFileLines() {
    // Close all the files.
    for (FILE *file : owned_files_) {
      ::system_api::GlobalLibcFsApi()->FClose(file);
    }
  }

  // Copy and assign OK.

  // Gets an iterator to the first element of the underlying data. Returned
  // iterator gets its own opened file which is closed when TypedFileLines is
  // destroyed.
  virtual iterator begin() const {
    FILE *cfile =
        ::system_api::GlobalLibcFsApi()->FOpen(file_path_.c_str(), "r");
    if (cfile == nullptr) {
      LOG(WARNING) << "Failed to open \"" << file_path_ << "\" for reading";
    } else {
      owned_files_.push_back(cfile);
    }

    return iterator(cfile);
  }
  virtual const_iterator cbegin() const {
    return begin();
  }

  // Gets the past-the-end iterator of the underlying data.
  virtual iterator end() const {
    return iterator(nullptr);
  }
  virtual const_iterator cend() const {
    return end();
  }

 private:
  // Path to the file being iterated over.
  const ::std::string file_path_;

  // TODO(vmarmol): Consider using emplace_back() with unique_ptrs when that is
  // available.
  // Files opened and owned by this factory.
  mutable ::std::vector<FILE *> owned_files_;
};

namespace file_lines_internal {

bool FileLinesParseToStringPiece(const char *parsed_line, StringPiece *data);

}  // namespace file_lines_internal

template <size_t buffer_size>
using StringFileLines = TypedFileLines<
    StringPiece, file_lines_internal::FileLinesParseToStringPiece, buffer_size>;

typedef StringFileLines<4096> FileLines;

}  // namespace util

#endif  // UTIL_FILE_LINES_H_
