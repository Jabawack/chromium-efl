/*
   Copyright (C) 2014 Samsung Electronics

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#include "content_browser_client_efl.h"

#include "command_line_efl.h"
#include "browser_main_parts_efl.h"
#include "browser_context_efl.h"
#include "web_contents_delegate_efl.h"
#include "resource_dispatcher_host_delegate_efl.h"
#include "browser/web_contents/web_contents_view_efl.h"
#include "browser/geolocation/access_token_store_efl.h"
#include "browser/renderer_host/render_message_filter_efl.h"
#include "browser/vibration/vibration_message_filter.h"
#include "content/public/browser/resource_dispatcher_host.h"
#include "content/public/browser/web_contents.h"
#include "common/web_contents_utils.h"
#include "components/editing/content/browser/editor_client_observer.h"

#if defined(OS_TIZEN)
#include "browser/geolocation/location_provider_efl.h"
#endif

using web_contents_utils::WebContentsFromFrameID;
using web_contents_utils::WebContentsFromViewID;

namespace content {

ContentBrowserClientEfl::ContentBrowserClientEfl()
  : browser_main_parts_efl_(NULL) {
}

BrowserMainParts* ContentBrowserClientEfl::CreateBrowserMainParts(
    const MainFunctionParams& parameters) {
  browser_main_parts_efl_ = new BrowserMainPartsEfl();
  return browser_main_parts_efl_;
}

WebContentsView* ContentBrowserClientEfl::OverrideCreateWebContentsView(
    WebContents* web_contents,
    RenderViewHostDelegateView** delegate_view) {
  WebContentsViewEfl* view = new WebContentsViewEfl(web_contents);
  *delegate_view = view;
  return view;
}

net::URLRequestContextGetter* ContentBrowserClientEfl::CreateRequestContext(
    BrowserContext* browser_context, ProtocolHandlerMap* protocol_handlers) {
  if (browser_context->IsOffTheRecord()) {
    LOG(ERROR) << "off the record browser context not implemented";
    return NULL;
  }

  return static_cast<BrowserContextEfl*>(browser_context)->
      CreateRequestContext(protocol_handlers);
}

AccessTokenStore* ContentBrowserClientEfl::CreateAccessTokenStore() {
  return new AccessTokenStoreEfl();
}

#if defined(OS_TIZEN)
LocationProvider* ContentBrowserClientEfl::OverrideSystemLocationProvider() {
  return LocationProviderEfl::Create();
}
#endif

void ContentBrowserClientEfl::AppendExtraCommandLineSwitches(CommandLine* command_line,
    int child_process_id) {
  CommandLineEfl::AppendProcessSpecificArgs(*command_line);
}

void ContentBrowserClientEfl::ResourceDispatcherHostCreated() {
  ResourceDispatcherHost* host = ResourceDispatcherHost::Get();
  resource_disp_host_del_efl_.reset(new ResourceDispatcherHostDelegateEfl());
  host->SetDelegate(resource_disp_host_del_efl_.get());
}

void ContentBrowserClientEfl::AllowCertificateError(
    int render_process_id, int render_frame_id, int cert_error,
    const net::SSLInfo& ssl_info, const GURL& request_url,
    ResourceType::Type resource_type, bool overridable,
    bool strict_enforcement, const base::Callback<void(bool)>& callback,
    CertificateRequestResultType* result) {

  WebContents* web_contents = WebContentsFromFrameID(render_process_id, render_frame_id);
  if (!web_contents) {
    NOTREACHED();
    return;
  }
  WebContentsDelegate * delegate = web_contents->GetDelegate();
  if (!delegate) {
    callback.Run(NULL);
    return;
  }
  static_cast<content::WebContentsDelegateEfl*>(delegate)->
     RequestCertificateConfirm(web_contents, cert_error, ssl_info, request_url,
         resource_type, overridable, strict_enforcement, callback, result);
}

void ContentBrowserClientEfl::RequestDesktopNotificationPermission(
    const GURL& source_origin, int callback_context,
    int render_process_id, int render_view_id) {
#if defined(ENABLE_NOTIFICATIONS)
  WebContents* web_contents = WebContentsFromViewID(render_process_id,
                                                    render_view_id);
  if (!web_contents)
    return;

  WebContentsDelegateEfl* delegate =
       static_cast<WebContentsDelegateEfl*>(web_contents->GetDelegate());
  if (!delegate)
    return;

  BrowserContextEfl* browser_context =
      static_cast<BrowserContextEfl*>(web_contents->GetBrowserContext());
  Ewk_Notification_Permission_Request* notification_permission =
       new Ewk_Notification_Permission_Request(
         delegate->web_view()->evas_object(), callback_context, source_origin);

  if (browser_context->GetNotificationController()->
        IsDefaultAllowed(notification_permission->origin->host)) {
    browser_context->GetNotificationController()->
        SetPermissionForNotification(notification_permission, true);
    delete notification_permission;
  } else {
    delegate->web_view()->
      SmartCallback<EWebViewCallbacks::NotificationPermissionRequest>()
        .call(notification_permission);
  }
#else
  NOTIMPLEMENTED();
#endif
}

void ContentBrowserClientEfl::ShowDesktopNotification(
    const ShowDesktopNotificationHostMsgParams& params,
    int render_process_id, int render_view_id, bool /*worker*/) {
#if defined(ENABLE_NOTIFICATIONS)
  WebContents* web_contents = WebContentsFromViewID(render_process_id, render_view_id);
  if (!web_contents)
    return;

  WebContentsDelegateEfl* delegate =
      static_cast<WebContentsDelegateEfl*>(web_contents->GetDelegate());
  if (!delegate)
    return;

  BrowserContextEfl* browser_context =
      static_cast<BrowserContextEfl*>(web_contents->GetBrowserContext());
  uint64_t old_notification_id = 0;

  if (!params.replace_id.empty() && browser_context->GetNotificationController()->
      IsNotificationPresent(params.replace_id, old_notification_id))
    CancelDesktopNotification(render_process_id, render_view_id, old_notification_id);

  browser_context->GetNotificationController()->
      AddNotification(params.notification_id, render_process_id,
                      render_view_id, params.replace_id);
  Ewk_Notification* notification = new Ewk_Notification(params);
  delegate->web_view()->
      SmartCallback<EWebViewCallbacks::NotificationShow>().call(notification);
#else
  NOTIMPLEMENTED();
#endif
}

