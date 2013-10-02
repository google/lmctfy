// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
// http://code.google.com/p/protobuf/
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// from google3/strings/strutil.cc

#include "strings/strutil.h"

#include <assert.h>
#include <errno.h>
#include <float.h>    // FLT_DIG and DBL_DIG
#include <limits>
#include <limits.h>
#include <stdio.h>
#include <iterator>
#include <memory>

#include "base/logging.h"

#ifdef _WIN32
// MSVC has only _snprintf, not snprintf.
//
// MinGW has both snprintf and _snprintf, but they appear to be different
// functions.  The former is buggy.  When invoked like so:
//   char buffer[32];
//   snprintf(buffer, 32, "%.*g\n", FLT_DIG, 1.23e10f);
// it prints "1.23000e+10".  This is plainly wrong:  %g should never print
// trailing zeros after the decimal point.  For some reason this bug only
// occurs with some input values, not all.  In any case, _snprintf does the
// right thing, so we use it.
#define snprintf _snprintf
#endif

inline bool IsNaN(double value) {
  // NaN is never equal to anything, even itself.
  return value != value;
}

// These are defined as macros on some platforms.  #undef them so that we can
// redefine them.
#undef isxdigit
#undef isprint

// The definitions of these in ctype.h change based on locale.  Since our
// string manipulation is all in relation to the protocol buffer and C++
// languages, we always want to use the C locale.  So, we re-define these
// exactly as we want them.
inline bool isxdigit(char c) {
  return ('0' <= c && c <= '9') ||
         ('a' <= c && c <= 'f') ||
         ('A' <= c && c <= 'F');
}

inline bool isprint(char c) {
  return c >= 0x20 && c <= 0x7E;
}

// ----------------------------------------------------------------------
// StripString
//    Replaces any occurrence of the character 'remove' (or the characters
//    in 'remove') with the character 'replacewith'.
// ----------------------------------------------------------------------
void StripString(string* s, const char* remove, char replacewith) {
  const char * str_start = s->c_str();
  const char * str = str_start;
  for (str = strpbrk(str, remove);
       str != NULL;
       str = strpbrk(str + 1, remove)) {
    (*s)[str - str_start] = replacewith;
  }
}

// ----------------------------------------------------------------------
// StringReplace()
//    Replace the "old" pattern with the "new" pattern in a string,
//    and append the result to "res".  If replace_all is false,
//    it only replaces the first instance of "old."
// ----------------------------------------------------------------------

void StringReplace(const string& s, const string& oldsub,
                   const string& newsub, bool replace_all,
                   string* res) {
  if (oldsub.empty()) {
    res->append(s);  // if empty, append the given string.
    return;
  }

  string::size_type start_pos = 0;
  string::size_type pos;
  do {
    pos = s.find(oldsub, start_pos);
    if (pos == string::npos) {
      break;
    }
    res->append(s, start_pos, pos - start_pos);
    res->append(newsub);
    start_pos = pos + oldsub.size();  // start searching again after the "old"
  } while (replace_all);
  res->append(s, start_pos, s.length() - start_pos);
}

// ----------------------------------------------------------------------
// StringReplace()
//    Give me a string and two patterns "old" and "new", and I replace
//    the first instance of "old" in the string with "new", if it
//    exists.  If "global" is true; call this repeatedly until it
//    fails.  RETURN a new string, regardless of whether the replacement
//    happened or not.
// ----------------------------------------------------------------------

string StringReplace(const string& s, const string& oldsub,
                     const string& newsub, bool replace_all) {
  string ret;
  StringReplace(s, oldsub, newsub, replace_all, &ret);
  return ret;
}

