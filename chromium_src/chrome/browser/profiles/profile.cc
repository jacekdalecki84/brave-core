/* Copyright (c) 2019 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "../../../../../../chrome/browser/profiles/profile.cc"  // NOLINT

#include "brave/chromium_src/chrome/browser/profiles/profile.h"

#include "brave/browser/tor/buildflags.h"
#include "brave/common/tor/tor_constants.h"

bool Profile::IsTorProfile() const {
#if BUILDFLAG(ENABLE_TOR)
  return GetPath().BaseName() == base::FilePath(tor::kTorProfileDir);
#else
  return false;
#endif
}

bool IsGuestProfile(Profile* profile) {
  DCHECK(profile);
  return (profile->HasOffTheRecordProfile() &&
          profile->GetOffTheRecordProfile() == profile &&
          profile->GetOriginalProfile()->IsGuestSession());
}