bool ContentBrowserClientEfl::AllowGetCookie(const GURL& url,
                                             const GURL& first_party,
                                             const net::CookieList& cookie_list,
                                             content::ResourceContext* context,
                                             int render_process_id,
                                             int render_frame_id) {
  if (!web_context_)
      return false;

  CookieManager* cookie_manager = web_context_->cookieManager();
  if (!cookie_manager)
    return false;

  return cookie_manager->AllowGetCookie(url,
                                        first_party,
                                        cookie_list,
                                        context,
                                        render_process_id,
                                        render_frame_id);
}

bool ContentBrowserClientEfl::AllowSetCookie(const GURL& url,
                                             const GURL& first_party,
                                             const std::string& cookie_line,
                                             content::ResourceContext* context,
                                             int render_process_id,
                                             int render_frame_id,
                                             net::CookieOptions* options) {
  if (!web_context_)
    return false;

  CookieManager* cookie_manager = web_context_->cookieManager();
  if (!cookie_manager)
    return false;

  return cookie_manager->AllowSetCookie(url,
                                        first_party,
                                        cookie_line,
                                        context,
                                        render_process_id,
                                        render_frame_id,
                                        options);
}

void ContentBrowserClientEfl::RenderProcessWillLaunch(
    content::RenderProcessHost* host) {

  BrowserContextEfl* browser_context = static_cast<BrowserContextEfl*>(
                                          host->GetBrowserContext());
  if (browser_context)
    web_context_ = browser_context->WebContext();

  host->AddFilter(new RenderMessageFilterEfl(host->GetID()));
  host->AddFilter(new VibrationMessageFilter());
  host->AddFilter(new editing::EditorClientObserver(host->GetID()));
}

}