// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Copyright 2014 Samsung Electronics. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "browser/renderer_host/render_widget_host_view_efl.h"

#include "base/auto_reset.h"
#include "base/basictypes.h"
#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/command_line.h"
#include "base/debug/trace_event.h"
#include "base/message_loop/message_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "browser/disambiguation_popup_efl.h"
#include "browser/renderer_host/im_context_efl.h"
#include "browser/renderer_host/scroll_detector.h"
#include "browser/renderer_host/web_event_factory_efl.h"
#include "content/browser/gpu/browser_gpu_channel_host_factory.h"
#include "content/browser/renderer_host/render_widget_host_impl.h"
#include "content/browser/renderer_host/ui_events_helper.h"
#include "content/browser/renderer_host/event_with_latency_info.h"
#include "content/public/browser/browser_thread.h"
#include "content/browser/renderer_host/dip_util.h"
#include "content/browser/gpu/gpu_process_host.h"
#include "content/common/gpu/client/gl_helper.h"
#include "content/common/input/did_overscroll_params.h"
#include "content/public/browser/render_widget_host_view_frame_subscriber.h"
#include "content/public/common/content_switches.h"
#include "content/common/view_messages.h"
#include "content/common/gpu/gpu_messages.h"
#include "common/render_messages_efl.h"
#include "eweb_context.h"
#include "gl/gl_shared_context_efl.h"
#include "gpu/command_buffer/service/mailbox_manager.h"
#include "media/base/video_util.h"
#include "selection_controller_efl.h"
#include "skia/ext/image_operations.h"
#include "third_party/WebKit/public/web/WebInputEvent.h"
#include "third_party/WebKit/public/platform/WebScreenInfo.h"
#include "third_party/WebKit/public/web/WebTouchPoint.h"
#include "ui/gfx/display.h"
#include "ui/gfx/rect.h"
#include "ui/gfx/screen.h"
#include "ui/views/widget/desktop_aura/desktop_screen.h"
#include "ui/events/event_utils.h"
#include "ui/events/gestures/gesture_types.h"
#include "ui/events/gestures/gesture_recognizer.h"
#include "browser/motion/wkext_motion.h"
#include "content/common/input_messages.h"
#include "common/webcursor_efl.h"

#include <assert.h>
#include <Ecore.h>
#include <Ecore_Evas.h>
#include <Ecore_Input.h>
#include <Ecore_X.h>
#include <Elementary.h>

#define EFL_MAX_WIDTH 10000
#define EFL_MAX_HEIGHT 10000  // borrowed from GTK+ port

#define MAX_SURFACE_WIDTH_EGL 4096 //max supported Framebuffer width
#define MAX_SURFACE_HEIGHT_EGL 4096 //max supported Framebuffer height

namespace content {

void RenderWidgetHostViewBase::GetDefaultScreenInfo(blink::WebScreenInfo* results) {
  gfx::Screen* screen = gfx::Screen::GetNativeScreen();
  if (!screen)
    return;

  const gfx::Display display = screen->GetPrimaryDisplay();
  results->rect = display.bounds();
  results->availableRect = display.work_area();
  results->deviceScaleFactor = display.device_scale_factor();
  // TODO(derat|oshima): Don't hardcode this. Get this from display object.
  results->depth = 24;
  results->depthPerComponent = 8;
}

RenderWidgetHostViewEfl::RenderWidgetHostViewEfl(RenderWidgetHost* widget, EWebView* eweb_view)
  : host_(RenderWidgetHostImpl::From(widget)),
    web_view_(NULL),
    im_context_(NULL),
    evas_(NULL),
    content_image_(NULL),
    scroll_detector_(new EflWebview::ScrollDetector()),
    m_IsEvasGLInit(0),
    device_scale_factor_(1.0f),
    m_magnifier(false),
    is_loading_(false),
    gesture_recognizer_(ui::GestureRecognizer::Create()),
    current_orientation_(0),
    evas_gl_(NULL),
    evas_gl_api_(NULL),
    evas_gl_context_(NULL),
    evas_gl_surface_(NULL),
    evas_gl_config_(NULL),
    egl_image_(NULL),
    current_pixmap_id_(0),
    next_pixmap_id_(0),
    surface_id_(0),
    is_hw_accelerated_(true),
    is_modifier_key_(false) {

#if defined(OS_TIZEN)
#if !defined(EWK_BRINGUP)
  const CommandLine& command_line = *CommandLine::ForCurrentProcess();

  if (command_line.HasSwitch(switches::kUseSWRenderingPath))
      is_hw_accelerated_ = false;
#endif
#endif

  set_eweb_view(eweb_view);
  host_->SetView(this);

  static bool scale_factor_initialized = false;
  if (!scale_factor_initialized) {
    std::vector<ui::ScaleFactor> supported_scale_factors;
    supported_scale_factors.push_back(ui::SCALE_FACTOR_100P);
    supported_scale_factors.push_back(ui::SCALE_FACTOR_200P);
    ui::SetSupportedScaleFactors(supported_scale_factors);
    scale_factor_initialized = true;
  }

  gesture_recognizer_->AddGestureEventHelper(this);

  disambiguation_popup_.reset(new DisambiguationPopupEfl(content_image_, this));
}

RenderWidgetHostViewEfl::~RenderWidgetHostViewEfl() {
  if (im_context_)
    delete im_context_;
}

gfx::Point RenderWidgetHostViewEfl::ConvertPointInViewPix(gfx::Point point) {
  return gfx::ToFlooredPoint(gfx::ScalePoint(point, device_scale_factor_));
}

gfx::Rect RenderWidgetHostViewEfl::GetViewBoundsInPix() const {
  int x, y, w, h;
  evas_object_geometry_get(content_image_, &x, &y, &w, &h);
  return gfx::Rect(x, y, w, h);
}

static const char* vertexShaderSourceSimple =
  "attribute vec4 a_position;   \n"
  "attribute vec2 a_texCoord;   \n"
  "varying vec2 v_texCoord;     \n"
  "void main() {                \n"
  "  gl_Position = a_position;  \n"
  "  v_texCoord = a_texCoord;   \n"
  "}                            \n";

static const char* fragmentShaderSourceSimple =
  "precision mediump float;                            \n"
  "varying vec2 v_texCoord;                            \n"
  "uniform sampler2D s_texture;                        \n"
  "void main() {                                       \n"
  "  gl_FragColor = texture2D( s_texture, v_texCoord );\n"
  "}                                                   \n";

#if defined(NDEBUG)
#define GL_CHECK_HELPER(code, msg) \
  ((code), false)
#else
static GLenum s_gl_err;
#define GL_CHECK_HELPER(code, msg) \
  (((code), ((s_gl_err = evas_gl_api_->glGetError()) == GL_NO_ERROR)) ? false : \
      ((LOG(ERROR) << "GL Error: " << s_gl_err << "    " << msg), true))
#endif

#define GL_CHECK(code) GL_CHECK_HELPER(code, "")
#define GL_CHECK_STATUS(msg) GL_CHECK_HELPER(1, msg)

static void GLCheckProgramHelper(Evas_GL_API* api, GLuint program,
                                 const char* file, int line) {
  GLint status;
  api->glGetProgramiv(program, GL_LINK_STATUS, &status);
  if (!status) {
    const GLsizei buf_length = 2048;
    scoped_ptr<GLchar[]> log(new GLchar[buf_length]);
    GLsizei length = 0;
    api->glGetProgramInfoLog(program, buf_length, &length, log.get());
    LOG(ERROR) << "GL program link failed in: " << file << ":" << line
               << ": " << log.get();
  }
}

#define GLCheckProgram(api, program) \
    GLCheckProgramHelper(api, program, __FILE__, __LINE__)

static void GLCheckShaderHelper(
    Evas_GL_API* api, GLuint shader, const char* file, int line) {
  GLint status;
  api->glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
  if (!status) {
    const GLsizei buf_length = 2048;
    scoped_ptr<GLchar[]> log(new GLchar[buf_length]);
    GLsizei length = 0;
    api->glGetShaderInfoLog(shader, buf_length, &length, log.get());
    LOG(ERROR) << "GL shader compile failed in " << file << ":" << line
               << ": " << log.get();
  }
}

#define GLCheckShader(api, shader) \
    GLCheckShaderHelper((api), (shader), __FILE__, __LINE__)

void RenderWidgetHostViewEfl::initializeProgram() {
  evas_gl_make_current(evas_gl_, evas_gl_surface_, evas_gl_context_);

  GL_CHECK_STATUS("GL Error before program initialization");

  const char* vertexShaderSourceProgram = vertexShaderSourceSimple;
  const char* fragmentShaderSourceProgram = fragmentShaderSourceSimple;
  GLuint vertexShader = evas_gl_api_->glCreateShader(GL_VERTEX_SHADER);
  GL_CHECK_STATUS("vertex shader");
  GLuint fragmentShader = evas_gl_api_->glCreateShader(GL_FRAGMENT_SHADER);
  GL_CHECK_STATUS("fragment shader");

  const GLfloat vertex_attributes[] = {
      -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
      -1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
       1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
       1.0f, -1.0f, 0.0f, 1.0f, 0.0f};

  GL_CHECK(evas_gl_api_->glGenBuffers(1, &vertex_buffer_obj_));
  GL_CHECK(evas_gl_api_->glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_obj_));
  GL_CHECK(evas_gl_api_->glBufferData(GL_ARRAY_BUFFER,
                                      sizeof(vertex_attributes),
                                      vertex_attributes, GL_STATIC_DRAW));

