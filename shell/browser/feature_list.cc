// Copyright (c) 2019 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "electron/shell/browser/feature_list.h"

#include <string>

#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/feature_list.h"
#include "content/public/common/content_features.h"
#include "electron/buildflags/buildflags.h"
#include "media/base/media_switches.h"

namespace electron {

void InitializeFeatureList() {
  auto* cmd_line = base::CommandLine::ForCurrentProcess();
  auto enable_features =
      cmd_line->GetSwitchValueASCII(::switches::kEnableFeatures);
  auto disable_features =
      cmd_line->GetSwitchValueASCII(::switches::kDisableFeatures);
  // Disable creation of spare renderer process with site-per-process mode,
  // it interferes with our process preference tracking for non sandboxed mode.
  // Can be reenabled when our site instance policy is aligned with chromium
  // when node integration is enabled.
  disable_features +=
      std::string(",") + features::kSpareRendererForSitePerProcess.name;

  // https://www.polymer-project.org/blog/2018-10-02-webcomponents-v0-deprecations
  // https://chromium-review.googlesource.com/c/chromium/src/+/1869562
  // Any website which uses older WebComponents will fail in without this
  // enabled, since Electron does not support origin trials.
  enable_features += std::string(",") + "WebComponentsV0Enabled" +
                     std::string(",") + "AudioWorkletRealtimeThread";

#if !BUILDFLAG(ENABLE_PICTURE_IN_PICTURE)
  disable_features += std::string(",") + media::kPictureInPicture.name;
#endif
  base::FeatureList::InitializeInstance(enable_features, disable_features);
}

}  // namespace electron
