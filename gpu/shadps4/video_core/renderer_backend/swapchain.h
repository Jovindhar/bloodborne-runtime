// The render adapter's swapchain: the window's images (docs/RENDER_ADAPTER.md, A6).
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"
#include "video_core/renderer_backend/format.h"

namespace VideoCore::Backend {

/// The images shown in the window. The backend owns the platform surface (a CAMetalLayer on
/// macOS) and picks the present mode from the settings when it is made.
class Swapchain {
public:
    virtual ~Swapchain() = default;

    /// Remakes the images for a window of `width` by `height` pixels.
    virtual void Resize(u32 width, u32 height) = 0;
    /// Takes the next image to draw into; false when the images must be remade first.
    virtual bool Acquire() = 0;
    /// Shows the acquired image; false when the images must be remade.
    virtual bool Present() = 0;
    /// False while the window is minimised (no images): frames are not shown.
    [[nodiscard]] virtual bool IsPresentable() const = 0;

    [[nodiscard]] virtual Format ImageFormat() const = 0;
    [[nodiscard]] virtual u32 Width() const = 0;
    [[nodiscard]] virtual u32 Height() const = 0;
    [[nodiscard]] virtual u32 ImageCount() const = 0;
    /// The acquired image and a view of it (the backend's objects, as Device::NativeHandle).
    [[nodiscard]] virtual u64 NativeImage() const = 0;
    [[nodiscard]] virtual u64 NativeImageView() const = 0;

    [[nodiscard]] virtual bool HasHDR() const = 0;
    /// HDR output, when HasHDR(); the images are remade.
    virtual void SetHDR(bool enabled) = 0;
    [[nodiscard]] virtual bool GetHDR() const = 0;
};

} // namespace VideoCore::Backend