  const GLfloat vertex_attributes_270[] = {
      -1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
      -1.0f,  1.0f, 0.0f, 0.0f, 0.0f,
       1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
       1.0f, -1.0f, 0.0f, 1.0f, 1.0f};

  GL_CHECK(evas_gl_api_->glGenBuffers(1, &vertex_buffer_obj_270_));
  GL_CHECK(evas_gl_api_->glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_obj_270_));
  GL_CHECK(evas_gl_api_->glBufferData(GL_ARRAY_BUFFER,
                                      sizeof(vertex_attributes_270),
                                      vertex_attributes_270, GL_STATIC_DRAW));

  const GLfloat vertex_attributes_90[] = {
      -1.0f, -1.0f, 0.0f, 0.0f, 1.0f,
      -1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
       1.0f,  1.0f, 0.0f, 1.0f, 0.0f,
       1.0f, -1.0f, 0.0f, 0.0f, 0.0f};

  GL_CHECK(evas_gl_api_->glGenBuffers(1, &vertex_buffer_obj_90_));
  GL_CHECK(evas_gl_api_->glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_obj_90_));
  GL_CHECK(evas_gl_api_->glBufferData(GL_ARRAY_BUFFER,
                                      sizeof(vertex_attributes_90),
                                      vertex_attributes_90, GL_STATIC_DRAW));

  const GLushort index_attributes[] = {0, 1, 2, 0, 2, 3};
  GL_CHECK(evas_gl_api_->glGenBuffers(1, &index_buffer_obj_));
  GL_CHECK(
      evas_gl_api_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer_obj_));
  GL_CHECK(evas_gl_api_->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                                      sizeof(index_attributes),
                                      index_attributes, GL_STATIC_DRAW));

  GL_CHECK(evas_gl_api_->glShaderSource(vertexShader, 1, &vertexShaderSourceProgram, 0));
  GL_CHECK(evas_gl_api_->glShaderSource(fragmentShader, 1, &fragmentShaderSourceProgram, 0));
  GL_CHECK(program_id_ = evas_gl_api_->glCreateProgram());
  GL_CHECK(evas_gl_api_->glCompileShader(vertexShader));
  GLCheckShader(evas_gl_api_, vertexShader);
  GL_CHECK(evas_gl_api_->glCompileShader(fragmentShader));
  GLCheckShader(evas_gl_api_, fragmentShader);
  GL_CHECK(evas_gl_api_->glAttachShader(program_id_, vertexShader));
  GL_CHECK(evas_gl_api_->glAttachShader(program_id_, fragmentShader));
  GL_CHECK(evas_gl_api_->glLinkProgram(program_id_));
  GLCheckProgram(evas_gl_api_, program_id_);

  GL_CHECK(position_attrib_ = evas_gl_api_->glGetAttribLocation(program_id_, "a_position"));
  GL_CHECK(texcoord_attrib_ = evas_gl_api_->glGetAttribLocation(program_id_, "a_texCoord"));
  GL_CHECK(source_texture_location_ = evas_gl_api_->glGetUniformLocation (program_id_, "s_texture" ));
}

void RenderWidgetHostViewEfl::PaintTextureToSurface(GLuint texture_id) {
  Evas_GL_API* gl_api = evasGlApi();
  DCHECK(gl_api);

  evas_gl_make_current(evas_gl_, evas_gl_surface_, evas_gl_context_);

  GL_CHECK_STATUS("GL error before texture paint.");

  gfx::Rect bounds = GetViewBoundsInPix();
  GL_CHECK(gl_api->glViewport(0, 0, bounds.width(), bounds.height()));
  GL_CHECK(gl_api->glClearColor(1.0, 1.0, 1.0, 1.0));
  GL_CHECK(gl_api->glClear(GL_COLOR_BUFFER_BIT));
  GL_CHECK(gl_api->glUseProgram(program_id_));

  current_orientation_ = ecore_evas_rotation_get(ecore_evas_ecore_evas_get(evas_));

  switch (current_orientation_) {
    case 270:
      GL_CHECK(gl_api->glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_obj_270_));
      break;
    case 90:
      GL_CHECK(gl_api->glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_obj_90_));
      break;
    default:
      GL_CHECK(gl_api->glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_obj_));
  } // switch(current_orientation_)

  GL_CHECK(gl_api->glEnableVertexAttribArray(position_attrib_));
  // Below 5 * sizeof(GLfloat) value specifies the size of a vertex
  // attribute (x, y, z, u, v).
  GL_CHECK(gl_api->glVertexAttribPointer(position_attrib_, 3, GL_FLOAT,
                                         GL_FALSE, 5 * sizeof(GLfloat), NULL));
  GL_CHECK(gl_api->glEnableVertexAttribArray(texcoord_attrib_));
  // Below 3 * sizeof(GLfloat) value specifies the location of texture
  // coordinate in the vertex.
  GL_CHECK(gl_api->glVertexAttribPointer(texcoord_attrib_, 2, GL_FLOAT,
                                         GL_FALSE, 5 * sizeof(GLfloat),
                                         (void*)(3 * sizeof(GLfloat))));
  GL_CHECK(gl_api->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer_obj_));

  GL_CHECK(gl_api->glActiveTexture(GL_TEXTURE0));
  GL_CHECK(gl_api->glBindTexture(GL_TEXTURE_2D, texture_id));
  GL_CHECK(gl_api->glUniform1i(source_texture_location_, 0));
  GL_CHECK(gl_api->glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, NULL));

  GL_CHECK(gl_api->glBindTexture(GL_TEXTURE_2D, 0));
  evas_gl_make_current(evas_gl_, 0, 0);
}

void RenderWidgetHostViewEfl::EvasObjectImagePixelsGetCallback(void* data, Evas_Object* obj) {
  RenderWidgetHostViewEfl* rwhv_efl = reinterpret_cast<RenderWidgetHostViewEfl*>(data);
  rwhv_efl->PaintTextureToSurface(rwhv_efl->texture_id_);
}

