// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include <dwmapi.h>
#include <shellapi.h>

#include "content/public/browser/browser_accessibility_state.h"
#include "shell/browser/browser.h"
#include "shell/browser/native_window_views.h"
#include "shell/browser/ui/views/root_view.h"
#include "shell/common/atom_constants.h"
#include "ui/base/win/accessibility_misc_utils.h"
#include "ui/display/display.h"
#include "ui/display/win/screen_win.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/widget/native_widget_private.h"

// Must be included after other Windows headers.
#include <UIAutomationCoreApi.h>

namespace electron {

namespace {

// Convert Win32 WM_APPCOMMANDS to strings.
const char* AppCommandToString(int command_id) {
  switch (command_id) {
    case APPCOMMAND_BROWSER_BACKWARD:
      return kBrowserBackward;
    case APPCOMMAND_BROWSER_FORWARD:
      return kBrowserForward;
    case APPCOMMAND_BROWSER_REFRESH:
      return "browser-refresh";
    case APPCOMMAND_BROWSER_STOP:
      return "browser-stop";
    case APPCOMMAND_BROWSER_SEARCH:
      return "browser-search";
    case APPCOMMAND_BROWSER_FAVORITES:
      return "browser-favorites";
    case APPCOMMAND_BROWSER_HOME:
      return "browser-home";
    case APPCOMMAND_VOLUME_MUTE:
      return "volume-mute";
    case APPCOMMAND_VOLUME_DOWN:
      return "volume-down";
    case APPCOMMAND_VOLUME_UP:
      return "volume-up";
    case APPCOMMAND_MEDIA_NEXTTRACK:
      return "media-nexttrack";
    case APPCOMMAND_MEDIA_PREVIOUSTRACK:
      return "media-previoustrack";
    case APPCOMMAND_MEDIA_STOP:
      return "media-stop";
    case APPCOMMAND_MEDIA_PLAY_PAUSE:
      return "media-play-pause";
    case APPCOMMAND_LAUNCH_MAIL:
      return "launch-mail";
    case APPCOMMAND_LAUNCH_MEDIA_SELECT:
      return "launch-media-select";
    case APPCOMMAND_LAUNCH_APP1:
      return "launch-app1";
    case APPCOMMAND_LAUNCH_APP2:
      return "launch-app2";
    case APPCOMMAND_BASS_DOWN:
      return "bass-down";
    case APPCOMMAND_BASS_BOOST:
      return "bass-boost";
    case APPCOMMAND_BASS_UP:
      return "bass-up";
    case APPCOMMAND_TREBLE_DOWN:
      return "treble-down";
    case APPCOMMAND_TREBLE_UP:
      return "treble-up";
    case APPCOMMAND_MICROPHONE_VOLUME_MUTE:
      return "microphone-volume-mute";
    case APPCOMMAND_MICROPHONE_VOLUME_DOWN:
      return "microphone-volume-down";
    case APPCOMMAND_MICROPHONE_VOLUME_UP:
      return "microphone-volume-up";
    case APPCOMMAND_HELP:
      return "help";
    case APPCOMMAND_FIND:
      return "find";
    case APPCOMMAND_NEW:
      return "new";
    case APPCOMMAND_OPEN:
      return "open";
    case APPCOMMAND_CLOSE:
      return "close";
    case APPCOMMAND_SAVE:
      return "save";
    case APPCOMMAND_PRINT:
      return "print";
    case APPCOMMAND_UNDO:
      return "undo";
    case APPCOMMAND_REDO:
      return "redo";
    case APPCOMMAND_COPY:
      return "copy";
    case APPCOMMAND_CUT:
      return "cut";
    case APPCOMMAND_PASTE:
      return "paste";
    case APPCOMMAND_REPLY_TO_MAIL:
      return "reply-to-mail";
    case APPCOMMAND_FORWARD_MAIL:
      return "forward-mail";
    case APPCOMMAND_SEND_MAIL:
      return "send-mail";
    case APPCOMMAND_SPELL_CHECK:
      return "spell-check";
    case APPCOMMAND_MIC_ON_OFF_TOGGLE:
      return "mic-on-off-toggle";
    case APPCOMMAND_CORRECTION_LIST:
      return "correction-list";
    case APPCOMMAND_MEDIA_PLAY:
      return "media-play";
    case APPCOMMAND_MEDIA_PAUSE:
      return "media-pause";
    case APPCOMMAND_MEDIA_RECORD:
      return "media-record";
    case APPCOMMAND_MEDIA_FAST_FORWARD:
      return "media-fast-forward";
    case APPCOMMAND_MEDIA_REWIND:
      return "media-rewind";
    case APPCOMMAND_MEDIA_CHANNEL_UP:
      return "media-channel-up";
    case APPCOMMAND_MEDIA_CHANNEL_DOWN:
      return "media-channel-down";
    case APPCOMMAND_DELETE:
      return "delete";
    case APPCOMMAND_DICTATE_OR_COMMAND_CONTROL_TOGGLE:
      return "dictate-or-command-control-toggle";
    default:
      return "unknown";
  }
}

bool IsScreenReaderActive() {
  UINT screenReader = 0;
  SystemParametersInfo(SPI_GETSCREENREADER, 0, &screenReader, 0);
  return screenReader && UiaClientsAreListening();
}

}  // namespace

std::set<NativeWindowViews*> NativeWindowViews::forwarding_windows_;
HHOOK NativeWindowViews::mouse_hook_ = NULL;

void NativeWindowViews::Maximize() {
  // Only use Maximize() when:
  // 1. window has WS_THICKFRAME style;
  // 2. and window is not frameless when there is autohide taskbar.
  if (::GetWindowLong(GetAcceleratedWidget(), GWL_STYLE) & WS_THICKFRAME) {
    if (IsVisible())
      widget()->Maximize();
    else
      widget()->native_widget_private()->Show(ui::SHOW_STATE_MAXIMIZED,
                                              gfx::Rect());
    return;
  } else {
    restore_bounds_ = GetBounds();
    auto display =
        display::Screen::GetScreen()->GetDisplayNearestPoint(GetPosition());
    SetBounds(display.work_area(), false);
  }
}

bool NativeWindowViews::ExecuteWindowsCommand(int command_id) {
  std::string command = AppCommandToString(command_id);
  NotifyWindowExecuteAppCommand(command);

  return false;
}

bool NativeWindowViews::PreHandleMSG(UINT message,
                                     WPARAM w_param,
                                     LPARAM l_param,
                                     LRESULT* result) {
  NotifyWindowMessage(message, w_param, l_param);

  // See code below for why blocking Chromium from handling messages.
  if (block_chromium_message_handler_) {
    // Handle the message with default proc.
    *result = DefWindowProc(GetAcceleratedWidget(), message, w_param, l_param);
    // Tell Chromium to ignore this message.
    return true;
  }

  switch (message) {
    // Screen readers send WM_GETOBJECT in order to get the accessibility
    // object, so take this opportunity to push Chromium into accessible
    // mode if it isn't already, always say we didn't handle the message
    // because we still want Chromium to handle returning the actual
    // accessibility object.
    case WM_GETOBJECT: {
      if (checked_for_a11y_support_)
        return false;

      const DWORD obj_id = static_cast<DWORD>(l_param);

      if (obj_id != static_cast<DWORD>(OBJID_CLIENT)) {
        return false;
      }

      if (!IsScreenReaderActive()) {
        return false;
      }

      checked_for_a11y_support_ = true;

      auto* const axState = content::BrowserAccessibilityState::GetInstance();
      if (axState && !axState->IsAccessibleBrowser()) {
        axState->OnScreenReaderDetected();
        Browser::Get()->OnAccessibilitySupportChanged();
      }

      return false;
    }
    case WM_GETMINMAXINFO: {
      WINDOWPLACEMENT wp;
      wp.length = sizeof(WINDOWPLACEMENT);

      // We do this to work around a Windows bug, where the minimized Window
      // would report that the closest display to it is not the one that it was
      // previously on (but the leftmost one instead). We restore the position
      // of the window during the restore operation, this way chromium can
      // use the proper display to calculate the scale factor to use.
      if (!last_normal_placement_bounds_.IsEmpty() &&
          GetWindowPlacement(GetAcceleratedWidget(), &wp)) {
        wp.rcNormalPosition = last_normal_placement_bounds_.ToRECT();

        // When calling SetWindowPlacement, Chromium would do window messages
        // handling. But since we are already in PreHandleMSG this would cause
        // crash in Chromium under some cases.
        //
        // We work around the crash by prevent Chromium from handling window
        // messages until the SetWindowPlacement call is done.
        //
        // See https://github.com/electron/electron/issues/21614 for more.
        block_chromium_message_handler_ = true;
        SetWindowPlacement(GetAcceleratedWidget(), &wp);
        block_chromium_message_handler_ = false;

        last_normal_placement_bounds_ = gfx::Rect();
      }

      return false;
    }
    case WM_COMMAND:
      // Handle thumbar button click message.
      if (HIWORD(w_param) == THBN_CLICKED)
        return taskbar_host_.HandleThumbarButtonEvent(LOWORD(w_param));
      return false;
    case WM_SIZING: {
      bool prevent_default = false;
      NotifyWindowWillResize(gfx::Rect(*reinterpret_cast<RECT*>(l_param)),
                             &prevent_default);
      if (prevent_default) {
        ::GetWindowRect(GetAcceleratedWidget(),
                        reinterpret_cast<RECT*>(l_param));
        return true;  // Tells Windows that the Sizing is handled.
      }
      return false;
    }
    case WM_SIZE: {
      // Handle window state change.
      HandleSizeEvent(w_param, l_param);
      return false;
    }
    case WM_MOVING: {
      bool prevent_default = false;
      NotifyWindowWillMove(gfx::Rect(*reinterpret_cast<RECT*>(l_param)),
                           &prevent_default);
      if (!movable_ || prevent_default) {
        ::GetWindowRect(GetAcceleratedWidget(),
                        reinterpret_cast<RECT*>(l_param));
        return true;  // Tells Windows that the Move is handled. If not true,
                      // frameless windows can be moved using
                      // -webkit-app-region: drag elements.
      }
      return false;
    }
    case WM_ENDSESSION: {
      if (w_param) {
        NotifyWindowEndSession();
      }
      return false;
    }
    case WM_PARENTNOTIFY: {
      if (LOWORD(w_param) == WM_CREATE) {
        // Because of reasons regarding legacy drivers and stuff, a window that
        // matches the client area is created and used internally by Chromium.
        // This is used when forwarding mouse messages. We only cache the first
        // occurrence (the webview window) because dev tools also cause this
        // message to be sent.
        if (!legacy_window_) {
          legacy_window_ = reinterpret_cast<HWND>(l_param);
        }
      }
      return false;
    }
    default:
      return false;
  }
}

void NativeWindowViews::HandleSizeEvent(WPARAM w_param, LPARAM l_param) {
  // Here we handle the WM_SIZE event in order to figure out what is the current
  // window state and notify the user accordingly.
  switch (w_param) {
    case SIZE_MAXIMIZED: {
      last_window_state_ = ui::SHOW_STATE_MAXIMIZED;
      NotifyWindowMaximize();
      break;
    }
    case SIZE_MINIMIZED:
      last_window_state_ = ui::SHOW_STATE_MINIMIZED;

      WINDOWPLACEMENT wp;
      wp.length = sizeof(WINDOWPLACEMENT);

      if (GetWindowPlacement(GetAcceleratedWidget(), &wp)) {
        last_normal_placement_bounds_ = gfx::Rect(wp.rcNormalPosition);
      }

      NotifyWindowMinimize();
      break;
    case SIZE_RESTORED:
      switch (last_window_state_) {
        case ui::SHOW_STATE_MAXIMIZED:
          last_window_state_ = ui::SHOW_STATE_NORMAL;
          NotifyWindowUnmaximize();
          break;
        case ui::SHOW_STATE_MINIMIZED:
          if (IsFullscreen()) {
            last_window_state_ = ui::SHOW_STATE_FULLSCREEN;
            NotifyWindowEnterFullScreen();
          } else {
            last_window_state_ = ui::SHOW_STATE_NORMAL;
            NotifyWindowRestore();
          }
          break;
        default:
          break;
      }
      break;
  }
}

void NativeWindowViews::SetForwardMouseMessages(bool forward) {
  // It's possible that the legacy_window_ is not set when spawning child
  // windows (ex: window.open JS api). The WM_PARENTNOTIFY gets sent to the
  // initial opener/parent when the legacy window is created, then the legacy
  // win gets re-attached to the newly created host native window. Here,
  // manual grab the legacy window if it's not yet being tracked
  if (!legacy_window_) {
    HWND parent_window = this->GetNativeWindowHandle();
    LPARAM native_view = reinterpret_cast<LPARAM>(this);
    EnumChildWindows(parent_window, FindLegacyWinFromChildWindowsProc,
                     native_view);
  }

  if (forward && !forwarding_mouse_messages_) {
    forwarding_mouse_messages_ = true;
    forwarding_windows_.insert(this);

    // Subclassing is used to fix some issues when forwarding mouse messages;
    // see comments in |SubclassProc|.
    SetWindowSubclass(legacy_window_, SubclassProc, 1,
                      reinterpret_cast<DWORD_PTR>(this));

    if (!mouse_hook_) {
      mouse_hook_ = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, NULL, 0);
    }
  } else if (!forward && forwarding_mouse_messages_) {
    forwarding_mouse_messages_ = false;
    forwarding_windows_.erase(this);

    RemoveWindowSubclass(legacy_window_, SubclassProc, 1);

    if (forwarding_windows_.size() == 0) {
      UnhookWindowsHookEx(mouse_hook_);
      mouse_hook_ = NULL;
    }
  }
}