// ----------------------------------------------------------------------
// SplitStringUsing()
//    Split a string using a character delimiter. Append the components
//    to 'result'.
//
// Note: For multi-character delimiters, this routine will split on *ANY* of
// the characters in the string, not the entire string as a single delimiter.
// ----------------------------------------------------------------------
template <typename ITR>
static inline
void SplitStringToIteratorUsing(const string& full,
                                const char* delim,
                                ITR& result) {
  // Optimize the common case where delim is a single character.
  if (delim[0] != '\0' && delim[1] == '\0') {
    char c = delim[0];
    const char* p = full.data();
    const char* end = p + full.size();
    while (p != end) {
      if (*p == c) {
        ++p;
      } else {
        const char* start = p;
        while (++p != end && *p != c);
        *result++ = string(start, p - start);
      }
    }
    return;
  }

  string::size_type begin_index, end_index;
  begin_index = full.find_first_not_of(delim);
  while (begin_index != string::npos) {
    end_index = full.find_first_of(delim, begin_index);
    if (end_index == string::npos) {
      *result++ = full.substr(begin_index);
      return;
    }
    *result++ = full.substr(begin_index, (end_index - begin_index));
    begin_index = full.find_first_not_of(delim, end_index);
  }
}

void SplitStringUsing(const string& full,
                      const char* delim,
                      vector<string>* result) {
  ::std::back_insert_iterator< vector<string> > it(*result);
  SplitStringToIteratorUsing(full, delim, it);
}

// Split a string using a character delimiter. Append the components
// to 'result'.  If there are consecutive delimiters, this function
// will return corresponding empty strings. The string is split into
// at most the specified number of pieces greedily. This means that the
// last piece may possibly be split further. To split into as many pieces
// as possible, specify 0 as the number of pieces.
//
// If "full" is the empty string, yields an empty string as the only value.
//
// If "pieces" is negative for some reason, it returns the whole string
// ----------------------------------------------------------------------
template <typename StringType, typename ITR>
static inline
void SplitStringToIteratorAllowEmpty(const StringType& full,
                                     const char* delim,
                                     int pieces,
                                     ITR& result) {
  string::size_type begin_index, end_index;
  begin_index = 0;

  for (int i = 0; (i < pieces-1) || (pieces == 0); i++) {
    end_index = full.find_first_of(delim, begin_index);
    if (end_index == string::npos) {
      *result++ = full.substr(begin_index);
      return;
    }
    *result++ = full.substr(begin_index, (end_index - begin_index));
    begin_index = end_index + 1;
  }
  *result++ = full.substr(begin_index);
}

void SplitStringAllowEmpty(const string& full, const char* delim,
                           vector<string>* result) {
  ::std::back_insert_iterator<vector<string> > it(*result);
  SplitStringToIteratorAllowEmpty(full, delim, 0, it);
}

// ----------------------------------------------------------------------
// JoinStrings()
//    This merges a vector of string components with delim inserted
//    as separaters between components.
//
// ----------------------------------------------------------------------
template <class ITERATOR>
static void JoinStringsIterator(const ITERATOR& start,
                                const ITERATOR& end,
                                const char* delim,
                                string* result) {
  CHECK(result != NULL);
  result->clear();
  int delim_length = strlen(delim);

  // Precompute resulting length so we can reserve() memory in one shot.
  int length = 0;
  for (ITERATOR iter = start; iter != end; ++iter) {
    if (iter != start) {
      length += delim_length;
    }
    length += iter->size();
  }
  result->reserve(length);

  // Now combine everything.
  for (ITERATOR iter = start; iter != end; ++iter) {
    if (iter != start) {
      result->append(delim, delim_length);
    }
    result->append(iter->data(), iter->size());
  }
}

void JoinStrings(const vector<string>& components,
                 const char* delim,
                 string * result) {
  JoinStringsIterator(components.begin(), components.end(), delim, result);
}

// ----------------------------------------------------------------------
// UnescapeCEscapeSequences()
//    This does all the unescaping that C does: \ooo, \r, \n, etc
//    Returns length of resulting string.
//    The implementation of \x parses any positive number of hex digits,
//    but it is an error if the value requires more than 8 bits, and the
//    result is truncated to 8 bits.
//
//    The second call stores its errors in a supplied string vector.
//    If the string vector pointer is NULL, it reports the errors with LOG().
// ----------------------------------------------------------------------

#define IS_OCTAL_DIGIT(c) (((c) >= '0') && ((c) <= '7'))

inline int hex_digit_to_int(char c) {
  /* Assume ASCII. */
  assert('0' == 0x30 && 'A' == 0x41 && 'a' == 0x61);
  assert(isxdigit(c));
  int x = static_cast<unsigned char>(c);
  if (x > '9') {
    x += 9;
  }
  return x & 0xf;
}