void RenderWidgetHostViewEfl::Init_EvasGL(int width, int height) {
  assert(width > 0 && height > 0);

  setenv("EVAS_GL_DIRECT_OVERRIDE", "1", 1);
  setenv("EVAS_GL_DIRECT_MEM_OPT", "1",1);

  evas_gl_config_ = evas_gl_config_new();
  evas_gl_config_->options_bits = EVAS_GL_OPTIONS_DIRECT;
  evas_gl_config_->color_format = EVAS_GL_RGBA_8888;
  evas_gl_config_->depth_bits = EVAS_GL_DEPTH_BIT_24;
  evas_gl_config_->stencil_bits = EVAS_GL_STENCIL_BIT_8;

  evas_gl_ = evas_gl_new(evas_);
  evas_gl_api_ = evas_gl_api_get(evas_gl_);
  evas_gl_context_ = evas_gl_context_create(
      evas_gl_, GLSharedContextEfl::GetEvasGLContext());
  if (!evas_gl_context_) {
    LOG(ERROR) << "set_eweb_view -- Create evas gl context Fail";
  }

  if(width > MAX_SURFACE_WIDTH_EGL)
    width = MAX_SURFACE_WIDTH_EGL;

  if(height > MAX_SURFACE_HEIGHT_EGL)
    height = MAX_SURFACE_HEIGHT_EGL;

  evas_gl_surface_ = evas_gl_surface_create(evas_gl_, evas_gl_config_, width, height);
  if (!evas_gl_surface_) {
    LOG(ERROR) << "set_eweb_view -- Create evas gl Surface Fail";
  } else {
    LOG(ERROR) << "set_eweb_view -- Create evas gl Surface Success";
  }

  Evas_Native_Surface nativeSurface;
  if (evas_gl_native_surface_get(evas_gl_, evas_gl_surface_, &nativeSurface)) {
    evas_object_image_native_surface_set(content_image_, &nativeSurface);
    evas_object_image_pixels_get_callback_set(content_image_, EvasObjectImagePixelsGetCallback, this);
  } else {
    LOG(ERROR) << "set_eweb_view -- Fail to get Natvie surface";
  }

  initializeProgram();

  m_IsEvasGLInit = 1;
}

void RenderWidgetHostViewEfl::set_eweb_view(EWebView* view) {
  web_view_ = view;
  evas_ = web_view_->GetEvas();
  DCHECK(evas_);

  content_image_ = web_view_->GetContentImageObject();
  DCHECK(content_image_);

  if (is_hw_accelerated_) {
    gfx::Rect bounds = GetViewBoundsInPix();
    if (bounds.width() == 0 && bounds.height() == 0) {
      LOG(ERROR)<<"set_eweb_view -- view width and height set to '0' --> skip to configure evasgl";
    } else {
      Init_EvasGL(bounds.width(), bounds.height());
    }
  }

  im_context_ = IMContextEfl::Create(this);
}

bool RenderWidgetHostViewEfl::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(RenderWidgetHostViewEfl, message)
    IPC_MESSAGE_HANDLER(EwkHostMsg_PlainTextGetContents, OnPlainTextGetContents)
    IPC_MESSAGE_HANDLER(EwkHostMsg_WebAppIconUrlGet, OnWebAppIconUrlGet)
    IPC_MESSAGE_HANDLER(EwkHostMsg_WebAppIconUrlsGet, OnWebAppIconUrlsGet)
    IPC_MESSAGE_HANDLER(EwkHostMsg_WebAppCapableGet, OnWebAppCapableGet)
    IPC_MESSAGE_HANDLER(EwkHostMsg_DidChangeContentsSize, OnDidChangeContentsSize)
    IPC_MESSAGE_HANDLER(EwkHostMsg_OrientationChangeEvent, OnOrientationChangeEvent)
    IPC_MESSAGE_HANDLER(EwkViewMsg_SelectionTextStyleState, OnSelectionTextStyleState)
    IPC_MESSAGE_HANDLER(EwkHostMsg_DidChangeMaxScrollOffset, OnDidChangeMaxScrollOffset)
    IPC_MESSAGE_HANDLER(EwkHostMsg_ReadMHTMLData, OnMHTMLContentGet)
    IPC_MESSAGE_HANDLER(EwkHostMsg_DidChangePageScaleFactor, OnDidChangePageScaleFactor)
    IPC_MESSAGE_HANDLER(EwkHostMsg_DidChangePageScaleRange, OnDidChangePageScaleRange)
#if !defined(EWK_BRINGUP)
    IPC_MESSAGE_HANDLER(ViewHostMsg_TextInputInFormStateChanged, OnTextInputInFormStateChanged)
#endif
#if defined(OS_TIZEN)
#if !defined(EWK_BRINGUP)
    IPC_MESSAGE_HANDLER(InputHostMsg_DidInputEventHandled, OnDidInputEventHandled)
#endif
#endif
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

bool RenderWidgetHostViewEfl::Send(IPC::Message* message) {
  return host_->Send(message);
}

void RenderWidgetHostViewEfl::OnSelectionTextStyleState(const SelectionStylePrams& params) {
  web_view_->OnQuerySelectionStyleReply(params);
}

#if 0
// [M37] Not support Backing store
BackingStore* RenderWidgetHostViewEfl::AllocBackingStore(const gfx::Size& size) {
  if (is_hw_accelerated_)
    return NULL;
  else
    return new BackingStoreEfl(host_, content_image_, size);
}
#endif

void RenderWidgetHostViewEfl::InitAsChild(gfx::NativeView parent_view) {
  NOTIMPLEMENTED();
}

void RenderWidgetHostViewEfl::InitAsPopup(RenderWidgetHostView*, const gfx::Rect&) {
  NOTIMPLEMENTED();
}

void RenderWidgetHostViewEfl::InitAsFullscreen(RenderWidgetHostView*) {
  NOTIMPLEMENTED();
}

RenderWidgetHost* RenderWidgetHostViewEfl::GetRenderWidgetHost() const {
  return host_;
}

Ecore_X_Window RenderWidgetHostViewEfl::GetEcoreXWindow() const {
  Ecore_Evas* ee = ecore_evas_ecore_evas_get(evas_);
  return ecore_evas_gl_x11_window_get(ee);
}

void RenderWidgetHostViewEfl::SetSize(const gfx::Size& size) {
  // This is a hack. See WebContentsView::SizeContents
  int width = std::min(size.width(), EFL_MAX_WIDTH);
  int height = std::min(size.height(), EFL_MAX_HEIGHT);
  if (popup_type_ != blink::WebPopupTypeNone) {
    // We're a popup, honor the size request.
    ecore_x_window_resize(GetEcoreXWindow(), width, height);
  }

  // Update the size of the RWH.
  //if (requested_size_.width() != width ||
  //    requested_size_.height() != height) {
    // Disabled for now, will enable it while implementing InitAsPopUp (P1) API
   //equested_size_ = gfx::Size(width, height);
    host_->SendScreenRects();
    host_->WasResized();
  //}

}

void RenderWidgetHostViewEfl::SetBounds(const gfx::Rect& rect) {
  // FIXME: ditto.
  NOTIMPLEMENTED();
}

gfx::Vector2dF RenderWidgetHostViewEfl::GetLastScrollOffset() const {
  // FIXME: Aura RWHV sets last_scroll_offset_ in OnSwapCompositorFrame()
  // Other ways to get scroll offset are already removed.
  // We need to switch to the ui::Compositor ASAP!
  return last_scroll_offset_;
}

gfx::NativeView RenderWidgetHostViewEfl::GetNativeView() const {
  // With aura this is expected to return an aura::Window*.
  // We don't have that so make sure nobody calls this.
  // NOTREACHED();
  return gfx::NativeView();
}

gfx::NativeViewId RenderWidgetHostViewEfl::GetNativeViewId() const {
  if (m_IsEvasGLInit) {
    Ecore_Evas* ee = ecore_evas_ecore_evas_get(evas_);
    return ecore_evas_window_get(ee);
  } else {
    return 0;
  }
}

gfx::NativeViewAccessible RenderWidgetHostViewEfl::GetNativeViewAccessible() {
  NOTIMPLEMENTED();
  return 0;
}

bool RenderWidgetHostViewEfl::IsSurfaceAvailableForCopy() const {
#warning "[M37] GetBackingStore does not exist. backing store removed from chromium"
  //return !!host_->GetBackingStore(false);
  return false;
}

void RenderWidgetHostViewEfl::Show() {
//  if (is_hw_accelerated_)
    evas_object_show(content_image_);
}

void RenderWidgetHostViewEfl::Hide() {
  //evas_object_hide(content_image_);
}

bool RenderWidgetHostViewEfl::IsShowing() {
  return evas_object_visible_get(content_image_);
}

gfx::Rect RenderWidgetHostViewEfl::GetViewBounds() const {
  return ConvertRectToDIP(device_scale_factor_, GetViewBoundsInPix());
}

bool RenderWidgetHostViewEfl::LockMouse() {
  NOTIMPLEMENTED();
  return false;
}

void RenderWidgetHostViewEfl::UnlockMouse() {
  NOTIMPLEMENTED();
}

