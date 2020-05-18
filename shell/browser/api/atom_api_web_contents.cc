// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/api/atom_api_web_contents.h"

#include <limits>
#include <memory>
#include <set>
#include <string>
#include <utility>

#include "base/message_loop/message_loop_current.h"
#include "base/no_destructor.h"
#include "base/optional.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_restrictions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/ssl/security_state_tab_helper.h"
#include "content/browser/frame_host/frame_tree_node.h"             // nogncheck
#include "content/browser/frame_host/render_frame_host_manager.h"   // nogncheck
#include "content/browser/renderer_host/render_widget_host_impl.h"  // nogncheck
#include "content/browser/renderer_host/render_widget_host_view_base.h"  // nogncheck
#include "content/common/widget_messages.h"
#include "content/public/browser/child_process_security_policy.h"
#include "content/public/browser/download_request_utils.h"
#include "content/public/browser/favicon_status.h"
#include "content/public/browser/native_web_keyboard_event.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/plugin_service.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/service_worker_context.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/context_menu_params.h"
#include "electron/buildflags/buildflags.h"
#include "electron/shell/common/api/api.mojom.h"
#include "mojo/public/cpp/system/platform_handle.h"
#include "native_mate/converter.h"
#include "native_mate/dictionary.h"
#include "native_mate/object_template_builder.h"
#include "ppapi/buildflags/buildflags.h"
#include "shell/browser/api/atom_api_browser_window.h"
#include "shell/browser/api/atom_api_debugger.h"
#include "shell/browser/api/atom_api_session.h"
#include "shell/browser/atom_browser_client.h"
#include "shell/browser/atom_browser_context.h"
#include "shell/browser/atom_browser_main_parts.h"
#include "shell/browser/atom_javascript_dialog_manager.h"
#include "shell/browser/atom_navigation_throttle.h"
#include "shell/browser/browser.h"
#include "shell/browser/child_web_contents_tracker.h"
#include "shell/browser/lib/bluetooth_chooser.h"
#include "shell/browser/native_window.h"
#include "shell/browser/session_preferences.h"
#include "shell/browser/ui/drag_util.h"
#include "shell/browser/ui/inspectable_web_contents.h"
#include "shell/browser/ui/inspectable_web_contents_view.h"
#include "shell/browser/web_contents_permission_helper.h"
#include "shell/browser/web_contents_preferences.h"
#include "shell/browser/web_contents_zoom_controller.h"
#include "shell/browser/web_view_guest_delegate.h"
#include "shell/common/api/atom_api_native_image.h"
#include "shell/common/api/event_emitter_caller.h"
#include "shell/common/color_util.h"
#include "shell/common/language_util.h"
#include "shell/common/mouse_util.h"
#include "shell/common/native_mate_converters/blink_converter.h"
#include "shell/common/native_mate_converters/content_converter.h"
#include "shell/common/native_mate_converters/file_path_converter.h"
#include "shell/common/native_mate_converters/gfx_converter.h"
#include "shell/common/native_mate_converters/gurl_converter.h"
#include "shell/common/native_mate_converters/image_converter.h"
#include "shell/common/native_mate_converters/map_converter.h"
#include "shell/common/native_mate_converters/net_converter.h"
#include "shell/common/native_mate_converters/network_converter.h"
#include "shell/common/native_mate_converters/once_callback.h"
#include "shell/common/native_mate_converters/string16_converter.h"
#include "shell/common/native_mate_converters/value_converter.h"
#include "shell/common/node_includes.h"
#include "shell/common/options_switches.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"
#include "third_party/blink/public/mojom/frame/find_in_page.mojom.h"
#include "third_party/blink/public/platform/web_cursor_info.h"
#include "third_party/blink/public/platform/web_input_event.h"
#include "ui/display/screen.h"
#include "ui/events/base_event_utils.h"

#if BUILDFLAG(ENABLE_OSR)
#include "shell/browser/osr/osr_render_widget_host_view.h"
#include "shell/browser/osr/osr_web_contents_view.h"
#endif

#if !defined(OS_MACOSX)
#include "ui/aura/window.h"
#else
#include "ui/base/cocoa/defaults_utils.h"
#endif

#if defined(OS_LINUX)
#include "ui/views/linux_ui/linux_ui.h"
#endif

#if defined(OS_LINUX) || defined(OS_WIN)
#include "third_party/blink/public/mojom/renderer_preferences.mojom.h"
#include "ui/gfx/font_render_params.h"
#endif

#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
#include "shell/browser/extensions/atom_extension_web_contents_observer.h"
#endif

namespace mate {

#if BUILDFLAG(ENABLE_PRINTING)
template <>
struct Converter<printing::PrinterBasicInfo> {
  static v8::Local<v8::Value> ToV8(v8::Isolate* isolate,
                                   const printing::PrinterBasicInfo& val) {
    mate::Dictionary dict(isolate, v8::Object::New(isolate));
    dict.Set("name", val.printer_name);
    dict.Set("description", val.printer_description);
    dict.Set("status", val.printer_status);
    dict.Set("isDefault", val.is_default ? true : false);
    dict.Set("options", val.options);
    return dict.GetHandle();
  }
};

template <>
struct Converter<printing::MarginType> {
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     printing::MarginType* out) {
    std::string type;
    if (ConvertFromV8(isolate, val, &type)) {
      if (type == "default") {
        *out = printing::DEFAULT_MARGINS;
        return true;
      }
      if (type == "none") {
        *out = printing::NO_MARGINS;
        return true;
      }
      if (type == "printableArea") {
        *out = printing::PRINTABLE_AREA_MARGINS;
        return true;
      }
      if (type == "custom") {
        *out = printing::CUSTOM_MARGINS;
        return true;
      }
    }
    return false;
  }
};

template <>
struct Converter<printing::DuplexMode> {
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     printing::DuplexMode* out) {
    std::string mode;
    if (ConvertFromV8(isolate, val, &mode)) {
      if (mode == "simplex") {
        *out = printing::SIMPLEX;
        return true;
      }
      if (mode == "longEdge") {
        *out = printing::LONG_EDGE;
        return true;
      }
      if (mode == "shortEdge") {
        *out = printing::SHORT_EDGE;
        return true;
      }
    }
    return false;
  }
};

#endif

template <>
struct Converter<WindowOpenDisposition> {
  static v8::Local<v8::Value> ToV8(v8::Isolate* isolate,
                                   WindowOpenDisposition val) {
    std::string disposition = "other";
    switch (val) {
      case WindowOpenDisposition::CURRENT_TAB:
        disposition = "default";
        break;
      case WindowOpenDisposition::NEW_FOREGROUND_TAB:
        disposition = "foreground-tab";
        break;
      case WindowOpenDisposition::NEW_BACKGROUND_TAB:
        disposition = "background-tab";
        break;
      case WindowOpenDisposition::NEW_POPUP:
      case WindowOpenDisposition::NEW_WINDOW:
        disposition = "new-window";
        break;
      case WindowOpenDisposition::SAVE_TO_DISK:
        disposition = "save-to-disk";
        break;
      default:
        break;
    }
    return mate::ConvertToV8(isolate, disposition);
  }
};

template <>
struct Converter<content::SavePageType> {
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     content::SavePageType* out) {
    std::string save_type;
    if (!ConvertFromV8(isolate, val, &save_type))
      return false;
    save_type = base::ToLowerASCII(save_type);
    if (save_type == "htmlonly") {
      *out = content::SAVE_PAGE_TYPE_AS_ONLY_HTML;
    } else if (save_type == "htmlcomplete") {
      *out = content::SAVE_PAGE_TYPE_AS_COMPLETE_HTML;
    } else if (save_type == "mhtml") {
      *out = content::SAVE_PAGE_TYPE_AS_MHTML;
    } else {
      return false;
    }
    return true;
  }
};

template <>
struct Converter<electron::api::WebContents::Type> {
  static v8::Local<v8::Value> ToV8(v8::Isolate* isolate,
                                   electron::api::WebContents::Type val) {
    using Type = electron::api::WebContents::Type;
    std::string type = "";
    switch (val) {
      case Type::BACKGROUND_PAGE:
        type = "backgroundPage";
        break;
      case Type::BROWSER_WINDOW:
        type = "window";
        break;
      case Type::BROWSER_VIEW:
        type = "browserView";
        break;
      case Type::REMOTE:
        type = "remote";
        break;
      case Type::WEB_VIEW:
        type = "webview";
        break;
      case Type::OFF_SCREEN:
        type = "offscreen";
        break;
      default:
        break;
    }
    return mate::ConvertToV8(isolate, type);
  }

  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     electron::api::WebContents::Type* out) {
    using Type = electron::api::WebContents::Type;
    std::string type;
    if (!ConvertFromV8(isolate, val, &type))
      return false;
    if (type == "backgroundPage") {
      *out = Type::BACKGROUND_PAGE;
    } else if (type == "browserView") {
      *out = Type::BROWSER_VIEW;
    } else if (type == "webview") {
      *out = Type::WEB_VIEW;
#if BUILDFLAG(ENABLE_OSR)
    } else if (type == "offscreen") {
      *out = Type::OFF_SCREEN;
#endif
    } else {
      return false;
    }
    return true;
  }
};

}  // namespace mate

namespace electron {

namespace api {

namespace {

// Called when CapturePage is done.
void OnCapturePageDone(util::Promise promise, const SkBitmap& bitmap) {
  // Hack to enable transparency in captured image
  promise.Resolve(gfx::Image::CreateFrom1xBitmap(bitmap));
}

#if BUILDFLAG(ENABLE_PRINTING)
// This will return false if no printer with the provided device_name can be
// found on the network. We need to check this because Chromium does not do
// sanity checking of device_name validity and so will crash on invalid names.
bool IsDeviceNameValid(const base::string16& device_name) {
#if defined(OS_MACOSX)
  base::ScopedCFTypeRef<CFStringRef> new_printer_id(
      base::SysUTF16ToCFStringRef(device_name));
  PMPrinter new_printer = PMPrinterCreateFromPrinterID(new_printer_id.get());
  bool printer_exists = new_printer != nullptr;
  PMRelease(new_printer);
  return printer_exists;
#elif defined(OS_WIN)
  printing::ScopedPrinterHandle printer;
  return printer.OpenPrinterWithName(device_name.c_str());
#endif
  return true;
}

base::string16 GetDefaultPrinterAsync() {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);