// Protocol buffers doesn't ever care about errors, but I don't want to remove
// the code.
#define LOG_STRING(LEVEL, VECTOR) LOG_IF(LEVEL, false)

int UnescapeCEscapeSequences(const char* source, char* dest) {
  return UnescapeCEscapeSequences(source, dest, NULL);
}

int UnescapeCEscapeSequences(const char* source, char* dest,
                             vector<string> *errors) {
  DCHECK(errors == NULL) << "Error reporting not implemented.";

  char* d = dest;
  const char* p = source;

  // Small optimization for case where source = dest and there's no escaping
  while ( p == d && *p != '\0' && *p != '\\' )
    p++, d++;

  while (*p != '\0') {
    if (*p != '\\') {
      *d++ = *p++;
    } else {
      switch ( *++p ) {                    // skip past the '\\'
        case '\0':
          LOG_STRING(ERROR, errors) << "String cannot end with \\";
          *d = '\0';
          return d - dest;   // we're done with p
        case 'a':  *d++ = '\a';  break;
        case 'b':  *d++ = '\b';  break;
        case 'f':  *d++ = '\f';  break;
        case 'n':  *d++ = '\n';  break;
        case 'r':  *d++ = '\r';  break;
        case 't':  *d++ = '\t';  break;
        case 'v':  *d++ = '\v';  break;
        case '\\': *d++ = '\\';  break;
        case '?':  *d++ = '\?';  break;    // \?  Who knew?
        case '\'': *d++ = '\'';  break;
        case '"':  *d++ = '\"';  break;
        case '0': case '1': case '2': case '3':  // octal digit: 1 to 3 digits
        case '4': case '5': case '6': case '7': {
          char ch = *p - '0';
          if ( IS_OCTAL_DIGIT(p[1]) )
            ch = ch * 8 + *++p - '0';
          if ( IS_OCTAL_DIGIT(p[1]) )      // safe (and easy) to do this twice
            ch = ch * 8 + *++p - '0';      // now points at last digit
          *d++ = ch;
          break;
        }
        case 'x': case 'X': {
          if (!isxdigit(p[1])) {
            if (p[1] == '\0') {
              LOG_STRING(ERROR, errors) << "String cannot end with \\x";
            } else {
              LOG_STRING(ERROR, errors) <<
                "\\x cannot be followed by non-hex digit: \\" << *p << p[1];
            }
            break;
          }
          unsigned int ch = 0;
          const char *hex_start = p;
          while (isxdigit(p[1]))  // arbitrarily many hex digits
            ch = (ch << 4) + hex_digit_to_int(*++p);
          if (ch > 0xFF)
            LOG_STRING(ERROR, errors) << "Value of " <<
              "\\" << string(hex_start, p+1-hex_start) << " exceeds 8 bits";
          *d++ = ch;
          break;
        }
#if 0  // TODO(kenton):  Support \u and \U?  Requires runetochar().
        case 'u': {
          // \uhhhh => convert 4 hex digits to UTF-8
          char32 rune = 0;
          const char *hex_start = p;
          for (int i = 0; i < 4; ++i) {
            if (isxdigit(p[1])) {  // Look one char ahead.
              rune = (rune << 4) + hex_digit_to_int(*++p);  // Advance p.
            } else {
              LOG_STRING(ERROR, errors)
                << "\\u must be followed by 4 hex digits: \\"
                <<  string(hex_start, p+1-hex_start);
              break;
            }
          }
          d += runetochar(d, &rune);
          break;
        }
        case 'U': {
          // \Uhhhhhhhh => convert 8 hex digits to UTF-8
          char32 rune = 0;
          const char *hex_start = p;
          for (int i = 0; i < 8; ++i) {
            if (isxdigit(p[1])) {  // Look one char ahead.
              // Don't change rune until we're sure this
              // is within the Unicode limit, but do advance p.
              char32 newrune = (rune << 4) + hex_digit_to_int(*++p);
              if (newrune > 0x10FFFF) {
                LOG_STRING(ERROR, errors)
                  << "Value of \\"
                  << string(hex_start, p + 1 - hex_start)
                  << " exceeds Unicode limit (0x10FFFF)";
                break;
              } else {
                rune = newrune;
              }
            } else {
              LOG_STRING(ERROR, errors)
                << "\\U must be followed by 8 hex digits: \\"
                <<  string(hex_start, p+1-hex_start);
              break;
            }
          }
          d += runetochar(d, &rune);
          break;
        }
#endif
        default:
          LOG_STRING(ERROR, errors) << "Unknown escape sequence: \\" << *p;
      }
      p++;                                 // read past letter we escaped
    }
  }
  *d = '\0';
  return d - dest;
}