void RenderWidgetHostViewEfl::WasShown() {
  host_->WasShown(ui::LatencyInfo());
}

void RenderWidgetHostViewEfl::WasHidden() {
  host_->WasHidden();
}

void RenderWidgetHostViewEfl::Focus() {
  web_view_->SetFocus(EINA_TRUE);
  host_->Focus();
}

bool RenderWidgetHostViewEfl::HasFocus() const {
  return web_view_->HasFocus();
}

void RenderWidgetHostViewEfl::MovePluginContainer(const WebPluginGeometry& move) {
  Ecore_X_Window surface_window = 0;
  PluginWindowToWidgetMap::const_iterator i = plugin_window_to_widget_map_.find(move.window);

  if (i != plugin_window_to_widget_map_.end())
    surface_window = i->second;

  if (!surface_window)
    return;

  if (!move.visible) {
    ecore_x_window_hide(surface_window);
    return;
  }

  ecore_x_window_show(surface_window);

  if (!move.rects_valid)
    return;

  ecore_x_window_move(surface_window, move.window_rect.x(), move.window_rect.y());
  ecore_x_window_resize(surface_window, move.window_rect.width(), move.window_rect.height());
}

void RenderWidgetHostViewEfl::MovePluginWindows(
    const std::vector<WebPluginGeometry>& moves) {
  for (size_t i = 0; i < moves.size(); i++)
    MovePluginContainer(moves[i]);
}

void RenderWidgetHostViewEfl::Blur() {
  host_->Blur();
}

void RenderWidgetHostViewEfl::UpdateCursor(const WebCursor& webcursor) {
  if (is_loading_) {
    // Setting native Loading cursor
    ecore_x_window_cursor_set(GetEcoreXWindow(), ecore_x_cursor_shape_get(ECORE_X_CURSOR_CLOCK));
  } else {
    WebCursor::CursorInfo cursor_info;
    webcursor.GetCursorInfo(&cursor_info);

    int cursor_type = GetCursorType(cursor_info.type);
    ecore_x_window_cursor_set(GetEcoreXWindow(), ecore_x_cursor_shape_get(cursor_type));
  }
  // Need to check for cursor visibility
  //ecore_x_window_cursor_show(GetEcoreXWindow(), true);

}

void RenderWidgetHostViewEfl::SetIsLoading(bool is_loading) {
  is_loading_ = is_loading;
  UpdateCursor(WebCursor());
  if (disambiguation_popup_)
    disambiguation_popup_->Dismiss();
}

void RenderWidgetHostViewEfl::TextInputStateChanged(
    const ViewHostMsg_TextInputState_Params& params) {
  if (!params.show_ime_if_needed && !eweb_view()->GetSettings()->useKeyPadWithoutUserAction())
    return;

  if (im_context_) {
    im_context_->UpdateInputMethodState(params.type);
    web_view_->QuerySelectionStyle();

    // Obsolete TextInputTypeChanged was doing it in similar code block
    // Probably also required here
    // Make Empty Rect for inputFieldZoom Gesture
    // Finally, the empty rect is not used.
    // host_->ScrollFocusedEditableNodeIntoRect(gfx::Rect(0, 0, 0, 0));}
  }

  if (GetSelectionController()) {
    GetSelectionController()->SetSelectionEditable(
        params.type == ui::TEXT_INPUT_TYPE_TEXT ||
        params.type == ui::TEXT_INPUT_TYPE_PASSWORD ||
        params.type == ui::TEXT_INPUT_TYPE_TEXT_AREA ||
        params.type == ui::TEXT_INPUT_TYPE_CONTENT_EDITABLE);
  }
}

void RenderWidgetHostViewEfl::ImeCancelComposition() {
  if (im_context_)
    im_context_->CancelComposition();
}

void RenderWidgetHostViewEfl::OnTextInputInFormStateChanged(bool is_in_form_tag) {
  if (im_context_)
    im_context_->SetIsInFormTag(is_in_form_tag);
}

void RenderWidgetHostViewEfl::ImeCompositionRangeChanged(
  const gfx::Range& range,
  const std::vector<gfx::Rect>& character_bounds) {
  SelectionControllerEfl* controller = web_view_->GetSelectionController();
  if (controller) {
    controller->SetCaretSelectionStatus(false);
    controller->HideHandleAndContextMenu();
  }
}

void RenderWidgetHostViewEfl::FocusedNodeChanged(bool is_editable_node) {
  SelectionControllerEfl* controller = web_view_->GetSelectionController();
  if (controller) {
    controller->SetCaretSelectionStatus(false);
    controller->HideHandleAndContextMenu();
  }
}

void RenderWidgetHostViewEfl::Destroy() {
  delete this;
}

void RenderWidgetHostViewEfl::SetTooltipText(const base::string16& text) {
  web_view_->SmartCallback<EWebViewCallbacks::TooltipTextSet>().call(UTF16ToUTF8(text).c_str());
}

void RenderWidgetHostViewEfl::SelectionChanged(const base::string16& text,
  size_t offset,
  const gfx::Range& range) {
  SelectionControllerEfl* controller = web_view_->GetSelectionController();
  if (controller)
    controller->UpdateSelectionData(text);
}

void RenderWidgetHostViewEfl::SelectionBoundsChanged(
  const ViewHostMsg_SelectionBounds_Params& params) {
  ViewHostMsg_SelectionBounds_Params guest_params(params);
  guest_params.anchor_rect = ConvertRectToPixel(device_scale_factor_, params.anchor_rect);
  guest_params.focus_rect = ConvertRectToPixel(device_scale_factor_, params.focus_rect);

  if (im_context_)
    im_context_->UpdateCaretBounds(gfx::UnionRects(guest_params.anchor_rect, guest_params.focus_rect));

  SelectionControllerEfl* controller = GetSelectionController();
  if (controller)
    controller->UpdateSelectionDataAndShow(guest_params.anchor_rect, guest_params.focus_rect, guest_params.is_anchor_first);
}

void RenderWidgetHostViewEfl::DidStopFlinging() {
#ifdef TIZEN_EDGE_EFFECT
  web_view_->edgeEffect()->hide();
#endif
  // Unhide Selection UI when scrolling with fling gesture
  if (GetSelectionController() && GetSelectionController()->GetScrollStatus())
    GetSelectionController()->SetScrollStatus(false);
}

void RenderWidgetHostViewEfl::ShowDisambiguationPopup(const gfx::Rect& rect_pixels, const SkBitmap& zoomed_bitmap) {
  disambiguation_popup_->Show(rect_pixels, zoomed_bitmap);
}

bool RenderWidgetHostViewEfl::CanDispatchToConsumer(ui::GestureConsumer* consumer) {
  return this == consumer;
}

void RenderWidgetHostViewEfl::DispatchCancelTouchEvent(ui::TouchEvent* event) {
}

void RenderWidgetHostViewEfl::DispatchGestureEvent(ui::GestureEvent*) {
}

#ifdef OS_TIZEN
void RenderWidgetHostViewEfl::SetRectSnapshot(const SkBitmap& bitmap) {
  web_view_->UpdateMagnifierScreen(bitmap);
}

void RenderWidgetHostViewEfl::GetSnapshotForRect(gfx::Rect& rect) {
#if !defined(EWK_BRINGUP)
  GpuProcessHost::SendOnIO(
    GpuProcessHost::GPU_PROCESS_KIND_SANDBOXED,
    CAUSE_FOR_GPU_LAUNCH_NO_LAUNCH,
    new GpuMsg_GetPixelRegion(surface_id_, rect));
#endif
}
#endif

void RenderWidgetHostViewEfl::CopyFromCompositingSurface(
  const gfx::Rect& src_subrect,
  const gfx::Size& /* dst_size */,
  const base::Callback<void(bool, const SkBitmap&)>& callback,
  const SkColorType color_type) {
  // FIXME: should find a way to do it effectively.
#warning "[M37] host_ does not have GetSnapshotFromRenderer function anymore"
  // host_->GetSnapshotFromRenderer(src_subrect, callback);
}

// CopyFromCompositingSurfaceToVideoFrame implementation borrowed from Aura port
bool RenderWidgetHostViewEfl::CanSubscribeFrame() const {
  return true;
}