BOOL CALLBACK
NativeWindowViews::FindLegacyWinFromChildWindowsProc(HWND hWnd, LPARAM lParam) {
  if (hWnd) {
    NativeWindowViews* native_view =
        reinterpret_cast<NativeWindowViews*>(lParam);
    wchar_t windowCaption[256];
    GetWindowTextW(hWnd, &windowCaption[0], 256);
    if (!wcscmp(windowCaption, L"Chrome Legacy Window")) {
      native_view->legacy_window_ = hWnd;
      // stop enumerating windows
      return false;
    }
  }

  // check next window
  return true;
}

LRESULT CALLBACK NativeWindowViews::SubclassProc(HWND hwnd,
                                                 UINT msg,
                                                 WPARAM w_param,
                                                 LPARAM l_param,
                                                 UINT_PTR subclass_id,
                                                 DWORD_PTR ref_data) {
  NativeWindowViews* window = reinterpret_cast<NativeWindowViews*>(ref_data);
  switch (msg) {
    case WM_MOUSELEAVE: {
      // When input is forwarded to underlying windows, this message is posted.
      // If not handled, it interferes with Chromium logic, causing for example
      // mouseleave events to fire. If those events are used to exit forward
      // mode, excessive flickering on for example hover items in underlying
      // windows can occur due to rapidly entering and leaving forwarding mode.
      // By consuming and ignoring the message, we're essentially telling
      // Chromium that we have not left the window despite somebody else getting
      // the messages. As to why this is catched for the legacy window and not
      // the actual browser window is simply that the legacy window somehow
      // makes use of these events; posting to the main window didn't work.
      if (window->forwarding_mouse_messages_) {
        return 0;
      }
      break;
    }
  }

  return DefSubclassProc(hwnd, msg, w_param, l_param);
}