  scoped_refptr<printing::PrintBackend> backend =
      printing::PrintBackend::CreateInstance(nullptr);
  std::string printer_name = backend->GetDefaultPrinterName();
  return base::UTF8ToUTF16(printer_name);
}
#endif
}  // namespace

WebContents::WebContents(v8::Isolate* isolate,
                         content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      type_(Type::REMOTE),
      weak_factory_(this) {
  web_contents->SetUserAgentOverride(GetBrowserContext()->GetUserAgent(),
                                     false);
  Init(isolate);
  AttachAsUserData(web_contents);
  InitZoomController(web_contents, mate::Dictionary::CreateEmpty(isolate));
  registry_.AddInterface(base::BindRepeating(&WebContents::BindElectronBrowser,
                                             base::Unretained(this)));
  bindings_.set_connection_error_handler(base::BindRepeating(
      &WebContents::OnElectronBrowserConnectionError, base::Unretained(this)));
}

WebContents::WebContents(v8::Isolate* isolate,
                         std::unique_ptr<content::WebContents> web_contents,
                         Type type)
    : content::WebContentsObserver(web_contents.get()),
      type_(type),
      weak_factory_(this) {
  DCHECK(type != Type::REMOTE)
      << "Can't take ownership of a remote WebContents";
  auto session = Session::CreateFrom(isolate, GetBrowserContext());
  session_.Reset(isolate, session.ToV8());

  mate::Dictionary options = mate::Dictionary::CreateEmpty(isolate);
  options.Set("transparent", true);

  if (type == Type::OFF_SCREEN) {
    options.Set("frame", false);

    mate::Dictionary webPreferences = mate::Dictionary::CreateEmpty(isolate);
    webPreferences.Set("offscreen", true);
    webPreferences.Set("transparent", true);
    options.Set("webPreferences", webPreferences);

    OffScreenWebContentsView* offscreenView = GetOffScreenWebContentsView();
    offscreenView->SetWebContents(web_contents.get());
    offscreenView->SetPaintCallback(
        base::BindRepeating(&WebContents::OnPaint, base::Unretained(this)));
  }

  // We may not call LoadURL on pre-created webcontents, so set background to always be transparent
  auto* const view = web_contents.get()->GetRenderWidgetHostView();
  if (view) {
    view->SetBackgroundColor(SK_ColorTRANSPARENT);
  }

  InitWithSessionAndOptions(isolate, std::move(web_contents), session, options);
}

WebContents::WebContents(v8::Isolate* isolate, const mate::Dictionary& options)
    : weak_factory_(this) {
  // Read options.
  options.Get("backgroundThrottling", &background_throttling_);

  // Get type
  options.Get("type", &type_);

#if BUILDFLAG(ENABLE_OSR)
  bool b = false;
  if (options.Get(options::kOffscreen, &b) && b)
    type_ = Type::OFF_SCREEN;
#endif

  // Init embedder earlier
  options.Get("embedder", &embedder_);

  // Whether to enable DevTools.
  options.Get("devTools", &enable_devtools_);

  // BrowserViews are not attached to a window initially so they should start
  // off as hidden. This is also important for compositor recycling. See:
  // https://github.com/electron/electron/pull/21372
  bool initially_shown = type_ != Type::BROWSER_VIEW;
  options.Get(options::kShow, &initially_shown);

  // Obtain the session.
  std::string partition;
  mate::Handle<api::Session> session;
  if (options.Get("session", &session) && !session.IsEmpty()) {
  } else if (options.Get("partition", &partition)) {
    session = Session::FromPartition(isolate, partition);
  } else {
    // Use the default session if not specified.
    session = Session::FromPartition(isolate, "");
  }
  session_.Reset(isolate, session.ToV8());

  std::unique_ptr<content::WebContents> web_contents;
  if (IsGuest()) {
    scoped_refptr<content::SiteInstance> site_instance =
        content::SiteInstance::CreateForURL(session->browser_context(),
                                            GURL("chrome-guest://fake-host"));
    content::WebContents::CreateParams params(session->browser_context(),
                                              site_instance);
    guest_delegate_.reset(
        new WebViewGuestDelegate(embedder_->web_contents(), this));
    params.guest_delegate = guest_delegate_.get();

#if BUILDFLAG(ENABLE_OSR)
    if (embedder_ && embedder_->IsOffScreen()) {
      auto* view = new OffScreenWebContentsView(
          false,
          base::BindRepeating(&WebContents::OnPaint, base::Unretained(this)));
      params.view = view;
      params.delegate_view = view;

      web_contents = content::WebContents::Create(params);
      view->SetWebContents(web_contents.get());
    } else {
#endif
      web_contents = content::WebContents::Create(params);
#if BUILDFLAG(ENABLE_OSR)
    }
  } else if (IsOffScreen()) {
    bool transparent = false;
    options.Get("transparent", &transparent);

    content::WebContents::CreateParams params(session->browser_context());
    auto* view = new OffScreenWebContentsView(
        transparent,
        base::BindRepeating(&WebContents::OnPaint, base::Unretained(this)));
    params.view = view;
    params.delegate_view = view;

    web_contents = content::WebContents::Create(params);
    view->SetWebContents(web_contents.get());
#endif
  } else {
    content::WebContents::CreateParams params(session->browser_context());
    params.initially_hidden = !initially_shown;
    web_contents = content::WebContents::Create(params);
  }

  InitWithSessionAndOptions(isolate, std::move(web_contents), session, options);
}

void WebContents::InitZoomController(content::WebContents* web_contents,
                                     const mate::Dictionary& options) {
  WebContentsZoomController::CreateForWebContents(web_contents);
  zoom_controller_ = WebContentsZoomController::FromWebContents(web_contents);
  double zoom_factor;
  if (options.Get(options::kZoomFactor, &zoom_factor))
    zoom_controller_->SetDefaultZoomFactor(zoom_factor);
}

void WebContents::InitWithSessionAndOptions(
    v8::Isolate* isolate,
    std::unique_ptr<content::WebContents> owned_web_contents,
    mate::Handle<api::Session> session,
    const mate::Dictionary& options) {
  Observe(owned_web_contents.get());
  // TODO(zcbenz): Make InitWithWebContents take unique_ptr.
  // At the time of writing we are going through a refactoring and I don't want
  // to make other people's work harder.
  InitWithWebContents(owned_web_contents.release(), session->browser_context(),
                      IsGuest());

  managed_web_contents()->GetView()->SetDelegate(this);

  auto* prefs = web_contents()->GetMutableRendererPrefs();

  // Collect preferred languages from OS and browser process. accept_languages
  // effects HTTP header, navigator.languages, and CJK fallback font selection.
  //
  // Note that an application locale set to the browser process might be
  // different with the one set to the preference list.
  // (e.g. overridden with --lang)
  std::string accept_languages =
      g_browser_process->GetApplicationLocale() + ",";
  for (auto const& language : electron::GetPreferredLanguages()) {
    if (language == g_browser_process->GetApplicationLocale())
      continue;
    accept_languages += language + ",";
  }
  accept_languages.pop_back();
  prefs->accept_languages = accept_languages;

#if defined(OS_LINUX) || defined(OS_WIN)
  // Update font settings.
  static const base::NoDestructor<gfx::FontRenderParams> params(
      gfx::GetFontRenderParams(gfx::FontRenderParamsQuery(), nullptr));
  prefs->should_antialias_text = params->antialiasing;
  prefs->use_subpixel_positioning = params->subpixel_positioning;
  prefs->hinting = params->hinting;
  prefs->use_autohinter = params->autohinter;
  prefs->use_bitmaps = params->use_bitmaps;
  prefs->subpixel_rendering = params->subpixel_rendering;
#endif

// Honor the system's cursor blink rate settings
#if defined(OS_MACOSX)
  base::TimeDelta interval;
  if (ui::TextInsertionCaretBlinkPeriod(&interval))
    prefs->caret_blink_interval = interval;
#elif defined(OS_LINUX)
  views::LinuxUI* linux_ui = views::LinuxUI::instance();
  if (linux_ui)
    prefs->caret_blink_interval = linux_ui->GetCursorBlinkInterval();
#elif defined(OS_WIN)
  const auto system_msec = ::GetCaretBlinkTime();
  if (system_msec != 0) {
    prefs->caret_blink_interval =
        (system_msec == INFINITE)
            ? base::TimeDelta()
            : base::TimeDelta::FromMilliseconds(system_msec);
  }
#endif

  // Save the preferences in C++.
  new WebContentsPreferences(web_contents(), options);

  WebContentsPermissionHelper::CreateForWebContents(web_contents());
  SecurityStateTabHelper::CreateForWebContents(web_contents());
  InitZoomController(web_contents(), options);
#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  extensions::AtomExtensionWebContentsObserver::CreateForWebContents(
      web_contents());
#endif

  registry_.AddInterface(base::BindRepeating(&WebContents::BindElectronBrowser,
                                             base::Unretained(this)));
  bindings_.set_connection_error_handler(base::BindRepeating(
      &WebContents::OnElectronBrowserConnectionError, base::Unretained(this)));

  web_contents()->SetUserAgentOverride(GetBrowserContext()->GetUserAgent(),
                                       false);

  if (IsGuest()) {
    NativeWindow* owner_window = nullptr;
    if (embedder_) {
      // New WebContents's owner_window is the embedder's owner_window.
      auto* relay =
          NativeWindowRelay::FromWebContents(embedder_->web_contents());
      if (relay)
        owner_window = relay->GetNativeWindow();
    }
    if (owner_window)
      SetOwnerWindow(owner_window);
  }

  Init(isolate);
  AttachAsUserData(web_contents());
}

WebContents::~WebContents() {
  // The destroy() is called.
  if (managed_web_contents()) {
    managed_web_contents()->GetView()->SetDelegate(nullptr);

    RenderViewDeleted(web_contents()->GetRenderViewHost());

    if (type_ == Type::BROWSER_WINDOW && owner_window()) {
      // For BrowserWindow we should close the window and clean up everything
      // before WebContents is destroyed.
      for (ExtendedWebContentsObserver& observer : observers_)
        observer.OnCloseContents();
      // BrowserWindow destroys WebContents asynchronously, manually emit the
      // destroyed event here.
      WebContentsDestroyed();
    } else if (Browser::Get()->is_shutting_down()) {
      // Destroy WebContents directly when app is shutting down.
      DestroyWebContents(false /* async */);
    } else {
      // Destroy WebContents asynchronously unless app is shutting down,
      // because destroy() might be called inside WebContents's event handler.
      DestroyWebContents(!IsGuest() /* async */);
      // The WebContentsDestroyed will not be called automatically because we
      // destroy the webContents in the next tick. So we have to manually
      // call it here to make sure "destroyed" event is emitted.
      WebContentsDestroyed();
    }
  }
}

void WebContents::DestroyWebContents(bool async) {
  // This event is only for internal use, which is emitted when WebContents is
  // being destroyed.
  Emit("will-destroy");
  ResetManagedWebContents(async);
}

bool WebContents::DidAddMessageToConsole(
    content::WebContents* source,
    blink::mojom::ConsoleMessageLevel level,
    const base::string16& message,
    int32_t line_no,
    const base::string16& source_id) {
  return Emit("console-message", static_cast<int32_t>(level), message, line_no,
              source_id);
}

void WebContents::OnCreateWindow(
    const GURL& target_url,
    const content::Referrer& referrer,
    const std::string& frame_name,
    WindowOpenDisposition disposition,
    const std::vector<std::string>& features,
    const scoped_refptr<network::ResourceRequestBody>& body) {
  if (type_ == Type::BROWSER_WINDOW || type_ == Type::OFF_SCREEN)
    Emit("-new-window", target_url, frame_name, disposition, features, body,
         referrer);
  else
    Emit("new-window", target_url, frame_name, disposition, features);
}

void WebContents::OnPrepareWebContentsCreation(
    content::WebContents::CreateParams& contentsCreateParams,
    const content::mojom::CreateNewWindowParams& windowCreateParams) {
  // HACK: Until electron PR lands to properly pass the right values
  // https://github.com/electron/electron/pull/19703
  std::string::size_type offscreenFlag =
      windowCreateParams.frame_name.find("\"offscreen\":true");
  bool isOffscreen = offscreenFlag != std::string::npos;

  if (isOffscreen) {
    auto* view = new OffScreenWebContentsView(true);
    contentsCreateParams.view = view;
    contentsCreateParams.delegate_view = view;
  }
}

void WebContents::WebContentsCreated(content::WebContents* source_contents,
                                     int opener_render_process_id,
                                     int opener_render_frame_id,
                                     const std::string& frame_name,
                                     const GURL& target_url,
                                     content::WebContents* new_contents) {
  ChildWebContentsTracker::CreateForWebContents(new_contents);
  auto* tracker = ChildWebContentsTracker::FromWebContents(new_contents);
  tracker->url = target_url;
  tracker->frame_name = frame_name;
}

bool WebContents::ShouldCreateWebContents(
    content::WebContents* web_contents,
    content::RenderFrameHost* opener,
    content::SiteInstance* source_site_instance,
    int32_t route_id,
    int32_t main_frame_route_id,
    int32_t main_frame_widget_route_id,
    content::mojom::WindowContainerType window_container_type,
    const GURL& opener_url,
    const std::string& frame_name,
    const GURL& target_url,
    const std::string& partition_id,
    content::SessionStorageNamespace* session_storage_namespace) {
  if (Emit("-will-add-new-contents", target_url, frame_name)) {
    return false;
  }
  return true;
}

void WebContents::AddNewContents(
    content::WebContents* source,
    std::unique_ptr<content::WebContents> new_contents,
    WindowOpenDisposition disposition,
    const gfx::Rect& initial_rect,
    bool user_gesture,
    bool* was_blocked) {
  auto* tracker = ChildWebContentsTracker::FromWebContents(new_contents.get());
  DCHECK(tracker);

  // HACK: Until electron PR lands to properly pass the right values
  // https://github.com/electron/electron/pull/19703
  std::string::size_type offscreenFlag =
      tracker->frame_name.find("\"offscreen\":true");
  bool isOffscreen = offscreenFlag != std::string::npos;

  auto screenType = Type::BROWSER_WINDOW;
  if (isOffscreen) {
    screenType = Type::OFF_SCREEN;
  }

  auto api_web_contents =
      CreateAndTake(isolate(), std::move(new_contents), screenType);
  if (Emit("-add-new-contents", api_web_contents, disposition, user_gesture,
           initial_rect.x(), initial_rect.y(), initial_rect.width(),
           initial_rect.height(), tracker->url, tracker->frame_name)) {
    // TODO(zcbenz): Can we make this sync?
    api_web_contents->DestroyWebContents(true /* async */);
  }
}

content::WebContents* WebContents::OpenURLFromTab(
    content::WebContents* source,
    const content::OpenURLParams& params) {
  if (params.disposition != WindowOpenDisposition::CURRENT_TAB) {
    if (type_ == Type::BROWSER_WINDOW || type_ == Type::OFF_SCREEN)
      Emit("-new-window", params.url, "", params.disposition);
    else
      Emit("new-window", params.url, "", params.disposition);
    return nullptr;
  }

  // Give user a chance to cancel navigation.
  if (Emit("will-navigate", params.url))
    return nullptr;

  // Don't load the URL if the web contents was marked as destroyed from a
  // will-navigate event listener
  if (IsDestroyed())
    return nullptr;

  return CommonWebContentsDelegate::OpenURLFromTab(source, params);
}

void WebContents::BeforeUnloadFired(content::WebContents* tab,
                                    bool proceed,
                                    bool* proceed_to_fire_unload) {
  if (type_ == Type::BROWSER_WINDOW || type_ == Type::OFF_SCREEN)
    *proceed_to_fire_unload = proceed;
  else
    *proceed_to_fire_unload = true;
}

void WebContents::SetContentsBounds(content::WebContents* source,
                                    const gfx::Rect& pos) {
  Emit("move", pos);
}

void WebContents::CloseContents(content::WebContents* source) {
  Emit("close");
  HideAutofillPopup();
  if (managed_web_contents())
    managed_web_contents()->GetView()->SetDelegate(nullptr);
  for (ExtendedWebContentsObserver& observer : observers_)
    observer.OnCloseContents();
}

void WebContents::ActivateContents(content::WebContents* source) {
  Emit("activate");
}

void WebContents::UpdateTargetURL(content::WebContents* source,
                                  const GURL& url) {
  Emit("update-target-url", url);
}

bool WebContents::HandleKeyboardEvent(
    content::WebContents* source,
    const content::NativeWebKeyboardEvent& event) {
  if (type_ == Type::WEB_VIEW && embedder_) {
    // Send the unhandled keyboard events back to the embedder.
    return embedder_->HandleKeyboardEvent(source, event);
  } else {
    // Go to the default keyboard handling.
    return CommonWebContentsDelegate::HandleKeyboardEvent(source, event);
  }
}

content::KeyboardEventProcessingResult WebContents::PreHandleKeyboardEvent(
    content::WebContents* source,
    const content::NativeWebKeyboardEvent& event) {
  if (event.GetType() == blink::WebInputEvent::Type::kRawKeyDown ||
      event.GetType() == blink::WebInputEvent::Type::kKeyUp) {
    bool prevent_default = Emit("before-input-event", event);
    if (prevent_default) {
      return content::KeyboardEventProcessingResult::HANDLED;
    }
  }

  return content::KeyboardEventProcessingResult::NOT_HANDLED;
}

void WebContents::ContentsZoomChange(bool zoom_in) {
  Emit("zoom-changed", zoom_in ? "in" : "out");
}

void WebContents::EnterFullscreenModeForTab(
    content::WebContents* source,
    const GURL& origin,
    const blink::WebFullscreenOptions& options) {
  auto* permission_helper =
      WebContentsPermissionHelper::FromWebContents(source);
  auto callback =
      base::BindRepeating(&WebContents::OnEnterFullscreenModeForTab,
                          base::Unretained(this), source, origin, options);
  permission_helper->RequestFullscreenPermission(callback);
}

void WebContents::OnEnterFullscreenModeForTab(
    content::WebContents* source,
    const GURL& origin,
    const blink::WebFullscreenOptions& options,
    bool allowed) {
  if (!allowed)
    return;
  CommonWebContentsDelegate::EnterFullscreenModeForTab(source, origin, options);
  Emit("enter-html-full-screen");
}

void WebContents::ExitFullscreenModeForTab(content::WebContents* source) {
  CommonWebContentsDelegate::ExitFullscreenModeForTab(source);
  Emit("leave-html-full-screen");
}

void WebContents::RendererUnresponsive(
    content::WebContents* source,
    content::RenderWidgetHost* render_widget_host,
    base::RepeatingClosure hang_monitor_restarter) {
  Emit("unresponsive");
}

void WebContents::RendererResponsive(
    content::WebContents* source,
    content::RenderWidgetHost* render_widget_host) {
  Emit("responsive");
  for (ExtendedWebContentsObserver& observer : observers_)
    observer.OnRendererResponsive();
}

bool WebContents::HandleContextMenu(content::RenderFrameHost* render_frame_host,
                                    const content::ContextMenuParams& params) {
  if (params.custom_context.is_pepper_menu) {
    Emit("pepper-context-menu", std::make_pair(params, web_contents()),
         base::BindOnce(&content::WebContents::NotifyContextMenuClosed,
                        base::Unretained(web_contents()),
                        params.custom_context));
  } else {
    Emit("context-menu", std::make_pair(params, web_contents()));
  }

  return true;
}

bool WebContents::OnGoToEntryOffset(int offset) {
  GoToOffset(offset);
  return false;
}

void WebContents::FindReply(content::WebContents* web_contents,
                            int request_id,
                            int number_of_matches,
                            const gfx::Rect& selection_rect,
                            int active_match_ordinal,
                            bool final_update) {
  if (!final_update)
    return;

  v8::Locker locker(isolate());
  v8::HandleScope handle_scope(isolate());
  mate::Dictionary result = mate::Dictionary::CreateEmpty(isolate());
  result.Set("requestId", request_id);
  result.Set("matches", number_of_matches);
  result.Set("selectionArea", selection_rect);
  result.Set("activeMatchOrdinal", active_match_ordinal);
  result.Set("finalUpdate", final_update);  // Deprecate after 2.0
  Emit("found-in-page", result);
}

bool WebContents::CheckMediaAccessPermission(
    content::RenderFrameHost* render_frame_host,
    const GURL& security_origin,
    blink::mojom::MediaStreamType type) {
  auto* web_contents =
      content::WebContents::FromRenderFrameHost(render_frame_host);
  auto* permission_helper =
      WebContentsPermissionHelper::FromWebContents(web_contents);
  return permission_helper->CheckMediaAccessPermission(security_origin, type);
}

void WebContents::RequestMediaAccessPermission(
    content::WebContents* web_contents,
    const content::MediaStreamRequest& request,
    content::MediaResponseCallback callback) {
  auto* permission_helper =
      WebContentsPermissionHelper::FromWebContents(web_contents);
  permission_helper->RequestMediaAccessPermission(request, std::move(callback));
}

void WebContents::RequestToLockMouse(content::WebContents* web_contents,
                                     bool user_gesture,
                                     bool last_unlocked_by_target) {
  auto* permission_helper =
      WebContentsPermissionHelper::FromWebContents(web_contents);
  permission_helper->RequestPointerLockPermission(user_gesture);
}

std::unique_ptr<content::BluetoothChooser> WebContents::RunBluetoothChooser(
    content::RenderFrameHost* frame,
    const content::BluetoothChooser::EventHandler& event_handler) {
  return std::make_unique<BluetoothChooser>(this, event_handler);
}

content::JavaScriptDialogManager* WebContents::GetJavaScriptDialogManager(
    content::WebContents* source) {
  if (!dialog_manager_)
    dialog_manager_.reset(new AtomJavaScriptDialogManager(this));

  return dialog_manager_.get();
}

void WebContents::OnAudioStateChanged(bool audible) {
  Emit("-audio-state-changed", audible);
}

void WebContents::BeforeUnloadFired(bool proceed,
                                    const base::TimeTicks& proceed_time) {
  // Do nothing, we override this method just to avoid compilation error since
  // there are two virtual functions named BeforeUnloadFired.
}

void WebContents::RenderViewCreated(content::RenderViewHost* render_view_host) {
  if (!background_throttling_)
    render_view_host->SetSchedulerThrottling(false);
}

void WebContents::RenderFrameCreated(
    content::RenderFrameHost* render_frame_host) {
  auto* rwhv = render_frame_host->GetView();
  if (!rwhv)
    return;

  auto* rwh_impl =
      static_cast<content::RenderWidgetHostImpl*>(rwhv->GetRenderWidgetHost());
  if (rwh_impl)
    rwh_impl->disable_hidden_ = !background_throttling_;
}

void WebContents::RenderViewHostChanged(content::RenderViewHost* old_host,
                                        content::RenderViewHost* new_host) {
  currently_committed_process_id_ = new_host->GetProcess()->GetID();
}

void WebContents::RenderViewDeleted(content::RenderViewHost* render_view_host) {
  // This event is necessary for tracking any states with respect to
  // intermediate render view hosts aka speculative render view hosts. Currently
  // used by object-registry.js to ref count remote objects.
  Emit("render-view-deleted", render_view_host->GetProcess()->GetID());

  if (-1 == currently_committed_process_id_ ||
      render_view_host->GetProcess()->GetID() ==
          currently_committed_process_id_) {
    currently_committed_process_id_ = -1;

    // When the RVH that has been deleted is the current RVH it means that the
    // the web contents are being closed. This is communicated by this event.
    // Currently tracked by guest-window-manager.js to destroy the
    // BrowserWindow.
    Emit("current-render-view-deleted",
         render_view_host->GetProcess()->GetID());
  }
}

void WebContents::RenderProcessGone(base::TerminationStatus status) {
  Emit("crashed", status == base::TERMINATION_STATUS_PROCESS_WAS_KILLED);
}

void WebContents::PluginCrashed(const base::FilePath& plugin_path,
                                base::ProcessId plugin_pid) {
#if BUILDFLAG(ENABLE_PLUGINS)
  content::WebPluginInfo info;
  auto* plugin_service = content::PluginService::GetInstance();
  plugin_service->GetPluginInfoByPath(plugin_path, &info);
  Emit("plugin-crashed", info.name, info.version);
#endif  // BUILDFLAG(ENABLE_PLUIGNS)
}

void WebContents::MediaStartedPlaying(const MediaPlayerInfo& video_type,
                                      const content::MediaPlayerId& id) {
  Emit("media-started-playing");
}

void WebContents::MediaStoppedPlaying(
    const MediaPlayerInfo& video_type,
    const content::MediaPlayerId& id,
    content::WebContentsObserver::MediaStoppedReason reason) {
  Emit("media-paused");
}

void WebContents::DidChangeThemeColor(base::Optional<SkColor> theme_color) {
  if (theme_color) {
    Emit("did-change-theme-color", electron::ToRGBHex(theme_color.value()));
  } else {
    Emit("did-change-theme-color", nullptr);
  }
}

void WebContents::OnInterfaceRequestFromFrame(
    content::RenderFrameHost* render_frame_host,
    const std::string& interface_name,
    mojo::ScopedMessagePipeHandle* interface_pipe) {
  registry_.TryBindInterface(interface_name, interface_pipe, render_frame_host);
}

void WebContents::DidAcquireFullscreen(content::RenderFrameHost* rfh) {
  set_fullscreen_frame(rfh);
}

void WebContents::DocumentLoadedInFrame(
    content::RenderFrameHost* render_frame_host) {
  if (!render_frame_host->GetParent())
    Emit("dom-ready");
}

void WebContents::DidFinishLoad(content::RenderFrameHost* render_frame_host,
                                const GURL& validated_url) {
  bool is_main_frame = !render_frame_host->GetParent();
  int frame_process_id = render_frame_host->GetProcess()->GetID();
  int frame_routing_id = render_frame_host->GetRoutingID();
  Emit("did-frame-finish-load", is_main_frame, frame_process_id,
       frame_routing_id);

  if (is_main_frame)
    Emit("did-finish-load");
}

void WebContents::DidFailLoad(content::RenderFrameHost* render_frame_host,
                              const GURL& url,
                              int error_code,
                              const base::string16& error_description) {
  bool is_main_frame = !render_frame_host->GetParent();
  int frame_process_id = render_frame_host->GetProcess()->GetID();
  int frame_routing_id = render_frame_host->GetRoutingID();
  Emit("did-fail-load", error_code, error_description, url, is_main_frame,
       frame_process_id, frame_routing_id);
}

void WebContents::DidStartLoading() {
  Emit("did-start-loading");
}

void WebContents::DidStopLoading() {
  Emit("did-stop-loading");
}

bool WebContents::EmitNavigationEvent(
    const std::string& event,
    content::NavigationHandle* navigation_handle) {
  bool is_main_frame = navigation_handle->IsInMainFrame();
  int frame_tree_node_id = navigation_handle->GetFrameTreeNodeId();
  content::FrameTreeNode* frame_tree_node =
      content::FrameTreeNode::GloballyFindByID(frame_tree_node_id);
  content::RenderFrameHostManager* render_manager =
      frame_tree_node->render_manager();
  content::RenderFrameHost* frame_host = nullptr;
  if (render_manager) {
    frame_host = render_manager->speculative_frame_host();
    if (!frame_host)
      frame_host = render_manager->current_frame_host();
  }
  int frame_process_id = -1, frame_routing_id = -1;
  if (frame_host) {
    frame_process_id = frame_host->GetProcess()->GetID();
    frame_routing_id = frame_host->GetRoutingID();
  }
  bool is_same_document = navigation_handle->IsSameDocument();
  auto url = navigation_handle->GetURL();
  return Emit(event, url, is_same_document, is_main_frame, frame_process_id,
              frame_routing_id);
}

void WebContents::BindElectronBrowser(
    mojom::ElectronBrowserRequest request,
    content::RenderFrameHost* render_frame_host) {
  auto id = bindings_.AddBinding(this, std::move(request), render_frame_host);
  frame_to_bindings_map_[render_frame_host].push_back(id);
}

void WebContents::OnElectronBrowserConnectionError() {
  auto binding_id = bindings_.dispatch_binding();
  auto* frame_host = bindings_.dispatch_context();
  base::Erase(frame_to_bindings_map_[frame_host], binding_id);
}

void WebContents::Message(bool internal,
                          const std::string& channel,
                          base::ListValue arguments) {
  // webContents.emit('-ipc-message', new Event(), internal, channel,
  // arguments);
  EmitWithSender("-ipc-message", bindings_.dispatch_context(), base::nullopt,
                 internal, channel, std::move(arguments));
}

void WebContents::Invoke(const std::string& channel,
                         base::ListValue arguments,
                         InvokeCallback callback) {
  // webContents.emit('-ipc-invoke', new Event(), channel, arguments);
  EmitWithSender("-ipc-invoke", bindings_.dispatch_context(),
                 std::move(callback), channel, std::move(arguments));
}

void WebContents::MessageSync(bool internal,
                              const std::string& channel,
                              base::ListValue arguments,
                              MessageSyncCallback callback) {
  // webContents.emit('-ipc-message-sync', new Event(sender, message), internal,
  // channel, arguments);
  EmitWithSender("-ipc-message-sync", bindings_.dispatch_context(),
                 std::move(callback), internal, channel, std::move(arguments));
}

void WebContents::MessageTo(bool internal,
                            bool send_to_all,
                            int32_t web_contents_id,
                            const std::string& channel,
                            base::ListValue arguments) {
  auto* web_contents = mate::TrackableObject<WebContents>::FromWeakMapID(
      isolate(), web_contents_id);

  if (web_contents) {
    web_contents->SendIPCMessageWithSender(internal, send_to_all, channel,
                                           std::move(arguments), ID());
  }
}

void WebContents::MessageHost(const std::string& channel,
                              base::ListValue arguments) {
  // webContents.emit('ipc-message-host', new Event(), channel, args);
  EmitWithSender("ipc-message-host", bindings_.dispatch_context(),
                 base::nullopt, channel, std::move(arguments));
}

void WebContents::UpdateDraggableRegions(
    std::vector<mojom::DraggableRegionPtr> regions) {
  for (ExtendedWebContentsObserver& observer : observers_)
    observer.OnDraggableRegionsUpdated(regions);
}

void WebContents::RenderFrameDeleted(
    content::RenderFrameHost* render_frame_host) {
  // A RenderFrameHost can be destroyed before the related Mojo binding is
  // closed, which can result in Mojo calls being sent for RenderFrameHosts
  // that no longer exist. To prevent this from happening, when a
  // RenderFrameHost goes away, we close all the bindings related to that
  // frame.
  auto it = frame_to_bindings_map_.find(render_frame_host);
  if (it == frame_to_bindings_map_.end())
    return;
  for (auto id : it->second)
    bindings_.RemoveBinding(id);
  frame_to_bindings_map_.erase(it);
}

void WebContents::DidStartNavigation(
    content::NavigationHandle* navigation_handle) {
  EmitNavigationEvent("did-start-navigation", navigation_handle);
}

void WebContents::DidRedirectNavigation(
    content::NavigationHandle* navigation_handle) {
  EmitNavigationEvent("did-redirect-navigation", navigation_handle);
}

void WebContents::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (!navigation_handle->HasCommitted())
    return;
  bool is_main_frame = navigation_handle->IsInMainFrame();
  content::RenderFrameHost* frame_host =
      navigation_handle->GetRenderFrameHost();
  int frame_process_id = -1, frame_routing_id = -1;
  if (frame_host) {
    frame_process_id = frame_host->GetProcess()->GetID();
    frame_routing_id = frame_host->GetRoutingID();
  }
  if (!navigation_handle->IsErrorPage()) {
    auto url = navigation_handle->GetURL();
    bool is_same_document = navigation_handle->IsSameDocument();
    if (is_same_document) {
      Emit("did-navigate-in-page", url, is_main_frame, frame_process_id,
           frame_routing_id);
    } else {
      const net::HttpResponseHeaders* http_response =
          navigation_handle->GetResponseHeaders();
      std::string http_status_text;
      int http_response_code = -1;
      if (http_response) {
        http_status_text = http_response->GetStatusText();
        http_response_code = http_response->response_code();
      }
      Emit("did-frame-navigate", url, http_response_code, http_status_text,
           is_main_frame, frame_process_id, frame_routing_id);
      if (is_main_frame) {
        Emit("did-navigate", url, http_response_code, http_status_text);
      }
    }
    if (IsGuest())
      Emit("load-commit", url, is_main_frame);
  } else {
    auto url = navigation_handle->GetURL();
    int code = navigation_handle->GetNetErrorCode();
    auto description = net::ErrorToShortString(code);
    Emit("did-fail-provisional-load", code, description, url, is_main_frame,
         frame_process_id, frame_routing_id);

    // Do not emit "did-fail-load" for canceled requests.
    if (code != net::ERR_ABORTED)
      Emit("did-fail-load", code, description, url, is_main_frame,
           frame_process_id, frame_routing_id);
  }
}

