// Copyright (c) 2016 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/osr/osr_web_contents_view.h"

#include "content/browser/web_contents/web_contents_impl.h"  // nogncheck
#include "content/public/browser/render_view_host.h"
#include "ui/display/screen.h"
#include "ui/display/screen_info.h"

namespace electron {

OffScreenWebContentsView::OffScreenWebContentsView(
    bool transparent,
    const OnPaintCallback& callback)
    : transparent_(transparent),
      offscreenPaintCallback_(callback),
      callback_(base::BindRepeating(&OffScreenWebContentsView::OnPaint,
                                    base::Unretained(this))) {
#if BUILDFLAG(IS_MAC)
  PlatformCreate();
#endif
}

OffScreenWebContentsView::OffScreenWebContentsView(bool transparent)
    : transparent_(transparent),
      callback_(base::BindRepeating(&OffScreenWebContentsView::OnPaint,
                                    base::Unretained(this))) {
#if BUILDFLAG(IS_MAC)
  PlatformCreate();
#endif
}

OffScreenWebContentsView::~OffScreenWebContentsView() {
  if (native_window_)
    native_window_->RemoveObserver(this);

#if BUILDFLAG(IS_MAC)
  PlatformDestroy();
#endif
}

void OffScreenWebContentsView::SetPaintCallback(
    const OnPaintCallback& callback) {
  this->offscreenPaintCallback_ = callback;
}

void OffScreenWebContentsView::OnPaint(const gfx::Rect& dirty_rect,
                                       const SkBitmap& bitmap) {
  if (this->offscreenPaintCallback_) {
    this->offscreenPaintCallback_.Run(dirty_rect, bitmap);
  }
}

void OffScreenWebContentsView::SetWebContents(
    content::WebContents* web_contents) {
  web_contents_ = web_contents;

  if (GetView())
    GetView()->InstallTransparency();
}

void OffScreenWebContentsView::SetNativeWindow(NativeWindow* window) {
  if (native_window_)
    native_window_->RemoveObserver(this);

  native_window_ = window;

  if (native_window_)
    native_window_->AddObserver(this);

  OnWindowResize();
}

void OffScreenWebContentsView::OnWindowResize() {
  // In offscreen mode call RenderWidgetHostView's SetSize explicitly
  if (GetView())
    GetView()->SetSize(GetSize());
}

void OffScreenWebContentsView::OnWindowClosed() {
  if (native_window_) {
    native_window_->RemoveObserver(this);
    native_window_ = nullptr;
  }
}

gfx::Size OffScreenWebContentsView::GetSize() {
  // guilded patch - return 1 by 1 size for offscreen window, instead of 0x0,
  // which crashes the GPU process for auto:blank offscreen portal windows
  // when the native view is set, the size/layout will be dynamically updated
  // see: FrameSinkVideoCapturerImpl::SetResolutionConstraints in
  // src\dbus\components\viz\service\frame_sinks\video_capture\frame_sink_video_capturer_impl.cc
  return native_window_ ? native_window_->GetSize() : gfx::Size(1, 1);
}

#if !BUILDFLAG(IS_MAC)
gfx::NativeView OffScreenWebContentsView::GetNativeView() const {
  if (!native_window_)
    return gfx::NativeView();
  return native_window_->GetNativeView();
}

gfx::NativeView OffScreenWebContentsView::GetContentNativeView() const {
  if (!native_window_)
    return gfx::NativeView();
  return native_window_->GetNativeView();
}

gfx::NativeWindow OffScreenWebContentsView::GetTopLevelNativeWindow() const {
  if (!native_window_)
    return gfx::NativeWindow();
  return native_window_->GetNativeWindow();
}
#endif

gfx::Rect OffScreenWebContentsView::GetContainerBounds() const {
  return GetViewBounds();
}

void OffScreenWebContentsView::Focus() {}

void OffScreenWebContentsView::SetInitialFocus() {}

void OffScreenWebContentsView::StoreFocus() {}

void OffScreenWebContentsView::RestoreFocus() {}

void OffScreenWebContentsView::FocusThroughTabTraversal(bool reverse) {}

content::DropData* OffScreenWebContentsView::GetDropData() const {
  return nullptr;
}

void OffScreenWebContentsView::TransferDragSecurityInfo(WebContentsView* view) {
  NOTREACHED();
}

gfx::Rect OffScreenWebContentsView::GetViewBounds() const {
  return GetView() ? GetView()->GetViewBounds() : gfx::Rect();
}

void OffScreenWebContentsView::CreateView(gfx::NativeView context) {}

content::RenderWidgetHostViewBase*
OffScreenWebContentsView::CreateViewForWidget(
    content::RenderWidgetHost* render_widget_host) {
  if (render_widget_host->GetView()) {
    return static_cast<content::RenderWidgetHostViewBase*>(
        render_widget_host->GetView());
  }

  return new OffScreenRenderWidgetHostView(
      transparent_, painting_, GetFrameRate(), callback_, render_widget_host,
      nullptr, GetSize());
}

content::RenderWidgetHostViewBase*
OffScreenWebContentsView::CreateViewForChildWidget(
    content::RenderWidgetHost* render_widget_host) {
  auto* web_contents_impl =
      static_cast<content::WebContentsImpl*>(web_contents_);

  auto* view = static_cast<OffScreenRenderWidgetHostView*>(
      web_contents_impl->GetOuterWebContents()
          ? web_contents_impl->GetOuterWebContents()->GetRenderWidgetHostView()
          : web_contents_impl->GetRenderWidgetHostView());

  return new OffScreenRenderWidgetHostView(transparent_, painting_,
                                           view->frame_rate(), callback_,
                                           render_widget_host, view, GetSize());
}

void OffScreenWebContentsView::SetPageTitle(const std::u16string& title) {}

void OffScreenWebContentsView::RenderViewReady() {
  if (GetView())
    GetView()->InstallTransparency();
}

void OffScreenWebContentsView::RenderViewHostChanged(
    content::RenderViewHost* old_host,
    content::RenderViewHost* new_host) {}

void OffScreenWebContentsView::SetOverscrollControllerEnabled(bool enabled) {}

void OffScreenWebContentsView::OnCapturerCountChanged() {}

#if BUILDFLAG(IS_MAC)
bool OffScreenWebContentsView::CloseTabAfterEventTrackingIfNeeded() {
  return false;
}
#endif  // BUILDFLAG(IS_MAC)

void OffScreenWebContentsView::StartDragging(
    const content::DropData& drop_data,
    const url::Origin& source_origin,
    blink::DragOperationsMask allowed_ops,
    const gfx::ImageSkia& image,
    const gfx::Vector2d& cursor_offset,
    const gfx::Rect& drag_obj_rect,
    const blink::mojom::DragEventSourceInfo& event_info,
    content::RenderWidgetHostImpl* source_rwh) {
  if (web_contents_)
    static_cast<content::WebContentsImpl*>(web_contents_)
        ->SystemDragEnded(source_rwh);
}

void OffScreenWebContentsView::UpdateDragOperation(
    ui::mojom::DragOperation operation,
    bool document_is_handling_drag) {}

void OffScreenWebContentsView::SetPainting(bool painting) {
  auto* view = GetView();
  painting_ = painting;
  if (view != nullptr) {
    view->SetPainting(painting);
  }
}

bool OffScreenWebContentsView::IsPainting() const {
  if (auto* view = GetView())
    return view->is_painting();
  return painting_;
}

void OffScreenWebContentsView::SetFrameRate(int frame_rate) {
  auto* view = GetView();
  frame_rate_ = frame_rate;
  if (view != nullptr) {
    view->SetFrameRate(frame_rate);
  }
}

int OffScreenWebContentsView::GetFrameRate() const {
  if (auto* view = GetView())
    return view->frame_rate();
  return frame_rate_;
}

OffScreenRenderWidgetHostView* OffScreenWebContentsView::GetView() const {
  if (web_contents_) {
    return static_cast<OffScreenRenderWidgetHostView*>(
        web_contents_->GetRenderViewHost()->GetWidget()->GetView());
  }
  return nullptr;
}

void OffScreenWebContentsView::FullscreenStateChanged(bool is_fullscreen) {}

void OffScreenWebContentsView::UpdateWindowControlsOverlay(
    const gfx::Rect& bounding_rect) {}

}  // namespace electron
