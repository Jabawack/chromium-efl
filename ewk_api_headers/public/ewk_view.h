/*
   Copyright (C) 2011 Samsung Electronics
   Copyright (C) 2012 Intel Corporation. All rights reserved.

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

/**
 * @file    ewk_view.h
 * @brief   WebKit main smart object.
 *
 * This object provides view related APIs of WebKit2 to EFL object.
 *
 * The following signals (see evas_object_smart_callback_add()) are emitted:
 *
 * - "back,forward,list,changed", void: reports that the view's back / forward list had changed.
 * - "close,window", void: window is closed.
 * - "create,window", Evas_Object**: a new window is created.
 * - "download,cancelled", Ewk_Download_Job*: reports that a download was effectively cancelled.
 * - "download,failed", Ewk_Download_Job_Error*: reports that a download failed with the given error.
 * - "download,finished", Ewk_Download_Job*: reports that a download completed successfully.
 * - "download,request", Ewk_Download_Job*: reports that a new download has been requested. The client should set the
 *   destination path by calling ewk_download_job_destination_set() or the download will fail.
 * - "form,submission,request", Ewk_Form_Submission_Request*: Reports that a form request is about to be submitted.
 *   The Ewk_Form_Submission_Request passed contains information about the text fields of the form. This
 *   is typically used to store login information that can be used later to pre-fill the form.
 *   The form will not be submitted until ewk_form_submission_request_submit() is called.
 *   It is possible to handle the form submission request asynchronously, by simply calling
 *   ewk_form_submission_request_ref() on the request and calling ewk_form_submission_request_submit()
 *   when done to continue with the form submission. If the last reference is removed on a
 *   #Ewk_Form_Submission_Request and the form has not been submitted yet,
 *   ewk_form_submission_request_submit() will be called automatically.
 * - "icon,changed", void: reports that the view's favicon has changed.
 * - "intent,request,new", Ewk_Intent*: reports new Web intent request.
 * - "intent,service,register", Ewk_Intent_Service*: reports new Web intent service registration.
 * - "load,error", const Ewk_Error*: reports main frame load failed.
 * - "load,finished", void: reports load finished.
 * - "load,progress", double*: load progress has changed (value from 0.0 to 1.0).
 * - "load,provisional,failed", const Ewk_Error*: view provisional load failed.
 * - "load,provisional,redirect", void: view received redirect for provisional load.
 * - "load,provisional,started", void: view started provisional load.
 * - "pageSave,success", void: page save operation was success.
 * - "pageSave,error", void: page save operation has failed.
 * - "policy,decision,navigation", Ewk_Navigation_Policy_Decision*: a navigation policy decision should be taken.
 *   To make a policy decision asynchronously, simply increment the reference count of the
 *   #Ewk_Navigation_Policy_Decision object using ewk_navigation_policy_decision_ref().
 * - "policy,decision,new,window", Ewk_Navigation_Policy_Decision*: a new window policy decision should be taken.
 *   To make a policy decision asynchronously, simply increment the reference count of the
 *   #Ewk_Navigation_Policy_Decision object using ewk_navigation_policy_decision_ref().
 * - "resource,request,failed", const Ewk_Resource_Load_Error*: a resource failed loading.
 * - "resource,request,finished", const Ewk_Resource*: a resource finished loading.
 * - "resource,request,new", const Ewk_Resource_Request*: a resource request was initiated.
 * - "resource,request,response", Ewk_Resource_Load_Response*: a response to a resource request was received.
 * - "resource,request,sent", const Ewk_Resource_Request*: a resource request was sent.
 * - "text,found", unsigned int*: the requested text was found and it gives the number of matches.
 * - "title,changed", const char*: title of the main frame was changed.
 * - "tooltip,text,set", const char*: tooltip was set.
 * - "tooltip,text,unset", void: tooltip was unset.
 * - "url,changed", const char*: url of the main frame was changed.
 * - "webprocess,crashed", Eina_Bool*: expects a @c EINA_TRUE if web process crash is handled; @c EINA_FALSE, otherwise.
 *
 *
 * Tizen specific signals
 * - "magnifier,show", void: magifier of text selection was showed.
 * - "magnifier,hide", void: magifier of text selection was hidden.
 */

#ifndef ewk_view_h
#define ewk_view_h