void WebContents::TitleWasSet(content::NavigationEntry* entry) {
  base::string16 final_title;
  bool explicit_set = true;
  if (entry) {
    auto title = entry->GetTitle();
    auto url = entry->GetURL();
    if (url.SchemeIsFile() && title.empty()) {
      final_title = base::UTF8ToUTF16(url.ExtractFileName());
      explicit_set = false;
    } else {
      final_title = title;
    }
  }
  Emit("page-title-updated", final_title, explicit_set);
}

void WebContents::DidUpdateFaviconURL(
    const std::vector<content::FaviconURL>& urls) {
  std::set<GURL> unique_urls;
  for (const auto& iter : urls) {
    if (iter.icon_type != content::FaviconURL::IconType::kFavicon)
      continue;
    const GURL& url = iter.icon_url;
    if (url.is_valid())
      unique_urls.insert(url);
  }
  Emit("page-favicon-updated", unique_urls);
}

void WebContents::DevToolsReloadPage() {
  Emit("devtools-reload-page");
}

void WebContents::DevToolsFocused() {
  Emit("devtools-focused");
}

void WebContents::DevToolsOpened() {
  v8::Locker locker(isolate());
  v8::HandleScope handle_scope(isolate());
  auto handle =
      FromOrCreate(isolate(), managed_web_contents()->GetDevToolsWebContents());
  devtools_web_contents_.Reset(isolate(), handle.ToV8());

  // Set inspected tabID.
  base::Value tab_id(ID());
  managed_web_contents()->CallClientFunction("DevToolsAPI.setInspectedTabId",
                                             &tab_id, nullptr, nullptr);

  // Inherit owner window in devtools when it doesn't have one.
  auto* devtools = managed_web_contents()->GetDevToolsWebContents();
  bool has_window = devtools->GetUserData(NativeWindowRelay::UserDataKey());
  if (owner_window() && !has_window)
    handle->SetOwnerWindow(devtools, owner_window());

  Emit("devtools-opened");
}

