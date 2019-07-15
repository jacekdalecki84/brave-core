// Copyright (c) 2019 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at http://mozilla.org/MPL/2.0/.

#include "brave/common/tor/tor_constants.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/prefs/pref_service_syncable_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"

#define CreatePrefService CreatePrefService_ChromiumImpl
#include "../../../../../../chrome/browser/profiles/pref_service_builder_utils.cc"
#undef CreatePrefService

std::unique_ptr<sync_preferences::PrefServiceSyncable> CreatePrefService(
    scoped_refptr<user_prefs::PrefRegistrySyncable> pref_registry,
    PrefStore* extension_pref_store,
    policy::PolicyService* policy_service,
    policy::ChromeBrowserPolicyConnector* browser_policy_connector,
    prefs::mojom::TrackedPreferenceValidationDelegatePtr
        pref_validation_delegate,
    scoped_refptr<base::SequencedTaskRunner> io_task_runner,
    SimpleFactoryKey* key,
    const base::FilePath& path,
    bool async_prefs) {
  if (path.BaseName() == base::FilePath(tor::kTorProfileDir)) { // TODO: Add an util function IsTorProfilePath
    ProfileManager* profile_manager = g_browser_process->profile_manager();
    base::FilePath original_profile_path = path.DirName().DirName();
    Profile* original_profile =
      profile_manager->GetProfileByPath(original_profile_path);
    return CreateIncognitoPrefServiceSyncable(
        PrefServiceSyncableFromProfile(original_profile),
        Profile::CreateExtensionPrefStore(original_profile, true),
        InProcessPrefServiceFactoryFactory::GetInstanceForKey(key)
            ->CreateDelegate());
  }
  return CreatePrefService_ChromiumImpl(
    pref_registry,
    extension_pref_store,
    policy_service,
    browser_policy_connector,
    std::move(pref_validation_delegate),
    io_task_runner,
    key,
    path,
    async_prefs);
}