// ----------------------------------------------------------------------
// UnescapeCEscapeString()
//    This does the same thing as UnescapeCEscapeSequences, but creates
//    a new string. The caller does not need to worry about allocating
//    a dest buffer. This should be used for non performance critical
//    tasks such as printing debug messages. It is safe for src and dest
//    to be the same.
//
//    The second call stores its errors in a supplied string vector.
//    If the string vector pointer is NULL, it reports the errors with LOG().
//
//    In the first and second calls, the length of dest is returned. In the
//    the third call, the new string is returned.
// ----------------------------------------------------------------------
int UnescapeCEscapeString(const string& src, string* dest) {
  return UnescapeCEscapeString(src, dest, NULL);
}

int UnescapeCEscapeString(const string& src, string* dest,
                          vector<string> *errors) {
  ::std::unique_ptr<char[]> unescaped(new char[src.size() + 1]);
  int len = UnescapeCEscapeSequences(src.c_str(), unescaped.get(), errors);
  CHECK(dest);
  dest->assign(unescaped.get(), len);
  return len;
}

string UnescapeCEscapeString(const string& src) {
  ::std::unique_ptr<char[]> unescaped(new char[src.size() + 1]);
  int len = UnescapeCEscapeSequences(src.c_str(), unescaped.get(), NULL);
  return string(unescaped.get(), len);
}

// ----------------------------------------------------------------------
// CEscapeString()
// CHexEscapeString()
//    Copies 'src' to 'dest', escaping dangerous characters using
//    C-style escape sequences. This is very useful for preparing query
//    flags. 'src' and 'dest' should not overlap. The 'Hex' version uses
//    hexadecimal rather than octal sequences.
//    Returns the number of bytes written to 'dest' (not including the \0)
//    or -1 if there was insufficient space.
//
//    Currently only \n, \r, \t, ", ', \ and !isprint() chars are escaped.
// ----------------------------------------------------------------------
int CEscapeInternal(const char* src, int src_len, char* dest,
                    int dest_len, bool use_hex, bool utf8_safe) {
  const char* src_end = src + src_len;
  int used = 0;
  bool last_hex_escape = false; // true if last output char was \xNN

  for (; src < src_end; src++) {
    if (dest_len - used < 2)   // Need space for two letter escape
      return -1;

    bool is_hex_escape = false;
    switch (*src) {
      case '\n': dest[used++] = '\\'; dest[used++] = 'n';  break;
      case '\r': dest[used++] = '\\'; dest[used++] = 'r';  break;
      case '\t': dest[used++] = '\\'; dest[used++] = 't';  break;
      case '\"': dest[used++] = '\\'; dest[used++] = '\"'; break;
      case '\'': dest[used++] = '\\'; dest[used++] = '\''; break;
      case '\\': dest[used++] = '\\'; dest[used++] = '\\'; break;
      default:
        // Note that if we emit \xNN and the src character after that is a hex
        // digit then that digit must be escaped too to prevent it being
        // interpreted as part of the character code by C.
        if ((!utf8_safe || static_cast<uint8>(*src) < 0x80) &&
            (!isprint(*src) ||
             (last_hex_escape && isxdigit(*src)))) {
          if (dest_len - used < 4) // need space for 4 letter escape
            return -1;
          sprintf(dest + used, (use_hex ? "\\x%02x" : "\\%03o"),
                  static_cast<uint8>(*src));
          is_hex_escape = use_hex;
          used += 4;
        } else {
          dest[used++] = *src; break;
        }
    }
    last_hex_escape = is_hex_escape;
  }

  if (dest_len - used < 1)   // make sure that there is room for \0
    return -1;

  dest[used] = '\0';   // doesn't count towards return value though
  return used;
}