void WebContents::DevToolsClosed() {
  v8::Locker locker(isolate());
  v8::HandleScope handle_scope(isolate());
  devtools_web_contents_.Reset();

  Emit("devtools-closed");
}

void WebContents::ShowAutofillPopup(content::RenderFrameHost* frame_host,
                                    const gfx::RectF& bounds,
                                    const std::vector<base::string16>& values,
                                    const std::vector<base::string16>& labels) {
  bool offscreen = IsOffScreen() || (embedder_ && embedder_->IsOffScreen());
  gfx::RectF popup_bounds(bounds);
  content::RenderFrameHost* embedder_frame_host = nullptr;
  if (embedder_) {
    auto* embedder_view = embedder_->web_contents()->GetMainFrame()->GetView();
    auto* view = web_contents()->GetMainFrame()->GetView();
    auto offset = view->GetViewBounds().origin() -
                  embedder_view->GetViewBounds().origin();
    popup_bounds.Offset(offset.x(), offset.y());
    embedder_frame_host = embedder_->web_contents()->GetMainFrame();
  }

  CommonWebContentsDelegate::ShowAutofillPopup(
      frame_host, embedder_frame_host, offscreen, popup_bounds, values, labels);
}

bool WebContents::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(WebContents, message)
    IPC_MESSAGE_HANDLER_CODE(WidgetHostMsg_SetCursor, OnCursorChange,
                             handled = false)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()

  return handled;
}

// There are three ways of destroying a webContents:
// 1. call webContents.destroy();
// 2. garbage collection;
// 3. user closes the window of webContents;
// 4. the embedder detaches the frame.
// For webview only #4 will happen, for BrowserWindow both #1 and #3 may
// happen. The #2 should never happen for webContents, because webview is
// managed by GuestViewManager, and BrowserWindow's webContents is managed
// by api::BrowserWindow.
// For #1, the destructor will do the cleanup work and we only need to make
// sure "destroyed" event is emitted. For #3, the content::WebContents will
// be destroyed on close, and WebContentsDestroyed would be called for it, so
// we need to make sure the api::WebContents is also deleted.
// For #4, the WebContents will be destroyed by embedder.
void WebContents::WebContentsDestroyed() {
  // Cleanup relationships with other parts.
  RemoveFromWeakMap();

  // We can not call Destroy here because we need to call Emit first, but we
  // also do not want any method to be used, so just mark as destroyed here.
  MarkDestroyed();

  Emit("destroyed");

  // For guest view based on OOPIF, the WebContents is released by the embedder
  // frame, and we need to clear the reference to the memory.
  if (IsGuest() && managed_web_contents()) {
    managed_web_contents()->ReleaseWebContents();
    ResetManagedWebContents(false);
  }

  // Destroy the native class in next tick.
  base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE, GetDestroyClosure());
}

void WebContents::NavigationEntryCommitted(
    const content::LoadCommittedDetails& details) {
  Emit("navigation-entry-commited", details.entry->GetURL(),
       details.is_same_document, details.did_replace_entry);
}

void WebContents::SetBackgroundThrottling(bool allowed) {
  background_throttling_ = allowed;

  auto* rfh = web_contents()->GetMainFrame();
  if (!rfh)
    return;

  auto* rwhv = rfh->GetView();
  if (!rwhv)
    return;

  auto* rwh_impl =
      static_cast<content::RenderWidgetHostImpl*>(rwhv->GetRenderWidgetHost());
  if (!rwh_impl)
    return;

  rwh_impl->disable_hidden_ = !background_throttling_;
  web_contents()->GetRenderViewHost()->SetSchedulerThrottling(allowed);

  if (rwh_impl->is_hidden()) {
    rwh_impl->WasShown(base::nullopt);
  }
}

int WebContents::GetProcessID() const {
  return web_contents()->GetMainFrame()->GetProcess()->GetID();
}

base::ProcessId WebContents::GetOSProcessID() const {
  base::ProcessHandle process_handle =
      web_contents()->GetMainFrame()->GetProcess()->GetProcess().Handle();
  return base::GetProcId(process_handle);
}

