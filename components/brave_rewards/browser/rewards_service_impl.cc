/* Copyright (c) 2019 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "brave/components/brave_rewards/browser/rewards_service_impl.h"

#include <stdint.h>

#include <algorithm>
#include <functional>
#include <limits>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/containers/flat_map.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/guid.h"
#include "base/i18n/time_formatting.h"
#include "base/time/time.h"
#include "base/logging.h"
#include "base/sequenced_task_runner.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/post_task.h"
#include "base/task_runner_util.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "bat/ledger/ledger.h"
#include "bat/ledger/auto_contribute_props.h"
#include "bat/ledger/media_event_info.h"
#include "bat/ledger/publisher_info.h"
#include "bat/ledger/wallet_properties.h"
#include "bat/ledger/transactions_info.h"
#include "brave/browser/ui/webui/brave_rewards_source.h"
#include "brave/components/brave_rewards/common/pref_names.h"
#include "brave/common/pref_names.h"
#include "brave/components/brave_ads/browser/ads_service.h"
#include "brave/components/brave_ads/browser/ads_service_factory.h"
#include "brave/components/brave_rewards/browser/auto_contribution_props.h"
#include "brave/components/brave_rewards/browser/balance_report.h"
#include "brave/components/brave_rewards/browser/content_site.h"
#include "brave/components/brave_rewards/browser/publisher_banner.h"
#include "brave/components/brave_rewards/browser/publisher_info_database.h"
#include "brave/components/brave_rewards/browser/rewards_fetcher_service_observer.h"
#include "brave/components/brave_rewards/browser/rewards_notification_service.h"
#include "brave/components/brave_rewards/browser/rewards_notification_service_impl.h"
#include "brave/components/brave_rewards/browser/rewards_service_factory.h"
#include "brave/components/brave_rewards/browser/rewards_service_observer.h"
#include "brave/components/brave_rewards/browser/switches.h"
#include "brave/components/brave_rewards/browser/wallet_properties.h"
#include "brave/components/services/bat_ledger/public/cpp/ledger_client_mojo_proxy.h"
#include "chrome/browser/bitmap_fetcher/bitmap_fetcher_service_factory.h"
#include "chrome/browser/browser_process_impl.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/country_codes/country_codes.h"
#include "components/favicon/core/favicon_service.h"
#include "components/favicon_base/favicon_types.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/url_data_source.h"
#include "content/public/common/service_manager_connection.h"
#include "extensions/buildflags/buildflags.h"
#include "mojo/public/cpp/bindings/map.h"
#include "net/base/escape.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "net/base/url_util.h"
#include "net/http/http_status_code.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/service_manager/public/cpp/connector.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/image/image.h"
#include "url/gurl.h"
#include "url/url_canon_stdstring.h"

#if !defined(OS_ANDROID)
#include "brave/components/brave_rewards/resources/grit/brave_rewards_resources.h"
#include "components/grit/brave_components_resources.h"
#else
#include "components/grit/components_resources.h"
#endif

#if BUILDFLAG(ENABLE_EXTENSIONS)
#include "brave/components/brave_rewards/browser/extension_rewards_service_observer.h"
#endif

using net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES;
using std::placeholders::_1;
using std::placeholders::_2;

namespace brave_rewards {

static const unsigned int kRetriesCountOnNetworkChange = 1;

class LogStreamImpl : public ledger::LogStream {
 public:
  LogStreamImpl(const char* file,
                int line,
                const ledger::LogLevel log_level) {
    logging::LogSeverity severity;

    switch (log_level) {
      case ledger::LogLevel::LOG_INFO:
        severity = logging::LOG_INFO;
        break;
      case ledger::LogLevel::LOG_WARNING:
        severity = logging::LOG_WARNING;
        break;
      case ledger::LogLevel::LOG_ERROR:
        severity = logging::LOG_ERROR;
        break;
      default:
        severity = logging::LOG_VERBOSE;
        break;
    }

    log_message_ = std::make_unique<logging::LogMessage>(file, line, severity);
  }

  LogStreamImpl(const char* file,
                int line,
                int log_level) {
    // VLOG has negative log level
    log_message_ =
        std::make_unique<logging::LogMessage>(file, line, -log_level);
  }

  std::ostream& stream() override {
    return log_message_->stream();
  }

 private:
  std::unique_ptr<logging::LogMessage> log_message_;
  DISALLOW_COPY_AND_ASSIGN(LogStreamImpl);
};

namespace {

ledger::ACTIVITY_MONTH GetPublisherMonth(const base::Time& time) {
  base::Time::Exploded exploded;
  time.LocalExplode(&exploded);
  return (ledger::ACTIVITY_MONTH)exploded.month;
}

int GetPublisherYear(const base::Time& time) {
  base::Time::Exploded exploded;
  time.LocalExplode(&exploded);
  return exploded.year;
}

ContentSite PublisherInfoToContentSite(
    const ledger::PublisherInfo& publisher_info) {
  ContentSite content_site(publisher_info.id);
  content_site.percentage = publisher_info.percent;
  content_site.verified = publisher_info.verified;
  content_site.excluded = publisher_info.excluded;
  content_site.name = publisher_info.name;
  content_site.url = publisher_info.url;
  content_site.provider = publisher_info.provider;
  content_site.favicon_url = publisher_info.favicon_url;
  content_site.id = publisher_info.id;
  content_site.weight = publisher_info.weight;
  content_site.reconcile_stamp = publisher_info.reconcile_stamp;
  return content_site;
}

std::string URLMethodToRequestType(ledger::URL_METHOD method) {
  switch (method) {
    case ledger::URL_METHOD::GET:
      return "GET";
    case ledger::URL_METHOD::POST:
      return "POST";
    case ledger::URL_METHOD::PUT:
      return "PUT";
    default:
      NOTREACHED();
      return "GET";
  }
}

std::string LoadStateOnFileTaskRunner(
    const base::FilePath& path) {
  std::string data;
  bool success = base::ReadFileToString(path, &data);

  // Make sure the file isn't empty.
  if (!success || data.empty()) {
    LOG(ERROR) << "Failed to read file: " << path.MaybeAsASCII();
    return std::string();
  }
  return data;
}

bool SaveMediaPublisherInfoOnFileTaskRunner(
    const std::string& media_key,
    const std::string& publisher_id,
    PublisherInfoDatabase* backend) {
  if (backend && backend->InsertOrUpdateMediaPublisherInfo(media_key,
        publisher_id))
    return true;

  return false;
}

ledger::PublisherInfoPtr
LoadPublisherInfoOnFileTaskRunner(
    const std::string& publisher_key,
    PublisherInfoDatabase* backend) {
  if (!backend)
    return nullptr;

  return backend->GetPublisherInfo(publisher_key);
}

ledger::PublisherInfoPtr
LoadMediaPublisherInfoOnFileTaskRunner(
    const std::string& media_key,
    PublisherInfoDatabase* backend) {
  ledger::PublisherInfoPtr info;
  if (!backend)
    return info;

  return backend->GetMediaPublisherInfo(media_key);
}

bool SavePublisherInfoOnFileTaskRunner(
    ledger::PublisherInfoPtr publisher_info,
    PublisherInfoDatabase* backend) {
  if (backend &&
      backend->InsertOrUpdatePublisherInfo(*publisher_info))
    return true;

  return false;
}

bool SaveActivityInfoOnFileTaskRunner(
    ledger::PublisherInfoPtr publisher_info,
    PublisherInfoDatabase* backend) {
  if (backend &&
      backend->InsertOrUpdateActivityInfo(*publisher_info))
    return true;

  return false;
}

ledger::PublisherInfoList GetActivityListOnFileTaskRunner(
    uint32_t start,
    uint32_t limit,
    ledger::ActivityInfoFilter filter,
    PublisherInfoDatabase* backend) {
  ledger::PublisherInfoList list;
  if (!backend)
    return list;

  if (filter.excluded ==
    ledger::EXCLUDE_FILTER::FILTER_EXCLUDED) {
    ignore_result(backend->GetExcludedList(&list));
  } else {
    ignore_result(backend->GetActivityList(start, limit, filter, &list));
  }
  return list;
}

ledger::PublisherInfoPtr GetPanelPublisherInfoOnFileTaskRunner(
    ledger::ActivityInfoFilter filter,
    PublisherInfoDatabase* backend) {
  ledger::PublisherInfoList list;
  if (!backend) {
    return nullptr;
  }

  return backend->GetPanelPublisher(filter);
}

// `callback` has a WeakPtr so this won't crash if the file finishes
// writing after RewardsServiceImpl has been destroyed
void PostWriteCallback(
    const base::Callback<void(bool success)>& callback,
    scoped_refptr<base::SequencedTaskRunner> reply_task_runner,
    bool write_success) {
  // We can't run |callback| on the current thread. Bounce back to
  // the |reply_task_runner| which is the correct sequenced thread.
  reply_task_runner->PostTask(FROM_HERE,
                              base::Bind(callback, write_success));
}

time_t GetCurrentTimestamp() {
  return base::Time::NowFromSystemTime().ToTimeT();
}

std::string LoadOnFileTaskRunner(const base::FilePath& path) {
  std::string data;
  bool success = base::ReadFileToString(path, &data);

  // Make sure the file isn't empty.
  if (!success || data.empty()) {
    LOG(ERROR) << "Failed to read file: " << path.MaybeAsASCII();
    return std::string();
  }
  return data;
}

bool ResetOnFileTaskRunner(const base::FilePath& path) {
  return base::DeleteFile(path, false);
}

void EnsureRewardsBaseDirectoryExists(const base::FilePath& path) {
  if (!DirectoryExists(path))
    base::CreateDirectory(path);
}

net::NetworkTrafficAnnotationTag
GetNetworkTrafficAnnotationTagForFaviconFetch() {
  return net::DefineNetworkTrafficAnnotation(
      "brave_rewards_favicon_fetcher", R"(
      semantics {
        sender:
          "Brave Rewards Media Fetcher"
        description:
          "Fetches favicon for media publishers in Rewards."
        trigger:
          "User visits a media publisher content."
        data: "Favicon for media publisher."
        destination: WEBSITE
      }
      policy {
        cookies_allowed: NO
        setting:
          "This feature cannot be disabled by settings."
        policy_exception_justification:
          "Not implemented."
      })");
}

net::NetworkTrafficAnnotationTag GetNetworkTrafficAnnotationTagForURLLoad() {
  return net::DefineNetworkTrafficAnnotation("rewards_service_impl", R"(
      semantics {
        sender: "Brave Rewards service"
        description:
          "This service lets users anonymously support the sites they visit by "
          "tallying the attention spent on visited sites and dividing up a "
          "monthly BAT contribution."
        trigger:
          "User-initiated for direct tipping or on a set interval while Brave "
          "is running for monthly contributions."
        data:
          "Publisher and contribution data."
        destination: WEBSITE
      }
      policy {
        cookies_allowed: NO
        setting:
          "You can enable or disable this feature via the BAT icon in the URL "
          "bar or by visiting brave://rewards/."
        policy_exception_justification:
          "Not implemented."
      })");
}

const char pref_prefix[] = "brave.rewards.";

}  // namespace

bool IsMediaLink(const GURL& url,
                 const GURL& first_party_url,
                 const GURL& referrer) {
  return ledger::Ledger::IsMediaLink(url.spec(),
                                     first_party_url.spec(),
                                     referrer.spec());
}


// read comment about file pathes at src\base\files\file_path.h
#if defined(OS_WIN)
const base::FilePath::StringType kLedger_state(L"ledger_state");
const base::FilePath::StringType kPublisher_state(L"publisher_state");
const base::FilePath::StringType kPublisher_info_db(L"publisher_info_db");
const base::FilePath::StringType kPublishers_list(L"publishers_list");
const base::FilePath::StringType kRewardsStatePath(L"rewards_service");
#else
const base::FilePath::StringType kLedger_state("ledger_state");
const base::FilePath::StringType kPublisher_state("publisher_state");
const base::FilePath::StringType kPublisher_info_db("publisher_info_db");
const base::FilePath::StringType kPublishers_list("publishers_list");
const base::FilePath::StringType kRewardsStatePath("rewards_service");
#endif

RewardsServiceImpl::RewardsServiceImpl(Profile* profile)
    : profile_(profile),
      bat_ledger_client_binding_(new bat_ledger::LedgerClientMojoProxy(this)),
#if BUILDFLAG(ENABLE_EXTENSIONS)
      extension_rewards_service_observer_(
          std::make_unique<ExtensionRewardsServiceObserver>(profile_)),
#endif
      file_task_runner_(base::CreateSequencedTaskRunnerWithTraits(
          {base::MayBlock(), base::TaskPriority::BEST_EFFORT,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN})),
      ledger_state_path_(profile_->GetPath().Append(kLedger_state)),
      publisher_state_path_(profile_->GetPath().Append(kPublisher_state)),
      publisher_info_db_path_(profile->GetPath().Append(kPublisher_info_db)),
      publisher_list_path_(profile->GetPath().Append(kPublishers_list)),
      rewards_base_path_(profile_->GetPath().Append(kRewardsStatePath)),
      publisher_info_backend_(
          new PublisherInfoDatabase(publisher_info_db_path_)),
      notification_service_(new RewardsNotificationServiceImpl(profile)),
#if BUILDFLAG(ENABLE_EXTENSIONS)
      private_observer_(
          std::make_unique<ExtensionRewardsServiceObserver>(profile_)),
#endif
      next_timer_id_(0) {
  file_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&EnsureRewardsBaseDirectoryExists,
                                rewards_base_path_));
  // Set up the rewards data source
  content::URLDataSource::Add(profile_,
                              std::make_unique<BraveRewardsSource>(profile_));
}

RewardsServiceImpl::~RewardsServiceImpl() {
  file_task_runner_->DeleteSoon(FROM_HERE, publisher_info_backend_.release());
  StopNotificationTimers();
}

void RewardsServiceImpl::ConnectionClosed() {
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(FROM_HERE,
      base::BindOnce(&RewardsServiceImpl::StartLedger, AsWeakPtr()),
      base::TimeDelta::FromSeconds(1));
}

void RewardsServiceImpl::Init() {
  AddObserver(notification_service_.get());
#if BUILDFLAG(ENABLE_EXTENSIONS)
  AddObserver(extension_rewards_service_observer_.get());
  private_observers_.AddObserver(private_observer_.get());
#endif

  StartLedger();
}

void RewardsServiceImpl::StartLedger() {
  bat_ledger::mojom::BatLedgerClientAssociatedPtrInfo client_ptr_info;
  bat_ledger_client_binding_.Bind(mojo::MakeRequest(&client_ptr_info));

  content::ServiceManagerConnection* connection =
          content::ServiceManagerConnection::GetForProcess();
  if (!connection) {
    return;
  }

  connection->GetConnector()->BindInterface(
      bat_ledger::mojom::kServiceName, &bat_ledger_service_);
  bat_ledger_service_.set_connection_error_handler(
      base::Bind(&RewardsServiceImpl::ConnectionClosed, AsWeakPtr()));

  bool isProduction = true;
  // Environment
  #if defined(OFFICIAL_BUILD)
    isProduction = true;
  #else
    isProduction = false;
  #endif
  SetProduction(isProduction);

  SetDebug(false);

  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();

  if (command_line.HasSwitch(switches::kRewards)) {
    std::string options = command_line.GetSwitchValueASCII(switches::kRewards);

    if (!options.empty()) {
      HandleFlags(options);
    }
  }

  bat_ledger_service_->Create(std::move(client_ptr_info),
      MakeRequest(&bat_ledger_));

  bat_ledger_->Initialize();
}

void RewardsServiceImpl::MaybeShowBackupNotification(uint64_t boot_stamp) {
  PrefService* pref_service = profile_->GetPrefs();
  bool user_has_funded = pref_service->GetBoolean(prefs::kRewardsUserHasFunded);
  bool backup_succeeded = pref_service->GetBoolean(
      prefs::kRewardsBackupSucceeded);
  if (user_has_funded && !backup_succeeded) {
    base::Time now = base::Time::Now();
    base::Time boot_timestamp = base::Time::FromDoubleT(boot_stamp);
    base::TimeDelta backup_notification_frequency =
        pref_service->GetTimeDelta(prefs::kRewardsBackupNotificationFrequency);
    base::TimeDelta backup_notification_interval =
        pref_service->GetTimeDelta(prefs::kRewardsBackupNotificationInterval);
    base::TimeDelta elapsed = now - boot_timestamp;
    if (elapsed > backup_notification_interval) {
      base::TimeDelta next_backup_notification_interval =
          backup_notification_interval + backup_notification_frequency;
      pref_service->SetTimeDelta(prefs::kRewardsBackupNotificationInterval,
                                 next_backup_notification_interval);
      RewardsNotificationService::RewardsNotificationArgs args;
      notification_service_->AddNotification(
          RewardsNotificationService::REWARDS_NOTIFICATION_BACKUP_WALLET, args,
          "rewards_notification_backup_wallet");
    }
  }
}

void RewardsServiceImpl::MaybeShowAddFundsNotification(
    uint64_t reconcile_stamp) {
  // Show add funds notification if reconciliation will occur in the
  // next 3 days and balance is too low.
  base::Time now = base::Time::Now();
  if (reconcile_stamp - now.ToDoubleT() <
      3 * base::Time::kHoursPerDay * base::Time::kSecondsPerHour) {
    if (ShouldShowNotificationAddFunds()) {
      MaybeShowNotificationAddFunds();
    }
  }
}

void RewardsServiceImpl::CreateWallet() {
  if (ready().is_signaled()) {
    if (Connected())
      bat_ledger_->CreateWallet();
  } else {
    ready().Post(FROM_HERE,
        base::Bind(&brave_rewards::RewardsService::CreateWallet,
            base::Unretained(this)));
  }
}

void RewardsServiceImpl::GetContentSiteList(
    uint32_t start,
    uint32_t limit,
    uint64_t min_visit_time,
    uint64_t reconcile_stamp,
    bool allow_non_verified,
    uint32_t min_visits,
    bool fetch_excluded,
    const GetContentSiteListCallback& callback) {
  ledger::ActivityInfoFilter filter;
  filter.min_duration = min_visit_time;
  filter.order_by.push_back(std::pair<std::string, bool>("ai.percent", false));
  filter.reconcile_stamp = reconcile_stamp;
  filter.excluded = fetch_excluded
    ? ledger::EXCLUDE_FILTER::FILTER_EXCLUDED
    : ledger::EXCLUDE_FILTER::FILTER_ALL_EXCEPT_EXCLUDED;
  filter.percent = 1;
  filter.non_verified = allow_non_verified;
  filter.min_visits = min_visits;

  bat_ledger_->GetActivityInfoList(
      start,
      limit,
      filter.ToJson(),
      base::BindOnce(&RewardsServiceImpl::OnGetContentSiteList,
                     AsWeakPtr(),
                     callback));
}

void RewardsServiceImpl::OnGetContentSiteList(
    const GetContentSiteListCallback& callback,
    ledger::PublisherInfoList list,
    uint32_t next_record) {
  std::unique_ptr<ContentSiteList> site_list(new ContentSiteList);

  for (auto &publisher : list) {
    site_list->push_back(PublisherInfoToContentSite(*publisher));
  }

  callback.Run(std::move(site_list), next_record);
}

void RewardsServiceImpl::OnLoad(SessionID tab_id, const GURL& url) {
  if (!Connected())
    return;

  auto origin = url.GetOrigin();
  const std::string baseDomain =
      GetDomainAndRegistry(origin.host(), INCLUDE_PRIVATE_REGISTRIES);

  if (baseDomain == "")
    return;

  const std::string publisher_url = origin.scheme() + "://" + baseDomain + "/";

  ledger::VisitDataPtr data = ledger::VisitData::New();
  data->tld = data->name = baseDomain;
  data->domain = origin.host(),
  data->path = url.path();
  data->tab_id = tab_id.id();
  data->url = publisher_url;
  bat_ledger_->OnLoad(std::move(data), GetCurrentTimestamp());
}

void RewardsServiceImpl::OnUnload(SessionID tab_id) {
  if (!Connected())
    return;

  bat_ledger_->OnUnload(tab_id.id(), GetCurrentTimestamp());
}

void RewardsServiceImpl::OnShow(SessionID tab_id) {
  if (!Connected())
    return;

  bat_ledger_->OnShow(tab_id.id(), GetCurrentTimestamp());
}

void RewardsServiceImpl::OnHide(SessionID tab_id) {
  if (!Connected())
    return;

  bat_ledger_->OnHide(tab_id.id(), GetCurrentTimestamp());
}

void RewardsServiceImpl::OnForeground(SessionID tab_id) {
  if (!Connected())
    return;

  bat_ledger_->OnForeground(tab_id.id(), GetCurrentTimestamp());
}

void RewardsServiceImpl::OnBackground(SessionID tab_id) {
  if (!Connected())
    return;

  bat_ledger_->OnBackground(tab_id.id(), GetCurrentTimestamp());
}

void RewardsServiceImpl::OnPostData(SessionID tab_id,
                                    const GURL& url,
                                    const GURL& first_party_url,
                                    const GURL& referrer,
                                    const std::string& post_data) {
  if (!Connected())
    return;

  std::string output;
  url::RawCanonOutputW<1024> canonOutput;
  url::DecodeURLEscapeSequences(post_data.c_str(),
                                post_data.length(),
                                url::DecodeURLMode::kUTF8OrIsomorphic,
                                &canonOutput);
  output = base::UTF16ToUTF8(base::StringPiece16(canonOutput.data(),
                                                 canonOutput.length()));

  if (output.empty())
    return;

  ledger::VisitDataPtr data = ledger::VisitData::New();
  data->path = url.spec(),
  data->tab_id = tab_id.id();

  bat_ledger_->OnPostData(url.spec(),
                          first_party_url.spec(),
                          referrer.spec(),
                          output,
                          std::move(data));
}

void RewardsServiceImpl::OnXHRLoad(SessionID tab_id,
                                   const GURL& url,
                                   const GURL& first_party_url,
                                   const GURL& referrer) {
  if (!Connected())
    return;

  std::map<std::string, std::string> parts;

  for (net::QueryIterator it(url); !it.IsAtEnd(); it.Advance()) {
    parts[it.GetKey()] = it.GetUnescapedValue();
  }

  ledger::VisitDataPtr data = ledger::VisitData::New();
  data->path = url.spec();
  data->tab_id = tab_id.id();

  bat_ledger_->OnXHRLoad(tab_id.id(),
                         url.spec(),
                         mojo::MapToFlatMap(parts),
                         first_party_url.spec(),
                         referrer.spec(),
                         std::move(data));
}

void RewardsServiceImpl::LoadPublisherInfo(
    const std::string& publisher_key,
    ledger::PublisherInfoCallback callback) {
  base::PostTaskAndReplyWithResult(file_task_runner_.get(), FROM_HERE,
      base::Bind(&LoadPublisherInfoOnFileTaskRunner,
          publisher_key, publisher_info_backend_.get()),
      base::Bind(&RewardsServiceImpl::OnPublisherInfoLoaded,
                     AsWeakPtr(),
                     callback));
}

void RewardsServiceImpl::OnPublisherInfoLoaded(
    ledger::PublisherInfoCallback callback,
    ledger::PublisherInfoPtr info) {
  if (!info) {
    callback(ledger::Result::NOT_FOUND, nullptr);
    return;
  }

  callback(ledger::Result::LEDGER_OK, std::move(info));
}

void RewardsServiceImpl::LoadMediaPublisherInfo(
    const std::string& media_key,
    ledger::PublisherInfoCallback callback) {
  base::PostTaskAndReplyWithResult(file_task_runner_.get(), FROM_HERE,
      base::Bind(&LoadMediaPublisherInfoOnFileTaskRunner,
          media_key, publisher_info_backend_.get()),
      base::Bind(&RewardsServiceImpl::OnMediaPublisherInfoLoaded,
                     AsWeakPtr(),
                     callback));
}

void RewardsServiceImpl::OnMediaPublisherInfoLoaded(
    ledger::PublisherInfoCallback callback,
    ledger::PublisherInfoPtr info) {
  if (!Connected())
    return;

  if (!info) {
    callback(ledger::Result::NOT_FOUND, nullptr);
    return;
  }

  callback(ledger::Result::LEDGER_OK, std::move(info));
}

void RewardsServiceImpl::SaveMediaPublisherInfo(
    const std::string& media_key,
    const std::string& publisher_id) {
base::PostTaskAndReplyWithResult(file_task_runner_.get(), FROM_HERE,
      base::Bind(&SaveMediaPublisherInfoOnFileTaskRunner,
                    media_key,
                    publisher_id,
                    publisher_info_backend_.get()),
      base::Bind(&RewardsServiceImpl::OnMediaPublisherInfoSaved,
                     AsWeakPtr()));
}

void RewardsServiceImpl::RestorePublishers() {
  if (!Connected())
    return;

  bat_ledger_->RestorePublishers();
}

void RewardsServiceImpl::OnMediaPublisherInfoSaved(bool success) {
  if (!success) {
    LOG(ERROR) << "Error in OnMediaPublisherInfoSaved";
  }
}

std::string RewardsServiceImpl::URIEncode(const std::string& value) {
  return net::EscapeQueryParamValue(value, false);
}

std::string RewardsServiceImpl::GenerateGUID() const {
  return base::GenerateGUID();
}

void RewardsServiceImpl::Shutdown() {
  RemoveObserver(notification_service_.get());
#if BUILDFLAG(ENABLE_EXTENSIONS)
  RemoveObserver(extension_rewards_service_observer_.get());
  private_observers_.RemoveObserver(private_observer_.get());
#endif
  BitmapFetcherService* image_service =
      BitmapFetcherServiceFactory::GetForBrowserContext(profile_);
  if (image_service) {
    for (auto request_id : request_ids_) {
      image_service->CancelRequest(request_id);
    }
  }

  for (auto* const loader : url_loaders_) {
    delete loader;
  }
  url_loaders_.clear();

  bat_ledger_.reset();
  RewardsService::Shutdown();
}

void RewardsServiceImpl::OnWalletInitialized(ledger::Result result) {
  if (!ready_.is_signaled())
    ready_.Signal();

  if (result == ledger::Result::WALLET_CREATED) {
    SetRewardsMainEnabled(true);
    SetAutoContribute(true);
    StartNotificationTimers(true);
  }

  TriggerOnWalletInitialized(result);
}

void RewardsServiceImpl::OnWalletProperties(
    ledger::Result result,
    ledger::WalletPropertiesPtr properties) {
  std::unique_ptr<brave_rewards::WalletProperties> wallet_properties;
  for (auto& observer : observers_) {
    if (properties) {
      wallet_properties.reset(new brave_rewards::WalletProperties);
      wallet_properties->parameters_choices = properties->parameters_choices;
      wallet_properties->parameters_range = properties->parameters_range;
      wallet_properties->parameters_days = properties->parameters_days;
      wallet_properties->monthly_amount = properties->fee_amount;

      for (size_t i = 0; i < properties->grants.size(); i ++) {
        brave_rewards::Grant grant;

        grant.altcurrency = properties->grants[i]->altcurrency;
        grant.probi = properties->grants[i]->probi;
        grant.expiryTime = properties->grants[i]->expiry_time;
        grant.type = properties->grants[i]->type;

        wallet_properties->grants.push_back(grant);
      }
    }

    // webui
    observer.OnWalletProperties(this,
                                static_cast<int>(result),
                                std::move(wallet_properties));
  }
}

void RewardsServiceImpl::OnGetAutoContributeProps(
    const GetAutoContributePropsCallback& callback,
    ledger::AutoContributePropsPtr props) {
  if (!props) {
    callback.Run(nullptr);
  }

  auto auto_contri_props =
    std::make_unique<brave_rewards::AutoContributeProps>();
  auto_contri_props->enabled_contribute = props->enabled_contribute;
  auto_contri_props->contribution_min_time = props->contribution_min_time;
  auto_contri_props->contribution_min_visits = props->contribution_min_visits;
  auto_contri_props->contribution_non_verified =
    props->contribution_non_verified;
  auto_contri_props->contribution_videos = props->contribution_videos;
  auto_contri_props->reconcile_stamp = props->reconcile_stamp;

  callback.Run(std::move(auto_contri_props));
}

void RewardsServiceImpl::OnGetRewardsInternalsInfo(
    GetRewardsInternalsInfoCallback callback,
    const std::string& json_info) {
  ledger::RewardsInternalsInfo info;
  info.loadFromJson(json_info);

  auto rewards_internals_info =
      std::make_unique<brave_rewards::RewardsInternalsInfo>();
  rewards_internals_info->payment_id = info.payment_id;
  rewards_internals_info->is_key_info_seed_valid = info.is_key_info_seed_valid;
  rewards_internals_info->persona_id = info.persona_id;
  rewards_internals_info->user_id = info.user_id;
  rewards_internals_info->boot_stamp = info.boot_stamp;

  for (const auto& item : info.current_reconciles) {
    ReconcileInfo reconcile_info;
    reconcile_info.viewing_id_ = item.second.viewingId_;
    reconcile_info.amount_ = item.second.amount_;
    reconcile_info.retry_step_ =
        static_cast<ContributionRetry>(item.second.retry_step_);
    reconcile_info.retry_level_ = item.second.retry_level_;
    rewards_internals_info->current_reconciles[item.first] = reconcile_info;
  }

  std::move(callback).Run(std::move(rewards_internals_info));
}

void RewardsServiceImpl::GetAutoContributeProps(
    const GetAutoContributePropsCallback& callback) {
  if (!Connected())
    return;

  bat_ledger_->GetAutoContributeProps(base::BindOnce(
        &RewardsServiceImpl::OnGetAutoContributeProps, AsWeakPtr(), callback));
}

void RewardsServiceImpl::OnGrant(ledger::Result result,
                                 ledger::GrantPtr grant) {
  TriggerOnGrant(result, std::move(grant));
}

void RewardsServiceImpl::OnGrantCaptcha(const std::string& image,
    const std::string& hint) {
  TriggerOnGrantCaptcha(image, hint);
}

void RewardsServiceImpl::OnRecoverWallet(ledger::Result result,
                                    double balance,
                                    std::vector<ledger::GrantPtr> grants) {
  TriggerOnRecoverWallet(result, balance, std::move(grants));
}

void RewardsServiceImpl::OnGrantFinish(ledger::Result result,
                                       ledger::GrantPtr grant) {
  ledger::BalanceReportInfo report_info;
  auto now = base::Time::Now();
  if (grant && result == ledger::Result::LEDGER_OK) {
    if (!Connected()) {
      return;
    }

    int report_type = grant->type == "ads"
      ? ledger::ReportType::ADS
      : ledger::ReportType::GRANT;
    bat_ledger_->SetBalanceReportItem(GetPublisherMonth(now),
                                      GetPublisherYear(now),
                                      report_type,
                                      grant->probi);
  }

  GetCurrentBalanceReport();
  TriggerOnGrantFinish(result, std::move(grant));
}

void RewardsServiceImpl::OnReconcileComplete(ledger::Result result,
  const std::string& viewing_id,
  ledger::REWARDS_CATEGORY category,
  const std::string& probi) {
  if (result == ledger::Result::LEDGER_OK) {
    auto now = base::Time::Now();
    if (!Connected())
      return;

    if (category == ledger::REWARDS_CATEGORY::RECURRING_TIP) {
      MaybeShowNotificationTipsPaid();
    }

    bat_ledger_->OnReconcileCompleteSuccess(viewing_id,
        category,
        probi,
        GetPublisherMonth(now),
        GetPublisherYear(now),
        GetCurrentTimestamp());
  }

  GetCurrentBalanceReport();
  for (auto& observer : observers_)
    observer.OnReconcileComplete(this,
                                 result,
                                 viewing_id,
                                 category,
                                 probi);
}

void RewardsServiceImpl::LoadLedgerState(
    ledger::OnLoadCallback callback) {
  base::PostTaskAndReplyWithResult(file_task_runner_.get(), FROM_HERE,
      base::BindOnce(&LoadStateOnFileTaskRunner, ledger_state_path_),
      base::BindOnce(&RewardsServiceImpl::OnLedgerStateLoaded,
                     AsWeakPtr(),
                     std::move(callback)));
}

void RewardsServiceImpl::OnLedgerStateLoaded(
    ledger::OnLoadCallback callback,
    const std::string& data) {
  if (!Connected())
    return;

  callback(data.empty() ? ledger::Result::NO_LEDGER_STATE
                        : ledger::Result::LEDGER_OK,
                        data);

  bat_ledger_->GetRewardsMainEnabled(
      base::BindOnce(&RewardsServiceImpl::StartNotificationTimers,
        AsWeakPtr()));
}

void RewardsServiceImpl::LoadPublisherState(
    ledger::OnLoadCallback callback) {
  if (!profile_->GetPrefs()->GetBoolean(prefs::kBraveRewardsEnabledMigrated)) {
    bat_ledger_->GetRewardsMainEnabled(
        base::BindOnce(&RewardsServiceImpl::SetRewardsMainEnabledPref,
          AsWeakPtr()));
  }
  base::PostTaskAndReplyWithResult(file_task_runner_.get(), FROM_HERE,
      base::BindOnce(&LoadStateOnFileTaskRunner, publisher_state_path_),
      base::BindOnce(&RewardsServiceImpl::OnPublisherStateLoaded,
                     AsWeakPtr(),
                     std::move(callback)));
}

void RewardsServiceImpl::OnPublisherStateLoaded(
    ledger::OnLoadCallback callback,
    const std::string& data) {
  if (!Connected())
    return;

  callback(
      data.empty() ? ledger::Result::NO_PUBLISHER_STATE
                   : ledger::Result::LEDGER_OK,
      data);
}

void RewardsServiceImpl::SaveLedgerState(const std::string& ledger_state,
                                      ledger::LedgerCallbackHandler* handler) {
  base::ImportantFileWriter writer(
      ledger_state_path_, file_task_runner_);

  writer.RegisterOnNextWriteCallbacks(
      base::Closure(),
      base::Bind(
        &PostWriteCallback,
        base::Bind(&RewardsServiceImpl::OnLedgerStateSaved, AsWeakPtr(),
            base::Unretained(handler)),
        base::SequencedTaskRunnerHandle::Get()));

  writer.WriteNow(std::make_unique<std::string>(ledger_state));
}

void RewardsServiceImpl::OnLedgerStateSaved(
    ledger::LedgerCallbackHandler* handler,
    bool success) {
  if (!Connected())
    return;

  handler->OnLedgerStateSaved(success ? ledger::Result::LEDGER_OK
                                      : ledger::Result::NO_LEDGER_STATE);
}

void RewardsServiceImpl::SavePublisherState(const std::string& publisher_state,
                                      ledger::LedgerCallbackHandler* handler) {
  base::ImportantFileWriter writer(publisher_state_path_, file_task_runner_);

  writer.RegisterOnNextWriteCallbacks(
      base::Closure(),
      base::Bind(
        &PostWriteCallback,
        base::Bind(&RewardsServiceImpl::OnPublisherStateSaved, AsWeakPtr(),
            base::Unretained(handler)),
        base::SequencedTaskRunnerHandle::Get()));

  writer.WriteNow(std::make_unique<std::string>(publisher_state));
}

void RewardsServiceImpl::OnPublisherStateSaved(
    ledger::LedgerCallbackHandler* handler,
    bool success) {
  if (!Connected())
    return;

  handler->OnPublisherStateSaved(success ? ledger::Result::LEDGER_OK
                                         : ledger::Result::LEDGER_ERROR);
}

void RewardsServiceImpl::LoadNicewareList(
  ledger::GetNicewareListCallback callback) {
  if (!Connected())
    return;

  std::string data = ui::ResourceBundle::GetSharedInstance().GetRawDataResource(
      IDR_BRAVE_REWARDS_NICEWARE_LIST).as_string();

  if (data.empty()) {
    LOG(ERROR) << "Failed to read in niceware list";
  }
  callback(data.empty() ? ledger::Result::LEDGER_ERROR
                        : ledger::Result::LEDGER_OK, data);
}

void RewardsServiceImpl::SavePublisherInfo(
    ledger::PublisherInfoPtr publisher_info,
    ledger::PublisherInfoCallback callback) {
  ledger::PublisherInfoPtr copy = publisher_info->Clone();
  base::PostTaskAndReplyWithResult(file_task_runner_.get(), FROM_HERE,
      base::BindOnce(&SavePublisherInfoOnFileTaskRunner,
                    std::move(copy),
                    publisher_info_backend_.get()),
      base::BindOnce(&RewardsServiceImpl::OnPublisherInfoSaved,
                     AsWeakPtr(),
                     callback,
                     std::move(publisher_info)));
}

void RewardsServiceImpl::OnPublisherInfoSaved(
    ledger::PublisherInfoCallback callback,
    ledger::PublisherInfoPtr info,
    bool success) {
  if (Connected()) {
    callback(success ? ledger::Result::LEDGER_OK
                     : ledger::Result::LEDGER_ERROR, std::move(info));
  }
}

void RewardsServiceImpl::SaveActivityInfo(
    ledger::PublisherInfoPtr publisher_info,
    ledger::PublisherInfoCallback callback) {
  ledger::PublisherInfoPtr copy = publisher_info->Clone();
  base::PostTaskAndReplyWithResult(file_task_runner_.get(), FROM_HERE,
      base::BindOnce(&SaveActivityInfoOnFileTaskRunner,
                    std::move(copy),
                    publisher_info_backend_.get()),
      base::BindOnce(&RewardsServiceImpl::OnActivityInfoSaved,
                     AsWeakPtr(),
                     callback,
                     std::move(publisher_info)));
}

void RewardsServiceImpl::OnActivityInfoSaved(
    ledger::PublisherInfoCallback callback,
    ledger::PublisherInfoPtr info,
    bool success) {
  if (Connected()) {
    callback(success ? ledger::Result::LEDGER_OK
                     : ledger::Result::LEDGER_ERROR, std::move(info));
  }
}

void RewardsServiceImpl::LoadActivityInfo(
    ledger::ActivityInfoFilter filter,
    ledger::PublisherInfoCallback callback) {
  base::PostTaskAndReplyWithResult(file_task_runner_.get(), FROM_HERE,
      base::Bind(&GetActivityListOnFileTaskRunner,
          // set limit to 2 to make sure there is
          // only 1 valid result for the filter
          0, 2, filter, publisher_info_backend_.get()),
      base::Bind(&RewardsServiceImpl::OnActivityInfoLoaded,
                     AsWeakPtr(),
                     callback,
                     filter.id));
}

void RewardsServiceImpl::OnPublisherActivityInfoLoaded(
    ledger::PublisherInfoCallback callback,
    uint32_t result,
    ledger::PublisherInfoPtr publisher) {
  callback(static_cast<ledger::Result>(result), std::move(publisher));
}

void RewardsServiceImpl::OnActivityInfoLoaded(
    ledger::PublisherInfoCallback callback,
    const std::string& publisher_key,
    ledger::PublisherInfoList list) {
  if (!Connected()) {
    return;
  }

  // activity info not found
  if (list.size() == 0) {
    // we need to try to get at least publisher info in this case
    // this way we preserve publisher info
    bat_ledger_->LoadPublisherInfo(
        publisher_key,
        base::BindOnce(&RewardsServiceImpl::OnPublisherActivityInfoLoaded,
                       AsWeakPtr(),
                       callback));
    return;
  } else if (list.size() > 1) {
    callback(ledger::Result::TOO_MANY_RESULTS, nullptr);
    return;
  }

  callback(ledger::Result::LEDGER_OK,
      std::move(list[0]));
}

void RewardsServiceImpl::LoadPanelPublisherInfo(
    ledger::ActivityInfoFilter filter,
    ledger::PublisherInfoCallback callback) {
  base::PostTaskAndReplyWithResult(file_task_runner_.get(), FROM_HERE,
      base::Bind(&GetPanelPublisherInfoOnFileTaskRunner,
                 filter,
                 publisher_info_backend_.get()),
      base::Bind(&RewardsServiceImpl::OnPanelPublisherInfoLoaded,
                     AsWeakPtr(),
                     callback));
}

void RewardsServiceImpl::OnPanelPublisherInfoLoaded(
    ledger::PublisherInfoCallback callback,
    ledger::PublisherInfoPtr publisher_info) {
  if (!publisher_info) {
    callback(ledger::Result::NOT_FOUND, nullptr);
    return;
  }

  callback(ledger::Result::LEDGER_OK, std::move(publisher_info));
}

void RewardsServiceImpl::GetActivityInfoList(
    uint32_t start,
    uint32_t limit,
    ledger::ActivityInfoFilter filter,
    ledger::PublisherInfoListCallback callback) {
  base::PostTaskAndReplyWithResult(file_task_runner_.get(), FROM_HERE,
      base::Bind(&GetActivityListOnFileTaskRunner,
                    start, limit, filter,
                    publisher_info_backend_.get()),
      base::Bind(&RewardsServiceImpl::OnPublisherInfoListLoaded,
                    AsWeakPtr(),
                    start,
                    limit,
                    callback));
}

void RewardsServiceImpl::OnPublisherInfoListLoaded(
    uint32_t start,
    uint32_t limit,
    ledger::PublisherInfoListCallback callback,
    ledger::PublisherInfoList list) {
  if (!Connected()) {
    return;
  }

  uint32_t next_record = 0;
  if (list.size() == limit)
    next_record = start + limit + 1;

  callback(std::move(list), next_record);
}

void RewardsServiceImpl::LoadURL(
    const std::string& url,
    const std::vector<std::string>& headers,
    const std::string& content,
    const std::string& contentType,
    const ledger::URL_METHOD method,
    ledger::LoadURLCallback callback) {

  if (url.empty()) {
    callback(net::HTTP_BAD_REQUEST, "", {});
    return;
  }

  GURL parsed_url(url);
  if (!parsed_url.is_valid()) {
    callback(net::HTTP_BAD_REQUEST, "", {});
    return;
  }

  if (test_response_callback_) {
    std::string test_response;
    std::map<std::string, std::string> test_headers;
    int response_status_code = net::HTTP_OK;
    test_response_callback_.Run(url,
                                &response_status_code,
                                &test_response,
                                &test_headers);
    callback(response_status_code, test_response, test_headers);
    return;
  }

  const std::string request_method = URLMethodToRequestType(method);
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL(url);
  request->method = request_method;
  request->allow_credentials = false;
  for (size_t i = 0; i < headers.size(); i++)
    request->headers.AddHeaderFromString(headers[i]);
  network::SimpleURLLoader* loader = network::SimpleURLLoader::Create(
      std::move(request),
      GetNetworkTrafficAnnotationTagForURLLoad()).release();
  loader->SetAllowHttpErrorResults(true);
  url_loaders_.insert(loader);
  loader->SetRetryOptions(kRetriesCountOnNetworkChange,
      network::SimpleURLLoader::RetryMode::RETRY_ON_NETWORK_CHANGE);

  if (!content.empty())
    loader->AttachStringForUpload(content, contentType);

  if (VLOG_IS_ON(ledger::LogLevel::LOG_REQUEST)) {
    std::string headers_log = "";
    for (auto const& header : headers) {
      headers_log += "> headers: " + header + "\n";
    }

    VLOG(ledger::LogLevel::LOG_REQUEST) << std::endl
      << "[ REQUEST ]" << std::endl
      << "> url: " << url << std::endl
      << "> method: " << request_method << std::endl
      << "> content: " << content << std::endl
      << "> contentType: " << contentType << std::endl
      << headers_log
      << "[ END REQUEST ]";
  }

  loader->DownloadToStringOfUnboundedSizeUntilCrashAndDie(
      content::BrowserContext::GetDefaultStoragePartition(profile_)
          ->GetURLLoaderFactoryForBrowserProcess().get(),
      base::BindOnce(&RewardsServiceImpl::OnURLLoaderComplete,
                     base::Unretained(this),
                     loader,
                     callback));
}

void RewardsServiceImpl::OnURLLoaderComplete(
    network::SimpleURLLoader* loader,
    ledger::LoadURLCallback callback,
    std::unique_ptr<std::string> response_body) {
  DCHECK(url_loaders_.find(loader) != url_loaders_.end());
  url_loaders_.erase(loader);
  std::unique_ptr<network::SimpleURLLoader> scoped_loader(loader);

  int response_code = -1;
  if (loader->ResponseInfo() && loader->ResponseInfo()->headers)
    response_code = loader->ResponseInfo()->headers->response_code();

  std::map<std::string, std::string> headers;
  if (loader->ResponseInfo()) {
    scoped_refptr<net::HttpResponseHeaders> headersList =
        loader->ResponseInfo()->headers;

    if (headersList) {
      size_t iter = 0;
      std::string key;
      std::string value;
      while (headersList->EnumerateHeaderLines(&iter, &key, &value)) {
        key = base::ToLowerASCII(key);
        headers[key] = value;
      }
    }
  }

  if (Connected()) {
    callback(response_code,
             response_body ? *response_body : std::string(),
             headers);
  }
}

void RewardsServiceImpl::TriggerOnWalletInitialized(ledger::Result result) {
  for (auto& observer : observers_)
    observer.OnWalletInitialized(this, result);
}

void RewardsServiceImpl::OnFetchWalletProperties(
    int result,
    ledger::WalletPropertiesPtr properties) {
  OnWalletProperties(static_cast<ledger::Result>(result),
                     std::move(properties));
}

void RewardsServiceImpl::FetchWalletProperties() {
  if (ready().is_signaled()) {
    if (!Connected()) {
      return;
    }

    bat_ledger_->FetchWalletProperties(
        base::BindOnce(&RewardsServiceImpl::OnFetchWalletProperties,
                       AsWeakPtr()));
  } else {
    ready().Post(FROM_HERE,
        base::Bind(&brave_rewards::RewardsService::FetchWalletProperties,
            base::Unretained(this)));
  }
}

void RewardsServiceImpl::FetchGrants(const std::string& lang,
    const std::string& payment_id) {
  if (!Connected()) {
    return;
  }

  bat_ledger_->FetchGrants(lang, payment_id);
}

void RewardsServiceImpl::TriggerOnGrant(ledger::Result result,
                                        ledger::GrantPtr grant) {
  brave_rewards::Grant properties;

  if (grant) {
    properties.promotionId = grant->promotion_id;
    properties.altcurrency = grant->altcurrency;
    properties.probi = grant->probi;
    properties.expiryTime = grant->expiry_time;
    properties.type = grant->type;
  }

  for (auto& observer : observers_)
    observer.OnGrant(this, result, properties);
}

void RewardsServiceImpl::GetGrantCaptcha(
    const std::string& promotion_id,
    const std::string& promotion_type) {
  if (!Connected()) {
    return;
  }
  std::vector<std::string> headers;
  headers.push_back("brave-product:brave-core");
  headers.push_back("promotion-id:" + promotion_id);
  headers.push_back("promotion-type:" + promotion_type);
  bat_ledger_->GetGrantCaptcha(headers);
}

void RewardsServiceImpl::TriggerOnGrantCaptcha(const std::string& image,
    const std::string& hint) {
  for (auto& observer : observers_)
    observer.OnGrantCaptcha(this, image, hint);
}

void RewardsServiceImpl::GetWalletPassphrase(
    const GetWalletPassphraseCallback& callback) {
  if (!Connected()) {
    return;
  }

  bat_ledger_->GetWalletPassphrase(callback);
}

void RewardsServiceImpl::RecoverWallet(const std::string& passPhrase) const {
  if (!Connected()) {
    return;
  }

  bat_ledger_->RecoverWallet(passPhrase);
}

void RewardsServiceImpl::TriggerOnRecoverWallet(
    ledger::Result result,
    double balance,
    std::vector<ledger::GrantPtr> grants) {
  std::vector<brave_rewards::Grant> newGrants;
  for (size_t i = 0; i < grants.size(); i ++) {
    if (!grants[i]) {
      continue;
    }

    brave_rewards::Grant grant;

    grant.altcurrency = grants[i]->altcurrency;
    grant.probi = grants[i]->probi;
    grant.expiryTime = grants[i]->expiry_time;
    grant.type = grants[i]->type;

    newGrants.push_back(grant);
  }

  for (auto& observer : observers_)
    observer.OnRecoverWallet(this, result, balance, newGrants);
}

void RewardsServiceImpl::SolveGrantCaptcha(const std::string& solution,
                                         const std::string& promotionId) const {
  if (!Connected()) {
    return;
  }

  bat_ledger_->SolveGrantCaptcha(solution, promotionId);
}

void RewardsServiceImpl::TriggerOnGrantFinish(ledger::Result result,
                                              ledger::GrantPtr grant) {
  brave_rewards::Grant properties;

  if (grant) {
    properties.promotionId = grant->promotion_id;
    properties.altcurrency = grant->altcurrency;
    properties.probi = grant->probi;
    properties.expiryTime = grant->expiry_time;
    properties.type = grant->type;
  }

  for (auto& observer : observers_) {
    observer.OnGrantFinish(this, result, properties);
  }
}

void RewardsServiceImpl::GetReconcileStamp(
    const GetReconcileStampCallback& callback)  {
  if (!Connected()) {
    return;
  }

  bat_ledger_->GetReconcileStamp(callback);
}

void RewardsServiceImpl::OnGetAddresses(const GetAddressesCallback& callback,
    const base::flat_map<std::string, std::string>& addresses) {
  callback.Run(mojo::FlatMapToMap(addresses));
}

void RewardsServiceImpl::GetAddresses(const GetAddressesCallback& callback) {
  if (!Connected()) {
    return;
  }
  int32_t current_country =
      country_codes::GetCountryIDFromPrefs(profile_->GetPrefs());
  if (!current_country_for_test_.empty() &&
      current_country_for_test_.size() > 1) {
    current_country = country_codes::CountryCharsToCountryID(
        current_country_for_test_.at(0), current_country_for_test_.at(1));
  }

  bat_ledger_->GetAddresses(current_country,
      base::BindOnce(&RewardsServiceImpl::OnGetAddresses,
              AsWeakPtr(), callback));
}

void RewardsServiceImpl::SetRewardsMainEnabled(bool enabled) {
  if (!Connected()) {
    return;
  }

  SetRewardsMainEnabledPref(enabled);
  bat_ledger_->SetRewardsMainEnabled(enabled);
  TriggerOnRewardsMainEnabled(enabled);
}

void RewardsServiceImpl::GetRewardsMainEnabled(
    const GetRewardsMainEnabledCallback& callback) const {
  if (!Connected()) {
    return;
  }

  bat_ledger_->GetRewardsMainEnabled(callback);
}

void RewardsServiceImpl::SetRewardsMainEnabledPref(bool enabled) {
  profile_->GetPrefs()->SetBoolean(prefs::kBraveRewardsEnabled, enabled);
  SetRewardsMainEnabledMigratedPref(true);
}

void RewardsServiceImpl::SetRewardsMainEnabledMigratedPref(bool enabled) {
  profile_->GetPrefs()->SetBoolean(
      prefs::kBraveRewardsEnabledMigrated, true);
}

void RewardsServiceImpl::SetCatalogIssuers(const std::string& json) {
  if (!Connected()) {
    return;
  }

  bat_ledger_->SetCatalogIssuers(json);
}

void RewardsServiceImpl::ConfirmAd(const std::string& json) {
  if (!Connected()) {
    return;
  }

  bat_ledger_->ConfirmAd(json);
}

void RewardsServiceImpl::SetConfirmationsIsReady(const bool is_ready) {
  auto* ads_service = brave_ads::AdsServiceFactory::GetForProfile(profile_);
  if (ads_service)
    ads_service->SetConfirmationsIsReady(is_ready);
}

void RewardsServiceImpl::ConfirmationsTransactionHistoryDidChange() {
    for (auto& observer : observers_)
    observer.OnTransactionHistoryChanged(this);
}

void RewardsServiceImpl::GetTransactionHistory(
    GetTransactionHistoryCallback callback) {
  if (!Connected()) {
    return;
  }

  bat_ledger_->GetTransactionHistory(
      base::BindOnce(&RewardsServiceImpl::OnGetTransactionHistory,
          AsWeakPtr(), std::move(callback)));
}

void RewardsServiceImpl::OnGetTransactionHistory(
    GetTransactionHistoryCallback callback,
    const std::string& transactions) {
  ledger::TransactionsInfo info;
  info.FromJson(transactions);

  std::move(callback).Run(info.estimated_pending_rewards,
      info.next_payment_date_in_seconds,
      info.ad_notifications_received_this_month);
}

void RewardsServiceImpl::SaveState(const std::string& name,
                                   const std::string& value,
                                   ledger::OnSaveCallback callback) {
  base::ImportantFileWriter writer(
      rewards_base_path_.AppendASCII(name), file_task_runner_);

  writer.RegisterOnNextWriteCallbacks(
      base::Closure(),
      base::Bind(&PostWriteCallback,
                 base::Bind(&RewardsServiceImpl::OnSavedState,
                            AsWeakPtr(),
                            std::move(callback)),
                 base::SequencedTaskRunnerHandle::Get()));

  writer.WriteNow(std::make_unique<std::string>(value));
}

void RewardsServiceImpl::LoadState(
    const std::string& name,
    ledger::OnLoadCallback callback) {
  base::PostTaskAndReplyWithResult(
      file_task_runner_.get(), FROM_HERE,
      base::BindOnce(&LoadOnFileTaskRunner,
                     rewards_base_path_.AppendASCII(name)),
      base::BindOnce(&RewardsServiceImpl::OnLoadedState,
                     AsWeakPtr(), std::move(callback)));
}

void RewardsServiceImpl::ResetState(
    const std::string& name,
    ledger::OnResetCallback callback) {
  base::PostTaskAndReplyWithResult(
      file_task_runner_.get(), FROM_HERE,
      base::BindOnce(&ResetOnFileTaskRunner,
                     rewards_base_path_.AppendASCII(name)),
      base::BindOnce(&RewardsServiceImpl::OnResetState,
                     AsWeakPtr(), std::move(callback)));
}

void RewardsServiceImpl::OnSavedState(
  ledger::OnSaveCallback callback, bool success) {
  if (!Connected())
    return;
  callback(success ? ledger::Result::LEDGER_OK : ledger::Result::LEDGER_ERROR);
}

void RewardsServiceImpl::OnLoadedState(
    ledger::OnLoadCallback callback,
    const std::string& value) {
  if (!Connected())
    return;
  if (value.empty())
    callback(ledger::Result::LEDGER_ERROR, value);
  else
    callback(ledger::Result::LEDGER_OK, value);
}

void RewardsServiceImpl::SetBooleanState(const std::string& name, bool value) {
  profile_->GetPrefs()->SetBoolean(pref_prefix + name, value);
}

bool RewardsServiceImpl::GetBooleanState(const std::string& name) const {
  return profile_->GetPrefs()->GetBoolean(pref_prefix + name);
}

void RewardsServiceImpl::SetIntegerState(const std::string& name, int value) {
  profile_->GetPrefs()->SetInteger(pref_prefix + name, value);
}

int RewardsServiceImpl::GetIntegerState(const std::string& name) const {
  return profile_->GetPrefs()->GetInteger(pref_prefix + name);
}

void RewardsServiceImpl::SetDoubleState(const std::string& name, double value) {
  profile_->GetPrefs()->SetDouble(pref_prefix + name, value);
}

double RewardsServiceImpl::GetDoubleState(const std::string& name) const {
  return profile_->GetPrefs()->GetDouble(pref_prefix + name);
}

void RewardsServiceImpl::SetStringState(const std::string& name,
                                        const std::string& value) {
  profile_->GetPrefs()->SetString(pref_prefix + name, value);
}

std::string RewardsServiceImpl::GetStringState(const std::string& name) const {
  return profile_->GetPrefs()->GetString(pref_prefix + name);
}

void RewardsServiceImpl::SetInt64State(const std::string& name, int64_t value) {
  profile_->GetPrefs()->SetInt64(pref_prefix + name, value);
}

int64_t RewardsServiceImpl::GetInt64State(const std::string& name) const {
  return profile_->GetPrefs()->GetInt64(pref_prefix + name);
}

void RewardsServiceImpl::SetUint64State(const std::string& name,
                                        uint64_t value) {
  profile_->GetPrefs()->SetUint64(pref_prefix + name, value);
}

uint64_t RewardsServiceImpl::GetUint64State(const std::string& name) const {
  return profile_->GetPrefs()->GetUint64(pref_prefix + name);
}

void RewardsServiceImpl::ClearState(const std::string& name) {
  profile_->GetPrefs()->ClearPref(pref_prefix + name);
}

void RewardsServiceImpl::KillTimer(uint32_t timer_id) {
  if (timers_.find(timer_id) == timers_.end())
    return;

  timers_[timer_id]->Stop();
  timers_.erase(timer_id);
}

void RewardsServiceImpl::OnResetState(
  ledger::OnResetCallback callback, bool success) {
  if (!Connected())
    return;
  callback(success ? ledger::Result::LEDGER_OK : ledger::Result::LEDGER_ERROR);
}

void RewardsServiceImpl::GetPublisherMinVisitTime(
    const GetPublisherMinVisitTimeCallback& callback) {
  if (!Connected()) {
    return;
  }

  bat_ledger_->GetPublisherMinVisitTime(callback);
}

void RewardsServiceImpl::SetPublisherMinVisitTime(
    uint64_t duration_in_seconds) const {
  if (!Connected()) {
    return;
  }

  bat_ledger_->SetPublisherMinVisitTime(duration_in_seconds);
}

void RewardsServiceImpl::GetPublisherMinVisits(
    const GetPublisherMinVisitsCallback& callback) {
  if (!Connected()) {
    return;
  }

  bat_ledger_->GetPublisherMinVisits(callback);
}

void RewardsServiceImpl::SetPublisherMinVisits(unsigned int visits) const {
  if (!Connected()) {
    return;
  }

  bat_ledger_->SetPublisherMinVisits(visits);
}

void RewardsServiceImpl::GetPublisherAllowNonVerified(
    const GetPublisherAllowNonVerifiedCallback& callback) {
  if (!Connected()) {
    return;
  }

  bat_ledger_->GetPublisherAllowNonVerified(callback);
}

void RewardsServiceImpl::SetPublisherAllowNonVerified(bool allow) const {
  if (!Connected()) {
    return;
  }

  bat_ledger_->SetPublisherAllowNonVerified(allow);
}

void RewardsServiceImpl::GetPublisherAllowVideos(
    const GetPublisherAllowVideosCallback& callback) {
  if (!Connected()) {
    return;
  }

  bat_ledger_->GetPublisherAllowVideos(callback);
}

void RewardsServiceImpl::SetPublisherAllowVideos(bool allow) const {
  if (!Connected()) {
    return;
  }

  bat_ledger_->SetPublisherAllowVideos(allow);
}

void RewardsServiceImpl::SetContributionAmount(const double amount) const {
  if (!Connected()) {
    return;
  }

  bat_ledger_->SetUserChangedContribution();
  bat_ledger_->SetContributionAmount(amount);
}

// TODO(brave): Remove me (and pure virtual definition)
// see https://github.com/brave/brave-core/commit/c4ef62c954a64fca18ae83ff8ffd611137323420#diff-aa3505dbf36b5d03d8ba0751e0c99904R385
// and https://github.com/brave-intl/bat-native-ledger/commit/27f3ceb471d61c84052737ff201fe18cb9a6af32#diff-e303122e010480b2226895b9470891a3R135
void RewardsServiceImpl::SetUserChangedContribution() const {
  if (!Connected()) {
    return;
  }

  bat_ledger_->SetUserChangedContribution();
}

void RewardsServiceImpl::GetAutoContribute(
    GetAutoContributeCallback callback) {
  if (!Connected()) {
    return;
  }

  bat_ledger_->GetAutoContribute(std::move(callback));
}

void RewardsServiceImpl::SetAutoContribute(bool enabled) const {
  if (!Connected()) {
    return;
  }

  bat_ledger_->SetAutoContribute(enabled);
}

void RewardsServiceImpl::TriggerOnRewardsMainEnabled(
    bool rewards_main_enabled) {
  for (auto& observer : observers_)
    observer.OnRewardsMainEnabled(this, rewards_main_enabled);
}

void RewardsServiceImpl::SavePublishersList(const std::string& publishers_list,
                                      ledger::LedgerCallbackHandler* handler) {
  base::ImportantFileWriter writer(
      publisher_list_path_, file_task_runner_);

  writer.RegisterOnNextWriteCallbacks(
      base::Closure(),
      base::Bind(
        &PostWriteCallback,
        base::Bind(&RewardsServiceImpl::OnPublishersListSaved, AsWeakPtr(),
            base::Unretained(handler)),
        base::SequencedTaskRunnerHandle::Get()));

  writer.WriteNow(std::make_unique<std::string>(publishers_list));
}

void RewardsServiceImpl::OnPublishersListSaved(
    ledger::LedgerCallbackHandler* handler,
    bool success) {
  if (!Connected()) {
    return;
  }

  handler->OnPublishersListSaved(success ? ledger::Result::LEDGER_OK
                                         : ledger::Result::LEDGER_ERROR);
}

void RewardsServiceImpl::SetTimer(uint64_t time_offset,
                                  uint32_t* timer_id) {
  if (next_timer_id_ == std::numeric_limits<uint32_t>::max())
    next_timer_id_ = 1;
  else
    ++next_timer_id_;

  *timer_id = next_timer_id_;

  timers_[next_timer_id_] = std::make_unique<base::OneShotTimer>();
  timers_[next_timer_id_]->Start(FROM_HERE,
      base::TimeDelta::FromSeconds(time_offset),
      base::BindOnce(
          &RewardsServiceImpl::OnTimer, AsWeakPtr(), next_timer_id_));
}

void RewardsServiceImpl::OnTimer(uint32_t timer_id) {
  if (!Connected()) {
    return;
  }

  timers_.erase(timer_id);
  bat_ledger_->OnTimer(timer_id);
}

void RewardsServiceImpl::LoadPublisherList(
    ledger::LedgerCallbackHandler* handler) {
  base::PostTaskAndReplyWithResult(file_task_runner_.get(), FROM_HERE,
      base::Bind(&LoadStateOnFileTaskRunner, publisher_list_path_),
      base::Bind(&RewardsServiceImpl::OnPublisherListLoaded,
          AsWeakPtr(), base::Unretained(handler)));
}

void RewardsServiceImpl::OnPublisherListLoaded(
    ledger::LedgerCallbackHandler* handler,
    const std::string& data) {
  if (!Connected()) {
    return;
  }

  handler->OnPublisherListLoaded(
      data.empty() ? ledger::Result::NO_PUBLISHER_LIST
                   : ledger::Result::LEDGER_OK,
      data);
}

void RewardsServiceImpl::OnGetAllBalanceReports(
    const GetAllBalanceReportsCallback& callback,
    const base::flat_map<std::string, std::string>& json_reports) {
  std::map<std::string, ledger::BalanceReportInfo> reports;
  for (auto const& report : json_reports) {
    ledger::BalanceReportInfo info;
    info.loadFromJson(report.second);
    reports[report.first] = info;
  }

  std::map<std::string, brave_rewards::BalanceReport> newReports;
  for (auto const& report : reports) {
    brave_rewards::BalanceReport newReport;
    const ledger::BalanceReportInfo oldReport = report.second;
    newReport.opening_balance = oldReport.opening_balance_;
    newReport.closing_balance = oldReport.closing_balance_;
    newReport.grants = oldReport.grants_;
    newReport.earning_from_ads = oldReport.earning_from_ads_;
    newReport.auto_contribute = oldReport.auto_contribute_;
    newReport.recurring_donation = oldReport.recurring_donation_;
    newReport.one_time_donation = oldReport.one_time_donation_;

    newReports[report.first] = newReport;
  }

  callback.Run(newReports);
}

void RewardsServiceImpl::GetAllBalanceReports(
    const GetAllBalanceReportsCallback& callback) {
  if (!Connected()) {
    return;
  }

  bat_ledger_->GetAllBalanceReports(
      base::BindOnce(&RewardsServiceImpl::OnGetAllBalanceReports,
        AsWeakPtr(), callback));
}

void RewardsServiceImpl::OnGetCurrentBalanceReport(
    bool success, const std::string& json_report) {
  ledger::BalanceReportInfo report;
  report.loadFromJson(json_report);

  if (success) {
    TriggerOnGetCurrentBalanceReport(report);
  }
}

void RewardsServiceImpl::GetCurrentBalanceReport() {
  auto now = base::Time::Now();
  if (!Connected()) {
    return;
  }

  bat_ledger_->GetBalanceReport(GetPublisherMonth(now), GetPublisherYear(now),
      base::BindOnce(&RewardsServiceImpl::OnGetCurrentBalanceReport,
        AsWeakPtr()));
}

void RewardsServiceImpl::IsWalletCreated(
    const IsWalletCreatedCallback& callback) {
  if (!Connected()) {
    callback.Run(false);
    return;
  }

  bat_ledger_->IsWalletCreated(callback);
}

void RewardsServiceImpl::GetPublisherActivityFromUrl(
    uint64_t windowId,
    const std::string& url,
    const std::string& favicon_url,
    const std::string& publisher_blob) {
  GURL parsedUrl(url);

  if (!parsedUrl.is_valid()) {
    return;
  }

  auto origin = parsedUrl.GetOrigin();
  std::string baseDomain =
      GetDomainAndRegistry(origin.host(), INCLUDE_PRIVATE_REGISTRIES);

  if (baseDomain == "") {
    ledger::PublisherInfoPtr info;
    OnPanelPublisherInfo(ledger::Result::NOT_FOUND, std::move(info), windowId);
    return;
  }

  if (!Connected())
    return;

  ledger::VisitDataPtr visit_data = ledger::VisitData::New();
  visit_data->domain = visit_data->name = baseDomain;
  visit_data->path = parsedUrl.PathForRequest();
  visit_data->url = origin.spec();
  visit_data->favicon_url = favicon_url;

  bat_ledger_->GetPublisherActivityFromUrl(
        windowId,
        std::move(visit_data),
        publisher_blob);
}

void RewardsServiceImpl::OnExcludedSitesChanged(
    const std::string& publisher_id,
    ledger::PUBLISHER_EXCLUDE exclude) {

  bool excluded = exclude == ledger::PUBLISHER_EXCLUDE::EXCLUDED;

  if (excluded) {
    DeleteActivityInfo(publisher_id);
  }

  for (auto& observer : observers_) {
    observer.OnExcludedSitesChanged(this, publisher_id, excluded);
  }
}

void RewardsServiceImpl::OnPanelPublisherInfo(
    ledger::Result result,
    ledger::PublisherInfoPtr info,
    uint64_t windowId) {
  if (result != ledger::Result::LEDGER_OK &&
      result != ledger::Result::NOT_FOUND) {
    return;
  }

  for (auto& observer : private_observers_)
    observer.OnPanelPublisherInfo(this,
                                  result,
                                  info.get(),
                                  windowId);
}

void RewardsServiceImpl::GetContributionAmount(
    const GetContributionAmountCallback& callback) {
  if (!Connected())
    return;

  bat_ledger_->GetContributionAmount(callback);
}

void RewardsServiceImpl::FetchFavIcon(const std::string& url,
                                      const std::string& favicon_key,
                                      ledger::FetchIconCallback callback) {
  GURL parsedUrl(url);

  if (!parsedUrl.is_valid()) {
    return;
  }

  std::vector<std::string>::iterator it;
  it = find(current_media_fetchers_.begin(),
      current_media_fetchers_.end(), url);
  if (it != current_media_fetchers_.end()) {
    LOG(WARNING) << "Already fetching favicon: " << url;
    return;
  }

  BitmapFetcherService* image_service =
      BitmapFetcherServiceFactory::GetForBrowserContext(profile_);
  if (image_service) {
    current_media_fetchers_.emplace_back(url);
    request_ids_.push_back(image_service->RequestImage(
          parsedUrl,
          // Image Service takes ownership of the observer
          new RewardsFetcherServiceObserver(
              favicon_key,
              parsedUrl,
              base::Bind(&RewardsServiceImpl::OnFetchFavIconCompleted,
                  base::Unretained(this), callback)),
          GetNetworkTrafficAnnotationTagForFaviconFetch()));
  }
}

void RewardsServiceImpl::OnFetchFavIconCompleted(
    ledger::FetchIconCallback callback,
    const std::string& favicon_key,
    const GURL& url,
    const BitmapFetcherService::RequestId& request_id,
    const SkBitmap& image) {
  GURL favicon_url(favicon_key);
  gfx::Image gfx_image = gfx::Image::CreateFrom1xBitmap(image);
  favicon::FaviconService* favicon_service =
          FaviconServiceFactory::GetForProfile(profile_,
              ServiceAccessType::EXPLICIT_ACCESS);
  favicon_service->SetOnDemandFavicons(
      favicon_url,
      url,
      favicon_base::IconType::kFavicon,
      gfx_image,
      base::BindOnce(&RewardsServiceImpl::OnSetOnDemandFaviconComplete,
          AsWeakPtr(), favicon_url.spec(), callback));

  std::vector<std::string>::iterator it_url;
  it_url = find(current_media_fetchers_.begin(),
      current_media_fetchers_.end(), url.spec());
  if (it_url != current_media_fetchers_.end()) {
    current_media_fetchers_.erase(it_url);
  }

  std::vector<BitmapFetcherService::RequestId>::iterator it_ids;
  it_ids = find(request_ids_.begin(), request_ids_.end(), request_id);
  if (it_ids != request_ids_.end()) {
    request_ids_.erase(it_ids);
  }
}

void RewardsServiceImpl::OnSetOnDemandFaviconComplete(
    const std::string& favicon_url,
    ledger::FetchIconCallback callback, bool success) {
  if (!Connected())
    return;

  callback(success, favicon_url);
}

void RewardsServiceImpl::GetPublisherBanner(
    const std::string& publisher_id,
    GetPublisherBannerCallback callback) {
  if (!Connected())
    return;

  bat_ledger_->GetPublisherBanner(publisher_id,
      base::BindOnce(&RewardsServiceImpl::OnPublisherBanner,
                     AsWeakPtr(),
                     std::move(callback)));
}

void RewardsServiceImpl::OnPublisherBanner(
    GetPublisherBannerCallback callback,
    ledger::PublisherBannerPtr banner) {
  std::unique_ptr<brave_rewards::PublisherBanner> new_banner;
  new_banner.reset(new brave_rewards::PublisherBanner());

  if (!banner) {
    std::move(callback).Run(nullptr);
    return;
  }

  new_banner->publisher_key = banner->publisher_key;
  new_banner->title = banner->title;
  new_banner->name = banner->name;
  new_banner->description = banner->description;
  new_banner->background = banner->background;
  new_banner->logo = banner->logo;
  new_banner->amounts = banner->amounts;
  new_banner->social = mojo::FlatMapToMap(banner->social);
  new_banner->provider = banner->provider;
  new_banner->verified = banner->verified;

  std::move(callback).Run(std::move(new_banner));
}

void RewardsServiceImpl::OnTipPublisherInfoSaved(ledger::Result result,
    ledger::PublisherInfoPtr info) {
}

void RewardsServiceImpl::OnTip(const std::string& publisher_key,
                               int amount,
                               bool recurring,
                               ledger::PublisherInfoPtr publisher_info) {
  if (recurring) {
    // TODO(nejczdovc): this needs to be wired through ledger code
    // If caller provided publisher info, save it to `publisher_info` table
    if (publisher_info) {
      SavePublisherInfo(std::move(publisher_info),
        std::bind(&RewardsServiceImpl::OnTipPublisherInfoSaved,
            this, _1, _2));
    }

    SaveRecurringTip(publisher_key, amount);
    return;
  }

  if (!Connected())
    return;

  bat_ledger_->DoDirectTip(publisher_key, amount, "BAT");
}

bool SaveContributionInfoOnFileTaskRunner(
    const brave_rewards::ContributionInfo info,
  PublisherInfoDatabase* backend) {
  if (backend && backend->InsertContributionInfo(info))
    return true;

  return false;
}

void RewardsServiceImpl::OnContributionInfoSaved(
    const ledger::REWARDS_CATEGORY category,
    bool success) {
  for (auto& observer : observers_) {
    observer.OnContributionSaved(this, success, category);
  }
}

void RewardsServiceImpl::SaveContributionInfo(const std::string& probi,
  const int month,
  const int year,
  const uint32_t date,
  const std::string& publisher_key,
  const ledger::REWARDS_CATEGORY category) {
  brave_rewards::ContributionInfo info;
  info.probi = probi;
  info.month = month;
  info.year = year;
  info.date = date;
  info.publisher_key = publisher_key;
  info.category = category;

  base::PostTaskAndReplyWithResult(file_task_runner_.get(), FROM_HERE,
      base::Bind(&SaveContributionInfoOnFileTaskRunner,
                    info,
                    publisher_info_backend_.get()),
      base::Bind(&RewardsServiceImpl::OnContributionInfoSaved,
                     AsWeakPtr(),
                     category));
}

bool SaveRecurringTipOnFileTaskRunner(
    const brave_rewards::RecurringDonation info,
  PublisherInfoDatabase* backend) {
  if (backend && backend->InsertOrUpdateRecurringTip(info))
    return true;

  return false;
}

void RewardsServiceImpl::OnRecurringTipSaved(bool success) {
  for (auto& observer : observers_) {
    observer.OnRecurringTipSaved(this, success);
  }
}

void RewardsServiceImpl::SaveRecurringTip(
    const std::string& publisher_key, const int amount) {
  brave_rewards::RecurringDonation info;
  info.publisher_key = publisher_key;
  info.amount = amount;
  info.added_date = GetCurrentTimestamp();

  base::PostTaskAndReplyWithResult(file_task_runner_.get(), FROM_HERE,
      base::Bind(&SaveRecurringTipOnFileTaskRunner,
                    info,
                    publisher_info_backend_.get()),
      base::Bind(&RewardsServiceImpl::OnRecurringTipSaved,
                     AsWeakPtr()));
}

void RewardsServiceImpl::OnTwitterPublisherInfoSaved(
    SaveMediaInfoCallback callback,
    int32_t result,
    ledger::PublisherInfoPtr publisher) {
  if (!Connected()) {
    std::move(callback).Run(nullptr);
    return;
  }
  if (Connected()) {
    ledger::Result result_converted = static_cast<ledger::Result>(result);
    std::unique_ptr<brave_rewards::ContentSite> site;

    if (result_converted == ledger::Result::LEDGER_OK) {
      site = std::make_unique<brave_rewards::ContentSite>(
          PublisherInfoToContentSite(*publisher));
    }

    std::move(callback).Run(std::move(site));
  }
}

void RewardsServiceImpl::SaveTwitterPublisherInfo(
      const std::map<std::string, std::string>& args,
    SaveMediaInfoCallback callback) {
  bat_ledger_->SaveMediaInfo(
      "twitter",
      mojo::MapToFlatMap(args),
      base::BindOnce(&RewardsServiceImpl::OnTwitterPublisherInfoSaved,
                     AsWeakPtr(),
                     std::move(callback)));
}

void RewardsServiceImpl::OnRedditPublisherInfoSaved(
    SaveMediaInfoCallback callback,
    int32_t result,
    ledger::PublisherInfoPtr publisher) {
  if (!Connected()) {
    std::move(callback).Run(nullptr);
    return;
  }

  ledger::Result result_converted = static_cast<ledger::Result>(result);
  std::unique_ptr<brave_rewards::ContentSite> site;

  if (result_converted == ledger::Result::LEDGER_OK) {
    site = std::make_unique<brave_rewards::ContentSite>(
        PublisherInfoToContentSite(*publisher));
  }
  std::move(callback).Run(std::move(site));
}

void RewardsServiceImpl::SaveRedditPublisherInfo(
    const std::map<std::string, std::string>& args,
    SaveMediaInfoCallback callback) {
  bat_ledger_->SaveMediaInfo(
      "reddit",
      mojo::MapToFlatMap(args),
      base::BindOnce(&RewardsServiceImpl::OnRedditPublisherInfoSaved,
                    AsWeakPtr(),
                    std::move(callback)));
}

ledger::PublisherInfoList GetRecurringTipsOnFileTaskRunner(
    PublisherInfoDatabase* backend) {
  ledger::PublisherInfoList list;
  if (!backend) {
    return list;
  }

  backend->GetRecurringTips(&list);

  return list;
}

void RewardsServiceImpl::OnGetRecurringTipsUI(
    GetRecurringTipsCallback callback,
    ledger::PublisherInfoList list) {
    std::unique_ptr<brave_rewards::ContentSiteList> new_list(
      new brave_rewards::ContentSiteList);

  for (auto& publisher : list) {
    brave_rewards::ContentSite site = PublisherInfoToContentSite(*publisher);
    site.percentage = publisher->weight;
    new_list->push_back(site);
  }

  std::move(callback).Run(std::move(new_list));
}

void RewardsServiceImpl::GetRecurringTipsUI(
    GetRecurringTipsCallback callback) {
  bat_ledger_->GetRecurringTips(
      base::BindOnce(&RewardsServiceImpl::OnGetRecurringTipsUI,
                     AsWeakPtr(),
                     std::move(callback)));
}

void RewardsServiceImpl::OnGetRecurringTips(
    const ledger::PublisherInfoListCallback callback,
    ledger::PublisherInfoList list) {
  if (!Connected()) {
    return;
  }

  callback(std::move(list), 0);
}

void RewardsServiceImpl::GetRecurringTips(
    ledger::PublisherInfoListCallback callback) {
  base::PostTaskAndReplyWithResult(file_task_runner_.get(), FROM_HERE,
      base::Bind(&GetRecurringTipsOnFileTaskRunner,
                 publisher_info_backend_.get()),
      base::Bind(&RewardsServiceImpl::OnGetRecurringTips,
                 AsWeakPtr(),
                 callback));
}

void RewardsServiceImpl::OnGetOneTimeTipsUI(
    GetRecurringTipsCallback callback,
    ledger::PublisherInfoList list) {
    std::unique_ptr<brave_rewards::ContentSiteList> new_list(
      new brave_rewards::ContentSiteList);

  for (auto& publisher : list) {
    brave_rewards::ContentSite site = PublisherInfoToContentSite(*publisher);
    site.percentage = publisher->weight;
    new_list->push_back(site);
  }

  std::move(callback).Run(std::move(new_list));
}

void RewardsServiceImpl::GetOneTimeTipsUI(GetOneTimeTipsCallback callback) {
  bat_ledger_->GetOneTimeTips(
      base::BindOnce(&RewardsServiceImpl::OnGetOneTimeTipsUI,
                     AsWeakPtr(),
                     std::move(callback)));
}

ledger::PublisherInfoList GetOneTimeTipsOnFileTaskRunner(
    PublisherInfoDatabase* backend) {
  ledger::PublisherInfoList list;
  if (!backend) {
    return list;
  }

  auto now = base::Time::Now();
  backend->GetOneTimeTips(&list, GetPublisherMonth(now), GetPublisherYear(now));

  return list;
}

void RewardsServiceImpl::GetOneTimeTips(
    ledger::PublisherInfoListCallback callback) {
  base::PostTaskAndReplyWithResult(file_task_runner_.get(), FROM_HERE,
      base::Bind(&GetOneTimeTipsOnFileTaskRunner,
                 publisher_info_backend_.get()),
      base::Bind(&RewardsServiceImpl::OnGetOneTimeTips,
                 AsWeakPtr(),
                 callback));
}

void RewardsServiceImpl::OnGetOneTimeTips(
    ledger::PublisherInfoListCallback callback,
    ledger::PublisherInfoList list) {
  if (!Connected()) {
    return;
  }

  callback(std::move(list), 0);
}

void RewardsServiceImpl::RemoveRecurringTip(const std::string& publisher_key) {
  if (!Connected())
    return;

  bat_ledger_->RemoveRecurringTip(publisher_key);
}

bool RemoveRecurringTipOnFileTaskRunner(
    const std::string& publisher_key, PublisherInfoDatabase* backend) {
  if (!backend) {
    return false;
  }

  return backend->RemoveRecurringTip(publisher_key);
}

void RewardsServiceImpl::OnRemovedRecurringTip(
    ledger::RecurringRemoveCallback callback, bool success) {
  if (!Connected()) {
    callback(success ?
        ledger::Result::LEDGER_OK : ledger::Result::LEDGER_ERROR);
  }

  for (auto& observer : observers_) {
    observer.OnRecurringTipRemoved(this, success);
  }
}

void RewardsServiceImpl::OnRemoveRecurring(const std::string& publisher_key,
    ledger::RecurringRemoveCallback callback) {
  base::PostTaskAndReplyWithResult(file_task_runner_.get(), FROM_HERE,
      base::Bind(&RemoveRecurringTipOnFileTaskRunner,
                    publisher_key,
                    publisher_info_backend_.get()),
      base::Bind(&RewardsServiceImpl::OnRemovedRecurringTip,
                     AsWeakPtr(), callback));
}

void RewardsServiceImpl::TriggerOnGetCurrentBalanceReport(
    const ledger::BalanceReportInfo& report) {
  for (auto& observer : private_observers_) {
    brave_rewards::BalanceReport balance_report;
    balance_report.opening_balance = report.opening_balance_;
    balance_report.closing_balance = report.closing_balance_;
    balance_report.grants = report.grants_;
    balance_report.earning_from_ads = report.earning_from_ads_;
    balance_report.auto_contribute = report.auto_contribute_;
    balance_report.recurring_donation = report.recurring_donation_;
    balance_report.one_time_donation = report.one_time_donation_;
    observer.OnGetCurrentBalanceReport(this, balance_report);
  }
}

void RewardsServiceImpl::UpdateAdsRewards() const {
  if (!Connected()) {
    return;
  }

  bat_ledger_->UpdateAdsRewards();
}

void RewardsServiceImpl::SetContributionAutoInclude(
    const std::string& publisher_key,
    bool exclude) {
  if (!Connected())
    return;

  ledger::PUBLISHER_EXCLUDE status =
      exclude
      ? ledger::PUBLISHER_EXCLUDE::EXCLUDED
      : ledger::PUBLISHER_EXCLUDE::INCLUDED;

  bat_ledger_->SetPublisherExclude(publisher_key, status);
}

RewardsNotificationService* RewardsServiceImpl::GetNotificationService() const {
  return notification_service_.get();
}

void RewardsServiceImpl::StartNotificationTimers(bool main_enabled) {
  if (!main_enabled) return;

  // Startup timer, begins after 30-second delay.
  PrefService* pref_service = profile_->GetPrefs();
  notification_startup_timer_ = std::make_unique<base::OneShotTimer>();
  notification_startup_timer_->Start(
      FROM_HERE,
      pref_service->GetTimeDelta(
        prefs::kRewardsNotificationStartupDelay),
      this,
      &RewardsServiceImpl::OnNotificationTimerFired);
  DCHECK(notification_startup_timer_->IsRunning());

  // Periodic timer, runs once per day by default.
  base::TimeDelta periodic_timer_interval =
      pref_service->GetTimeDelta(prefs::kRewardsNotificationTimerInterval);
  notification_periodic_timer_ = std::make_unique<base::RepeatingTimer>();
  notification_periodic_timer_->Start(
      FROM_HERE, periodic_timer_interval, this,
      &RewardsServiceImpl::OnNotificationTimerFired);
  DCHECK(notification_periodic_timer_->IsRunning());
}

void RewardsServiceImpl::StopNotificationTimers() {
  notification_startup_timer_.reset();
  notification_periodic_timer_.reset();
}

void RewardsServiceImpl::OnNotificationTimerFired() {
  if (!Connected())
    return;

  bat_ledger_->GetBootStamp(
      base::BindOnce(&RewardsServiceImpl::MaybeShowBackupNotification,
        AsWeakPtr()));
  GetReconcileStamp(
      base::Bind(&RewardsServiceImpl::MaybeShowAddFundsNotification,
        AsWeakPtr()));
  FetchGrants(std::string(), std::string());
}

void RewardsServiceImpl::MaybeShowNotificationAddFunds() {
  bat_ledger_->HasSufficientBalanceToReconcile(
      base::BindOnce(&RewardsServiceImpl::ShowNotificationAddFunds,
        AsWeakPtr()));
}

void RewardsServiceImpl::MaybeShowNotificationAddFundsForTesting(
    base::OnceCallback<void(bool)> callback) {
  bat_ledger_->HasSufficientBalanceToReconcile(std::move(callback));
}

bool RewardsServiceImpl::ShouldShowNotificationAddFunds() const {
  base::Time next_time =
      profile_->GetPrefs()->GetTime(prefs::kRewardsAddFundsNotification);
  return (next_time.is_null() || base::Time::Now() > next_time);
}

void RewardsServiceImpl::ShowNotificationAddFunds(bool sufficient) {
  if (sufficient) return;

  base::Time next_time = base::Time::Now() + base::TimeDelta::FromDays(3);
  profile_->GetPrefs()->SetTime(prefs::kRewardsAddFundsNotification, next_time);
  RewardsNotificationService::RewardsNotificationArgs args;
  notification_service_->AddNotification(
      RewardsNotificationService::REWARDS_NOTIFICATION_INSUFFICIENT_FUNDS, args,
      "rewards_notification_insufficient_funds");
}

void RewardsServiceImpl::MaybeShowNotificationTipsPaid() {
  GetAutoContribute(base::BindOnce(
      &RewardsServiceImpl::ShowNotificationTipsPaid,
      AsWeakPtr()));
}

void RewardsServiceImpl::ShowNotificationTipsPaid(bool ac_enabled) {
  if (ac_enabled)
    return;

  RewardsNotificationService::RewardsNotificationArgs args;
  notification_service_->AddNotification(
      RewardsNotificationService::REWARDS_NOTIFICATION_TIPS_PROCESSED, args,
      "rewards_notification_tips_processed");
}

std::unique_ptr<ledger::LogStream> RewardsServiceImpl::Log(
    const char* file,
    int line,
    const ledger::LogLevel log_level) const {
  return std::make_unique<LogStreamImpl>(file, line, log_level);
}

std::unique_ptr<ledger::LogStream> RewardsServiceImpl::VerboseLog(
                     const char* file,
                     int line,
                     int log_level) const {
  return std::make_unique<LogStreamImpl>(file, line, log_level);
}

// static
void RewardsServiceImpl::HandleFlags(const std::string& options) {
  std::vector<std::string> flags = base::SplitString(
      options, ",", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  for (const auto& flag : flags) {
    if (flag.empty()) {
      continue;
    }

    std::vector<std::string> values = base::SplitString(
      flag, "=", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

    if (values.size() != 2) {
      continue;
    }

    std::string name = base::ToLowerASCII(values[0]);
    std::string value = values[1];

    if (value.empty()) {
      continue;
    }

    if (name == "staging") {
      bool is_production;
      std::string lower = base::ToLowerASCII(value);

      if (lower == "true" || lower == "1") {
        is_production = false;
      } else {
        is_production = true;
      }

      SetProduction(is_production);
      continue;
    }

    if (name == "debug") {
      bool is_debug;
      std::string lower = base::ToLowerASCII(value);

      if (lower == "true" || lower == "1") {
        is_debug = true;
      } else {
        is_debug = false;
      }

      SetDebug(is_debug);
      continue;
    }

    if (name == "reconcile-interval") {
      int reconcile_int;
      bool success = base::StringToInt(value, &reconcile_int);

      if (success && reconcile_int > 0) {
        SetReconcileTime(reconcile_int);
      }

      continue;
    }

    if (name == "short-retries") {
      std::string lower = base::ToLowerASCII(value);
      bool short_retries;

      if (lower == "true" || lower == "1") {
        short_retries = true;
      } else {
        short_retries = false;
      }

      SetShortRetries(short_retries);
    }

    if (name == "current-country") {
      const std::string& current_country_(base::ToUpperASCII(value));
      if (!current_country_.empty() && current_country_.size() == 2) {
        SetCurrentCountry(current_country_);
      }
    }
  }
}

bool RewardsServiceImpl::CheckImported() {
  PrefService* prefs = profile_->GetOriginalProfile()->GetPrefs();
  const int pinned_item_count = prefs->GetInteger(
      kBravePaymentsPinnedItemCount);
  if (pinned_item_count > 0) {
    prefs->SetInteger(kBravePaymentsPinnedItemCount, 0);
  }

  return pinned_item_count > 0;
}

void RewardsServiceImpl::SetBackupCompleted() {
  profile_->GetPrefs()->SetBoolean(prefs::kRewardsBackupSucceeded, true);
}

void RewardsServiceImpl::GetRewardsInternalsInfo(
    GetRewardsInternalsInfoCallback callback) {
  bat_ledger_->GetRewardsInternalsInfo(
      base::BindOnce(&RewardsServiceImpl::OnGetRewardsInternalsInfo,
                     AsWeakPtr(), std::move(callback)));
}

void RewardsServiceImpl::OnTip(
    const std::string& publisher_key,
    int amount,
    bool recurring) {
  OnTip(publisher_key, amount, recurring,
      static_cast<ledger::PublisherInfoPtr>(nullptr));
}

void RewardsServiceImpl::OnTip(
    const std::string& publisher_key,
    int amount,
    bool recurring,
    std::unique_ptr<brave_rewards::ContentSite> site) {

  if (!site) {
    return;
  }

  ledger::PublisherInfoPtr info;
  info->id = publisher_key;
  info->verified = site->verified;
  info->excluded = ledger::PUBLISHER_EXCLUDE::DEFAULT;
  info->name = site->name;
  info->url = site->url;
  info->provider = site->provider;
  info->favicon_url = site->favicon_url;

  OnTip(publisher_key, amount, recurring, std::move(info));
}

bool RewardsServiceImpl::Connected() const {
  return bat_ledger_.is_bound();
}

void RewardsServiceImpl::SetLedgerEnvForTesting() {
  bat_ledger_service_->SetTesting();

  SetPublisherMinVisitTime(1);

  // this is needed because we are using braveledger_bat_helper::buildURL
  // directly in BraveRewardsBrowserTest
  #if defined(OFFICIAL_BUILD)
  ledger::is_production = true;
  #else
  ledger::is_production = false;
  #endif
}

void RewardsServiceImpl::StartMonthlyContributionForTest() {
  bat_ledger_->StartMonthlyContribution();
}

void RewardsServiceImpl::CheckInsufficientFundsForTesting() {
  MaybeShowNotificationAddFunds();
}

void RewardsServiceImpl::GetProduction(const GetProductionCallback& callback) {
  bat_ledger_service_->GetProduction(callback);
}

void RewardsServiceImpl::GetDebug(const GetDebugCallback& callback) {
  bat_ledger_service_->GetDebug(callback);
}

void RewardsServiceImpl::GetReconcileTime(
    const GetReconcileTimeCallback& callback) {
  bat_ledger_service_->GetReconcileTime(callback);
}

void RewardsServiceImpl::GetShortRetries(
    const GetShortRetriesCallback& callback) {
  bat_ledger_service_->GetShortRetries(callback);
}

void RewardsServiceImpl::SetProduction(bool production) {
  bat_ledger_service_->SetProduction(production);
}

void RewardsServiceImpl::SetDebug(bool debug) {
  bat_ledger_service_->SetDebug(debug);
}

void RewardsServiceImpl::SetReconcileTime(int32_t time) {
  bat_ledger_service_->SetReconcileTime(time);
}

void RewardsServiceImpl::SetShortRetries(bool short_retries) {
  bat_ledger_service_->SetShortRetries(short_retries);
}

void RewardsServiceImpl::SetCurrentCountry(
    const std::string& current_country) {
  current_country_for_test_ = current_country;
}

ledger::Result SavePendingContributionOnFileTaskRunner(
    PublisherInfoDatabase* backend,
    const ledger::PendingContributionList& list) {
  if (!backend) {
    return ledger::Result::LEDGER_ERROR;
  }

  bool result = backend->InsertPendingContribution(list);

  return result ? ledger::Result::LEDGER_OK : ledger::Result::LEDGER_ERROR;
}

void RewardsServiceImpl::OnSavePendingContribution(ledger::Result result) {
  for (auto& observer : observers_)
    observer.OnPendingContributionSaved(this, result);
}

void RewardsServiceImpl::SavePendingContribution(
      ledger::PendingContributionList list) {
  base::PostTaskAndReplyWithResult(
      file_task_runner_.get(),
      FROM_HERE,
      base::Bind(&SavePendingContributionOnFileTaskRunner,
                 publisher_info_backend_.get(),
                 std::move(list)),
      base::Bind(&RewardsServiceImpl::OnSavePendingContribution,
                 AsWeakPtr()));
}

void RewardsServiceImpl::GetPendingContributionsTotalUI(
    const GetPendingContributionsTotalCallback& callback) {
  bat_ledger_->GetPendingContributionsTotal(std::move(callback));
}

double PendingContributionsTotalOnFileTaskRunner(
    PublisherInfoDatabase* backend) {
  if (!backend) {
    return 0.0;
  }

  return backend->GetReservedAmount();
}

void RewardsServiceImpl::OnGetPendingContributionsTotal(
    const ledger::PendingContributionsTotalCallback& callback,
    double amount) {
  if (!Connected()) {
    callback(0.0);
    return;
  }

  callback(amount);
}

void RewardsServiceImpl::GetPendingContributionsTotal(
    const ledger::PendingContributionsTotalCallback& callback) {
  base::PostTaskAndReplyWithResult(
      file_task_runner_.get(),
      FROM_HERE,
      base::Bind(&PendingContributionsTotalOnFileTaskRunner,
                 publisher_info_backend_.get()),
      base::Bind(&RewardsServiceImpl::OnGetPendingContributionsTotal,
                 AsWeakPtr(),
                 callback));
}

bool RestorePublisherOnFileTaskRunner(PublisherInfoDatabase* backend) {
  if (!backend) {
    return false;
  }

  return backend->RestorePublishers();
}

void RewardsServiceImpl::OnRestorePublishers(
    ledger::OnRestoreCallback callback) {
  base::PostTaskAndReplyWithResult(
      file_task_runner_.get(),
      FROM_HERE,
      base::Bind(&RestorePublisherOnFileTaskRunner,
                 publisher_info_backend_.get()),
      base::Bind(&RewardsServiceImpl::OnRestorePublishersInternal,
                 AsWeakPtr(),
                 callback));
}

void RewardsServiceImpl::OnRestorePublishersInternal(
    ledger::OnRestoreCallback callback,
    bool result) {
  callback(result);
}

bool SaveNormalizedPublisherListOnFileTaskRunner(
    PublisherInfoDatabase* backend,
    ledger::PublisherInfoList list) {
  if (!backend) {
    return false;
  }

  return backend->InsertOrUpdateActivityInfos(list);
}

void RewardsServiceImpl::SaveNormalizedPublisherList(
    ledger::PublisherInfoList list) {
  ContentSiteList site_list;
  for (const auto& publisher : list) {
    if (publisher->percent >= 1) {
      site_list.push_back(PublisherInfoToContentSite(*publisher));
    }
  }
  // TODO(bridiver) - this doesn't belong here
  std::sort(site_list.begin(), site_list.end());

  base::PostTaskAndReplyWithResult(
      file_task_runner_.get(),
      FROM_HERE,
      base::BindOnce(&SaveNormalizedPublisherListOnFileTaskRunner,
                     publisher_info_backend_.get(),
                     std::move(list)),
      base::BindOnce(&RewardsServiceImpl::OnPublisherListNormalizedSaved,
                     AsWeakPtr(),
                     std::move(site_list)));
}

void RewardsServiceImpl::OnPublisherListNormalizedSaved(
    ContentSiteList site_list,
    bool success) {
  if (!success) {
    LOG(ERROR) << "Problem saving normalized publishers "
                  "in SaveNormalizedPublisherList";
    return;
  }

  for (auto& observer : observers_) {
    observer.OnPublisherListNormalized(this, site_list);
  }
}

void RewardsServiceImpl::GetAddressesForPaymentId(
    const GetAddressesCallback& callback) {
  if (!Connected()) {
    return;
  }

  bat_ledger_->GetAddressesForPaymentId(
      base::BindOnce(&RewardsServiceImpl::OnGetAddresses,
                     AsWeakPtr(),
                     callback));
}

bool DeleteActivityInfoOnFileTaskRunner(PublisherInfoDatabase* backend,
                                        const std::string& publisher_key,
                                        uint64_t reconcile_stamp) {
  if (backend &&
      backend->DeleteActivityInfo(publisher_key, reconcile_stamp)) {
    return true;
  }

  return false;
}

void RewardsServiceImpl::DeleteActivityInfo(const std::string& publisher_key) {
  GetReconcileStamp(
      base::Bind(&RewardsServiceImpl::OnDeleteActivityInfoStamp,
                 AsWeakPtr(),
                 publisher_key));
}

void RewardsServiceImpl::OnDeleteActivityInfoStamp(
    const std::string& publisher_key,
    uint64_t reconcile_stamp) {
  base::PostTaskAndReplyWithResult(
      file_task_runner_.get(),
      FROM_HERE,
      base::Bind(&DeleteActivityInfoOnFileTaskRunner,
                 publisher_info_backend_.get(),
                 publisher_key,
                 reconcile_stamp),
      base::Bind(&RewardsServiceImpl::OnDeleteActivityInfo,
                 AsWeakPtr(),
                 publisher_key));
}

void RewardsServiceImpl::OnDeleteActivityInfo(
    const std::string& publisher_key,
    bool result) {
  if (!result) {
    LOG(ERROR) << "Problem deleting activity info for "
               << publisher_key;
  }
}

void RewardsServiceImpl::RefreshPublisher(
    const std::string& publisher_key,
    RefreshPublisherCallback callback) {
  if (!Connected()) {
    std::move(callback).Run(false, std::string());
    return;
  }
  bat_ledger_->RefreshPublisher(
      publisher_key,
      base::BindOnce(&RewardsServiceImpl::OnRefreshPublisher,
        AsWeakPtr(), std::move(callback), publisher_key));
}

void RewardsServiceImpl::OnRefreshPublisher(
    RefreshPublisherCallback callback,
    const std::string& publisher_key,
    bool verified) {
  std::move(callback).Run(verified, publisher_key);
}

const RewardsNotificationService::RewardsNotificationsMap&
RewardsServiceImpl::GetAllNotifications() {
  return notification_service_->GetAllNotifications();
}

void RewardsServiceImpl::SetInlineTipSetting(const std::string& key,
                                             bool enabled) {
  bat_ledger_->SetInlineTipSetting(key, enabled);
}

void RewardsServiceImpl::GetInlineTipSetting(
      const std::string& key,
      GetInlineTipSettingCallback callback) {
  bat_ledger_->GetInlineTipSetting(
      key,
      base::BindOnce(&RewardsServiceImpl::OnInlineTipSetting,
          AsWeakPtr(),
          std::move(callback)));
}

void RewardsServiceImpl::OnInlineTipSetting(
    GetInlineTipSettingCallback callback,
    bool enabled) {
  std::move(callback).Run(enabled);
}

void RewardsServiceImpl::GetShareURL(
      const std::string& type,
      const std::map<std::string, std::string>& args,
      GetShareURLCallback callback) {
  bat_ledger_->GetShareURL(
      type,
      mojo::MapToFlatMap(args),
      base::BindOnce(&RewardsServiceImpl::OnShareURL,
          AsWeakPtr(),
          std::move(callback)));
}

void RewardsServiceImpl::OnShareURL(
    GetShareURLCallback callback,
    const std::string& url) {
  std::move(callback).Run(url);
}

ledger::PendingContributionInfoList PendingContributionsOnFileTaskRunner(
    PublisherInfoDatabase* backend) {
  ledger::PendingContributionInfoList list;
  if (!backend) {
    return list;
  }

  backend->GetPendingContributions(&list);

  return list;
}

PendingContributionInfo PendingContributionLedgerToRewards(
    const ledger::PendingContributionInfoPtr contribution) {
  PendingContributionInfo info;
  info.publisher_key = contribution->publisher_key;
  info.category = contribution->category;
  info.verified = contribution->verified;
  info.name = contribution->name;
  info.url = contribution->url;
  info.provider = contribution->provider;
  info.favicon_url = contribution->favicon_url;
  info.amount = contribution->amount;
  info.added_date = contribution->added_date;
  info.viewing_id = contribution->viewing_id;
  info.expiration_date = contribution->expiration_date;
  return info;
}

void RewardsServiceImpl::OnGetPendingContributionsUI(
    GetPendingContributionsCallback callback,
    ledger::PendingContributionInfoList list) {
  std::unique_ptr<brave_rewards::PendingContributionInfoList> new_list(
      new brave_rewards::PendingContributionInfoList);
  for (auto &item : list) {
    brave_rewards::PendingContributionInfo new_contribution =
        PendingContributionLedgerToRewards(std::move(item));
    new_list->push_back(new_contribution);
  }

  std::move(callback).Run(std::move(new_list));
}

void RewardsServiceImpl::GetPendingContributionsUI(
    GetPendingContributionsCallback callback) {
  bat_ledger_->GetPendingContributions(
      base::BindOnce(&RewardsServiceImpl::OnGetPendingContributionsUI,
                     AsWeakPtr(),
                     std::move(callback)));
}

void RewardsServiceImpl::OnGetPendingContributions(
    const ledger::PendingContributionInfoListCallback& callback,
    ledger::PendingContributionInfoList list) {
  if (!Connected()) {
    return;
  }

  callback(std::move(list));
}

void RewardsServiceImpl::GetPendingContributions(
    const ledger::PendingContributionInfoListCallback& callback) {
  base::PostTaskAndReplyWithResult(
      file_task_runner_.get(),
      FROM_HERE,
      base::Bind(&PendingContributionsOnFileTaskRunner,
                 publisher_info_backend_.get()),
      base::Bind(&RewardsServiceImpl::OnGetPendingContributions,
                 AsWeakPtr(),
                 callback));
}

void RewardsServiceImpl::OnPendingContributionRemovedUI(int32_t result) {
  for (auto& observer : observers_) {
    observer.OnPendingContributionRemoved(this, result);
  }
}

void RewardsServiceImpl::RemovePendingContributionUI(
    const std::string& publisher_key,
    const std::string& viewing_id,
    uint64_t added_date) {
  bat_ledger_->RemovePendingContribution(
      publisher_key,
      viewing_id,
      added_date,
      base::BindOnce(&RewardsServiceImpl::OnPendingContributionRemovedUI,
                     AsWeakPtr()));
}

bool RemovePendingContributionOnFileTaskRunner(
    PublisherInfoDatabase* backend,
    const std::string& publisher_key,
    const std::string& viewing_id,
    uint64_t added_date) {
  if (!backend) {
    return false;
  }

  return backend->RemovePendingContributions(publisher_key,
                                             viewing_id,
                                             added_date);
}

void RewardsServiceImpl::RemovePendingContribution(
    const std::string& publisher_key,
    const std::string& viewing_id,
    uint64_t added_date,
    const ledger::RemovePendingContributionCallback& callback) {
  base::PostTaskAndReplyWithResult(
      file_task_runner_.get(),
      FROM_HERE,
      base::Bind(&RemovePendingContributionOnFileTaskRunner,
                 publisher_info_backend_.get(),
                 publisher_key,
                 viewing_id,
                 added_date),
      base::Bind(&RewardsServiceImpl::OnPendingContributionRemoved,
                 AsWeakPtr(),
                 callback));
}

void RewardsServiceImpl::OnPendingContributionRemoved(
    ledger::RemovePendingContributionCallback callback,
    bool result) {
  ledger::Result result_new = result
      ? ledger::Result::LEDGER_OK
      : ledger::Result::LEDGER_ERROR;

  callback(result_new);
}

bool RemoveAllPendingContributionOnFileTaskRunner(
    PublisherInfoDatabase* backend) {
  if (!backend) {
    return false;
  }

  return backend->RemoveAllPendingContributions();
}

void RewardsServiceImpl::OnRemoveAllPendingContributionsUI(int32_t result) {
  for (auto& observer : observers_) {
    observer.OnPendingContributionRemoved(this, result);
  }
}

void RewardsServiceImpl::RemoveAllPendingContributionsUI() {
  bat_ledger_->RemoveAllPendingContributions(
      base::BindOnce(&RewardsServiceImpl::OnRemoveAllPendingContributionsUI,
                     AsWeakPtr()));
}

void RewardsServiceImpl::OnRemoveAllPendingContribution(
    ledger::RemovePendingContributionCallback callback,
    bool result) {
  ledger::Result result_new = result
      ? ledger::Result::LEDGER_OK
      : ledger::Result::LEDGER_ERROR;

  callback(result_new);
}

void RewardsServiceImpl::RemoveAllPendingContributions(
    const ledger::RemovePendingContributionCallback& callback) {
  base::PostTaskAndReplyWithResult(
      file_task_runner_.get(),
      FROM_HERE,
      base::Bind(&RemoveAllPendingContributionOnFileTaskRunner,
                 publisher_info_backend_.get()),
      base::Bind(&RewardsServiceImpl::OnRemoveAllPendingContribution,
                 AsWeakPtr(),
                 callback));
}

void RewardsServiceImpl::GetCountryCodes(
    const std::vector<std::string>& countries,
    ledger::GetCountryCodesCallback callback) {
  std::vector<std::int32_t> country_codes;
  for (const auto& country : countries) {
    country_codes.push_back(country_codes::CountryCharsToCountryID(
        country.at(0), country.at(1)));
  }
  callback(country_codes);
}

void RewardsServiceImpl::OnContributeUnverifiedPublishers(
      ledger::Result result,
      const std::string& publisher_key,
      const std::string& publisher_name) {
  switch (result) {
    case ledger::Result::PENDING_NOT_ENOUGH_FUNDS:
    {
      RewardsNotificationService::RewardsNotificationArgs args;
      notification_service_->AddNotification(
          RewardsNotificationService::
          REWARDS_NOTIFICATION_PENDING_NOT_ENOUGH_FUNDS,
          args,
          "rewards_notification_not_enough_funds");
      break;
    }
    case ledger::Result::PENDING_PUBLISHER_REMOVED:
    {
      for (auto& observer : observers_) {
        observer.OnPendingContributionRemoved(this, ledger::Result::LEDGER_OK);
      }
      break;
    }
    case ledger::Result::VERIFIED_PUBLISHER:
    {
      RewardsNotificationService::RewardsNotificationArgs args;
      args.push_back(publisher_name);
      notification_service_->AddNotification(
          RewardsNotificationService::REWARDS_NOTIFICATION_VERIFIED_PUBLISHER,
          args,
          "rewards_notification_verified_publisher_" + publisher_key);
      break;
    }
    default:
      break;
  }
}

void RewardsServiceImpl::OnFetchBalance(FetchBalanceCallback callback,
                                        int32_t result,
                                        ledger::BalancePtr balance) {
  auto new_balance = std::make_unique<brave_rewards::Balance>();

  if (balance) {
    new_balance->total = balance->total;
    new_balance->rates = mojo::FlatMapToMap(balance->rates);
    new_balance->wallets = mojo::FlatMapToMap(balance->wallets);

    if (balance->total > 0) {
      profile_->GetPrefs()->SetBoolean(prefs::kRewardsUserHasFunded, true);
    }
  }

  std::move(callback).Run(result, std::move(new_balance));
}

void RewardsServiceImpl::FetchBalance(FetchBalanceCallback callback) {
  bat_ledger_->FetchBalance(
      base::BindOnce(&RewardsServiceImpl::OnFetchBalance,
                     AsWeakPtr(),
                     std::move(callback)));
}

}  // namespace brave_rewards
