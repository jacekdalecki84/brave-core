// Copyright (c) 2019 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at http://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/webui/brave_new_tab_message_handler.h"

#include <string>

#include "base/bind.h"
#include "base/values.h"
#include "brave/browser/search_engines/search_engine_provider_util.h"
#include "brave/browser/ui/webui/brave_new_tab_ui.h"
#include "brave/common/pref_names.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/web_ui_data_source.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_service.h"

namespace {

bool IsPrivateNewTab(Profile* profile) {
  return profile->IsTorProfile() || profile->IsIncognitoProfile();
}

} // namespace

// static
BraveNewTabMessageHandler* BraveNewTabMessageHandler::Create(
      content::WebUIDataSource* source, Profile* profile) {
  //
  // Initial Values
  //
  // Stats
  PrefService* prefs = profile->GetPrefs();
  source->AddInteger(
    "adsBlockedStat",
    prefs->GetUint64(kAdsBlocked));
  source->AddInteger(
    "trackersBlockedStat",
    prefs->GetUint64(kTrackersBlocked));
  source->AddInteger(
    "javascriptBlockedStat",
    prefs->GetUint64(kJavascriptBlocked));
  source->AddInteger(
    "httpsUpgradesStat",
    prefs->GetUint64(kHttpsUpgrades));
  source->AddInteger(
    "fingerprintingBlockedStat",
    prefs->GetUint64(kFingerprintingBlocked));
  // Private Tab info
  if (IsPrivateNewTab(profile)) {
    source->AddBoolean(
      "useAlternativePrivateSearchEngine",
      prefs->GetBoolean(kUseAlternativeSearchEngineProvider));
    source->AddBoolean(
      "isTor", profile->IsTorProfile());
    source->AddBoolean(
      "isQwant", brave::IsRegionForQwant(profile));
  }
  // Preferences
  source->AddBoolean(
      "showBackgroundImage",
      prefs->GetBoolean(kNewTabPageShowBackgroundImage));
  source->AddBoolean(
      "showClock",
      prefs->GetBoolean(kNewTabPageShowClock));
  source->AddBoolean(
      "showTopSites",
      prefs->GetBoolean(kNewTabPageShowTopSites));
  source->AddBoolean(
      "showStats",
      prefs->GetBoolean(kNewTabPageShowStats));
  return new BraveNewTabMessageHandler(profile);
}

BraveNewTabMessageHandler::BraveNewTabMessageHandler(Profile* profile)
    : profile_(profile) {
}

BraveNewTabMessageHandler::~BraveNewTabMessageHandler() {}

void BraveNewTabMessageHandler::OnJavascriptAllowed() {
  // Observe relevant preferences
  PrefService* prefs = profile_->GetPrefs();
  pref_change_registrar_.Init(prefs);
  // Stats
  pref_change_registrar_.Add(kAdsBlocked,
    base::Bind(&BraveNewTabMessageHandler::OnStatsChanged,
    base::Unretained(this)));
  pref_change_registrar_.Add(kTrackersBlocked,
    base::Bind(&BraveNewTabMessageHandler::OnStatsChanged,
    base::Unretained(this)));
  pref_change_registrar_.Add(kHttpsUpgrades,
    base::Bind(&BraveNewTabMessageHandler::OnStatsChanged,
    base::Unretained(this)));
  if (IsPrivateNewTab(profile_)) {
    // Private New Tab Page preferences
    pref_change_registrar_.Add(kUseAlternativeSearchEngineProvider,
      base::Bind(&BraveNewTabMessageHandler::OnPrivatePropertiesChanged,
      base::Unretained(this)));
    pref_change_registrar_.Add(kAlternativeSearchEngineProviderInTor,
      base::Bind(&BraveNewTabMessageHandler::OnPrivatePropertiesChanged,
      base::Unretained(this)));
  }
  // New Tab Page preferences
  pref_change_registrar_.Add(kNewTabPageShowBackgroundImage,
    base::Bind(&BraveNewTabMessageHandler::OnPreferencesChanged,
    base::Unretained(this)));
  pref_change_registrar_.Add(kNewTabPageShowClock,
    base::Bind(&BraveNewTabMessageHandler::OnPreferencesChanged,
    base::Unretained(this)));
  pref_change_registrar_.Add(kNewTabPageShowStats,
    base::Bind(&BraveNewTabMessageHandler::OnPreferencesChanged,
    base::Unretained(this)));
  pref_change_registrar_.Add(kNewTabPageShowTopSites,
    base::Bind(&BraveNewTabMessageHandler::OnPreferencesChanged,
    base::Unretained(this)));
}

