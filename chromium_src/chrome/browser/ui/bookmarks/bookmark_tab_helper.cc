/* Copyright (c) 2019 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "brave/common/pref_names.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"

namespace {
bool ShouldShowBookmarkBarOnNTP(Profile* profile) {
  PrefService* prefs = profile->GetPrefs();
    return prefs->GetBoolean(kAlwaysShowBookmarkBarOnNTP);
}
}  // namespace

#define ReturnFalseIfBookmarkBarShouldHide(profile) \
    if (!ShouldShowBookmarkBarOnNTP(profile))  \
      return false;

#include "../../../../../../chrome/browser/ui/bookmarks/bookmark_tab_helper.cc"  // NOLINT