base::ProcessId WebContents::GetOSProcessIdForFrame(
    const std::string& name,
    const std::string& document_url) const {
  for (auto* frame : web_contents()->GetAllFrames()) {
    if (frame->GetFrameName() == name &&
        frame->GetLastCommittedURL().spec() == document_url) {
      return base::GetProcId(frame->GetProcess()->GetProcess().Handle());
    }
  }
  return base::kNullProcessId;
}

WebContents::Type WebContents::GetType() const {
  return type_;
}

bool WebContents::Equal(const WebContents* web_contents) const {
  return ID() == web_contents->ID();
}

void WebContents::LoadURL(const GURL& url, const mate::Dictionary& options) {
  if (!url.is_valid() || url.spec().size() > url::kMaxURLChars) {
    Emit("did-fail-load", static_cast<int>(net::ERR_INVALID_URL),
         net::ErrorToShortString(net::ERR_INVALID_URL),
         url.possibly_invalid_spec(), true);
    return;
  }

  content::NavigationController::LoadURLParams params(url);

  if (!options.Get("httpReferrer", &params.referrer)) {
    GURL http_referrer;
    if (options.Get("httpReferrer", &http_referrer))
      params.referrer =
          content::Referrer(http_referrer.GetAsReferrer(),
                            network::mojom::ReferrerPolicy::kDefault);
  }

  std::string user_agent;
  if (options.Get("userAgent", &user_agent))
    web_contents()->SetUserAgentOverride(user_agent, false);

  std::string extra_headers;
  if (options.Get("extraHeaders", &extra_headers))
    params.extra_headers = extra_headers;

  scoped_refptr<network::ResourceRequestBody> body;
  if (options.Get("postData", &body)) {
    params.post_data = body;
    params.load_type = content::NavigationController::LOAD_TYPE_HTTP_POST;
  }

  GURL base_url_for_data_url;
  if (options.Get("baseURLForDataURL", &base_url_for_data_url)) {
    params.base_url_for_data_url = base_url_for_data_url;
    params.load_type = content::NavigationController::LOAD_TYPE_DATA;
  }

  bool reload_ignoring_cache = false;
  if (options.Get("reloadIgnoringCache", &reload_ignoring_cache) &&
      reload_ignoring_cache) {
    params.reload_type = content::ReloadType::BYPASSING_CACHE;
  }

  params.transition_type = ui::PAGE_TRANSITION_TYPED;
  params.should_clear_history_list = true;
  params.override_user_agent = content::NavigationController::UA_OVERRIDE_TRUE;
  // Discord non-committed entries to ensure that we don't re-use a pending
  // entry
  web_contents()->GetController().DiscardNonCommittedEntries();
  web_contents()->GetController().LoadURLWithParams(params);

  // Set the background color of RenderWidgetHostView.
  // We have to call it right after LoadURL because the RenderViewHost is only
  // created after loading a page.
  auto* const view = web_contents()->GetRenderWidgetHostView();
  if (view) {
    auto* web_preferences = WebContentsPreferences::From(web_contents());
    std::string color_name;
    if (web_preferences->GetPreference(options::kBackgroundColor,
                                       &color_name)) {
      view->SetBackgroundColor(ParseHexColor(color_name));
    } else {
      view->SetBackgroundColor(SK_ColorTRANSPARENT);
    }
  }
}

void WebContents::DownloadURL(const GURL& url) {
  auto* browser_context = web_contents()->GetBrowserContext();
  auto* download_manager =
      content::BrowserContext::GetDownloadManager(browser_context);
  std::unique_ptr<download::DownloadUrlParameters> download_params(
      content::DownloadRequestUtils::CreateDownloadForWebContentsMainFrame(
          web_contents(), url, MISSING_TRAFFIC_ANNOTATION));
  download_manager->DownloadUrl(std::move(download_params));
}

GURL WebContents::GetURL() const {
  return web_contents()->GetURL();
}

base::string16 WebContents::GetTitle() const {
  return web_contents()->GetTitle();
}

bool WebContents::IsLoading() const {
  return web_contents()->IsLoading();
}

bool WebContents::IsLoadingMainFrame() const {
  return web_contents()->IsLoadingToDifferentDocument();
}

bool WebContents::IsWaitingForResponse() const {
  return web_contents()->IsWaitingForResponse();
}

void WebContents::Stop() {
  web_contents()->Stop();
}

void WebContents::GoBack() {
  electron::AtomBrowserClient::SuppressRendererProcessRestartForOnce();
  web_contents()->GetController().GoBack();
}

void WebContents::GoForward() {
  electron::AtomBrowserClient::SuppressRendererProcessRestartForOnce();
  web_contents()->GetController().GoForward();
}

void WebContents::GoToOffset(int offset) {
  electron::AtomBrowserClient::SuppressRendererProcessRestartForOnce();
  web_contents()->GetController().GoToOffset(offset);
}

const std::string WebContents::GetWebRTCIPHandlingPolicy() const {
  return web_contents()->GetMutableRendererPrefs()->webrtc_ip_handling_policy;
}

void WebContents::SetWebRTCIPHandlingPolicy(
    const std::string& webrtc_ip_handling_policy) {
  if (GetWebRTCIPHandlingPolicy() == webrtc_ip_handling_policy)
    return;
  web_contents()->GetMutableRendererPrefs()->webrtc_ip_handling_policy =
      webrtc_ip_handling_policy;

  content::RenderViewHost* host = web_contents()->GetRenderViewHost();
  if (host)
    host->SyncRendererPrefs();
}

bool WebContents::IsCrashed() const {
  return web_contents()->IsCrashed();
}

void WebContents::SetUserAgent(const std::string& user_agent,
                               mate::Arguments* args) {
  web_contents()->SetUserAgentOverride(user_agent, false);
}

std::string WebContents::GetUserAgent() {
  return web_contents()->GetUserAgentOverride();
}

v8::Local<v8::Promise> WebContents::SavePage(
    const base::FilePath& full_file_path,
    const content::SavePageType& save_type) {
  util::Promise promise(isolate());
  v8::Local<v8::Promise> handle = promise.GetHandle();

  auto* handler = new SavePageHandler(web_contents(), std::move(promise));
  handler->Handle(full_file_path, save_type);

  return handle;
}

void WebContents::OpenDevTools(mate::Arguments* args) {
  if (type_ == Type::REMOTE)
    return;

  if (!enable_devtools_)
    return;

  std::string state;
  if (type_ == Type::WEB_VIEW || !owner_window()) {
    state = "detach";
  }
  bool activate = true;
  if (args && args->Length() == 1) {
    mate::Dictionary options;
    if (args->GetNext(&options)) {
      options.Get("mode", &state);
      options.Get("activate", &activate);
    }
  }
  managed_web_contents()->SetDockState(state);
  managed_web_contents()->ShowDevTools(activate);
}

void WebContents::CloseDevTools() {
  if (type_ == Type::REMOTE)
    return;

  managed_web_contents()->CloseDevTools();
}

bool WebContents::IsDevToolsOpened() {
  if (type_ == Type::REMOTE)
    return false;

  return managed_web_contents()->IsDevToolsViewShowing();
}

bool WebContents::IsDevToolsFocused() {
  if (type_ == Type::REMOTE)
    return false;

  return managed_web_contents()->GetView()->IsDevToolsViewFocused();
}

void WebContents::EnableDeviceEmulation(
    const blink::WebDeviceEmulationParams& params) {
  if (type_ == Type::REMOTE)
    return;

  auto* frame_host = web_contents()->GetMainFrame();
  if (frame_host) {
    auto* widget_host =
        frame_host ? frame_host->GetView()->GetRenderWidgetHost() : nullptr;
    if (!widget_host)
      return;
    widget_host->Send(new WidgetMsg_EnableDeviceEmulation(
        widget_host->GetRoutingID(), params));
  }
}

void WebContents::DisableDeviceEmulation() {
  if (type_ == Type::REMOTE)
    return;

  auto* frame_host = web_contents()->GetMainFrame();
  if (frame_host) {
    auto* widget_host =
        frame_host ? frame_host->GetView()->GetRenderWidgetHost() : nullptr;
    if (!widget_host)
      return;
    widget_host->Send(
        new WidgetMsg_DisableDeviceEmulation(widget_host->GetRoutingID()));
  }
}

void WebContents::ToggleDevTools() {
  if (IsDevToolsOpened())
    CloseDevTools();
  else
    OpenDevTools(nullptr);
}

void WebContents::InspectElement(int x, int y) {
  if (type_ == Type::REMOTE)
    return;

  if (!enable_devtools_)
    return;

  if (!managed_web_contents()->GetDevToolsWebContents())
    OpenDevTools(nullptr);
  managed_web_contents()->InspectElement(x, y);
}

void WebContents::InspectSharedWorker() {
  if (type_ == Type::REMOTE)
    return;

  if (!enable_devtools_)
    return;

  for (const auto& agent_host : content::DevToolsAgentHost::GetOrCreateAll()) {
    if (agent_host->GetType() ==
        content::DevToolsAgentHost::kTypeSharedWorker) {
      OpenDevTools(nullptr);
      managed_web_contents()->AttachTo(agent_host);
      break;
    }
  }
}

void WebContents::InspectServiceWorker() {
  if (type_ == Type::REMOTE)
    return;

  if (!enable_devtools_)
    return;

  for (const auto& agent_host : content::DevToolsAgentHost::GetOrCreateAll()) {
    if (agent_host->GetType() ==
        content::DevToolsAgentHost::kTypeServiceWorker) {
      OpenDevTools(nullptr);
      managed_web_contents()->AttachTo(agent_host);
      break;
    }
  }
}

void WebContents::SetIgnoreMenuShortcuts(bool ignore) {
  auto* web_preferences = WebContentsPreferences::From(web_contents());
  DCHECK(web_preferences);
  web_preferences->preference()->SetKey("ignoreMenuShortcuts",
                                        base::Value(ignore));
}

void WebContents::SetAudioMuted(bool muted) {
  web_contents()->SetAudioMuted(muted);
}

bool WebContents::IsAudioMuted() {
  return web_contents()->IsAudioMuted();
}

bool WebContents::IsCurrentlyAudible() {
  return web_contents()->IsCurrentlyAudible();
}

#if BUILDFLAG(ENABLE_PRINTING)
void WebContents::OnGetDefaultPrinter(
    base::DictionaryValue print_settings,
    printing::CompletionCallback print_callback,
    base::string16 device_name,
    bool silent,
    base::string16 default_printer) {
  base::string16 printer_name =
      device_name.empty() ? default_printer : device_name;

  // If there are no valid printers available on the network, we bail.
  if (printer_name.empty() || !IsDeviceNameValid(printer_name)) {
    if (print_callback)
      std::move(print_callback).Run(false, "no valid printers available");
    return;
  }

  print_settings.SetStringKey(printing::kSettingDeviceName, printer_name);

  auto* print_view_manager =
      printing::PrintViewManagerBasic::FromWebContents(web_contents());
  auto* focused_frame = web_contents()->GetFocusedFrame();
  auto* rfh = focused_frame && focused_frame->HasSelection()
                  ? focused_frame
                  : web_contents()->GetMainFrame();

  print_view_manager->PrintNow(
      rfh,
      std::make_unique<PrintMsg_PrintPages>(rfh->GetRoutingID(), silent,
                                            std::move(print_settings)),
      std::move(print_callback));
}

