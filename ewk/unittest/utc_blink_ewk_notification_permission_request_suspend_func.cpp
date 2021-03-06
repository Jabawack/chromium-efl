// Copyright 2014 Samsung Electronics. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "utc_blink_ewk_base.h"

class utc_blink_ewk_notification_permission_request_suspend : public utc_blink_ewk_base {
protected:
  /* Callback for "notification,permission,request" */
  static void notificationPermissionRequest(void* data, Evas_Object* webview, void* event_info)
  {
    Ewk_Context *context = ewk_view_context_get(webview);
    if(!event_info || !context) {
      utc_message("event_info: %p\ncontext: %p", event_info, context);
      FAIL();
      return;
    }

    //allow the notification
    ewk_notification_permission_request_suspend((Ewk_Notification_Permission_Request*)event_info);
    ewk_notification_permission_request_set((Ewk_Notification_Permission_Request*)event_info, EINA_TRUE);
  }

  /* Callback for "notification,show" */
  static void notificationShow(void* data, Evas_Object* webview, void* event_info)
  {
    if (!data || !event_info) {
      utc_message("data: %p\nevent_info: %p", data, event_info);
      FAIL();
    }

    utc_blink_ewk_notification_permission_request_suspend* owner = static_cast<utc_blink_ewk_notification_permission_request_suspend*>(data);
    owner->EventLoopStop(Success);
  }

  /* Startup function */
  virtual void PostSetUp()
  {
    evas_object_smart_callback_add(GetEwkWebView(), "notification,permission,request", notificationPermissionRequest, this);
    evas_object_smart_callback_add(GetEwkWebView(), "notification,show", notificationShow, this);
  }

  /* Cleanup function */
  virtual void PreTearDown()
  {
    evas_object_smart_callback_del(GetEwkWebView(), "notification,permission,request", notificationPermissionRequest);
    evas_object_smart_callback_del(GetEwkWebView(), "notification,show", notificationShow);
  }

protected:
  static const char* const resource_relative_path;
  static const char* const notification_title_ref;
};

const char* const utc_blink_ewk_notification_permission_request_suspend::resource_relative_path = "/common/sample_notification_1.html";
const char* const utc_blink_ewk_notification_permission_request_suspend::notification_title_ref = "Notification Title";

/**
* @brief Positive test case for ewk_notification_permission_request_suspened()
*/
TEST_F(utc_blink_ewk_notification_permission_request_suspend, POS_TEST)
{
  std::string resource_url = GetResourceUrl(resource_relative_path);
  if (!ewk_view_url_set(GetEwkWebView(), resource_url.c_str())) {
    FAIL();
  }

  MainLoopResult result = EventLoopStart();
  EXPECT_EQ(result, Success);
}

/**
* @brief Checking whether function works properly in case of NULL value pass
*/
TEST_F(utc_blink_ewk_notification_permission_request_suspend, NEG_TEST)
{
  ewk_notification_permission_request_suspend(NULL);
  // If  NULL argument passing wont give segmentation fault negative test case will pass
  SUCCEED();
}
