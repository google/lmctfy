// Copyright 2011 Google Inc.  All rights reserved.
// Author: mec@google.com (Michael Chastain)
//
// This code is adapted from Plan 9's cleanname() routine.  Hence we also
// include the following:
//
// Copyright (C) 2003, Lucent Technologies Inc. and others. All Rights
// Reserved.

#ifndef FILE_BASE_CLEANPATH_H__
#define FILE_BASE_CLEANPATH_H__

#include <string>

// Rewrite input string to output string.
//   Compress multiple /
//   Eliminate .
//   Process ..
const ::std::string Plan9_CleanPath(const ::std::string&);

#endif  // FILE_BASE_CLEANPATH_H__