void WebContents::Print(mate::Arguments* args) {
  mate::Dictionary options = mate::Dictionary::CreateEmpty(args->isolate());
  base::DictionaryValue settings;

  if (args->Length() >= 1 && !args->GetNext(&options)) {
    args->ThrowError("webContents.print(): Invalid print settings specified.");
    return;
  }

  printing::CompletionCallback callback;
  if (args->Length() == 2 && !args->GetNext(&callback)) {
    args->ThrowError(
        "webContents.print(): Invalid optional callback provided.");
    return;
  }

  // Set optional silent printing
  bool silent = false;
  options.Get("silent", &silent);

  bool print_background = false;
  options.Get("printBackground", &print_background);
  settings.SetBoolean(printing::kSettingShouldPrintBackgrounds,
                      print_background);

  // Set custom margin settings
  mate::Dictionary margins = mate::Dictionary::CreateEmpty(args->isolate());
  if (options.Get("margins", &margins)) {
    printing::MarginType margin_type = printing::DEFAULT_MARGINS;
    margins.Get("marginType", &margin_type);
    settings.SetInteger(printing::kSettingMarginsType, margin_type);

    if (margin_type == printing::CUSTOM_MARGINS) {
      auto custom_margins = std::make_unique<base::DictionaryValue>();
      int top = 0;
      margins.Get("top", &top);
      custom_margins->SetInteger(printing::kSettingMarginTop, top);
      int bottom = 0;
      margins.Get("bottom", &bottom);
      custom_margins->SetInteger(printing::kSettingMarginBottom, bottom);
      int left = 0;
      margins.Get("left", &left);
      custom_margins->SetInteger(printing::kSettingMarginLeft, left);
      int right = 0;
      margins.Get("right", &right);
      custom_margins->SetInteger(printing::kSettingMarginRight, right);
      settings.SetDictionary(printing::kSettingMarginsCustom,
                             std::move(custom_margins));
    }
  } else {
    settings.SetInteger(printing::kSettingMarginsType,
                        printing::DEFAULT_MARGINS);
  }

  settings.SetBoolean(printing::kSettingHeaderFooterEnabled, false);

  // Set whether to print color or greyscale
  bool print_color = true;
  options.Get("color", &print_color);
  int color_setting = print_color ? printing::COLOR : printing::GRAY;
  settings.SetInteger(printing::kSettingColor, color_setting);

  bool landscape = false;
  options.Get("landscape", &landscape);
  settings.SetBoolean(printing::kSettingLandscape, landscape);

  // We set the default to the system's default printer and only update
  // if at the Chromium level if the user overrides.
  // Printer device name as opened by the OS.
  base::string16 device_name;
  options.Get("deviceName", &device_name);
  if (!device_name.empty() && !IsDeviceNameValid(device_name)) {
    args->ThrowError("webContents.print(): Invalid deviceName provided.");
    return;
  }

  int scale_factor = 100;
  options.Get("scaleFactor", &scale_factor);
  settings.SetInteger(printing::kSettingScaleFactor, scale_factor);

  int pages_per_sheet = 1;
  options.Get("pagesPerSheet", &pages_per_sheet);
  settings.SetInteger(printing::kSettingPagesPerSheet, pages_per_sheet);

  bool collate = true;
  options.Get("collate", &collate);
  settings.SetBoolean(printing::kSettingCollate, collate);

  int copies = 1;
  options.Get("copies", &copies);
  settings.SetInteger(printing::kSettingCopies, copies);

  // For now we don't want to allow the user to enable these settings
  // but we need to set them or a CHECK is hit.
  settings.SetBoolean(printing::kSettingPrintToPDF, false);
  settings.SetBoolean(printing::kSettingCloudPrintDialog, false);
  settings.SetBoolean(printing::kSettingPrintWithPrivet, false);
  settings.SetBoolean(printing::kSettingShouldPrintSelectionOnly, false);
  settings.SetBoolean(printing::kSettingPrintWithExtension, false);
  settings.SetBoolean(printing::kSettingRasterizePdf, false);

  // Set custom page ranges to print
  std::vector<mate::Dictionary> page_ranges;
  if (options.Get("pageRanges", &page_ranges)) {
    std::unique_ptr<base::ListValue> page_range_list(new base::ListValue());
    for (size_t i = 0; i < page_ranges.size(); ++i) {
      int from, to;
      if (page_ranges[i].Get("from", &from) && page_ranges[i].Get("to", &to)) {
        std::unique_ptr<base::DictionaryValue> range(
            new base::DictionaryValue());
        range->SetInteger(printing::kSettingPageRangeFrom, from);
        range->SetInteger(printing::kSettingPageRangeTo, to);
        page_range_list->Append(std::move(range));
      } else {
        continue;
      }
    }
    if (page_range_list->GetSize() > 0)
      settings.SetList(printing::kSettingPageRange, std::move(page_range_list));
  }

  // Set custom duplex mode
  printing::DuplexMode duplex_mode;
  options.Get("duplexMode", &duplex_mode);
  settings.SetInteger(printing::kSettingDuplexMode, duplex_mode);

  // Set custom dots per inch (dpi)
  mate::Dictionary dpi_settings;
  int dpi = 72;
  if (options.Get("dpi", &dpi_settings)) {
    int horizontal = 72;
    dpi_settings.Get("horizontal", &horizontal);
    settings.SetInteger(printing::kSettingDpiHorizontal, horizontal);
    int vertical = 72;
    dpi_settings.Get("vertical", &vertical);
    settings.SetInteger(printing::kSettingDpiVertical, vertical);
  } else {
    settings.SetInteger(printing::kSettingDpiHorizontal, dpi);
    settings.SetInteger(printing::kSettingDpiVertical, dpi);
  }

  base::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::ThreadPool(), base::MayBlock(), base::TaskPriority::USER_BLOCKING},
      base::BindOnce(&GetDefaultPrinterAsync),
      base::BindOnce(&WebContents::OnGetDefaultPrinter,
                     weak_factory_.GetWeakPtr(), std::move(settings),
                     std::move(callback), device_name, silent));
}

std::vector<printing::PrinterBasicInfo> WebContents::GetPrinterList() {
  std::vector<printing::PrinterBasicInfo> printers;
  auto print_backend = printing::PrintBackend::CreateInstance(nullptr);
  {
    // TODO(deepak1556): Deprecate this api in favor of an
    // async version and post a non blocing task call.
    base::ThreadRestrictions::ScopedAllowIO allow_io;
    print_backend->EnumeratePrinters(&printers);
  }
  return printers;
}

v8::Local<v8::Promise> WebContents::PrintToPDF(
    const base::DictionaryValue& settings) {
  util::Promise promise(isolate());
  v8::Local<v8::Promise> handle = promise.GetHandle();
  PrintPreviewMessageHandler::FromWebContents(web_contents())
      ->PrintToPDF(settings, std::move(promise));
  return handle;
}
#endif

void WebContents::AddWorkSpace(mate::Arguments* args,
                               const base::FilePath& path) {
  if (path.empty()) {
    args->ThrowError("path cannot be empty");
    return;
  }
  DevToolsAddFileSystem(std::string(), path);
}

void WebContents::RemoveWorkSpace(mate::Arguments* args,
                                  const base::FilePath& path) {
  if (path.empty()) {
    args->ThrowError("path cannot be empty");
    return;
  }
  DevToolsRemoveFileSystem(path);
}

void WebContents::Undo() {
  web_contents()->Undo();
}

void WebContents::Redo() {
  web_contents()->Redo();
}

void WebContents::Cut() {
  web_contents()->Cut();
}

void WebContents::Copy() {
  web_contents()->Copy();
}

void WebContents::Paste() {
  web_contents()->Paste();
}

void WebContents::PasteAndMatchStyle() {
  web_contents()->PasteAndMatchStyle();
}

void WebContents::Delete() {
  web_contents()->Delete();
}

void WebContents::SelectAll() {
  web_contents()->SelectAll();
}

void WebContents::Unselect() {
  web_contents()->CollapseSelection();
}

void WebContents::Replace(const base::string16& word) {
  web_contents()->Replace(word);
}

void WebContents::ReplaceMisspelling(const base::string16& word) {
  web_contents()->ReplaceMisspelling(word);
}

uint32_t WebContents::FindInPage(mate::Arguments* args) {
  base::string16 search_text;
  if (!args->GetNext(&search_text) || search_text.empty()) {
    args->ThrowError("Must provide a non-empty search content");
    return 0;
  }

  uint32_t request_id = GetNextRequestId();
  mate::Dictionary dict;
  auto options = blink::mojom::FindOptions::New();
  if (args->GetNext(&dict)) {
    dict.Get("forward", &options->forward);
    dict.Get("matchCase", &options->match_case);
    dict.Get("findNext", &options->find_next);
  }

  web_contents()->Find(request_id, search_text, std::move(options));
  return request_id;
}

void WebContents::StopFindInPage(content::StopFindAction action) {
  web_contents()->StopFinding(action);
}

void WebContents::ShowDefinitionForSelection() {
#if defined(OS_MACOSX)
  auto* const view = web_contents()->GetRenderWidgetHostView();
  if (view)
    view->ShowDefinitionForSelection();
#endif
}

void WebContents::CopyImageAt(int x, int y) {
  auto* const host = web_contents()->GetMainFrame();
  if (host)
    host->CopyImageAt(x, y);
}

void WebContents::Focus() {
  web_contents()->Focus();
}

#if !defined(OS_MACOSX)
bool WebContents::IsFocused() const {
  auto* view = web_contents()->GetRenderWidgetHostView();
  if (!view)
    return false;

  if (GetType() != Type::BACKGROUND_PAGE) {
    auto* window = web_contents()->GetNativeView()->GetToplevelWindow();
    if (window && !window->IsVisible())
      return false;
  }

  return view->HasFocus();
}
#endif

void WebContents::TabTraverse(bool reverse) {
  web_contents()->FocusThroughTabTraversal(reverse);
}

bool WebContents::SendIPCMessage(bool internal,
                                 bool send_to_all,
                                 const std::string& channel,
                                 base::ListValue args) {
  return SendIPCMessageWithSender(internal, send_to_all, channel,
                                  std::move(args));
}

bool WebContents::SendIPCMessageWithSender(bool internal,
                                           bool send_to_all,
                                           const std::string& channel,
                                           base::ListValue args,
                                           int32_t sender_id) {
  std::vector<content::RenderFrameHost*> target_hosts;
  if (!send_to_all) {
    auto* frame_host = web_contents()->GetMainFrame();
    if (frame_host) {
      target_hosts.push_back(frame_host);
    }
  } else {
    target_hosts = web_contents()->GetAllFrames();
  }

  for (auto* frame_host : target_hosts) {
    mojom::ElectronRendererAssociatedPtr electron_ptr;
    frame_host->GetRemoteAssociatedInterfaces()->GetInterface(
        mojo::MakeRequest(&electron_ptr));
    electron_ptr->Message(internal, false, channel,
                          base::ListValue(args.Clone().GetList()), sender_id);
  }
  return true;
}

bool WebContents::SendIPCMessageToFrame(bool internal,
                                        bool send_to_all,
                                        int32_t frame_id,
                                        const std::string& channel,
                                        base::ListValue args) {
  auto frames = web_contents()->GetAllFrames();
  auto iter = std::find_if(frames.begin(), frames.end(), [frame_id](auto* f) {
    return f->GetRoutingID() == frame_id;
  });
  if (iter == frames.end())
    return false;
  if (!(*iter)->IsRenderFrameLive())
    return false;

  mojom::ElectronRendererAssociatedPtr electron_ptr;
  (*iter)->GetRemoteAssociatedInterfaces()->GetInterface(
      mojo::MakeRequest(&electron_ptr));
  electron_ptr->Message(internal, send_to_all, channel, std::move(args),
                        0 /* sender_id */);
  return true;
}

void WebContents::SendInputEvent(v8::Isolate* isolate,
                                 v8::Local<v8::Value> input_event) {
  content::RenderWidgetHostView* view =
      web_contents()->GetRenderWidgetHostView();
  if (!view)
    return;

  content::RenderWidgetHost* rwh = view->GetRenderWidgetHost();
  blink::WebInputEvent::Type type =
      mate::GetWebInputEventType(isolate, input_event);
  if (blink::WebInputEvent::IsMouseEventType(type)) {
    blink::WebMouseEvent mouse_event;
    if (mate::ConvertFromV8(isolate, input_event, &mouse_event)) {
      if (IsOffScreen()) {
#if BUILDFLAG(ENABLE_OSR)
        GetOffScreenRenderWidgetHostView()->SendMouseEvent(mouse_event);
#endif
      } else {
        rwh->ForwardMouseEvent(mouse_event);
      }
      return;
    }
  } else if (blink::WebInputEvent::IsKeyboardEventType(type)) {
    content::NativeWebKeyboardEvent keyboard_event(
        blink::WebKeyboardEvent::kRawKeyDown,
        blink::WebInputEvent::kNoModifiers, ui::EventTimeForNow());
    if (mate::ConvertFromV8(isolate, input_event, &keyboard_event)) {
      rwh->ForwardKeyboardEvent(keyboard_event);
      return;
    }
  } else if (type == blink::WebInputEvent::kMouseWheel) {
    blink::WebMouseWheelEvent mouse_wheel_event;
    if (mate::ConvertFromV8(isolate, input_event, &mouse_wheel_event)) {
      if (IsOffScreen()) {
#if BUILDFLAG(ENABLE_OSR)
        GetOffScreenRenderWidgetHostView()->SendMouseWheelEvent(
            mouse_wheel_event);
#endif
      } else {
        // Chromium expects phase info in wheel events (and applies a
        // DCHECK to verify it). See: https://crbug.com/756524.
        mouse_wheel_event.phase = blink::WebMouseWheelEvent::kPhaseBegan;
        mouse_wheel_event.dispatch_type = blink::WebInputEvent::kBlocking;
        rwh->ForwardWheelEvent(mouse_wheel_event);

        // Send a synthetic wheel event with phaseEnded to finish scrolling.
        mouse_wheel_event.has_synthetic_phase = true;
        mouse_wheel_event.delta_x = 0;
        mouse_wheel_event.delta_y = 0;
        mouse_wheel_event.phase = blink::WebMouseWheelEvent::kPhaseEnded;
        mouse_wheel_event.dispatch_type =
            blink::WebInputEvent::kEventNonBlocking;
        rwh->ForwardWheelEvent(mouse_wheel_event);
      }
      return;
    }
  }

  isolate->ThrowException(
      v8::Exception::Error(mate::StringToV8(isolate, "Invalid event object")));
}