void BraveNewTabMessageHandler::OnJavascriptDisallowed() {
  pref_change_registrar_.RemoveAll();
}

void BraveNewTabMessageHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
    "newTabPageInitialized",
    base::BindRepeating(
      &BraveNewTabMessageHandler::HandleInitialized,
      base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
    "toggleAlternativePrivateSearchEngine",
    base::BindRepeating(
      &BraveNewTabMessageHandler::HandleToggleAlternativeSearchEngineProvider,
      base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
    "saveNewTabPagePref",
    base::BindRepeating(
      &BraveNewTabMessageHandler::HandleSaveNewTabPagePref,
      base::Unretained(this)));
}

void BraveNewTabMessageHandler::HandleInitialized(const base::ListValue* args) {
  AllowJavascript();
}

void BraveNewTabMessageHandler::HandleToggleAlternativeSearchEngineProvider(
    const base::ListValue* args) {
  brave::ToggleUseAlternativeSearchEngineProvider(profile_);
}

void BraveNewTabMessageHandler::HandleSaveNewTabPagePref(
    const base::ListValue* args) {
  if (args->GetSize() != 2) {
    LOG(ERROR) << "Invalid input";
    return;
  }
  PrefService* prefs = profile_->GetPrefs();
  // Collect args
  std::string settingsKeyInput = args->GetList()[0].GetString();
  auto settingsValue = args->GetList()[1].Clone();
  // Validate args
  // Note: if we introduce any non-bool settings values
  // then perform this type check within the appropriate key conditionals.
  if (!settingsValue.is_bool()) {
    LOG(ERROR) << "Invalid value type";
    return;
  }
  const auto settingsValueBool = settingsValue.GetBool();
  std::string settingsKey;
  if (settingsKeyInput == "showBackgroundImage") {
    settingsKey = kNewTabPageShowBackgroundImage;
  } else if (settingsKeyInput == "showClock") {
    settingsKey = kNewTabPageShowClock;
  } else if (settingsKeyInput == "showTopSites") {
    settingsKey = kNewTabPageShowTopSites;
  } else if (settingsKeyInput == "showStats") {
    settingsKey = kNewTabPageShowStats;
  } else {
    LOG(ERROR) << "Invalid setting key";
    return;
  }
  prefs->SetBoolean(settingsKey, settingsValueBool);
}

void BraveNewTabMessageHandler::OnPrivatePropertiesChanged() {
  PrefService* prefs = profile_->GetPrefs();
  base::DictionaryValue private_data;
  private_data.SetBoolean(
      "useAlternativePrivateSearchEngine",
      prefs->GetBoolean(kUseAlternativeSearchEngineProvider));
  FireWebUIListener("private-tab-data-updated", private_data);
}

void BraveNewTabMessageHandler::OnStatsChanged() {
  PrefService* prefs = profile_->GetPrefs();
  base::DictionaryValue stats_data;
  stats_data.SetInteger(
    "adsBlockedStat",
    prefs->GetUint64(kAdsBlocked));
  stats_data.SetInteger(
    "trackersBlockedStat",
    prefs->GetUint64(kTrackersBlocked));
  stats_data.SetInteger(
    "javascriptBlockedStat",
    prefs->GetUint64(kJavascriptBlocked));
  stats_data.SetInteger(
    "httpsUpgradesStat",
    prefs->GetUint64(kHttpsUpgrades));
  stats_data.SetInteger(
    "fingerprintingBlockedStat",
    prefs->GetUint64(kFingerprintingBlocked));
  FireWebUIListener("stats-updated", stats_data);
}

void BraveNewTabMessageHandler::OnPreferencesChanged() {
  PrefService* prefs = profile_->GetPrefs();
  base::DictionaryValue pref_data;
  pref_data.SetBoolean(
      "showBackgroundImage",
      prefs->GetBoolean(kNewTabPageShowBackgroundImage));
  pref_data.SetBoolean(
      "showClock",
      prefs->GetBoolean(kNewTabPageShowClock));
  pref_data.SetBoolean(
      "showTopSites",
      prefs->GetBoolean(kNewTabPageShowTopSites));
  pref_data.SetBoolean(
      "showStats",
      prefs->GetBoolean(kNewTabPageShowStats));
  FireWebUIListener("preferences-changed", pref_data);
}