void RenderWidgetHostViewEfl::BeginFrameSubscription(
    scoped_ptr<RenderWidgetHostViewFrameSubscriber> subscriber) {
  frame_subscriber_ = subscriber.Pass();
}

void RenderWidgetHostViewEfl::EndFrameSubscription() {
  idle_frame_subscriber_textures_.clear();
  frame_subscriber_.reset();
}

#ifdef TIZEN_EDGE_EFFECT
void RenderWidgetHostViewEfl::DidOverscroll(const DidOverscrollParams& params) {
  const gfx::Vector2dF& accumulated_overscroll = params.accumulated_overscroll;
  const gfx::Vector2dF& latest_overscroll_delta = params.latest_overscroll_delta;

  if (latest_overscroll_delta.x() < 0 && (int)accumulated_overscroll.x() < 0)
    web_view_->edgeEffect()->show("edge,left");
  if (latest_overscroll_delta.x() > 0 && (int)accumulated_overscroll.x() > 0)
    web_view_->edgeEffect()->show("edge,right");
  if (latest_overscroll_delta.y() < 0 && (int)accumulated_overscroll.y() < 0)
    web_view_->edgeEffect()->show("edge,top");
  if (latest_overscroll_delta.y() > 0 && (int)accumulated_overscroll.y() > 0)
    web_view_->edgeEffect()->show("edge,bottom");
}
#endif

#ifdef TIZEN_CONTENTS_DETECTION
void RenderWidgetHostViewEfl::OnContentsDetected(const char* message) {
  web_view_->ShowContentsDetectedPopup(message);
}
#endif

void RenderWidgetHostViewEfl::ReturnSubscriberTexture(
    base::WeakPtr<RenderWidgetHostViewEfl> rwhvefl,
    scoped_refptr<OwnedMailbox> subscriber_texture,
    uint32 sync_point) {
  if (!subscriber_texture.get())
    return;
  if (!rwhvefl)
    return;
  DCHECK_NE(
      rwhvefl->active_frame_subscriber_textures_.count(subscriber_texture.get()),
      0u);

  subscriber_texture->UpdateSyncPoint(sync_point);

  rwhvefl->active_frame_subscriber_textures_.erase(subscriber_texture.get());
  if (rwhvefl->frame_subscriber_ && subscriber_texture->texture_id())
    rwhvefl->idle_frame_subscriber_textures_.push_back(subscriber_texture);
}

void RenderWidgetHostViewEfl::CopyFromCompositingSurfaceFinishedForVideo(
    base::WeakPtr<RenderWidgetHostViewEfl> rwhvefl,
    const base::Callback<void(bool)>& callback,
    scoped_refptr<OwnedMailbox> subscriber_texture,
    scoped_ptr<cc::SingleReleaseCallback> release_callback,
    bool result) {
  callback.Run(result);

  GLHelper* gl_helper = ImageTransportFactory::GetInstance()->GetGLHelper();
  uint32 sync_point = gl_helper ? gl_helper->InsertSyncPoint() : 0;
  if (release_callback) {
    // A release callback means the texture came from the compositor, so there
    // should be no |subscriber_texture|.
    DCHECK(!subscriber_texture.get());
    release_callback->Run(sync_point, false);
  }
  ReturnSubscriberTexture(rwhvefl, subscriber_texture, sync_point);
}

void RenderWidgetHostViewEfl::CopyFromCompositingSurfaceHasResultForVideo(
    base::WeakPtr<RenderWidgetHostViewEfl> rwhvefl,
    scoped_refptr<OwnedMailbox> subscriber_texture,
    scoped_refptr<media::VideoFrame> video_frame,
    const base::Callback<void(bool)>& callback,
    scoped_ptr<cc::CopyOutputResult> result) {
  base::ScopedClosureRunner scoped_callback_runner(base::Bind(callback, false));
  base::ScopedClosureRunner scoped_return_subscriber_texture(
      base::Bind(&ReturnSubscriberTexture, rwhvefl, subscriber_texture, 0));

  if (!rwhvefl)
    return;
  if (result->IsEmpty())
    return;
  if (result->size().IsEmpty())
    return;

  // Compute the dest size we want after the letterboxing resize. Make the
  // coordinates and sizes even because we letterbox in YUV space
  // (see CopyRGBToVideoFrame). They need to be even for the UV samples to
  // line up correctly.
  // The video frame's coded_size() and the result's size() are both physical
  // pixels.
  gfx::Rect region_in_frame =
      media::ComputeLetterboxRegion(gfx::Rect(video_frame->coded_size()),
                                    result->size());
  region_in_frame = gfx::Rect(region_in_frame.x() & ~1,
                              region_in_frame.y() & ~1,
                              region_in_frame.width() & ~1,
                              region_in_frame.height() & ~1);
  if (region_in_frame.IsEmpty())
    return;

  if (!result->HasTexture()) {
    DCHECK(result->HasBitmap());
    scoped_ptr<SkBitmap> bitmap = result->TakeBitmap();
    // Scale the bitmap to the required size, if necessary.
    SkBitmap scaled_bitmap;
    if (result->size().width() != region_in_frame.width() ||
        result->size().height() != region_in_frame.height()) {
      skia::ImageOperations::ResizeMethod method =
          skia::ImageOperations::RESIZE_GOOD;
      scaled_bitmap = skia::ImageOperations::Resize(*bitmap.get(), method,
                                                    region_in_frame.width(),
                                                    region_in_frame.height());
    } else {
      scaled_bitmap = *bitmap.get();
    }

    {
      SkAutoLockPixels scaled_bitmap_locker(scaled_bitmap);

      media::CopyRGBToVideoFrame(
          reinterpret_cast<uint8*>(scaled_bitmap.getPixels()),
          scaled_bitmap.rowBytes(),
          region_in_frame,
          video_frame.get());
    }
    ignore_result(scoped_callback_runner.Release());
    callback.Run(true);
    return;
  }

  ImageTransportFactory* factory = ImageTransportFactory::GetInstance();
  GLHelper* gl_helper = factory->GetGLHelper();
  if (!gl_helper)
    return;
  if (subscriber_texture.get() && !subscriber_texture->texture_id())
    return;

  cc::TextureMailbox texture_mailbox;
  scoped_ptr<cc::SingleReleaseCallback> release_callback;
  result->TakeTexture(&texture_mailbox, &release_callback);
  DCHECK(texture_mailbox.IsTexture());
  if (!texture_mailbox.IsTexture())
    return;

  gfx::Rect result_rect(result->size());

  content::ReadbackYUVInterface* yuv_readback_pipeline =
      rwhvefl->yuv_readback_pipeline_.get();
  if (yuv_readback_pipeline == NULL ||
      yuv_readback_pipeline->scaler()->SrcSize() != result_rect.size() ||
      yuv_readback_pipeline->scaler()->SrcSubrect() != result_rect ||
      yuv_readback_pipeline->scaler()->DstSize() != region_in_frame.size()) {
    GLHelper::ScalerQuality quality = GLHelper::SCALER_QUALITY_FAST;
    std::string quality_switch = switches::kTabCaptureDownscaleQuality;
    // If we're scaling up, we can use the "best" quality.
    if (result_rect.size().width() < region_in_frame.size().width() &&
        result_rect.size().height() < region_in_frame.size().height())
      quality_switch = switches::kTabCaptureUpscaleQuality;

    std::string switch_value =
        CommandLine::ForCurrentProcess()->GetSwitchValueASCII(quality_switch);
    if (switch_value == "fast")
      quality = GLHelper::SCALER_QUALITY_FAST;
    else if (switch_value == "good")
      quality = GLHelper::SCALER_QUALITY_GOOD;
    else if (switch_value == "best")
      quality = GLHelper::SCALER_QUALITY_BEST;

    rwhvefl->yuv_readback_pipeline_.reset(
        gl_helper->CreateReadbackPipelineYUV(quality,
                                             result_rect.size(),
                                             result_rect,
                                             video_frame->coded_size(),
                                             region_in_frame,
                                             true,
                                             true));
    yuv_readback_pipeline = rwhvefl->yuv_readback_pipeline_.get();
  }

  ignore_result(scoped_callback_runner.Release());
  ignore_result(scoped_return_subscriber_texture.Release());
  base::Callback<void(bool result)> finished_callback = base::Bind(
      &RenderWidgetHostViewEfl::CopyFromCompositingSurfaceFinishedForVideo,
      rwhvefl->AsWeakPtr(),
      callback,
      subscriber_texture,
      base::Passed(&release_callback));
  yuv_readback_pipeline->ReadbackYUV(
      texture_mailbox.mailbox(),
      texture_mailbox.sync_point(),
      video_frame,
      finished_callback);
}

