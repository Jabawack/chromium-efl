// Copyright 2013 Samsung Electronics. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>

#include "API/ewk_context_menu_private.h"
#include <Elementary.h>
#include "base/files/file_path.h"
#include "net/base/filename_util.h"
#include "components/clipboard/clipboard_helper_efl.h"
#include "context_menu_controller_efl.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/navigation_entry.h"
#include "eweb_view.h"
#include "net/base/net_util.h"
#include "third_party/WebKit/public/platform/WebReferrerPolicy.h"
#include "ui/base/clipboard/clipboard.h"

#include "tizen_webview/public/tw_webview.h"
#include "tizen_webview/public/tw_selection_controller.h"

// TODO: remove this header dependency
#include "selection_controller_efl.h"

#include "content/browser/frame_host/navigator.h"

using tizen_webview::WebView;
using tizen_webview::SelectionController;

namespace content {

ContextMenuControllerEfl::~ContextMenuControllerEfl() {
  if (popup_)
    evas_object_del(popup_);

  Eina_List *ittr;
  Eina_List *next;
  void *data;
  EINA_LIST_FOREACH_SAFE(menu_items_, ittr, next, data) {
    _Ewk_Context_Menu_Item *item = static_cast<_Ewk_Context_Menu_Item*> (data);
    ContextMenuItemEfl *context_item = item->menu_item_;
    menu_items_ =  eina_list_remove(menu_items_, item);
    delete context_item;
    delete item;
  }
}

bool ContextMenuControllerEfl::PopulateAndShowContextMenu(const ContextMenuParams &params) {
  EWebView* view = webview_->GetImpl();
  if (!view)
    return false;

  params_ = params;
  menu_items_ = NULL;
  GetProposedContextMenu();
  _Ewk_Context_Menu *new_menu = new _Ewk_Context_Menu();
  new_menu->menu_list_ = menu_items_;
  view->SmartCallback<EWebViewCallbacks::ContextMenuCustomize>().call(new_menu);
  menu_items_ = new_menu->menu_list_;
  delete new_menu;
  if (!CreateContextMenu())
    return false;

  return ShowContextMenu();
}

void ContextMenuControllerEfl::AddItemToPropsedList(ContextMenuOptionType item,
                                                    ContextMenuOption option,
                                                    std::string title,
                                                    std::string image_url,
                                                    std::string link_url,
                                                    std::string icon_path) {
  ContextMenuItemEfl *new_menu_item = new ContextMenuItemEfl(item, option, title, image_url, link_url, icon_path);
  _Ewk_Context_Menu_Item *new_item = new _Ewk_Context_Menu_Item();
  new_item->menu_item_ = new_menu_item;
  menu_items_ = eina_list_append(menu_items_, new_item);
}

void ContextMenuControllerEfl::GetProposedContextMenu() {
  if (!params_.link_url.is_empty()) {
    AddItemToPropsedList(MENU_ITEM_TYPE_ACTION,
                         MENU_ITEM_OPEN_LINK_IN_NEW_WINDOW,
                         std::string(dgettext("WebKit", "IDS_WEBVIEW_OPT_OPEN_LINK_IN_NEW_TAB_ABB")),
                         params_.link_url.spec(),
                         params_.link_url.spec(),
                         std::string());
    AddItemToPropsedList(MENU_ITEM_TYPE_ACTION,
                         MENU_ITEM_COPY_LINK_TO_CLIPBOARD,
                         std::string(dgettext("WebKit", "IDS_WEBVIEW_OPT_COPY_LINK_URL_ABB")),
                         std::string(),
                         params_.link_url.spec(),
                         std::string());
    AddItemToPropsedList(MENU_ITEM_TYPE_ACTION,
                         MENU_ITEM_DOWNLOAD_LINK_TO_DISK,
                         std::string(dgettext("WebKit", "IDS_BR_BODY_SAVE_LINK")),
                         std::string(),
                         params_.link_url.spec(),
                         std::string());
  }
  if (!params_.selection_text.empty())
    AddItemToPropsedList(MENU_ITEM_TYPE_ACTION,
                         MENU_ITEM_CUT,
                         std::string(dgettext("WebKit", "IDS_WEBVIEW_OPT_CUT_ABB")),
                         std::string(),
                         std::string(),
                         std::string());
  if (params_.is_editable) {
    AddItemToPropsedList(MENU_ITEM_TYPE_ACTION,
                         MENU_ITEM_SELECT_WORD,
                         std::string(dgettext("WebKit", "IDS_WEBVIEW_OPT_SELECT_ABB")),
                         std::string(),
                         std::string(),
                         std::string());
    AddItemToPropsedList(MENU_ITEM_TYPE_ACTION,
                         MENU_ITEM_PASTE,
                         std::string(dgettext("WebKit", "IDS_WEBVIEW_OPT_PASTE")),
                         std::string(),
                         std::string(),
                         std::string());
  }
  if (!params_.selection_text.empty()) {
    AddItemToPropsedList(MENU_ITEM_TYPE_ACTION,
                         MENU_ITEM_COPY,
                         std::string(dgettext("WebKit", "IDS_WEBVIEW_OPT_COPY")),
                         std::string(),
                         std::string(),
                         std::string());
    AddItemToPropsedList(MENU_ITEM_TYPE_ACTION,
                         MENU_ITEM_SELECT_ALL,
                         std::string(dgettext("WebKit", "IDS_WEBVIEW_OPT_SELECT_ALL_ABB")),
                         std::string(),
                         std::string(),
                         std::string());
  }
  if (params_.has_image_contents) {
    AddItemToPropsedList(MENU_ITEM_TYPE_ACTION,
                         MENU_ITEM_OPEN_IMAGE_IN_NEW_WINDOW,
                         std::string(dgettext("WebKit", "IDS_WEBVIEW_OPT_OPEN_IMAGE_IN_NEW_TAB_ABB")),
                         params_.src_url.spec(),
                         params_.src_url.spec(),
                         std::string());
    AddItemToPropsedList(MENU_ITEM_TYPE_ACTION,
                         MENU_ITEM_DOWNLOAD_IMAGE_TO_DISK,
                         std::string(dgettext("WebKit", "IDS_WEBVIEW_OPT_SAVE_IMAGE_ABB")),
                         params_.src_url.spec(),
                         params_.src_url.spec(),
                         std::string());
    AddItemToPropsedList(MENU_ITEM_TYPE_ACTION,
                         MENU_ITEM_COPY_IMAGE_TO_CLIPBOARD,
                         std::string(dgettext("WebKit", "IDS_WEBVIEW_OPT_COPY_IMAGE")),
                         params_.src_url.spec(),
                         params_.src_url.spec(),
                         std::string());
  }
  if (!params_.has_image_contents && !params_.link_url.is_empty()) {
    AddItemToPropsedList(MENU_ITEM_TYPE_ACTION,
                         MENU_ITEM_TEXT_SELECTION_MODE,
                         std::string(dgettext("WebKit", "IDS_WEBVIEW_OPT_SELECTION_MODE_ABB")),
                         params_.link_url.spec(),
                         params_.link_url.spec(),
                         std::string());
  }
  AddItemToPropsedList(MENU_ITEM_TYPE_ACTION,
                       MENU_ITEM_CLIPBOARD,
                       std::string(dgettext("WebKit", "IDS_WEBVIEW_OPT_CLIPBOARD")),
                       std::string(),
                       std::string(),
                       std::string());
}

bool ContextMenuControllerEfl::CreateContextMenu() {
  if (popup_)
    evas_object_del(popup_);

  if (!eina_list_count(menu_items_))
    return false;

  popup_ = elm_ctxpopup_add(GetWebViewEvasObject());
  evas_object_data_set(popup_, "ContextEfl", this);
  elm_object_tree_focus_allow_set(popup_, false);

  Eina_List *ittr;
  void *data;
  EINA_LIST_FOREACH(menu_items_, ittr, data) {
    _Ewk_Context_Menu_Item *item = static_cast<_Ewk_Context_Menu_Item*> (data);
    ContextMenuItemEfl *context_item = item->menu_item_;
    if (!context_item->Title().empty()) {
      if (!context_item->IconPath().empty()) {
        Evas_Object* icon = elm_image_add(popup_);
        elm_image_file_set(icon, context_item->IconPath().c_str(), NULL);
        elm_ctxpopup_item_append(popup_, context_item->Title().c_str(), icon, contextMenuItemSelectedCallback, context_item);
      } else {
        elm_ctxpopup_item_append(popup_, context_item->Title().c_str(), NULL, contextMenuItemSelectedCallback, context_item);
      }
    }
  }
  return true;
}

void ContextMenuControllerEfl::contextMenuCancelCallback(void* data, Evas_Object* obj, void* eventInfo) {
  ContextMenuControllerEfl* menu_controller = static_cast<ContextMenuControllerEfl*>(evas_object_data_get(obj, "ContextEfl"));
  if (menu_controller) {
    evas_object_del(menu_controller->GetPopUp());
    menu_controller->ResetPopUp();
    menu_controller->HideContextMenu();
    menu_controller->HideSelectionHandle();
    evas_object_data_del(obj, "ContextEfl");
  }
}

void ContextMenuControllerEfl::contextMenuItemSelectedCallback(void* data, Evas_Object* obj, void* eventInfo) {
  Evas_Object* pop_up = obj;
#if !defined(OS_TIZEN)
  // In desktop evas_object_data_get(obj) is not working because obj is not the object to which data was attached.
  // So get the parent, obj parent is internal box, box parent is the popup to which object was attached.
  // On target the obj that is obtained is the popup so the data is proper. Need to find why this difference?
  // Temp added this patch so as to work both on target and device.
  Evas_Object* list = elm_object_parent_widget_get(obj);
  pop_up = elm_object_parent_widget_get(list);
#endif
  ContextMenuControllerEfl* menu_controller = static_cast<ContextMenuControllerEfl*>(evas_object_data_get(pop_up, "ContextEfl"));
  if (menu_controller) {
    ContextMenuItemEfl* selected_menu_item = static_cast<ContextMenuItemEfl*>(data);
    if (selected_menu_item)
       menu_controller->MenuItemSelected(selected_menu_item);

    evas_object_del(menu_controller->GetPopUp());
    menu_controller->ResetPopUp();
    menu_controller->HideContextMenu();
    evas_object_data_del(pop_up, "ContextEfl");
  }
}

bool ContextMenuControllerEfl::ShowContextMenu() {
  if (!popup_)
    return false;

  if (type_ == MENU_TYPE_SELECTION) {
    int webViewX, webViewY, webViewWidth, webViewHeight;
    Evas_Point point;

    gfx::Point popupPosition = gfx::Point(params_.x, params_.y);

    if (!webview_)
      return false;

    Evas_Object* wv_evas_obj = GetWebViewEvasObject();
    evas_object_geometry_get(wv_evas_obj, &webViewX, &webViewY, &webViewWidth, &webViewHeight);
    popupPosition.set_x(popupPosition.x() + webViewX);
    popupPosition.set_y(popupPosition.y() + webViewY);

    point.x = popupPosition.x();
    point.y = popupPosition.y();

    evas_object_smart_callback_call(wv_evas_obj, "contextmenu,willshow", &point);

    bool allowed = true;
    evas_object_smart_callback_call(wv_evas_obj, "contextmenu,allowed", &allowed);
    if (!allowed) {
      evas_object_del(popup_);
      return false;
    }

    elm_object_style_set(popup_, "copypaste");
    elm_ctxpopup_horizontal_set(popup_, EINA_TRUE);

    int drawDirection;
    SelectionController* controller = webview_->GetSelectionController();

    if (!controller) {
      evas_object_del(popup_);
      return false;
    }

    CastToSelectionControllerEfl(controller)->ChangeContextMenuPosition(popupPosition, drawDirection);
    switch(drawDirection) {
      case 0:
        elm_ctxpopup_direction_priority_set(popup_, ELM_CTXPOPUP_DIRECTION_DOWN, ELM_CTXPOPUP_DIRECTION_DOWN, ELM_CTXPOPUP_DIRECTION_DOWN, ELM_CTXPOPUP_DIRECTION_DOWN);
        break;
      case 1:
        elm_ctxpopup_direction_priority_set(popup_, ELM_CTXPOPUP_DIRECTION_UP, ELM_CTXPOPUP_DIRECTION_UP, ELM_CTXPOPUP_DIRECTION_UP, ELM_CTXPOPUP_DIRECTION_UP);
        break;
      case 2:
        elm_ctxpopup_direction_priority_set(popup_, ELM_CTXPOPUP_DIRECTION_LEFT, ELM_CTXPOPUP_DIRECTION_LEFT, ELM_CTXPOPUP_DIRECTION_LEFT, ELM_CTXPOPUP_DIRECTION_LEFT);
        break;
      case 3:
        elm_ctxpopup_direction_priority_set(popup_, ELM_CTXPOPUP_DIRECTION_RIGHT, ELM_CTXPOPUP_DIRECTION_RIGHT, ELM_CTXPOPUP_DIRECTION_RIGHT, ELM_CTXPOPUP_DIRECTION_RIGHT);
        break;
      default:
        elm_ctxpopup_direction_priority_set(popup_, ELM_CTXPOPUP_DIRECTION_UP, ELM_CTXPOPUP_DIRECTION_DOWN, ELM_CTXPOPUP_DIRECTION_LEFT, ELM_CTXPOPUP_DIRECTION_RIGHT);
    };
    evas_object_move(popup_, popupPosition.x(), popupPosition.y());
#ifdef OS_TIZEN_MOBILE
    elm_ctxpopup_auto_hide_disabled_set(popup_, EINA_TRUE);
#endif
  } else {
    evas_object_move(popup_, params_.x, params_.y);
  }
  evas_object_smart_callback_add(popup_, "dismissed", contextMenuCancelCallback, 0);
  evas_object_show(popup_);
  return true;
}

void ContextMenuControllerEfl::HideSelectionHandle() {
  SelectionController* controller = webview_->GetSelectionController();
  if (controller)
    controller->HideHandle();
}

void ContextMenuControllerEfl::OnDownloadStarted(
    content::DownloadItem* item,
    content::DownloadInterruptReason interrupt_reason) {
  item->AddObserver(this);
}

void ContextMenuControllerEfl::OnDownloadUpdated(content::DownloadItem* download) {
  if(download->AllDataSaved()) {
    std::string DownloadPath = download->GetForcedFilePath().value();
    ClipboardHelperEfl::GetInstance()->SetData(DownloadPath.c_str(),ClipboardHelperEfl::CLIPBOARD_DATA_TYPE_IMAGE);
    download->RemoveObserver(this);
  }
}

base::FilePath ContextMenuControllerEfl::DownloadFile(const GURL url,
                                                      const base::FilePath outputDir,
                                                      const DownloadUrlParameters::OnStartedCallback& callback = DownloadUrlParameters::OnStartedCallback()) {
  const GURL referrer = web_contents_.GetVisibleURL();
  DownloadManager* dlm = BrowserContext::GetDownloadManager(web_contents_.GetBrowserContext());

  scoped_ptr<DownloadUrlParameters> dl_params(
      DownloadUrlParameters::FromWebContents(&web_contents_, url));
  dl_params->set_post_id(-1);
  dl_params->set_referrer(
      content::Referrer(referrer, blink::WebReferrerPolicyAlways));
  dl_params->set_referrer_encoding("utf8");
  base::FilePath fileName = net::GenerateFileName(url,"","","","","");
  base::FilePath fullPath = outputDir.Append(fileName);

  while (PathExists(fullPath)) {
    unsigned int i;
    base::FilePath fileNameTmp;
    for (i = 0; PathExists(fullPath) && i <= 999; i++) {
      char buffer[6];
      snprintf(buffer, sizeof(buffer), "(%d)", i);
      fileNameTmp = fileName.InsertBeforeExtension(std::string(buffer));
      fullPath = outputDir.Append(fileNameTmp);
    }
  }

  dl_params->set_file_path(fullPath);
  dl_params->set_prompt(true);
  dl_params->set_callback(callback);
  dlm->DownloadUrl(dl_params.Pass());
  return fullPath;
}

bool ContextMenuControllerEfl::TriggerDownloadCb(const GURL url) {
  BrowserContextEfl* browser_context = static_cast<BrowserContextEfl*>(web_contents_.GetBrowserContext());
  if (browser_context) {
    EwkDidStartDownloadCallback* start_download_callback =
        browser_context->WebContext()->DidStartDownloadCallback();
    if (start_download_callback) {
      DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
      start_download_callback->TriggerCallback(url.spec());
      return true;
    }
  }
  return false;
}

void ContextMenuControllerEfl::OpenInNewTab(const GURL url) {
  if (!url.is_valid())
    return;
  NavigationController::LoadURLParams params(url);
  const GURL referrer = web_contents_.GetVisibleURL();
  WebContentsImpl& wc = static_cast<WebContentsImpl&>(web_contents_);
  wc.GetFrameTree()->root()->navigator()->RequestOpenURL(
      wc.GetFrameTree()->root()->current_frame_host(), referrer,
      Referrer(), NEW_FOREGROUND_TAB,
      false, false);
}

void ContextMenuControllerEfl::MenuItemSelected(ContextMenuItemEfl *menu_item) {
  EWebView* view = webview_->GetImpl();
  if (!view)
    return;

  // FIXME: Add cases as and when required
  switch(menu_item->GetContextMenuOption())
  {
    case MENU_ITEM_OPEN_LINK_IN_NEW_WINDOW: {
      OpenInNewTab(GURL(menu_item->LinkURL()));
      break;
    }
    case MENU_ITEM_GO_BACK: {
      view->GoBack();
      break;
    }
    case MENU_ITEM_GO_FORWARD: {
      view->GoForward();
      break;
    }
    case MENU_ITEM_RELOAD: {
      view->ReloadIgnoringCache();
      break;
    }
    case MENU_ITEM_COPY: {
      view->ExecuteEditCommand("copy", NULL);
      if (params_.is_editable) {
        HideSelectionHandle();
        Eina_Rectangle left_rect, right_rect;
        view->GetSelectionRange(&left_rect, &right_rect);
        Evas_Coord x, y;
        evas_object_geometry_get(view->evas_object(), &x, &y, 0, 0);
        right_rect.x += x;
        right_rect.y += y;
        view->MoveCaret(gfx::Point(right_rect.x, right_rect.y));
      } else {
        view->ExecuteEditCommand("Unselect", NULL);
      }
      break;
    }
    case MENU_ITEM_TEXT_SELECTION_MODE: {
      Evas_Coord x, y;
      evas_object_geometry_get(view->evas_object(), &x, &y, 0, 0);
      view->SelectLinkText(gfx::Point(params_.x - x, params_.y - y));
      break;
    }
    case MENU_ITEM_COPY_IMAGE_TO_CLIPBOARD: {
      DownloadFile(GURL(menu_item->ImageURL()),
                        base::FilePath("/tmp/"),
                        base::Bind(&ContextMenuControllerEfl::OnDownloadStarted,weak_ptr_factory_.GetWeakPtr()));
      break;
    }
    case MENU_ITEM_COPY_LINK_TO_CLIPBOARD: {
      ClipboardHelperEfl::GetInstance()->SetData(menu_item->LinkURL(),ClipboardHelperEfl::CLIPBOARD_DATA_TYPE_URL);
      break;
    }
    case MENU_ITEM_DOWNLOAD_LINK_TO_DISK: {
#ifdef OS_TIZEN_MOBILE
      if (!TriggerDownloadCb(GURL(menu_item->LinkURL())))
        DownloadFile(GURL(menu_item->LinkURL()), base::FilePath("/opt/usr/media/Downloads/Others/"));
#else
      if (!TriggerDownloadCb(GURL(menu_item->LinkURL())))
        DownloadFile(GURL(menu_item->LinkURL()), base::FilePath("/tmp/"));
#endif
      break;
    }
    case MENU_ITEM_DOWNLOAD_IMAGE_TO_DISK: {
#ifdef OS_TIZEN_MOBILE
      if (!TriggerDownloadCb(GURL(menu_item->LinkURL())))
        DownloadFile(GURL(menu_item->ImageURL()), base::FilePath("/opt/usr/media/Images/"));
#else
      if (!TriggerDownloadCb(GURL(menu_item->LinkURL())))
        DownloadFile(GURL(menu_item->ImageURL()), base::FilePath("/tmp/"));
#endif
      break;
    }
    case MENU_ITEM_OPEN_IMAGE_IN_NEW_WINDOW: {
      OpenInNewTab(GURL(menu_item->ImageURL()));
      break;
    }
    case MENU_ITEM_SELECT_WORD: {
      Evas_Coord x, y;
      evas_object_geometry_get(view->evas_object(), &x, &y, 0, 0);
      view->SelectClosestWord(gfx::Point(params_.x - x, params_.y - y));
      HideSelectionHandle();
      break;
    }
    case MENU_ITEM_SELECT_ALL: {
      view->ExecuteEditCommand("SelectAll", NULL);
      HideSelectionHandle();
      break;
    }
    case MENU_ITEM_PASTE: {
      view->ExecuteEditCommand("paste", NULL);
      HideSelectionHandle();
      break;
    }
    case MENU_ITEM_CUT: {
      view->ExecuteEditCommand("cut", NULL);
      break;
    }
    case MENU_ITEM_CLIPBOARD: {
      //TODO: only text is supported, set false for richly editable.
      // Need to find if view has rich editor to support paste of non-text things.
      ClipboardHelperEfl::GetInstance()->OpenClipboardWindow(view, false);
      break;
    }
    default:
      NOTREACHED();
  }
  _Ewk_Context_Menu_Item item;
  item.menu_item_ = menu_item;
  view->SmartCallback<EWebViewCallbacks::ContextMenuItemSelected>().call(&item);
}

void ContextMenuControllerEfl::HideContextMenu() {
  if (popup_)
    evas_object_del(popup_);

  Eina_List *ittr;
  Eina_List *next;
  void *data;
  EINA_LIST_FOREACH_SAFE(menu_items_, ittr, next, data) {
    _Ewk_Context_Menu_Item *item = static_cast<_Ewk_Context_Menu_Item*> (data);
    ContextMenuItemEfl *context_item = item->menu_item_;
    menu_items_ =  eina_list_remove(menu_items_, item);
    delete context_item;
    delete item;
  }
  menu_items_ = 0;
}

Evas_Object* ContextMenuControllerEfl::GetWebViewEvasObject() {
  return webview_->AsEvasObject();
}

}
