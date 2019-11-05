// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHELL_BROWSER_ATOM_PATHS_H_
#define SHELL_BROWSER_ATOM_PATHS_H_

#include "base/base_paths.h"

#if defined(OS_WIN)
#include "base/base_paths_win.h"
#elif defined(OS_MACOSX)
#include "base/base_paths_mac.h"
#endif

#if defined(OS_POSIX)
#include "base/base_paths_posix.h"
#endif

namespace electron {

enum {
  PATH_START = 11000,

  /**
   * Top-level directory under which apps can write their data,
   * e.g. XDG_CONFIG_HOME, '~/Library/Application Support', %APPDATA%
   * Note 1: Apps generally should use DIR_USER_DATA intead.
   * Note 2: Not to be confused with base::DIR_APP_DATA, which is
   * similar but not available on all platforms.
   */
  DIR_APP_DATA = PATH_START,

  /**
   * Directory where apps can write their data.
   * Default: `DIR_APP_DATA/appname`
   */
  DIR_USER_DATA,

  /**
   * Top-level directory under which apps can write their cache data,
   * e.g. XDG_CACHE_HOME, NSCachesDirectory, or DIR_APP_DATA
   * Note 1: Apps generally should use DIR_USER_CACHE instead.
   * Note 2: Not to be confused with base::DIR_CACHE, which is
   * similar but not available on all platforms.
   */
  DIR_CACHE,

  /**
   * Directory where apps can write their cache data.
   * Default: `DIR_CACHE/appname`
   */
  DIR_USER_CACHE,

  /**
   * Directory where apps can write their logs.
   * Default: `DIR_USER_DATA/logs`
   */
  DIR_APP_LOGS,

  PATH_END
};

void RegisterAtomPathProvider();

}  // namespace electron

#endif  // SHELL_BROWSER_ATOM_PATHS_H_