LRESULT CALLBACK NativeWindowViews::MouseHookProc(int n_code,
                                                  WPARAM w_param,
                                                  LPARAM l_param) {
  if (n_code < 0) {
    return CallNextHookEx(NULL, n_code, w_param, l_param);
  }

  // Post a WM_MOUSEMOVE message for those windows whose client area contains
  // the cursor since they are in a state where they would otherwise ignore all
  // mouse input.
  if (w_param == WM_MOUSEMOVE) {
    for (auto* window : forwarding_windows_) {
      // At first I considered enumerating windows to check whether the cursor
      // was directly above the window, but since nothing bad seems to happen
      // if we post the message even if some other window occludes it I have
      // just left it as is.
      RECT client_rect;
      GetClientRect(window->legacy_window_, &client_rect);
      POINT p = reinterpret_cast<MSLLHOOKSTRUCT*>(l_param)->pt;
      ScreenToClient(window->legacy_window_, &p);
      if (PtInRect(&client_rect, p)) {
        WPARAM w = 0;  // No virtual keys pressed for our purposes
        LPARAM l = MAKELPARAM(p.x, p.y);
        PostMessage(window->legacy_window_, WM_MOUSEMOVE, w, l);
      }
    }
  }

  return CallNextHookEx(NULL, n_code, w_param, l_param);
}

}  // namespace electron
