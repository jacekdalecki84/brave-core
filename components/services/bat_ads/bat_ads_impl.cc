/* Copyright (c) 2019 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "brave/components/services/bat_ads/bat_ads_impl.h"

#include <map>
#include <utility>
#include <vector>

#include "bat/ads/ad_content.h"
#include "bat/ads/ads.h"
#include "bat/ads/ads_history.h"
#include "bat/ads/category_content.h"
#include "brave/components/services/bat_ads/bat_ads_client_mojo_bridge.h"
#include "mojo/public/cpp/bindings/map.h"

namespace bat_ads {

namespace {

ads::NotificationResultInfoResultType ToNotificationResultInfoResultType(
    int32_t result_type) {
  return (ads::NotificationResultInfoResultType)result_type;
}

ads::AdContent::LikeAction ToAdsLikeAction(int action) {
  return static_cast<ads::AdContent::LikeAction>(action);
}

ads::CategoryContent::OptAction ToAdsOptAction(int action) {
  return static_cast<ads::CategoryContent::OptAction>(action);
}

}

BatAdsImpl::BatAdsImpl(mojom::BatAdsClientAssociatedPtrInfo client_info)
    : bat_ads_client_mojo_proxy_(
          new BatAdsClientMojoBridge(std::move(client_info))),
      ads_(ads::Ads::CreateInstance(bat_ads_client_mojo_proxy_.get())) {}

BatAdsImpl::~BatAdsImpl() {}

void BatAdsImpl::Initialize(InitializeCallback callback) {
  // TODO(Terry Mancey): Initialize needs a real callback
  ads_->Initialize();
  std::move(callback).Run();
}

void BatAdsImpl::ClassifyPage(const std::string& url,
                              const std::string& page) {
  ads_->ClassifyPage(url, page);
}

void BatAdsImpl::TabClosed(int32_t tab_id) {
  ads_->TabClosed(tab_id);
}

void BatAdsImpl::OnTimer(uint32_t timer_id) {
  ads_->OnTimer(timer_id);
}

void BatAdsImpl::OnUnIdle() {
  ads_->OnUnIdle();
}

void BatAdsImpl::OnIdle() {
  ads_->OnIdle();
}

void BatAdsImpl::OnForeground() {
  ads_->OnForeground();
}

void BatAdsImpl::OnBackground() {
  ads_->OnBackground();
}

void BatAdsImpl::OnMediaPlaying(int32_t tab_id) {
  ads_->OnMediaPlaying(tab_id);
}

void BatAdsImpl::OnMediaStopped(int32_t tab_id) {
  ads_->OnMediaStopped(tab_id);
}

void BatAdsImpl::TabUpdated(int32_t tab_id,
                const std::string& url,
                bool is_active,
                bool is_incognito) {
  ads_->TabUpdated(tab_id, url, is_active, is_incognito);
}

void BatAdsImpl::RemoveAllHistory(RemoveAllHistoryCallback callback) {
  // TODO(Terry Mancey): RemoveAllHistory needs a real callback
  ads_->RemoveAllHistory();
  std::move(callback).Run();
}

void BatAdsImpl::SetConfirmationsIsReady(const bool is_ready) {
  ads_->SetConfirmationsIsReady(is_ready);
}

void BatAdsImpl::ServeSampleAd() {
  ads_->ServeSampleAd();
}

void BatAdsImpl::GenerateAdReportingNotificationShownEvent(
      const std::string& notification_info) {
  auto info = std::make_unique<ads::NotificationInfo>();
  if (info->FromJson(notification_info) == ads::Result::SUCCESS) {
    ads_->GenerateAdReportingNotificationShownEvent(*info);
  }
}

void BatAdsImpl::GenerateAdReportingNotificationResultEvent(
      const std::string& notification_info,
      int32_t result_type) {
  auto info = std::make_unique<ads::NotificationInfo>();
  if (info->FromJson(notification_info) == ads::Result::SUCCESS) {
    ads_->GenerateAdReportingNotificationResultEvent(
        *info,
        ToNotificationResultInfoResultType(result_type));
  }
}

void BatAdsImpl::GetAdsHistory(GetAdsHistoryCallback callback) {
  std::map<std::string, std::vector<std::string>> result;

  auto ads_history_map = ads_->GetAdsHistory();
  for (const auto& entry : ads_history_map) {
    std::vector<std::string> ads_history;
    for (const auto& ads_history_entry : entry.second) {
      ads_history.push_back(ads_history_entry.ToJson());
    }
    result[entry.first] = ads_history;
  }

  std::move(callback).Run(mojo::MapToFlatMap(result));
}

void BatAdsImpl::ToggleAdThumbUp(const std::string& id,
                                 const std::string& creative_set_id,
                                 int action,
                                 ToggleAdThumbUpCallback callback) {
  int like_action =
      ads_->ToggleAdThumbUp(id, creative_set_id, ToAdsLikeAction(action));
  std::move(callback).Run(id, like_action);
}

void BatAdsImpl::ToggleAdThumbDown(const std::string& id,
                                   const std::string& creative_set_id,
                                   int action,
                                   ToggleAdThumbDownCallback callback) {
  int like_action =
      ads_->ToggleAdThumbDown(id, creative_set_id, ToAdsLikeAction(action));
  std::move(callback).Run(id, like_action);
}

void BatAdsImpl::ToggleAdOptInAction(const std::string& category,
                                     int action,
                                     ToggleAdOptInActionCallback callback) {
  int opt_action = ads_->ToggleAdOptInAction(category, ToAdsOptAction(action));
  std::move(callback).Run(category, opt_action);
}

void BatAdsImpl::ToggleAdOptOutAction(const std::string& category,
                                      int action,
                                      ToggleAdOptOutActionCallback callback) {
  int opt_action = ads_->ToggleAdOptOutAction(category, ToAdsOptAction(action));
  std::move(callback).Run(category, opt_action);
}

void BatAdsImpl::ToggleSaveAd(const std::string& id,
                              const std::string& creative_set_id,
                              bool saved,
                              ToggleSaveAdCallback callback) {
  bool saved_result = ads_->ToggleSaveAd(id, creative_set_id, saved);
  std::move(callback).Run(id, saved_result);
}

void BatAdsImpl::ToggleFlagAd(const std::string& id,
                              const std::string& creative_set_id,
                              bool flagged,
                              ToggleFlagAdCallback callback) {
  bool flagged_result = ads_->ToggleFlagAd(id, creative_set_id, flagged);
  std::move(callback).Run(id, flagged_result);
}

}  // namespace bat_ads
