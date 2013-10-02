// Copyright 2004 Google Inc.
// All rights reserved.
// Author: Rob Pike
//
// This code is adapted from Plan 9's cleanname() routine.  Hence we also
// include the following:
//
// Copyright (C) 2003, Lucent Technologies Inc. and others. All Rights Reserved.

#include "file/base/cleanpath.h"

#include <stdio.h>

using ::std::string;

// Wrapped version of Plan 9 code to canonicalize a file name.

// SEP was a macro in the original; we use an inline function here
// but leave the name alone.
static inline bool SEP(int x) {
  return x == '/' || x == '\0';
}

const string Plan9_CleanPath(const string& pathname) {
  // Allocate C string to modify in place.
  // The code can grow name by one character so allocate a one extra character
  int max_size = pathname.size() + 2;
  char *name = new char[max_size];
  snprintf(name, max_size, "%s", pathname.c_str());

  // In place, rewrite name to compress multiple /, eliminate ., and process ..
  char *p, *q, *dotdot;
  int rooted;

  rooted = name[0] == '/';

  /*
   * invariants:
   *  p points at beginning of path element we're considering.
   *  q points just past the last path element we wrote (no slash).
   *  dotdot points just past the point where .. cannot backtrack
   *    any further (no slash).
   */
  p = q = dotdot = name+rooted;
  while (*p) {
    if (p[0] == '/')  /* null element */
      p++;
    else if (p[0] == '.' && SEP(p[1]))
      p += 1;  /* don't count the separator in case it is nul */
    else if (p[0] == '.' && p[1] == '.' && SEP(p[2])) {
      p += 2;
      if (q > dotdot) {  /* can backtrack */
        while (--q > dotdot && *q != '/') { }
      } else if (!rooted) {  /* /.. is / but ./../ is .. */
        if (q != name)
          *q++ = '/';
        *q++ = '.';
        *q++ = '.';
        dotdot = q;
      }
    } else {  /* real path element */
      if (q != name+rooted)
        *q++ = '/';
      while ((*q = *p) != '/' && *q != 0)
        p++, q++;
    }
  }
  if (q == name)  /* empty string is really ``.'' */
    *q++ = '.';
  *q = '\0';

  string s(name);
  delete [] name;
  return s;
}
