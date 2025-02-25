/* Copyright (c) 2019 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_BRAVE_REWARDS_BROWSER_REWARDS_SERVICE_IMPL_H_
#define BRAVE_COMPONENTS_BRAVE_REWARDS_BROWSER_REWARDS_SERVICE_IMPL_H_

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/flat_set.h"
#include "bat/ledger/ledger.h"
#include "bat/ledger/wallet_properties.h"
#include "base/files/file_path.h"
#include "base/observer_list.h"
#include "base/one_shot_event.h"
#include "base/memory/weak_ptr.h"
#include "bat/ledger/ledger_client.h"
#include "brave/components/services/bat_ledger/public/interfaces/bat_ledger.mojom.h"
#include "brave/components/brave_rewards/browser/rewards_service.h"
#include "chrome/browser/bitmap_fetcher/bitmap_fetcher_service.h"
#include "content/public/browser/browser_thread.h"
#include "extensions/buildflags/buildflags.h"
#include "mojo/public/cpp/bindings/associated_binding.h"
#include "brave/components/brave_rewards/browser/balance_report.h"
#include "brave/components/brave_rewards/browser/content_site.h"
#include "brave/components/brave_rewards/browser/contribution_info.h"
#include "ui/gfx/image/image.h"
#include "brave/components/brave_rewards/browser/publisher_banner.h"
#include "brave/components/brave_rewards/browser/rewards_service_private_observer.h"

#if BUILDFLAG(ENABLE_EXTENSIONS)
#include "brave/components/brave_rewards/browser/extension_rewards_service_observer.h"
#endif

namespace base {
class OneShotTimer;
class RepeatingTimer;
class SequencedTaskRunner;
}  // namespace base

namespace ledger {
class Ledger;
class LedgerCallbackHandler;
struct LedgerMediaPublisherInfo;
}  // namespace ledger

namespace leveldb {
class DB;
}  // namespace leveldb

namespace network {
class SimpleURLLoader;
}  // namespace network


class Profile;
class BraveRewardsBrowserTest;

namespace brave_rewards {

class PublisherInfoDatabase;
class RewardsNotificationServiceImpl;
class BraveRewardsBrowserTest;

using GetProductionCallback = base::Callback<void(bool)>;
using GetDebugCallback = base::Callback<void(bool)>;
using GetReconcileTimeCallback = base::Callback<void(int32_t)>;
using GetShortRetriesCallback = base::Callback<void(bool)>;
using GetTestResponseCallback =
    base::Callback<void(const std::string& url,
                        int* response_status_code,
                        std::string* response,
                        std::map<std::string, std::string>* headers)>;

class RewardsServiceImpl : public RewardsService,
                           public ledger::LedgerClient,
                           public base::SupportsWeakPtr<RewardsServiceImpl> {
 public:
  explicit RewardsServiceImpl(Profile* profile);
  ~RewardsServiceImpl() override;

  // KeyedService:
  void Shutdown() override;

  void Init();
  void StartLedger();
  void CreateWallet() override;
  void FetchWalletProperties() override;
  void FetchGrants(const std::string& lang,
                   const std::string& paymentId) override;
  void GetGrantCaptcha(
      const std::string& promotion_id,
      const std::string& promotion_type) override;
  void SolveGrantCaptcha(const std::string& solution,
                         const std::string& promotionId) const override;
  void GetWalletPassphrase(
      const GetWalletPassphraseCallback& callback) override;
  void RecoverWallet(const std::string& passPhrase) const override;
  void GetContentSiteList(
      uint32_t start,
      uint32_t limit,
      uint64_t min_visit_time,
      uint64_t reconcile_stamp,
      bool allow_non_verified,
      uint32_t min_visits,
      bool fetch_excluded,
      const GetContentSiteListCallback& callback) override;
  void OnGetContentSiteList(
      const GetContentSiteListCallback& callback,
      ledger::PublisherInfoList list,
      uint32_t next_record);
  void OnLoad(SessionID tab_id, const GURL& url) override;
  void OnUnload(SessionID tab_id) override;
  void OnShow(SessionID tab_id) override;
  void OnHide(SessionID tab_id) override;
  void OnForeground(SessionID tab_id) override;
  void OnBackground(SessionID tab_id) override;
  void OnXHRLoad(SessionID tab_id,
                 const GURL& url,
                 const GURL& first_party_url,
                 const GURL& referrer) override;
  void OnPostData(SessionID tab_id,
                  const GURL& url,
                  const GURL& first_party_url,
                  const GURL& referrer,
                  const std::string& post_data) override;
  std::string URIEncode(const std::string& value) override;
  void GetReconcileStamp(const GetReconcileStampCallback& callback) override;
  void GetAddresses(const GetAddressesCallback& callback) override;
  void GetAutoContribute(
      GetAutoContributeCallback callback) override;
  void GetPublisherMinVisitTime(
      const GetPublisherMinVisitTimeCallback& callback) override;
  void GetPublisherMinVisits(
      const GetPublisherMinVisitsCallback& callback) override;
  void GetPublisherAllowNonVerified(
      const GetPublisherAllowNonVerifiedCallback& callback) override;
  void GetPublisherAllowVideos(
      const GetPublisherAllowVideosCallback& callback) override;
  void LoadPublisherInfo(
      const std::string& publisher_key,
      ledger::PublisherInfoCallback callback) override;
  void LoadMediaPublisherInfo(
      const std::string& media_key,
      ledger::PublisherInfoCallback callback) override;
  void SaveMediaPublisherInfo(const std::string& media_key,
                              const std::string& publisher_id) override;
  void RestorePublishers() override;
  void GetAllBalanceReports(
      const GetAllBalanceReportsCallback& callback) override;
  void GetCurrentBalanceReport() override;
  void IsWalletCreated(const IsWalletCreatedCallback& callback) override;
  void GetPublisherActivityFromUrl(
      uint64_t window_id,
      const std::string& url,
      const std::string& favicon_url,
      const std::string& publisher_blob) override;
  void GetContributionAmount(
      const GetContributionAmountCallback& callback) override;
  void GetPublisherBanner(const std::string& publisher_id,
                          GetPublisherBannerCallback callback) override;
  void OnPublisherBanner(GetPublisherBannerCallback callback,
                         ledger::PublisherBannerPtr banner);
  void RemoveRecurringTip(const std::string& publisher_key) override;
  void OnGetRecurringTipsUI(
      GetRecurringTipsCallback callback,
      ledger::PublisherInfoList list);
  void GetRecurringTipsUI(GetRecurringTipsCallback callback) override;
  void GetOneTimeTips(
      ledger::PublisherInfoListCallback callback) override;
  void SetContributionAutoInclude(
      const std::string& publisher_key,
      bool exclude) override;
  RewardsNotificationService* GetNotificationService() const override;
  bool CheckImported() override;
  void SetBackupCompleted() override;
  void GetRewardsInternalsInfo(
      GetRewardsInternalsInfoCallback callback) override;

  void HandleFlags(const std::string& options);
  void SetProduction(bool production);
  void GetProduction(const GetProductionCallback& callback);
  void SetDebug(bool debug);
  void GetDebug(const GetDebugCallback& callback);
  void SetReconcileTime(int32_t time);
  void GetReconcileTime(const GetReconcileTimeCallback& callback);
  void SetShortRetries(bool short_retries);
  void GetShortRetries(const GetShortRetriesCallback& callback);
  void SetCurrentCountry(const std::string& current_country);

  void GetAutoContributeProps(
      const GetAutoContributePropsCallback& callback) override;
  void GetPendingContributionsTotalUI(
      const GetPendingContributionsTotalCallback& callback) override;
  void GetRewardsMainEnabled(
      const GetRewardsMainEnabledCallback& callback) const override;

  void GetAddressesForPaymentId(const GetAddressesCallback& callback) override;

  void GetOneTimeTipsUI(GetOneTimeTipsCallback callback) override;
  void RefreshPublisher(
      const std::string& publisher_key,
      RefreshPublisherCallback callback) override;

  void SaveRecurringTip(const std::string& publisher_key,
                             const int amount) override;

  const RewardsNotificationService::RewardsNotificationsMap&
  GetAllNotifications() override;

  void SetContributionAmount(const double amount) const override;

  void SaveTwitterPublisherInfo(
      const std::map<std::string, std::string>& args,
      SaveMediaInfoCallback callback) override;

  void SaveRedditPublisherInfo(
      const std::map<std::string, std::string>& args,
      SaveMediaInfoCallback callback) override;

  void SetInlineTipSetting(const std::string& key, bool enabled) override;

  void GetInlineTipSetting(
      const std::string& key,
      GetInlineTipSettingCallback callback) override;

  void GetShareURL(
      const std::string& type,
      const std::map<std::string, std::string>& args,
      GetShareURLCallback callback) override;

  void GetPendingContributionsUI(
    GetPendingContributionsCallback callback) override;

  void RemovePendingContributionUI(const std::string& publisher_key,
                                 const std::string& viewing_id,
                                 uint64_t added_date) override;

  void RemoveAllPendingContributionsUI() override;

  void OnTip(const std::string& publisher_key,
             int amount,
             bool recurring) override;

  void SetPublisherMinVisitTime(uint64_t duration_in_seconds) const override;

  void FetchBalance(FetchBalanceCallback callback) override;

  // Testing methods
  void SetLedgerEnvForTesting();
  void StartMonthlyContributionForTest();
  void CheckInsufficientFundsForTesting();
  void MaybeShowNotificationAddFundsForTesting(
      base::OnceCallback<void(bool)> callback);

 private:
  friend class ::BraveRewardsBrowserTest;
  FRIEND_TEST_ALL_PREFIXES(RewardsServiceTest, OnWalletProperties);

  const base::OneShotEvent& ready() const { return ready_; }
  void OnLedgerStateSaved(ledger::LedgerCallbackHandler* handler,
                          bool success);
  void OnLedgerStateLoaded(ledger::OnLoadCallback callback,
                              const std::string& data);
  void LoadNicewareList(ledger::GetNicewareListCallback callback) override;
  void OnPublisherStateSaved(ledger::LedgerCallbackHandler* handler,
                             bool success);
  void OnPublisherStateLoaded(ledger::OnLoadCallback callback,
                              const std::string& data);
  void TriggerOnWalletInitialized(ledger::Result result);
  void OnFetchWalletProperties(int result,
                               ledger::WalletPropertiesPtr properties);
  void TriggerOnGrant(ledger::Result result, ledger::GrantPtr grant);
  void TriggerOnGrantCaptcha(const std::string& image, const std::string& hint);
  void TriggerOnRecoverWallet(ledger::Result result,
                              double balance,
                              std::vector<ledger::GrantPtr> grants);
  void TriggerOnGrantFinish(ledger::Result result, ledger::GrantPtr grant);
  void TriggerOnRewardsMainEnabled(bool rewards_main_enabled);
  void OnPublisherInfoSaved(ledger::PublisherInfoCallback callback,
                            ledger::PublisherInfoPtr info,
                            bool success);
  void OnActivityInfoSaved(ledger::PublisherInfoCallback callback,
                            ledger::PublisherInfoPtr info,
                            bool success);
  void OnActivityInfoLoaded(ledger::PublisherInfoCallback callback,
                            const std::string& publisher_key,
                            ledger::PublisherInfoList list);
  void OnMediaPublisherInfoSaved(bool success);
  void OnPublisherInfoLoaded(ledger::PublisherInfoCallback callback,
                             ledger::PublisherInfoPtr info);
  void OnMediaPublisherInfoLoaded(ledger::PublisherInfoCallback callback,
                             ledger::PublisherInfoPtr info);
  void OnPublisherInfoListLoaded(uint32_t start,
                                 uint32_t limit,
                                 ledger::PublisherInfoListCallback callback,
                                 ledger::PublisherInfoList list);
  void OnPublishersListSaved(ledger::LedgerCallbackHandler* handler,
                             bool success);
  void OnTimer(uint32_t timer_id);
  void OnPublisherListLoaded(ledger::LedgerCallbackHandler* handler,
                             const std::string& data);
  void OnSavedState(ledger::OnSaveCallback callback, bool success);
  void OnLoadedState(ledger::OnLoadCallback callback,
                                  const std::string& value);
  void OnResetState(ledger::OnResetCallback callback,
                                 bool success);
  void OnTipPublisherInfoSaved(ledger::Result result,
                                   ledger::PublisherInfoPtr info);
  void OnTip(const std::string& publisher_key,
             int amount,
             bool recurring,
             ledger::PublisherInfoPtr publisher_info);
  void OnContributionInfoSaved(const ledger::REWARDS_CATEGORY category,
                               bool success);
  void OnRecurringTipSaved(bool success);
  void OnGetRecurringTips(
      const ledger::PublisherInfoListCallback callback,
      ledger::PublisherInfoList list);
  void OnGetOneTimeTips(ledger::PublisherInfoListCallback callback,
                        ledger::PublisherInfoList list);
  void OnRemovedRecurringTip(ledger::RecurringRemoveCallback callback,
                          bool success);
  void OnRemoveRecurring(const std::string& publisher_key,
                         ledger::RecurringRemoveCallback callback) override;
  void TriggerOnGetCurrentBalanceReport(
      const ledger::BalanceReportInfo& report);
  void MaybeShowBackupNotification(uint64_t boot_stamp);
  void MaybeShowAddFundsNotification(uint64_t reconcile_stamp);
  void OnPublisherListNormalizedSaved(ContentSiteList site_list,
                                      bool success);
  void OnWalletProperties(
      ledger::Result result,
      ledger::WalletPropertiesPtr properties) override;
  void OnTip(const std::string& publisher_key, int amount, bool recurring,
      std::unique_ptr<brave_rewards::ContentSite> site) override;

  void DeleteActivityInfo(const std::string& publisher_key);

  void OnDeleteActivityInfoStamp(const std::string& publisher_key,
                                 uint64_t reconcile_stamp);

  void OnDeleteActivityInfo(const std::string& publisher_key,
                            bool result);

  void OnGetOneTimeTipsUI(GetRecurringTipsCallback callback,
                          ledger::PublisherInfoList list);

  void OnPublisherActivityInfoLoaded(ledger::PublisherInfoCallback callback,
                                     uint32_t result,
                                     ledger::PublisherInfoPtr info);

  void OnInlineTipSetting(GetInlineTipSettingCallback callback, bool enabled);

  void OnShareURL(GetShareURLCallback callback, const std::string& url);

  void OnPendingContributionRemoved(
    ledger::RemovePendingContributionCallback callback,
    bool result);

  void OnRemoveAllPendingContribution(
    ledger::RemovePendingContributionCallback callback,
    bool result);

  void OnGetPendingContributionsUI(
    GetPendingContributionsCallback callback,
    ledger::PendingContributionInfoList list);

  void OnGetPendingContributions(
    const ledger::PendingContributionInfoListCallback& callback,
    ledger::PendingContributionInfoList list);

  void OnURLLoaderComplete(network::SimpleURLLoader* loader,
                           ledger::LoadURLCallback callback,
                           std::unique_ptr<std::string> response_body);

  void StartNotificationTimers(bool main_enabled);
  void StopNotificationTimers();
  void OnNotificationTimerFired();

  void MaybeShowNotificationAddFunds();
  bool ShouldShowNotificationAddFunds() const;
  void ShowNotificationAddFunds(bool sufficient);

  void MaybeShowNotificationTipsPaid();
  void ShowNotificationTipsPaid(bool ac_enabled);

  void OnPendingContributionRemovedUI(int32_t result);

  void OnRemoveAllPendingContributionsUI(int32_t result);

  void OnGetPendingContributionsTotal(
    const ledger::PendingContributionsTotalCallback& callback,
    double amount);

  void OnFetchBalance(FetchBalanceCallback callback,
                      int32_t result,
                      ledger::BalancePtr balance);

  // ledger::LedgerClient
  std::string GenerateGUID() const override;
  void OnWalletInitialized(ledger::Result result) override;
  void OnGrant(ledger::Result result,
               ledger::GrantPtr grant) override;
  void OnGrantCaptcha(const std::string& image,
                      const std::string& hint) override;
  void OnRecoverWallet(ledger::Result result,
                      double balance,
                      std::vector<ledger::GrantPtr> grants) override;
  void OnReconcileComplete(ledger::Result result,
                           const std::string& viewing_id,
                           ledger::REWARDS_CATEGORY category,
                           const std::string& probi) override;
  void OnGrantFinish(ledger::Result result,
                     ledger::GrantPtr grant) override;
  void LoadLedgerState(ledger::OnLoadCallback callback) override;
  void LoadPublisherState(ledger::OnLoadCallback callback) override;
  void SaveLedgerState(const std::string& ledger_state,
                       ledger::LedgerCallbackHandler* handler) override;
  void SavePublisherState(const std::string& publisher_state,
                          ledger::LedgerCallbackHandler* handler) override;
  void SavePublisherInfo(ledger::PublisherInfoPtr publisher_info,
                         ledger::PublisherInfoCallback callback) override;
  void SaveActivityInfo(ledger::PublisherInfoPtr publisher_info,
                        ledger::PublisherInfoCallback callback) override;
  void LoadActivityInfo(ledger::ActivityInfoFilter filter,
                         ledger::PublisherInfoCallback callback) override;
  void LoadPanelPublisherInfo(ledger::ActivityInfoFilter filter,
                              ledger::PublisherInfoCallback callback) override;
  void GetActivityInfoList(
      uint32_t start,
      uint32_t limit,
      ledger::ActivityInfoFilter filter,
      ledger::PublisherInfoListCallback callback) override;
  void SavePublishersList(const std::string& publishers_list,
                          ledger::LedgerCallbackHandler* handler) override;
  void SetTimer(uint64_t time_offset, uint32_t* timer_id) override;
  void LoadPublisherList(ledger::LedgerCallbackHandler* handler) override;
  void LoadURL(const std::string& url,
      const std::vector<std::string>& headers,
      const std::string& content,
      const std::string& contentType,
      const ledger::URL_METHOD method,
      ledger::LoadURLCallback callback) override;
  void SetRewardsMainEnabled(bool enabled) override;
  void SetPublisherMinVisits(unsigned int visits) const override;
  void SetPublisherAllowNonVerified(bool allow) const override;
  void SetPublisherAllowVideos(bool allow) const override;
  void SetUserChangedContribution() const override;
  void SetAutoContribute(bool enabled) const override;
  void UpdateAdsRewards() const override;
  void SetCatalogIssuers(const std::string& json) override;
  void ConfirmAd(const std::string& json) override;
  void SetConfirmationsIsReady(const bool is_ready) override;
  void GetTransactionHistory(
      GetTransactionHistoryCallback callback) override;
  void ConfirmationsTransactionHistoryDidChange() override;

  void OnExcludedSitesChanged(const std::string& publisher_id,
                              ledger::PUBLISHER_EXCLUDE exclude) override;
  void OnPanelPublisherInfo(ledger::Result result,
                            ledger::PublisherInfoPtr info,
                            uint64_t window_id) override;
  void FetchFavIcon(const std::string& url,
                    const std::string& favicon_key,
                    ledger::FetchIconCallback callback) override;
  void OnFetchFavIconCompleted(ledger::FetchIconCallback callback,
                          const std::string& favicon_key,
                          const GURL& url,
                          const BitmapFetcherService::RequestId& request_id,
                          const SkBitmap& image);
  void OnSetOnDemandFaviconComplete(const std::string& favicon_url,
                                    ledger::FetchIconCallback callback,
                                    bool success);
  void SaveContributionInfo(const std::string& probi,
                            const int month,
                            const int year,
                            const uint32_t date,
                            const std::string& publisher_key,
                            const ledger::REWARDS_CATEGORY category) override;
  void GetRecurringTips(
      ledger::PublisherInfoListCallback callback) override;
  std::unique_ptr<ledger::LogStream> Log(
                     const char* file,
                     int line,
                     const ledger::LogLevel log_level) const override;

  std::unique_ptr<ledger::LogStream> VerboseLog(
                     const char* file,
                     int line,
                     int log_level) const override;
  void SaveState(const std::string& name,
                 const std::string& value,
                 ledger::OnSaveCallback callback) override;
  void LoadState(const std::string& name,
                 ledger::OnLoadCallback callback) override;
  void ResetState(const std::string& name,
                  ledger::OnResetCallback callback) override;
  void SetBooleanState(const std::string& name, bool value) override;
  bool GetBooleanState(const std::string& name) const override;
  void SetIntegerState(const std::string& name, int value) override;
  int GetIntegerState(const std::string& name) const override;
  void SetDoubleState(const std::string& name, double value) override;
  double GetDoubleState(const std::string& name) const override;
  void SetStringState(const std::string& name,
                      const std::string& value) override;
  std::string GetStringState(const std::string& name) const override;
  void SetInt64State(const std::string& name, int64_t value) override;
  int64_t GetInt64State(const std::string& name) const override;
  void SetUint64State(const std::string& name, uint64_t value) override;
  uint64_t GetUint64State(const std::string& name) const override;
  void ClearState(const std::string& name) override;


  void KillTimer(uint32_t timer_id) override;

  void OnRestorePublishers(ledger::OnRestoreCallback callback) override;
  void OnPanelPublisherInfoLoaded(
      ledger::PublisherInfoCallback callback,
      ledger::PublisherInfoPtr publisher_info);

  void SavePendingContribution(
      ledger::PendingContributionList list) override;

  void OnSavePendingContribution(ledger::Result result);

  void OnRestorePublishersInternal(ledger::OnRestoreCallback callback,
                                   bool result);

  void SaveNormalizedPublisherList(
      ledger::PublisherInfoList list) override;

  void GetPendingContributions(
    const ledger::PendingContributionInfoListCallback& callback) override;

  void RemovePendingContribution(
    const std::string& publisher_key,
    const std::string& viewing_id,
    uint64_t added_date,
    const ledger::RemovePendingContributionCallback& callback) override;

  void RemoveAllPendingContributions(
    const ledger::RemovePendingContributionCallback& callback) override;

  void GetPendingContributionsTotal(
    const ledger::PendingContributionsTotalCallback& callback) override;

  // end ledger::LedgerClient

  // Mojo Proxy methods
  void OnGetTransactionHistory(
      GetTransactionHistoryCallback callback,
      const std::string& transactions);
  void OnGetAllBalanceReports(
      const GetAllBalanceReportsCallback& callback,
      const base::flat_map<std::string, std::string>& json_reports);
  void OnGetCurrentBalanceReport(
      bool success, const std::string& json_report);
  void OnGetAddresses(
      const GetAddressesCallback& callback,
      const base::flat_map<std::string, std::string>& addresses);
  void OnGetAutoContributeProps(
      const GetAutoContributePropsCallback& callback,
      ledger::AutoContributePropsPtr props);
  void OnGetRewardsInternalsInfo(GetRewardsInternalsInfoCallback callback,
                                 const std::string& json_props);
  void SetRewardsMainEnabledPref(bool enabled);
  void SetRewardsMainEnabledMigratedPref(bool enabled);
  void OnRefreshPublisher(
      RefreshPublisherCallback callback,
      const std::string& publisher_key,
      bool verified);
  void OnTwitterPublisherInfoSaved(SaveMediaInfoCallback callback,
                                   int32_t result,
                                   ledger::PublisherInfoPtr publisher);
  void OnRedditPublisherInfoSaved(
      SaveMediaInfoCallback callback,
      int32_t result,
      ledger::PublisherInfoPtr publisher);
  void GetCountryCodes(
      const std::vector<std::string>& countries,
      ledger::GetCountryCodesCallback callback) override;

  void OnContributeUnverifiedPublishers(
      ledger::Result result,
      const std::string& publisher_key,
      const std::string& publisher_name) override;

  bool Connected() const;
  void ConnectionClosed();

  Profile* profile_;  // NOT OWNED
  mojo::AssociatedBinding<bat_ledger::mojom::BatLedgerClient>
      bat_ledger_client_binding_;
  bat_ledger::mojom::BatLedgerAssociatedPtr bat_ledger_;
  bat_ledger::mojom::BatLedgerServicePtr bat_ledger_service_;

#if BUILDFLAG(ENABLE_EXTENSIONS)
  std::unique_ptr<ExtensionRewardsServiceObserver>
      extension_rewards_service_observer_;
#endif
  const scoped_refptr<base::SequencedTaskRunner> file_task_runner_;
  const base::FilePath ledger_state_path_;
  const base::FilePath publisher_state_path_;
  const base::FilePath publisher_info_db_path_;
  const base::FilePath publisher_list_path_;
  const base::FilePath rewards_base_path_;
  std::unique_ptr<PublisherInfoDatabase> publisher_info_backend_;
  std::unique_ptr<RewardsNotificationServiceImpl> notification_service_;
  base::ObserverList<RewardsServicePrivateObserver> private_observers_;
#if BUILDFLAG(ENABLE_EXTENSIONS)
  std::unique_ptr<ExtensionRewardsServiceObserver> private_observer_;
#endif

  base::OneShotEvent ready_;
  base::flat_set<network::SimpleURLLoader*> url_loaders_;
  std::map<uint32_t, std::unique_ptr<base::OneShotTimer>> timers_;
  std::vector<std::string> current_media_fetchers_;
  std::vector<BitmapFetcherService::RequestId> request_ids_;
  std::unique_ptr<base::OneShotTimer> notification_startup_timer_;
  std::unique_ptr<base::RepeatingTimer> notification_periodic_timer_;

  uint32_t next_timer_id_;

  GetTestResponseCallback test_response_callback_;
  std::string current_country_for_test_;

  DISALLOW_COPY_AND_ASSIGN(RewardsServiceImpl);
};

}  // namespace brave_rewards

#endif  // BRAVE_COMPONENTS_BRAVE_REWARDS_BROWSER_REWARDS_SERVICE_IMPL_H_
