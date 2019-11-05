// Copyright (c) 2019 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/atom_paths.h"

#if defined(USE_X11)
#include "base/environment.h"
#include "base/nix/xdg_util.h"
#endif

#include "base/files/file_path.h"
#include "base/path_service.h"
#include "shell/common/application_info.h"

namespace {

#if defined(USE_X11)
base::FilePath getXdgConfigPath() {
  static base::FilePath path;
  if (path.empty()) {
    auto env = base::Environment::Create();
    path = base::nix::GetXDGDirectory(
        env.get(), base::nix::kXdgConfigHomeEnvVar, base::nix::kDotConfigDir);
  }
  return path;
}
#endif

bool getDirWithChild(int key,
                     base::FilePath* result,
                     const base::StringPiece child) {
  base::FilePath tmp;
  if (!base::PathService::Get(key, &tmp))
    return false;
  *result = tmp.Append(base::FilePath::FromUTF8Unsafe(child));
  return true;
}

}  // unnamed namespace

namespace electron {

bool AtomPathProvider(int key, base::FilePath* result) {
  switch (key) {
    case DIR_APP_DATA:
#if !defined(USE_X11)
      return base::PathService::Get(base::DIR_APP_DATA, result);
#else
      *result = getXdgConfigPath();
      return true;
#endif

    case DIR_CACHE: {
#if defined(OS_POSIX)
      return base::PathService::Get(base::DIR_CACHE, result);
#else
      return base::PathService::Get(base::DIR_APP_DATA, result);
#endif
      break;
    }

    case DIR_USER_DATA:
      return getDirWithChild(DIR_APP_DATA, result, GetApplicationName());

    case DIR_USER_CACHE:
      return getDirWithChild(DIR_CACHE, result, GetApplicationName());

    case DIR_APP_LOGS:
      return getDirWithChild(DIR_USER_DATA, result, "logs");

    default:
      return false;
  }
}

void RegisterAtomPathProvider() {
  base::PathService::RegisterProvider(AtomPathProvider, PATH_START, PATH_END);
}

}  // namespace electron