void WebContents::BeginFrameSubscription(mate::Arguments* args) {
  bool only_dirty = false;
  FrameSubscriber::FrameCaptureCallback callback;

  args->GetNext(&only_dirty);
  if (!args->GetNext(&callback)) {
    args->ThrowError();
    return;
  }

  frame_subscriber_.reset(
      new FrameSubscriber(web_contents(), callback, only_dirty));
}

void WebContents::EndFrameSubscription() {
  frame_subscriber_.reset();
}

void WebContents::StartDrag(const mate::Dictionary& item,
                            mate::Arguments* args) {
  base::FilePath file;
  std::vector<base::FilePath> files;
  if (!item.Get("files", &files) && item.Get("file", &file)) {
    files.push_back(file);
  }

  mate::Handle<NativeImage> icon;
  if (!item.Get("icon", &icon) && !file.empty()) {
    // TODO(zcbenz): Set default icon from file.
  }

  // Error checking.
  if (icon.IsEmpty()) {
    args->ThrowError("Must specify 'icon' option");
    return;
  }

#if defined(OS_MACOSX)
  // NSWindow.dragImage requires a non-empty NSImage
  if (icon->image().IsEmpty()) {
    args->ThrowError("Must specify non-empty 'icon' option");
    return;
  }
#endif

  // Start dragging.
  if (!files.empty()) {
    base::MessageLoopCurrent::ScopedNestableTaskAllower allow;
    DragFileItems(files, icon->image(), web_contents()->GetNativeView());
  } else {
    args->ThrowError("Must specify either 'file' or 'files' option");
  }
}

v8::Local<v8::Promise> WebContents::CapturePage(mate::Arguments* args) {
  gfx::Rect rect;
  util::Promise promise(isolate());
  v8::Local<v8::Promise> handle = promise.GetHandle();

  // get rect arguments if they exist
  args->GetNext(&rect);

  auto* const view = web_contents()->GetRenderWidgetHostView();
  if (!view) {
    promise.Resolve(gfx::Image());
    return handle;
  }

  // Capture full page if user doesn't specify a |rect|.
  const gfx::Size view_size =
      rect.IsEmpty() ? view->GetViewBounds().size() : rect.size();

  // By default, the requested bitmap size is the view size in screen
  // coordinates.  However, if there's more pixel detail available on the
  // current system, increase the requested bitmap size to capture it all.
  gfx::Size bitmap_size = view_size;
  const gfx::NativeView native_view = view->GetNativeView();
  const float scale = display::Screen::GetScreen()
                          ->GetDisplayNearestView(native_view)
                          .device_scale_factor();
  if (scale > 1.0f)
    bitmap_size = gfx::ScaleToCeiledSize(view_size, scale);

  view->CopyFromSurface(gfx::Rect(rect.origin(), view_size), bitmap_size,
                        base::BindOnce(&OnCapturePageDone, std::move(promise)));
  return handle;
}

void WebContents::IncrementCapturerCount(mate::Arguments* args) {
  gfx::Size size;

  // get size arguments if they exist
  args->GetNext(&size);

  web_contents()->IncrementCapturerCount(size);
}

void WebContents::DecrementCapturerCount(mate::Arguments* args) {
  web_contents()->DecrementCapturerCount();
}

bool WebContents::IsBeingCaptured() {
  return web_contents()->IsBeingCaptured();
}

void WebContents::OnCursorChange(const content::WebCursor& cursor) {
  const content::CursorInfo& info = cursor.info();

  if (info.type == ui::CursorType::kCustom) {
    Emit("cursor-changed", CursorTypeToString(info),
         gfx::Image::CreateFrom1xBitmap(info.custom_image),
         info.image_scale_factor,
         gfx::Size(info.custom_image.width(), info.custom_image.height()),
         info.hotspot);
  } else {
    Emit("cursor-changed", CursorTypeToString(info));
  }
}

bool WebContents::IsGuest() const {
  return type_ == Type::WEB_VIEW;
}

void WebContents::AttachToIframe(content::WebContents* embedder_web_contents,
                                 int embedder_frame_id) {
  if (guest_delegate_)
    guest_delegate_->AttachToIframe(embedder_web_contents, embedder_frame_id);
}

bool WebContents::IsOffScreen() const {
#if BUILDFLAG(ENABLE_OSR)
  return type_ == Type::OFF_SCREEN;
#else
  return false;
#endif
}

#if BUILDFLAG(ENABLE_OSR)
void WebContents::OnPaint(const gfx::Rect& dirty_rect, const SkBitmap& bitmap) {
  Emit("paint", dirty_rect, gfx::Image::CreateFrom1xBitmap(bitmap));
}

void WebContents::StartPainting() {
  auto* osr_wcv = GetOffScreenWebContentsView();
  if (osr_wcv)
    osr_wcv->SetPainting(true);
}

void WebContents::StopPainting() {
  auto* osr_wcv = GetOffScreenWebContentsView();
  if (osr_wcv)
    osr_wcv->SetPainting(false);
}

bool WebContents::IsPainting() const {
  auto* osr_wcv = GetOffScreenWebContentsView();
  return osr_wcv && osr_wcv->IsPainting();
}

void WebContents::SetFrameRate(int frame_rate) {
  auto* osr_wcv = GetOffScreenWebContentsView();
  if (osr_wcv)
    osr_wcv->SetFrameRate(frame_rate);
}

int WebContents::GetFrameRate() const {
  auto* osr_wcv = GetOffScreenWebContentsView();
  return osr_wcv ? osr_wcv->GetFrameRate() : 0;
}
#endif

void WebContents::Invalidate() {
  if (IsOffScreen()) {
#if BUILDFLAG(ENABLE_OSR)
    auto* osr_rwhv = GetOffScreenRenderWidgetHostView();
    if (osr_rwhv)
      osr_rwhv->Invalidate();
#endif
  } else {
    auto* const window = owner_window();
    if (window)
      window->Invalidate();
  }
}

gfx::Size WebContents::GetSizeForNewRenderView(content::WebContents* wc) {
  if (IsOffScreen() && wc == web_contents()) {
    auto* relay = NativeWindowRelay::FromWebContents(web_contents());
    if (relay) {
      auto* owner_window = relay->GetNativeWindow();
      return owner_window ? owner_window->GetSize() : gfx::Size();
    }
  }

  return gfx::Size();
}

void WebContents::SetZoomLevel(double level) {
  zoom_controller_->SetZoomLevel(level);
}

double WebContents::GetZoomLevel() const {
  return zoom_controller_->GetZoomLevel();
}

void WebContents::SetZoomFactor(mate::Arguments* args, double factor) {
  if (factor < std::numeric_limits<double>::epsilon()) {
    args->ThrowError("'zoomFactor' must be a double greater than 0.0");
    return;
  }

  auto level = content::ZoomFactorToZoomLevel(factor);
  SetZoomLevel(level);
}

double WebContents::GetZoomFactor() const {
  auto level = GetZoomLevel();
  return content::ZoomLevelToZoomFactor(level);
}

void WebContents::SetTemporaryZoomLevel(double level) {
  zoom_controller_->SetTemporaryZoomLevel(level);
}

void WebContents::DoGetZoomLevel(DoGetZoomLevelCallback callback) {
  std::move(callback).Run(GetZoomLevel());
}

void WebContents::ShowAutofillPopup(const gfx::RectF& bounds,
                                    const std::vector<base::string16>& values,
                                    const std::vector<base::string16>& labels) {
  content::RenderFrameHost* frame_host = bindings_.dispatch_context();
  ShowAutofillPopup(frame_host, bounds, values, labels);
}

void WebContents::HideAutofillPopup() {
  CommonWebContentsDelegate::HideAutofillPopup();
}

std::vector<base::FilePath::StringType> WebContents::GetPreloadPaths() const {
  auto result = SessionPreferences::GetValidPreloads(GetBrowserContext());

  if (auto* web_preferences = WebContentsPreferences::From(web_contents())) {
    base::FilePath::StringType preload;
    if (web_preferences->GetPreloadPath(&preload)) {
      result.emplace_back(preload);
    }
  }

  return result;
}

v8::Local<v8::Value> WebContents::GetWebPreferences(
    v8::Isolate* isolate) const {
  auto* web_preferences = WebContentsPreferences::From(web_contents());
  if (!web_preferences)
    return v8::Null(isolate);
  return mate::ConvertToV8(isolate, *web_preferences->preference());
}

v8::Local<v8::Value> WebContents::GetLastWebPreferences(
    v8::Isolate* isolate) const {
  auto* web_preferences = WebContentsPreferences::From(web_contents());
  if (!web_preferences)
    return v8::Null(isolate);
  return mate::ConvertToV8(isolate, *web_preferences->last_preference());
}

bool WebContents::IsRemoteModuleEnabled() const {
  if (web_contents()->GetVisibleURL().SchemeIs("devtools")) {
    return false;
  }
  if (auto* web_preferences = WebContentsPreferences::From(web_contents())) {
    return web_preferences->IsRemoteModuleEnabled();
  }
  return true;
}

v8::Local<v8::Value> WebContents::GetOwnerBrowserWindow() const {
  if (owner_window())
    return BrowserWindow::From(isolate(), owner_window());
  else
    return v8::Null(isolate());
}

int32_t WebContents::ID() const {
  return weak_map_id();
}

v8::Local<v8::Value> WebContents::Session(v8::Isolate* isolate) {
  return v8::Local<v8::Value>::New(isolate, session_);
}

content::WebContents* WebContents::HostWebContents() {
  if (!embedder_)
    return nullptr;
  return embedder_->web_contents();
}

void WebContents::SetEmbedder(const WebContents* embedder) {
  if (embedder) {
    NativeWindow* owner_window = nullptr;
    auto* relay = NativeWindowRelay::FromWebContents(embedder->web_contents());
    if (relay) {
      owner_window = relay->GetNativeWindow();
    }
    if (owner_window)
      SetOwnerWindow(owner_window);

    content::RenderWidgetHostView* rwhv =
        web_contents()->GetRenderWidgetHostView();
    if (rwhv) {
      rwhv->Hide();
      rwhv->Show();
    }
  }
}

void WebContents::SetDevToolsWebContents(const WebContents* devtools) {
  if (managed_web_contents())
    managed_web_contents()->SetDevToolsWebContents(devtools->web_contents());
}

v8::Local<v8::Value> WebContents::GetNativeView() const {
  gfx::NativeView ptr = web_contents()->GetNativeView();
  auto buffer = node::Buffer::Copy(isolate(), reinterpret_cast<char*>(&ptr),
                                   sizeof(gfx::NativeView));
  if (buffer.IsEmpty())
    return v8::Null(isolate());
  else
    return buffer.ToLocalChecked();
}

v8::Local<v8::Value> WebContents::DevToolsWebContents(v8::Isolate* isolate) {
  if (devtools_web_contents_.IsEmpty())
    return v8::Null(isolate);
  else
    return v8::Local<v8::Value>::New(isolate, devtools_web_contents_);
}

v8::Local<v8::Value> WebContents::Debugger(v8::Isolate* isolate) {
  if (debugger_.IsEmpty()) {
    auto handle = electron::api::Debugger::Create(isolate, web_contents());
    debugger_.Reset(isolate, handle.ToV8());
  }
  return v8::Local<v8::Value>::New(isolate, debugger_);
}