// Efl port - Implementation done, will enable this function after getting video test site to verify
void RenderWidgetHostViewEfl::CopyFromCompositingSurfaceToVideoFrame(
  const gfx::Rect& src_subrect,
  const scoped_refptr<media::VideoFrame>& target,
  const base::Callback<void(bool)>& callback) {
  NOTIMPLEMENTED();
  callback.Run(false);
}

bool RenderWidgetHostViewEfl::CanCopyToVideoFrame() const {
#warning "[M37] host_ no longer has is_accelerated_compositing_active function"
  //return host_->is_accelerated_compositing_active();
  return false;
}

void RenderWidgetHostViewEfl::AcceleratedSurfaceInitialized(int host_id, int route_id) {
  // FIXME: new API in M34. need proper implementation.
  NOTIMPLEMENTED();
}

// Defined in gl_current_context_efl.cc because of conflicts of
// texture_manager.h with efl GL API wrappers.

extern GLuint GetTextureIdFromTexture(gpu::gles2::Texture* texture);

void RenderWidgetHostViewEfl::AcceleratedSurfaceBuffersSwapped(
  const GpuHostMsg_AcceleratedSurfaceBuffersSwapped_Params& params,
  int gpu_host_id) {

  if (m_IsEvasGLInit) {
    gpu::gles2::MailboxManager* manager =
        GLSharedContextEfl::GetMailboxManager();

    gpu::gles2::Texture* texture =
        manager->ConsumeTexture(GL_TEXTURE_2D, params.mailbox);

    texture_id_ = GetTextureIdFromTexture(texture);
    evas_object_image_pixels_dirty_set(content_image_, true);
  }

  AcceleratedSurfaceMsg_BufferPresented_Params ack_params;
  ack_params.sync_point = 0;
  RenderWidgetHostImpl::AcknowledgeBufferPresent(
    params.route_id, gpu_host_id, ack_params);
}

void RenderWidgetHostViewEfl::AcceleratedSurfacePostSubBuffer(
  const GpuHostMsg_AcceleratedSurfacePostSubBuffer_Params& params,
  int gpu_host_id) {
  NOTIMPLEMENTED();
}

void RenderWidgetHostViewEfl::AcceleratedSurfaceSuspend() {
  NOTIMPLEMENTED();
}

void RenderWidgetHostViewEfl::AcceleratedSurfaceRelease() {
  NOTIMPLEMENTED();
}

bool RenderWidgetHostViewEfl::HasAcceleratedSurface(const gfx::Size&) {
  return false;
}

void RenderWidgetHostViewEfl::GetScreenInfo(
    blink::WebScreenInfo* results) {
  gfx::Screen* screen = gfx::Screen::GetNativeScreen();
  if (!screen)
    return;

  const gfx::Display display = screen->GetPrimaryDisplay();
  results->rect = display.bounds();
  results->availableRect = display.work_area();

  device_scale_factor_ = display.device_scale_factor();
  results->deviceScaleFactor = device_scale_factor_;

  // TODO(derat|oshima): Don't hardcode this. Get this from display object.
  results->depth = 24;
  results->depthPerComponent = 8;
}

gfx::Rect RenderWidgetHostViewEfl::GetBoundsInRootWindow() {
  Ecore_Evas* ee = ecore_evas_ecore_evas_get(evas_);
  int x, y, w, h;
  ecore_evas_geometry_get(ee, &x, &y, &w, &h);
  return gfx::Rect(x, y, w, h);
}

gfx::GLSurfaceHandle RenderWidgetHostViewEfl::GetCompositingSurface() {
  if (is_hw_accelerated_) {
    return gfx::GLSurfaceHandle(gfx::kNullPluginWindow, gfx::TEXTURE_TRANSPORT);
  }
}

void RenderWidgetHostViewEfl::ResizeCompositingSurface(const gfx::Size& size) {
  web_view_->DidChangeContentsArea(size.width(), size.height());
}

void RenderWidgetHostViewEfl::RenderProcessGone(base::TerminationStatus, int error_code) {
  // RenderWidgetHostImpl sets |view_| i.e. RenderWidgetHostViewEfl to NULL immediately after this call.
  // It expects RenderWidgetHostView to delete itself.
  // We only inform |web_view_| that renderer has crashed.
  // and in "process,crashed" callback, app is expected to delete the view.
  web_view_->set_renderer_crashed();
  Destroy();
}

void RenderWidgetHostViewEfl::HandleShow() {
  host_->WasShown(ui::LatencyInfo());
}

void RenderWidgetHostViewEfl::HandleHide() {
  host_->WasHidden();
}

void RenderWidgetHostViewEfl::HandleResize(int width, int height) {
// Have to use  UpdateScreenInfo(GetNativeView()); when real native surface is used.
  host_->WasResized();
}

void RenderWidgetHostViewEfl::HandleFocusIn() {
  if (im_context_)
    im_context_->OnFocusIn();

  host_->SetActive(true);
  host_->GotFocus();
  //Will resume the videos playbacks if any were paused when Application was
  // hidden
  host_->WasShown(ui::LatencyInfo());
}

void RenderWidgetHostViewEfl::HandleFocusOut() {
  if (im_context_)
    im_context_->OnFocusOut();

  host_->SetActive(false);
  host_->LostCapture();
  Blur();
}

void RenderWidgetHostViewEfl::set_magnifier(bool status) {
   m_magnifier = status;
}

void RenderWidgetHostViewEfl::HandleEvasEvent(const Evas_Event_Mouse_Down* event) {
  host_->ForwardMouseEvent(WebEventFactoryEfl::toWebMouseEvent(web_view_->GetEvas(), web_view_->evas_object(), event, device_scale_factor_));
}

void RenderWidgetHostViewEfl::HandleEvasEvent(const Evas_Event_Mouse_Up* event) {
  if (im_context_)
    im_context_->Reset();

  host_->ForwardMouseEvent(WebEventFactoryEfl::toWebMouseEvent(web_view_->GetEvas(), web_view_->evas_object(), event, device_scale_factor_));
}

void RenderWidgetHostViewEfl::HandleEvasEvent(const Evas_Event_Mouse_Move* event) {
  host_->ForwardMouseEvent(WebEventFactoryEfl::toWebMouseEvent(web_view_->GetEvas(), web_view_->evas_object(), event, device_scale_factor_));
}

void RenderWidgetHostViewEfl::HandleEvasEvent(const Evas_Event_Mouse_Wheel* event) {
  host_->ForwardWheelEvent(WebEventFactoryEfl::toWebMouseEvent(web_view_->GetEvas(), web_view_->evas_object(), event, device_scale_factor_));
}