int CEscapeString(const char* src, int src_len, char* dest, int dest_len) {
  return CEscapeInternal(src, src_len, dest, dest_len, false, false);
}

// ----------------------------------------------------------------------
// CEscape()
// CHexEscape()
//    Copies 'src' to result, escaping dangerous characters using
//    C-style escape sequences. This is very useful for preparing query
//    flags. 'src' and 'dest' should not overlap. The 'Hex' version
//    hexadecimal rather than octal sequences.
//
//    Currently only \n, \r, \t, ", ', \ and !isprint() chars are escaped.
// ----------------------------------------------------------------------
string CEscape(const string& src) {
  const int dest_length = src.size() * 4 + 1; // Maximum possible expansion
  ::std::unique_ptr<char[]> dest(new char[dest_length]);
  const int len = CEscapeInternal(src.data(), src.size(),
                                  dest.get(), dest_length, false, false);
  DCHECK_GE(len, 0);
  return string(dest.get(), len);
}

namespace strings {

string Utf8SafeCEscape(const string& src) {
  const int dest_length = src.size() * 4 + 1; // Maximum possible expansion
  ::std::unique_ptr<char[]> dest(new char[dest_length]);
  const int len = CEscapeInternal(src.data(), src.size(),
                                  dest.get(), dest_length, false, true);
  DCHECK_GE(len, 0);
  return string(dest.get(), len);
}

string CHexEscape(const string& src) {
  const int dest_length = src.size() * 4 + 1; // Maximum possible expansion
  ::std::unique_ptr<char[]> dest(new char[dest_length]);
  const int len = CEscapeInternal(src.data(), src.size(),
                                  dest.get(), dest_length, true, false);
  DCHECK_GE(len, 0);
  return string(dest.get(), len);
}

}  // namespace strings

// ----------------------------------------------------------------------
// NoLocaleStrtod()
//   This code will make you cry.
// ----------------------------------------------------------------------

// Returns a string identical to *input except that the character pointed to
// by radix_pos (which should be '.') is replaced with the locale-specific
// radix character.
string LocalizeRadix(const char* input, const char* radix_pos) {
  // Determine the locale-specific radix character by calling sprintf() to
  // print the number 1.5, then stripping off the digits.  As far as I can
  // tell, this is the only portable, thread-safe way to get the C library
  // to divuldge the locale's radix character.  No, localeconv() is NOT
  // thread-safe.
  char temp[16];
  int size = sprintf(temp, "%.1f", 1.5);
  CHECK_EQ(temp[0], '1');
  CHECK_EQ(temp[size-1], '5');
  CHECK_LE(size, 6);

  // Now replace the '.' in the input with it.
  string result;
  result.reserve(strlen(input) + size - 3);
  result.append(input, radix_pos);
  result.append(temp + 1, size - 2);
  result.append(radix_pos + 1);
  return result;
}

double NoLocaleStrtod(const char* text, char** original_endptr) {
  // We cannot simply set the locale to "C" temporarily with setlocale()
  // as this is not thread-safe.  Instead, we try to parse in the current
  // locale first.  If parsing stops at a '.' character, then this is a
  // pretty good hint that we're actually in some other locale in which
  // '.' is not the radix character.

  char* temp_endptr;
  double result = strtod(text, &temp_endptr);
  if (original_endptr != NULL) *original_endptr = temp_endptr;
  if (*temp_endptr != '.') return result;

  // Parsing halted on a '.'.  Perhaps we're in a different locale?  Let's
  // try to replace the '.' with a locale-specific radix character and
  // try again.
  string localized = LocalizeRadix(text, temp_endptr);
  const char* localized_cstr = localized.c_str();
  char* localized_endptr;
  result = strtod(localized_cstr, &localized_endptr);
  if ((localized_endptr - localized_cstr) >
      (temp_endptr - text)) {
    // This attempt got further, so replacing the decimal must have helped.
    // Update original_endptr to point at the right location.
    if (original_endptr != NULL) {
      // size_diff is non-zero if the localized radix has multiple bytes.
      int size_diff = localized.size() - strlen(text);
      // const_cast is necessary to match the strtod() interface.
      *original_endptr = const_cast<char*>(
        text + (localized_endptr - localized_cstr - size_diff));
    }
  }

  return result;
}