void WebContents::GrantOriginAccess(const GURL& url) {
  content::ChildProcessSecurityPolicy::GetInstance()->GrantCommitOrigin(
      web_contents()->GetMainFrame()->GetProcess()->GetID(),
      url::Origin::Create(url));
}

v8::Local<v8::Promise> WebContents::TakeHeapSnapshot(
    const base::FilePath& file_path) {
  util::Promise promise(isolate());
  v8::Local<v8::Promise> handle = promise.GetHandle();

  base::ThreadRestrictions::ScopedAllowIO allow_io;
  base::File file(file_path,
                  base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
  if (!file.IsValid()) {
    promise.RejectWithErrorMessage("takeHeapSnapshot failed");
    return handle;
  }

  auto* frame_host = web_contents()->GetMainFrame();
  if (!frame_host) {
    promise.RejectWithErrorMessage("takeHeapSnapshot failed");
    return handle;
  }

  // This dance with `base::Owned` is to ensure that the interface stays alive
  // until the callback is called. Otherwise it would be closed at the end of
  // this function.
  auto electron_ptr = std::make_unique<mojom::ElectronRendererAssociatedPtr>();
  frame_host->GetRemoteAssociatedInterfaces()->GetInterface(
      mojo::MakeRequest(electron_ptr.get()));
  auto* raw_ptr = electron_ptr.get();
  (*raw_ptr)->TakeHeapSnapshot(
      mojo::WrapPlatformFile(file.TakePlatformFile()),
      base::BindOnce(
          [](mojom::ElectronRendererAssociatedPtr* ep, util::Promise promise,
             bool success) {
            if (success) {
              promise.Resolve();
            } else {
              promise.RejectWithErrorMessage("takeHeapSnapshot failed");
            }
          },
          base::Owned(std::move(electron_ptr)), std::move(promise)));
  return handle;
}

// static
void WebContents::BuildPrototype(v8::Isolate* isolate,
                                 v8::Local<v8::FunctionTemplate> prototype) {
  prototype->SetClassName(mate::StringToV8(isolate, "WebContents"));
  mate::ObjectTemplateBuilder(isolate, prototype->PrototypeTemplate())
      .MakeDestroyable()
      .SetMethod("setBackgroundThrottling",
                 &WebContents::SetBackgroundThrottling)
      .SetMethod("getProcessId", &WebContents::GetProcessID)
      .SetMethod("getOSProcessId", &WebContents::GetOSProcessID)
      .SetMethod("_getOSProcessIdForFrame",
                 &WebContents::GetOSProcessIdForFrame)
      .SetMethod("equal", &WebContents::Equal)
      .SetMethod("_loadURL", &WebContents::LoadURL)
      .SetMethod("downloadURL", &WebContents::DownloadURL)
      .SetMethod("_getURL", &WebContents::GetURL)
      .SetMethod("getTitle", &WebContents::GetTitle)
      .SetMethod("isLoading", &WebContents::IsLoading)
      .SetMethod("isLoadingMainFrame", &WebContents::IsLoadingMainFrame)
      .SetMethod("isWaitingForResponse", &WebContents::IsWaitingForResponse)
      .SetMethod("_stop", &WebContents::Stop)
      .SetMethod("_goBack", &WebContents::GoBack)
      .SetMethod("_goForward", &WebContents::GoForward)
      .SetMethod("_goToOffset", &WebContents::GoToOffset)
      .SetMethod("isCrashed", &WebContents::IsCrashed)
      .SetMethod("_setUserAgent", &WebContents::SetUserAgent)
      .SetMethod("_getUserAgent", &WebContents::GetUserAgent)
      .SetProperty("userAgent", &WebContents::GetUserAgent,
                   &WebContents::SetUserAgent)
      .SetMethod("savePage", &WebContents::SavePage)
      .SetMethod("openDevTools", &WebContents::OpenDevTools)
      .SetMethod("closeDevTools", &WebContents::CloseDevTools)
      .SetMethod("isDevToolsOpened", &WebContents::IsDevToolsOpened)
      .SetMethod("isDevToolsFocused", &WebContents::IsDevToolsFocused)
      .SetMethod("enableDeviceEmulation", &WebContents::EnableDeviceEmulation)
      .SetMethod("disableDeviceEmulation", &WebContents::DisableDeviceEmulation)
      .SetMethod("toggleDevTools", &WebContents::ToggleDevTools)
      .SetMethod("inspectElement", &WebContents::InspectElement)
      .SetMethod("setIgnoreMenuShortcuts", &WebContents::SetIgnoreMenuShortcuts)
      .SetMethod("_setAudioMuted", &WebContents::SetAudioMuted)
      .SetMethod("_isAudioMuted", &WebContents::IsAudioMuted)
      .SetProperty("audioMuted", &WebContents::IsAudioMuted,
                   &WebContents::SetAudioMuted)
      .SetMethod("isCurrentlyAudible", &WebContents::IsCurrentlyAudible)
      .SetMethod("undo", &WebContents::Undo)
      .SetMethod("redo", &WebContents::Redo)
      .SetMethod("cut", &WebContents::Cut)
      .SetMethod("copy", &WebContents::Copy)
      .SetMethod("paste", &WebContents::Paste)
      .SetMethod("pasteAndMatchStyle", &WebContents::PasteAndMatchStyle)
      .SetMethod("delete", &WebContents::Delete)
      .SetMethod("selectAll", &WebContents::SelectAll)
      .SetMethod("unselect", &WebContents::Unselect)
      .SetMethod("replace", &WebContents::Replace)
      .SetMethod("replaceMisspelling", &WebContents::ReplaceMisspelling)
      .SetMethod("findInPage", &WebContents::FindInPage)
      .SetMethod("stopFindInPage", &WebContents::StopFindInPage)
      .SetMethod("focus", &WebContents::Focus)
      .SetMethod("isFocused", &WebContents::IsFocused)
      .SetMethod("tabTraverse", &WebContents::TabTraverse)
      .SetMethod("_send", &WebContents::SendIPCMessage)
      .SetMethod("_sendToFrame", &WebContents::SendIPCMessageToFrame)
      .SetMethod("sendInputEvent", &WebContents::SendInputEvent)
      .SetMethod("beginFrameSubscription", &WebContents::BeginFrameSubscription)
      .SetMethod("endFrameSubscription", &WebContents::EndFrameSubscription)
      .SetMethod("startDrag", &WebContents::StartDrag)
      .SetMethod("attachToIframe", &WebContents::AttachToIframe)
      .SetMethod("detachFromOuterFrame", &WebContents::DetachFromOuterFrame)
      .SetMethod("isOffscreen", &WebContents::IsOffScreen)
#if BUILDFLAG(ENABLE_OSR)
      .SetMethod("startPainting", &WebContents::StartPainting)
      .SetMethod("stopPainting", &WebContents::StopPainting)
      .SetMethod("isPainting", &WebContents::IsPainting)
      .SetMethod("_setFrameRate", &WebContents::SetFrameRate)
      .SetMethod("_getFrameRate", &WebContents::GetFrameRate)
      .SetProperty("frameRate", &WebContents::GetFrameRate,
                   &WebContents::SetFrameRate)
#endif
      .SetMethod("invalidate", &WebContents::Invalidate)
      .SetMethod("_setZoomLevel", &WebContents::SetZoomLevel)
      .SetMethod("_getZoomLevel", &WebContents::GetZoomLevel)
      .SetProperty("zoomLevel", &WebContents::GetZoomLevel,
                   &WebContents::SetZoomLevel)
      .SetMethod("_setZoomFactor", &WebContents::SetZoomFactor)
      .SetMethod("_getZoomFactor", &WebContents::GetZoomFactor)
      .SetProperty("zoomFactor", &WebContents::GetZoomFactor,
                   &WebContents::SetZoomFactor)
      .SetMethod("getType", &WebContents::GetType)
      .SetMethod("_getPreloadPaths", &WebContents::GetPreloadPaths)
      .SetMethod("getWebPreferences", &WebContents::GetWebPreferences)
      .SetMethod("getLastWebPreferences", &WebContents::GetLastWebPreferences)
      .SetMethod("_isRemoteModuleEnabled", &WebContents::IsRemoteModuleEnabled)
      .SetMethod("getOwnerBrowserWindow", &WebContents::GetOwnerBrowserWindow)
      .SetMethod("inspectServiceWorker", &WebContents::InspectServiceWorker)
      .SetMethod("inspectSharedWorker", &WebContents::InspectSharedWorker)
#if BUILDFLAG(ENABLE_PRINTING)
      .SetMethod("_print", &WebContents::Print)
      .SetMethod("_getPrinters", &WebContents::GetPrinterList)
      .SetMethod("_printToPDF", &WebContents::PrintToPDF)
#endif
      .SetMethod("addWorkSpace", &WebContents::AddWorkSpace)
      .SetMethod("removeWorkSpace", &WebContents::RemoveWorkSpace)
      .SetMethod("showDefinitionForSelection",
                 &WebContents::ShowDefinitionForSelection)
      .SetMethod("copyImageAt", &WebContents::CopyImageAt)
      .SetMethod("capturePage", &WebContents::CapturePage)
      .SetMethod("setEmbedder", &WebContents::SetEmbedder)
      .SetMethod("setDevToolsWebContents", &WebContents::SetDevToolsWebContents)
      .SetMethod("getNativeView", &WebContents::GetNativeView)
      .SetMethod("incrementCapturerCount", &WebContents::IncrementCapturerCount)
      .SetMethod("decrementCapturerCount", &WebContents::DecrementCapturerCount)
      .SetMethod("isBeingCaptured", &WebContents::IsBeingCaptured)
      .SetMethod("setWebRTCIPHandlingPolicy",
                 &WebContents::SetWebRTCIPHandlingPolicy)
      .SetMethod("getWebRTCIPHandlingPolicy",
                 &WebContents::GetWebRTCIPHandlingPolicy)
      .SetMethod("_grantOriginAccess", &WebContents::GrantOriginAccess)
      .SetMethod("takeHeapSnapshot", &WebContents::TakeHeapSnapshot)
      .SetProperty("id", &WebContents::ID)
      .SetProperty("session", &WebContents::Session)
      .SetProperty("hostWebContents", &WebContents::HostWebContents)
      .SetProperty("devToolsWebContents", &WebContents::DevToolsWebContents)
      .SetProperty("debugger", &WebContents::Debugger);
}

AtomBrowserContext* WebContents::GetBrowserContext() const {
  return static_cast<AtomBrowserContext*>(web_contents()->GetBrowserContext());
}

// static
mate::Handle<WebContents> WebContents::Create(v8::Isolate* isolate,
                                              const mate::Dictionary& options) {
  return mate::CreateHandle(isolate, new WebContents(isolate, options));
}

// static
mate::Handle<WebContents> WebContents::CreateAndTake(
    v8::Isolate* isolate,
    std::unique_ptr<content::WebContents> web_contents,
    Type type) {
  return mate::CreateHandle(
      isolate, new WebContents(isolate, std::move(web_contents), type));
}

// static
mate::Handle<WebContents> WebContents::From(
    v8::Isolate* isolate,
    content::WebContents* web_contents) {
  auto* existing = TrackableObject::FromWrappedClass(isolate, web_contents);
  if (existing)
    return mate::CreateHandle(isolate, static_cast<WebContents*>(existing));
  else
    return mate::Handle<WebContents>();
}

// static
mate::Handle<WebContents> WebContents::FromOrCreate(
    v8::Isolate* isolate,
    content::WebContents* web_contents) {
  auto existing = From(isolate, web_contents);
  if (!existing.IsEmpty())
    return existing;
  else
    return mate::CreateHandle(isolate, new WebContents(isolate, web_contents));
}

}  // namespace api

}  // namespace electron

namespace {

using electron::api::WebContents;

void Initialize(v8::Local<v8::Object> exports,
                v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context,
                void* priv) {
  v8::Isolate* isolate = context->GetIsolate();
  mate::Dictionary dict(isolate, exports);
  dict.Set("WebContents", WebContents::GetConstructor(isolate)
                              ->GetFunction(context)
                              .ToLocalChecked());
  dict.SetMethod("create", &WebContents::Create);
  dict.SetMethod("fromId", &mate::TrackableObject<WebContents>::FromWeakMapID);
  dict.SetMethod("getAllWebContents",
                 &mate::TrackableObject<WebContents>::GetAll);
}

}  // namespace

NODE_LINKED_MODULE_CONTEXT_AWARE(atom_browser_web_contents, Initialize)