void RenderWidgetHostViewEfl::HandleEvasEvent(const Evas_Event_Key_Down* event) {
  bool wasFiltered = false;

  if (WebEventFactoryEfl::isHardwareBackKey(event) && disambiguation_popup_) {
    disambiguation_popup_->Dismiss();
  }

  if (!strcmp(event->key, "XF86Phone")) {
    host_->WasHidden();
  }

  if (!strcmp(event->key, "XF86PowerOff")) {
    host_->WasHidden();
  }

#ifdef TIZEN_CONTENTS_DETECTION
  if (!strcmp(event->key, "XF86Stop")) {
    PopupControllerEfl* popup_controller = web_view_->GetPopupController();
    if (popup_controller)
      popup_controller->closePopup();
  }
#endif

  //if (!strcmp(event->key, "XF86Stop") || !strcmp(event->key, "BackSpace")) {
  if (!strcmp(event->key, "BackSpace")) {
    SelectionControllerEfl* controller = web_view_->GetSelectionController();
    if (controller)
      controller->HideHandleAndContextMenu();
  }

  if (im_context_) {
    im_context_->HandleKeyDownEvent(event, &wasFiltered);
    NativeWebKeyboardEvent n_event = WebEventFactoryEfl::toWebKeyboardEvent(evas_, event);

    if (wasFiltered)
      n_event.isSystemKey = true;

    // Do not forward keyevent now if there is fake key event
    // handling at the moment to preserve orders of events as in Webkit
    if (im_context_->GetPreeditQueue().empty() ||
        keyupev_queue_.empty()) {
      host_->ForwardKeyboardEvent(n_event);
    } else {
      NativeWebKeyboardEvent *n_event_ptr = new NativeWebKeyboardEvent();

      n_event_ptr->timeStampSeconds = n_event.timeStampSeconds;
      n_event_ptr->modifiers = n_event.modifiers;
      n_event_ptr->type = n_event.type;
      n_event_ptr->nativeKeyCode = n_event.nativeKeyCode;
      n_event_ptr->windowsKeyCode = n_event.windowsKeyCode;
      n_event_ptr->isSystemKey = n_event.isSystemKey;
      n_event_ptr->unmodifiedText[0] = n_event.unmodifiedText[0];
      n_event_ptr->text[0] = n_event.text[0];

      keydownev_queue_.push(n_event_ptr);
    }

    keyupev_queue_.push(n_event.windowsKeyCode);
  } else
    host_->ForwardKeyboardEvent(WebEventFactoryEfl::toWebKeyboardEvent(evas_, event));
}

void RenderWidgetHostViewEfl::HandleEvasEvent(const Evas_Event_Key_Up* event) {
  bool wasFiltered = false;
  if (im_context_)
    im_context_->HandleKeyUpEvent(event, &wasFiltered);

  if (!im_context_)
      host_->ForwardKeyboardEvent(WebEventFactoryEfl::toWebKeyboardEvent(evas_, event));
}

#ifdef OS_TIZEN
void RenderWidgetHostViewEfl::FilterInputMotion(const blink::WebGestureEvent& gesture_event) {
  if (gesture_event.type == blink::WebInputEvent::GesturePinchUpdate) {
    Evas_Coord_Point position;

    position.x = gesture_event.x;
    position.y = gesture_event.y;
    wkext_motion_tilt_position_update(&position);
  }
}

void RenderWidgetHostViewEfl::makePinchZoom(void* eventInfo) {
#if !defined(EWK_BRINGUP)
  Wkext_Motion_Event* motionEvent = static_cast<Wkext_Motion_Event*>(eventInfo);

  ui::GestureEvent event(ui::ET_GESTURE_PINCH_UPDATE,
      motionEvent->position.x, motionEvent->position.y, 0, ui::EventTimeForNow(),
      ui::GestureEventDetails(ui::ET_GESTURE_PINCH_UPDATE, motionEvent->scale, 0), 1);
  HandleGesture(&event);
#endif
}

void RenderWidgetHostViewEfl::OnDidInputEventHandled(const blink::WebInputEvent* input_event, bool processed) {
#if !defined(EWK_BRINGUP)
  if (!im_context_)
    return;

  if (blink::WebInputEvent::isKeyboardEventType(input_event->type)) {
    if (input_event->type == blink::WebInputEvent::KeyDown) {

      // Handling KeyDown event of modifier key(Shift for example)
      if (input_event->modifiers && !is_modifier_key_) {
        is_modifier_key_ = true;
        return;
      }
      // Handling KeyDown event of key+modifier (Shift+a=A for example)
      if (is_modifier_key_) {
        HandleCommitQueue(processed);
        HandlePreeditQueue(processed);
        HandleKeyUpQueue();
        HandleKeyDownQueue();
        is_modifier_key_ = false;
      }

      HandleCommitQueue(processed);
      HandlePreeditQueue(processed);

      HandleKeyUpQueue();
      HandleKeyDownQueue();
    }
  }
#endif
}
#endif

void RenderWidgetHostViewEfl::HandleGesture(ui::GestureEvent* event) {
  if ((event->type() == ui::ET_GESTURE_PINCH_BEGIN ||
       event->type() == ui::ET_GESTURE_PINCH_UPDATE ||
       event->type() == ui::ET_GESTURE_PINCH_END) &&
      (!pinch_zoom_enabled_ || eweb_view()->IsFullscreen())) {
    event->SetHandled();
    return;
  }

  if (event->type() == ui::ET_GESTURE_PINCH_END) {
    eweb_view()->SmartCallback<EWebViewCallbacks::ZoomFinished>().call();
  }

  blink::WebGestureEvent gesture = content::MakeWebGestureEventFromUIEvent(*event);

  if (event->type() == ui::ET_GESTURE_TAP ||
      event->type() == ui::ET_GESTURE_TAP_CANCEL) {
      float size = 32.0f; //Default value
#if defined(OS_TIZEN_MOBILE)
      size = elm_config_finger_size_get() / device_scale_factor();
#endif
      gesture.data.tap.width = size;
      gesture.data.tap.height = size;
  }

  gesture.x = event->x();
  gesture.y = event->y();

  const gfx::Point root_point = event->root_location();
  gesture.globalX = root_point.x();
  gesture.globalY = root_point.y();

  if (event->type() == ui::ET_GESTURE_TAP_DOWN) {
    // Webkit does not stop a fling-scroll on tap-down. So explicitly send an
    // event to stop any in-progress flings.
    blink::WebGestureEvent fling_cancel = gesture;
    fling_cancel.type = blink::WebInputEvent::GestureFlingCancel;
    fling_cancel.sourceDevice = blink::WebGestureDeviceTouchscreen;
    host_->ForwardGestureEvent(fling_cancel);
  } else if (event->type() == ui::ET_GESTURE_SCROLL_UPDATE) {
    if (m_magnifier)
      return;
  } else if (event->type() == ui::ET_GESTURE_SCROLL_BEGIN) {
    if (GetSelectionController())
      GetSelectionController()->SetScrollStatus(true);
  } else if (event->type() == ui::ET_GESTURE_SCROLL_END) {
    if (GetSelectionController() && GetSelectionController()->GetScrollStatus())
      GetSelectionController()->SetScrollStatus(false);
  } else if (event->type() == ui::ET_GESTURE_TAP_CANCEL) {
    if (GetSelectionController() && GetSelectionController()->GetSelectionStatus()) {
      GetSelectionController()->UpdateSelectionDataAndShow(
        GetSelectionController()->GetLeftRect(),
        GetSelectionController()->GetRightRect(),
        false /* unused */);
    }
  } else if (event->type() == ui::ET_GESTURE_END) {
    // Gesture end event is received (1) After scroll end (2) After Fling start
#ifdef TIZEN_EDGE_EFFECT
    web_view_->edgeEffect()->hide();
#endif
  }
#ifdef TIZEN_EDGE_EFFECT
  else if (event->type() == ui::ET_GESTURE_SCROLL_UPDATE) {
    if (gesture.data.scrollUpdate.deltaX < 0)
      web_view_->edgeEffect()->hide("edge,left");
    else if (gesture.data.scrollUpdate.deltaX > 0)
      web_view_->edgeEffect()->hide("edge,right");
    if (gesture.data.scrollUpdate.deltaY < 0)
      web_view_->edgeEffect()->hide("edge,top");
    else if (gesture.data.scrollUpdate.deltaY > 0)
      web_view_->edgeEffect()->hide("edge,bottom");
  } else if (event->type() == ui::ET_GESTURE_PINCH_BEGIN) {
      web_view_->edgeEffect()->disable();
  } else if (event->type() == ui::ET_GESTURE_PINCH_END) {
      web_view_->edgeEffect()->enable();
  }
#endif

#ifdef OS_TIZEN
  FilterInputMotion(gesture);
  if (gesture.type != blink::WebInputEvent::Undefined)
    host_->ForwardGestureEventWithLatencyInfo(gesture, *event->latency());
#endif

  event->SetHandled();
}

// Copied from render_widget_host_view_aura.cc
void UpdateWebTouchEventAfterDispatch(blink::WebTouchEvent* event,
  blink::WebTouchPoint* point) {
  if (point->state != blink::WebTouchPoint::StateReleased
      && point->state != blink::WebTouchPoint::StateCancelled)
    return;
  --event->touchesLength;
  for (unsigned i = point - event->touches;
       i < event->touchesLength;
       ++i)
    event->touches[i] = event->touches[i + 1];
}

