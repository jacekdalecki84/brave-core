/* Copyright (c) 2019 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_BRAVE_CONTENT_BROWSER_CLIENT_H_
#define BRAVE_BROWSER_BRAVE_CONTENT_BROWSER_CLIENT_H_

#include <memory>
#include <string>
#include <vector>

#include "chrome/browser/chrome_content_browser_client.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/content_browser_client.h"

class PrefChangeRegistrar;

namespace content {
class BrowserContext;
}

class BraveContentBrowserClient : public ChromeContentBrowserClient {
 public:
  explicit BraveContentBrowserClient(StartupData* startup_data = nullptr);
  ~BraveContentBrowserClient() override;

  // Overridden from ChromeContentBrowserClient:
  std::unique_ptr<content::BrowserMainParts> CreateBrowserMainParts(
      const content::MainFunctionParams& parameters) override;
  void BrowserURLHandlerCreated(content::BrowserURLHandler* handler) override;
  bool AllowGetCookie(const GURL& url,
                      const GURL& first_party,
                      const net::CookieList& cookie_list,
                      content::ResourceContext* context,
                      int render_process_id,
                      int render_frame_id) override;
  bool AllowSetCookie(const GURL& url,
                      const GURL& first_party,
                      const net::CanonicalCookie& cookie,
                      content::ResourceContext* context,
                      int render_process_id,
                      int render_frame_id) override;

  bool HandleExternalProtocol(
      const GURL& url,
      content::ResourceRequestInfo::WebContentsGetter web_contents_getter,
      int child_id,
      content::NavigationUIData* navigation_data,
      bool is_main_frame,
      ui::PageTransition page_transition,
      bool has_user_gesture,
      network::mojom::URLLoaderFactoryRequest* factory_request,
      network::mojom::URLLoaderFactory*& out_factory) override;  // NOLINT

  content::ContentBrowserClient::AllowWebBluetoothResult AllowWebBluetooth(
      content::BrowserContext* browser_context,
      const url::Origin& requesting_origin,
      const url::Origin& embedding_origin) override;

  base::Optional<service_manager::Manifest> GetServiceManifestOverlay(
      base::StringPiece name) override;
  std::vector<service_manager::Manifest> GetExtraServiceManifests() override;

  void AdjustUtilityServiceProcessCommandLine(
      const service_manager::Identity& identity,
      base::CommandLine* command_line) override;

  void MaybeHideReferrer(content::BrowserContext* browser_context,
                         const GURL& request_url,
                         const GURL& document_url,
                         bool is_main_frame,
                         content::Referrer* referrer) override;

  GURL GetEffectiveURL(content::BrowserContext* browser_context,
                       const GURL& url) override;
  static bool HandleURLOverrideRewrite(GURL* url,
      content::BrowserContext* browser_context);
  std::vector<std::unique_ptr<content::NavigationThrottle>>
      CreateThrottlesForNavigation(content::NavigationHandle* handle) override;

 private:
  bool AllowAccessCookie(const GURL& url, const GURL& first_party,
      content::ResourceContext* context, int render_process_id,
      int render_frame_id);
  void OnAllowGoogleAuthChanged();

  std::unique_ptr<PrefChangeRegistrar, content::BrowserThread::DeleteOnUIThread>
      pref_change_registrar_;
  bool allow_google_auth_;

  DISALLOW_COPY_AND_ASSIGN(BraveContentBrowserClient);
};

#endif  // BRAVE_BROWSER_BRAVE_CONTENT_BROWSER_CLIENT_H_