#include <Evas.h>
#include "ewk_enums.h"
#include "ewk_touch.h"
#include "ewk_security_origin.h"
//#include "ewk_intercept_request.h"
#ifdef __cplusplus
extern "C" {
#endif

/// Enum values containing text directionality values.
typedef enum {
    EWK_TEXT_DIRECTION_RIGHT_TO_LEFT,
    EWK_TEXT_DIRECTION_LEFT_TO_RIGHT
} Ewk_Text_Direction;

enum Ewk_Password_Popup_Option {
    EWK_PASSWORD_POPUP_SAVE,
    EWK_PASSWORD_POPUP_NOT_NOW,
    EWK_PASSWORD_POPUP_NEVER,
    EWK_PASSWORD_POPUP_OK = EWK_PASSWORD_POPUP_SAVE,
    EWK_PASSWORD_POPUP_CANCEL =EWK_PASSWORD_POPUP_NOT_NOW
};
typedef enum Ewk_Password_Popup_Option Ewk_Password_Popup_Option;

typedef struct Ewk_View_Smart_Data Ewk_View_Smart_Data;
typedef struct Ewk_View_Smart_Class Ewk_View_Smart_Class;

// FIXME: these should be moved elsewhere.
typedef struct Ewk_Page_Group Ewk_Page_Group;

// #if PLATFORM(TIZEN)
/// Creates a type name for _Ewk_Event_Gesture.
typedef struct Ewk_Event_Gesture Ewk_Event_Gesture;

/// Represents a gesture event.
struct Ewk_Event_Gesture {
    Ewk_Gesture_Type type; /**< type of the gesture event */
    Evas_Coord_Point position; /**< position of the gesture event */
    Evas_Point velocity; /**< velocity of the gesture event. The unit is pixel per second. */
    double scale; /**< scale of the gesture event */
    int count; /**< count of the gesture */
    unsigned int timestamp; /**< timestamp of the gesture */
};

// #if ENABLE(TIZEN_FOCUS_UI)
enum Ewk_Unfocus_Direction {
    EWK_UNFOCUS_DIRECTION_NONE = 0,
    EWK_UNFOCUS_DIRECTION_FORWARD,
    EWK_UNFOCUS_DIRECTION_BACKWARD,
    EWK_UNFOCUS_DIRECTION_UP,
    EWK_UNFOCUS_DIRECTION_DOWN,
    EWK_UNFOCUS_DIRECTION_LEFT,
    EWK_UNFOCUS_DIRECTION_RIGHT,
};
typedef enum Ewk_Unfocus_Direction Ewk_Unfocus_Direction;
// #endif

// #if ENABLE(TIZEN_INPUT_TAG_EXTENSION)
/**
 * \enum    Ewk_Input_Type
 * @brief   Provides type of focused input element
 */
enum Ewk_Input_Type {
    EWK_INPUT_TYPE_TEXT,
    EWK_INPUT_TYPE_TELEPHONE,
    EWK_INPUT_TYPE_NUMBER,
    EWK_INPUT_TYPE_EMAIL,
    EWK_INPUT_TYPE_URL,
    EWK_INPUT_TYPE_PASSWORD,
    EWK_INPUT_TYPE_COLOR,
    EWK_INPUT_TYPE_DATE,
    EWK_INPUT_TYPE_DATETIME,
    EWK_INPUT_TYPE_DATETIMELOCAL,
    EWK_INPUT_TYPE_MONTH,
    EWK_INPUT_TYPE_TIME,
    EWK_INPUT_TYPE_WEEK
};
typedef enum Ewk_Input_Type Ewk_Input_Type;
// #endif // ENABLE(TIZEN_INPUT_TAG_EXTENSION)

// #if ENABLE(TIZEN_WEBKIT2_TEXT_SELECTION)
/**
 * \enum    Ewk_Selection_Handle_Type
 * @brief   Provides type of selection handle
 */
enum Ewk_Selection_Handle_Type {
    EWK_SELECTION_HANDLE_TYPE_LEFT,
    EWK_SELECTION_HANDLE_TYPE_RIGHT,
    EWK_SELECTION_HANDLE_TYPE_LARGE
};
typedef enum Ewk_Selection_Handle_Type Ewk_Selection_Handle_Type;
// #endif // ENABLE(TIZEN_WEBKIT2_TEXT_SELECTION)
// #endif // #if PLATFORM(TIZEN)

/// Ewk view's class, to be overridden by sub-classes.
struct Ewk_View_Smart_Class {
    Evas_Smart_Class sc; /**< all but 'data' is free to be changed. */
    unsigned long version;

    Eina_Bool (*popup_menu_show)(Ewk_View_Smart_Data *sd, Eina_Rectangle rect, Ewk_Text_Direction text_direction, double page_scale_factor, Eina_List* items, int selected_index);
    Eina_Bool (*popup_menu_hide)(Ewk_View_Smart_Data *sd);
    Eina_Bool (*popup_menu_update)(Ewk_View_Smart_Data *sd, Eina_Rectangle rect, Ewk_Text_Direction text_direction, Eina_List* items, int selected_index);

    Eina_Bool (*text_selection_down)(Ewk_View_Smart_Data *sd, int x, int y);
    Eina_Bool (*text_selection_up)(Ewk_View_Smart_Data *sd, int x, int y);

    Eina_Bool (*input_picker_show)(Ewk_View_Smart_Data *sd, Ewk_Input_Type inputType, const char* inputValue);

    // event handling:
    //  - returns true if handled
    //  - if overridden, have to call parent method if desired
    Eina_Bool (*focus_in)(Ewk_View_Smart_Data *sd);
    Eina_Bool (*focus_out)(Ewk_View_Smart_Data *sd);
    Eina_Bool (*fullscreen_enter)(Ewk_View_Smart_Data *sd, Ewk_Security_Origin *origin);
    Eina_Bool (*fullscreen_exit)(Ewk_View_Smart_Data *sd);
    Eina_Bool (*mouse_wheel)(Ewk_View_Smart_Data *sd, const Evas_Event_Mouse_Wheel *ev);
    Eina_Bool (*mouse_down)(Ewk_View_Smart_Data *sd, const Evas_Event_Mouse_Down *ev);
    Eina_Bool (*mouse_up)(Ewk_View_Smart_Data *sd, const Evas_Event_Mouse_Up *ev);
    Eina_Bool (*mouse_move)(Ewk_View_Smart_Data *sd, const Evas_Event_Mouse_Move *ev);
    Eina_Bool (*key_down)(Ewk_View_Smart_Data *sd, const Evas_Event_Key_Down *ev);
    Eina_Bool (*key_up)(Ewk_View_Smart_Data *sd, const Evas_Event_Key_Up *ev);

    // color picker:
    //   - Shows and hides color picker.
    Eina_Bool (*input_picker_color_request)(Ewk_View_Smart_Data *sd, int r, int g, int b, int a);
    Eina_Bool (*input_picker_color_dismiss)(Ewk_View_Smart_Data *sd);

    // storage:
    //   - Web database.
    unsigned long long (*exceeded_database_quota)(Ewk_View_Smart_Data *sd, const char *databaseName, const char *displayName, unsigned long long currentQuota, unsigned long long currentOriginUsage, unsigned long long currentDatabaseUsage, unsigned long long expectedUsage);

    Eina_Bool (*formdata_candidate_show)(Ewk_View_Smart_Data *sd, int x, int y, int w, int h);
    Eina_Bool (*formdata_candidate_hide)(Ewk_View_Smart_Data *sd);
    Eina_Bool (*formdata_candidate_update_data)(Ewk_View_Smart_Data *sd, Eina_List *dataList);
    Eina_Bool (*formdata_candidate_is_showing)(Ewk_View_Smart_Data *sd);

    Eina_Bool (*gesture_start)(Ewk_View_Smart_Data *sd, const Ewk_Event_Gesture *ev);
    Eina_Bool (*gesture_end)(Ewk_View_Smart_Data *sd, const Ewk_Event_Gesture *ev);
    Eina_Bool (*gesture_move)(Ewk_View_Smart_Data *sd, const Ewk_Event_Gesture *ev);

    void (*selection_handle_down)(Ewk_View_Smart_Data *sd, Ewk_Selection_Handle_Type handleType, int x, int y);
    void (*selection_handle_move)(Ewk_View_Smart_Data *sd, Ewk_Selection_Handle_Type handleType, int x, int y);
    void (*selection_handle_up)(Ewk_View_Smart_Data *sd, Ewk_Selection_Handle_Type handleType, int x, int y);

    Eina_Bool (*window_geometry_set)(Ewk_View_Smart_Data *sd, Evas_Coord x, Evas_Coord y, Evas_Coord width, Evas_Coord height);
    Eina_Bool (*window_geometry_get)(Ewk_View_Smart_Data *sd, Evas_Coord *x, Evas_Coord *y, Evas_Coord *width, Evas_Coord *height);
};

// #if PLATFORM(TIZEN)
/**
 * Callback for ewk_view_web_app_capable_get
 *
 * @param capable web application capable
 * @param user_data user_data will be passsed when ewk_view_web_app_capable_get is called
 */
typedef void (*Ewk_Web_App_Capable_Get_Callback)(Eina_Bool capable, void* user_data);

/**
 * Callback for ewk_view_web_app_icon_get
 *
 * @param icon_url web application icon
 * @param user_data user_data will be passsed when ewk_view_web_app_icon_get is called
 */
typedef void (*Ewk_Web_App_Icon_URL_Get_Callback)(const char* icon_url, void* user_data);

/**
 * Callback for ewk_view_web_app_icon_urls_get.
 *
 * @param icon_urls list of Ewk_Web_App_Icon_Data for web app
 * @param user_data user_data will be passsed when ewk_view_web_app_icon_urls_get is called
 */
typedef void (*Ewk_Web_App_Icon_URLs_Get_Callback)(Eina_List *icon_urls, void *user_data);
// #endif

/**
 * The version you have to put into the version field
 * in the @a Ewk_View_Smart_Class structure.
 */
#define EWK_VIEW_SMART_CLASS_VERSION 1UL

/**
 * Initializer for whole Ewk_View_Smart_Class structure.
 *
 * @param smart_class_init initializer to use for the "base" field
 * (Evas_Smart_Class).
 *
 * @see EWK_VIEW_SMART_CLASS_INIT_NULL
 * @see EWK_VIEW_SMART_CLASS_INIT_VERSION
 * @see EWK_VIEW_SMART_CLASS_INIT_NAME_VERSION
 */
#define EWK_VIEW_SMART_CLASS_INIT(smart_class_init) {smart_class_init, EWK_VIEW_SMART_CLASS_VERSION}

/**
* Initializer to zero a whole Ewk_View_Smart_Class structure.
*
* @see EWK_VIEW_SMART_CLASS_INIT_VERSION
* @see EWK_VIEW_SMART_CLASS_INIT_NAME_VERSION
* @see EWK_VIEW_SMART_CLASS_INIT
*/
#define EWK_VIEW_SMART_CLASS_INIT_NULL EWK_VIEW_SMART_CLASS_INIT(EVAS_SMART_CLASS_INIT_NULL)

/**
 * Initializer to zero a whole Ewk_View_Smart_Class structure and set
 * name and version.
 *
 * Similar to EWK_VIEW_SMART_CLASS_INIT_NULL, but will set version field of
 * Evas_Smart_Class (base field) to latest EVAS_SMART_CLASS_VERSION and name
 * to the specific value.
 *
 * It will keep a reference to name field as a "const char *", that is,
 * name must be available while the structure is used (hint: static or global!)
 * and will not be modified.
 *
 * @see EWK_VIEW_SMART_CLASS_INIT_NULL
 * @see EWK_VIEW_SMART_CLASS_INIT_VERSION
 * @see EWK_VIEW_SMART_CLASS_INIT
 */
#define EWK_VIEW_SMART_CLASS_INIT_NAME_VERSION(name) EWK_VIEW_SMART_CLASS_INIT(EVAS_SMART_CLASS_INIT_NAME_VERSION(name))

typedef struct EWebView EWebView;
/**
 * @brief Contains an internal View data.
 *
 * It is to be considered private by users, but may be extended or
 * changed by sub-classes (that's why it's in public header file).
 */
struct Ewk_View_Smart_Data {
    Evas_Object_Smart_Clipped_Data base;
    const Ewk_View_Smart_Class* api; /**< reference to casted class instance */
    Evas_Object* self; /**< reference to owner object */
    Evas_Object* image; /**< reference to evas_object_image for drawing web contents */
    EWebView* priv; /**< should never be accessed, c++ stuff */
    struct {
        Evas_Coord x, y, w, h; /**< last used viewport */
    } view;
    struct { /**< what changed since last smart_calculate */
        Eina_Bool any:1;

        // WebKit use these but we don't. We should remove these if we are sure
        // we do it right.
        Eina_Bool size:1;
        Eina_Bool position:1;
    } changed;
};

/**
 * Enum values used to specify search options.
 * @brief   Provides option to find text
 * @info    Keep this in sync with WKFindOptions.h
 */
enum _Ewk_Find_Options {
  EWK_FIND_OPTIONS_NONE, /**< no search flags, this means a case sensitive, no wrap, forward only search. */
  EWK_FIND_OPTIONS_CASE_INSENSITIVE = 1 << 0, /**< case insensitive search. */
  EWK_FIND_OPTIONS_BACKWARDS = 1 << 3, /**< search backwards. */
  EWK_FIND_OPTIONS_AT_WORD_STARTS = 1 << 1, /**< search text only at the beginning of the words. */
  EWK_FIND_OPTIONS_TREAT_MEDIAL_CAPITAL_AS_WORD_START = 1 << 2, /**< treat capital letters in the middle of words as word start. */
  EWK_FIND_OPTIONS_WRAP_AROUND = 1 << 4, /**< if not present search will stop at the end of the document. */
  EWK_FIND_OPTIONS_SHOW_OVERLAY = 1 << 5, /**< show overlay */
  EWK_FIND_OPTIONS_SHOW_FIND_INDICATOR = 1 << 6, /**< show indicator */
  EWK_FIND_OPTIONS_SHOW_HIGHLIGHT = 1 << 7 /**< show highlight */
};
typedef enum _Ewk_Find_Options Ewk_Find_Options;

enum Ewk_Page_Visibility_State {
    EWK_PAGE_VISIBILITY_STATE_VISIBLE,
    EWK_PAGE_VISIBILITY_STATE_HIDDEN,
    EWK_PAGE_VISIBILITY_STATE_PRERENDER
};
typedef enum Ewk_Page_Visibility_State Ewk_Page_Visibility_State;

enum Ewk_Http_Method {
    EWK_HTTP_METHOD_GET,
    EWK_HTTP_METHOD_HEAD,
    EWK_HTTP_METHOD_POST,
    EWK_HTTP_METHOD_PUT,
    EWK_HTTP_METHOD_DELETE,
};
typedef enum Ewk_Http_Method Ewk_Http_Method;

enum _Ewk_CSP_Header_Type {
  EWK_REPORT_ONLY,
  EWK_ENFORCE_POLICY,
  EWK_DEFAULT_POLICY
};
typedef enum _Ewk_CSP_Header_Type Ewk_CSP_Header_Type;

/**
 * Callback for ewk_view_script_execute
 *
 * @param o the view object
 * @param result_value value returned by script
 * @param user_data user data
 */
typedef void (*Ewk_View_Script_Execute_Callback)(Evas_Object* o, const char* result_value, void* user_data);

/**
 * Callback for ewk_view_plain_text_get
 *
 * @param o the view object
 * @param plain_text the contents of the given frame converted to plain text
 * @param user_data user data
 */
typedef void (*Ewk_View_Plain_Text_Get_Callback)(Evas_Object* o, const char* plain_text, void* user_data);

/**
 * Creates a type name for the callback function used to get the page contents.
 *
 * @param o view object
 * @param data mhtml data of the page contents
 * @param user_data user data will be passed when ewk_view_mhtml_data_get is called
 */
typedef void (*Ewk_View_MHTML_Data_Get_Callback)(Evas_Object *o, const char *data, void *user_data);



typedef Eina_Bool (*Ewk_View_Password_Confirm_Popup_Callback)(Evas_Object* o, const char* message, void* user_data);

typedef Eina_Bool (*Ewk_View_JavaScript_Alert_Callback)(Evas_Object* o, const char* alert_text, void* user_data);


typedef Eina_Bool (*Ewk_View_JavaScript_Confirm_Callback)(Evas_Object* o, const char* message, void* user_data);
/**
 * Callback for ewk_view_javascript_prompt_callback_set
 *
 * @param o the view object
 * @param message the text to be displayed on the prompt popup
 * @param default_value default text to be entered in the prompt dialog
 * @param user_data user data
 */
typedef Eina_Bool (*Ewk_View_JavaScript_Prompt_Callback)(Evas_Object* o, const char* message, const char* default_value, void* user_data);


//#if ENABLE(TIZEN_SUPPORT_BEFORE_UNLOAD_CONFIRM_PANEL)
typedef Eina_Bool (*Ewk_View_Before_Unload_Confirm_Panel_Callback)(Evas_Object* o, const char* message, void* user_data);

//#if ENABLE(TIZEN_APPLICATION_CACHE)
typedef Eina_Bool (*Ewk_View_Applicacion_Cache_Permission_Callback)(Evas_Object* o, Ewk_Security_Origin* origin,  void* user_data);

typedef Eina_Bool (*Ewk_View_Exceeded_Indexed_Database_Quota_Callback)(Evas_Object* o, Ewk_Security_Origin* origin, long long currentQuota, void* user_data);


typedef Eina_Bool (*Ewk_View_Exceeded_Database_Quota_Callback)(Evas_Object* o, Ewk_Security_Origin* origin, const char* database_name, unsigned long long expectedQuota, void* user_data);

typedef Eina_Bool (*Ewk_View_Exceeded_Local_File_System_Quota_Callback)(Evas_Object* o, Ewk_Security_Origin* origin, long long currentQuota, void* user_data);


typedef Eina_Bool (*Ewk_Orientation_Lock_Cb)(Evas_Object* o, Eina_Bool need_lock, int orientation, void* user_data);

#ifdef __cplusplus
}
#endif

#endif // ewk_view_h