void RenderWidgetHostViewEfl::HandleTouchEvent(ui::TouchEvent* event) {
  if (!gesture_recognizer_->ProcessTouchEventPreDispatch(*event, this)) {
    event->StopPropagation();
    return;
  }

  // Update the touch event first.
  blink::WebTouchPoint* point =
    content::UpdateWebTouchEventFromUIEvent(*event, &touch_event_);
  // Forward the touch event only if a touch point was updated, and there's a
  // touch-event handler in the page, and no other touch-event is in the queue.
  // It is important to always consume the event if there is a touch-event
  // handler in the page, or some touch-event is already in the queue, even if
  // no point has been updated, to make sure that this event does not get
  // processed by the gesture recognizer before the events in the queue.
  if (host_->ShouldForwardTouchEvent()) {
    event->StopPropagation();
  }

  bool forwarded = false;
  if (point) {
    if (host_->ShouldForwardTouchEvent()) {
      forwarded = true;
      host_->ForwardTouchEventWithLatencyInfo(touch_event_, *event->latency());
    }
    UpdateWebTouchEventAfterDispatch(&touch_event_, point);
  }

  // If we forward it to the renderer than either blink handles it or we will
  // have a second round with it in ProcessAckedTouchEvent.
  if (forwarded)
    return;

  scoped_ptr<ui::GestureRecognizer::Gestures> gestures(
        gesture_recognizer_->ProcessTouchEventPostDispatch(*event, ui::ER_UNHANDLED, this));
  if (!gestures)
    return;
  for (size_t j = 0; j < gestures->size(); ++j) {
    ui::GestureEvent* event = gestures->get().at(j);
    HandleGesture(event);
  }
}

void RenderWidgetHostViewEfl::ProcessAckedTouchEvent(
    const TouchEventWithLatencyInfo& touch,
    InputEventAckState ack_result) {
  ScopedVector<ui::TouchEvent> events;
  if (!MakeUITouchEventsFromWebTouchEvents(touch, &events, LOCAL_COORDINATES))
    return;

  ui::EventResult result = (ack_result == INPUT_EVENT_ACK_STATE_CONSUMED) ?
      ui::ER_HANDLED : ui::ER_UNHANDLED;
  for (ScopedVector<ui::TouchEvent>::const_iterator iter = events.begin(),
      end = events.end(); iter != end; ++iter)  {
    scoped_ptr<ui::GestureRecognizer::Gestures> gestures(
        gesture_recognizer_->ProcessTouchEventOnAsyncAck(**iter, result, this));
    if (gestures) {
      for (size_t j = 0; j < gestures->size(); ++j) {
        ui::GestureEvent* event = gestures->get().at(j);
        HandleGesture(event);
      }
    }
  }
}

void RenderWidgetHostViewEfl::OnPlainTextGetContents(const std::string& content_text, int plain_text_get_callback_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  eweb_view()->InvokePlainTextGetCallback(content_text, plain_text_get_callback_id);
}

void RenderWidgetHostViewEfl::OnWebAppCapableGet(bool capable, int callback_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  eweb_view()->InvokeWebAppCapableGetCallback(capable, callback_id);
}

void RenderWidgetHostViewEfl::OnWebAppIconUrlGet(const std::string &icon_url, int callback_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  eweb_view()->InvokeWebAppIconUrlGetCallback(icon_url, callback_id);
}

void RenderWidgetHostViewEfl::OnWebAppIconUrlsGet(const std::map<std::string, std::string> &icon_urls, int callback_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  eweb_view()->InvokeWebAppIconUrlsGetCallback(icon_urls, callback_id);
}

void RenderWidgetHostViewEfl::OnDidChangeContentsSize(int width, int height) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  web_view_->DidChangeContentsSize(width, height);
  host_->ScrollFocusedEditableNodeIntoRect(gfx::Rect(0, 0, 0, 0));

  if (is_hw_accelerated_ && !m_IsEvasGLInit)
    Init_EvasGL(width, height);
}

void RenderWidgetHostViewEfl::OnOrientationChangeEvent(int orientation) {
  current_orientation_ = orientation;
}

void RenderWidgetHostViewEfl::OnDidChangeMaxScrollOffset(int maxScrollX, int maxScrollY) {
  scroll_detector_->SetMaxScroll(maxScrollX, maxScrollY);
}

void RenderWidgetHostViewEfl::SelectRange(const gfx::Point& start, const gfx::Point& end) {
  RenderViewHost* rvh =  RenderViewHost::From(host_);
  WebContentsImpl* wci = static_cast<WebContentsImpl*>(
        content::WebContents::FromRenderViewHost(rvh));
  wci->SelectRange(gfx::Point(start.x() / device_scale_factor_, start.y() / device_scale_factor_),
                   gfx::Point(end.x() / device_scale_factor_, end.y() / device_scale_factor_));
}

void RenderWidgetHostViewEfl::MoveCaret(const gfx::Point& point) {
  host_->MoveCaret(gfx::Point(point.x() / device_scale_factor_, point.y() / device_scale_factor_));
}

void RenderWidgetHostViewEfl::OnMHTMLContentGet(const std::string& mhtml_content, int callback_id) {
  eweb_view()->OnMHTMLContentGet(mhtml_content, callback_id);
}

void RenderWidgetHostViewEfl::OnDidChangePageScaleFactor(double scale_factor) {
  eweb_view()->DidChangePageScaleFactor(scale_factor);
}

void RenderWidgetHostViewEfl::OnDidChangePageScaleRange(double min_scale, double max_scale) {
  eweb_view()->DidChangePageScaleRange(min_scale, max_scale);
}

SelectionControllerEfl* RenderWidgetHostViewEfl::GetSelectionController() {
  return web_view_->GetSelectionController();
}

void RenderWidgetHostViewEfl::ClearQueues() {
  while (!keyupev_queue_.empty()) {
    keyupev_queue_.pop();
  }

  while (!keydownev_queue_.empty()) {
    delete keydownev_queue_.front();
    keydownev_queue_.pop();
  }
}

void RenderWidgetHostViewEfl::HandleCommitQueue(bool processed) {
  if (!im_context_)
    return;

  if (!processed) {
    if (!im_context_->GetCommitQueue().empty()) {
      base::string16 text16 = im_context_->GetCommitQueue().front();
      host_->ImeConfirmComposition(text16, gfx::Range::InvalidRange(), false);
      im_context_->CommitQueuePop();
    }
  } else {
    if (!im_context_->GetCommitQueue().empty())
      im_context_->CommitQueuePop();
  }
}

void RenderWidgetHostViewEfl::HandlePreeditQueue(bool processed) {
  if (!im_context_)
    return;

  if (!processed) {
    if (!im_context_->GetPreeditQueue().empty()) {
      ui::CompositionText composition_ = im_context_->GetPreeditQueue().front();

      const std::vector<blink::WebCompositionUnderline>& underlines =
      reinterpret_cast<const std::vector<blink::WebCompositionUnderline>&>(
      composition_.underlines);

      host_->ImeSetComposition(
      composition_.text, underlines, composition_.selection.start(),
      composition_.selection.end());
      im_context_->PreeditQueuePop();
    }
  } else {
    if (!im_context_->GetPreeditQueue().empty())
      im_context_->PreeditQueuePop();
  }
}

void RenderWidgetHostViewEfl::HandleKeyUpQueue() {
  if (!im_context_)
    return;

  if (keyupev_queue_.empty())
    return;

  int keyCode = keyupev_queue_.front();
  SendCompositionKeyUpEvent(keyCode);
  keyupev_queue_.pop();
}

void RenderWidgetHostViewEfl::HandleKeyDownQueue() {
  if (!im_context_)
    return;

  if (keydownev_queue_.empty())
    return;

  NativeWebKeyboardEvent *n_event = keydownev_queue_.front();
  host_->ForwardKeyboardEvent(*n_event);
  keydownev_queue_.pop();
  delete n_event;
}

void RenderWidgetHostViewEfl::SendCompositionKeyUpEvent(char c) {
  NativeWebKeyboardEvent event;
  event.windowsKeyCode = c;
  event.skip_in_browser = false;
  event.type = blink::WebInputEvent::KeyUp;
  host_->ForwardKeyboardEvent(event);
}

}  // namespace content
