// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "video_core/amdgpu/regs_depth.h"
#include "video_core/amdgpu/resource.h"
#include <utility>

#include "video_core/renderer_backend/device.h"
#include "video_core/renderer_backend/format.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/texture_cache/types.h"

namespace AmdGpu {
struct ColorBuffer;
}

namespace Shader {
struct ImageResource;
}

namespace Vulkan {
class Instance;
class Scheduler;
} // namespace Vulkan

namespace VideoCore {

struct ImageViewInfo {
    ImageViewInfo() = default;
    ImageViewInfo(const AmdGpu::Image& image, const Shader::ImageResource& desc) noexcept;
    ImageViewInfo(const AmdGpu::ColorBuffer& col_buffer) noexcept;
    ImageViewInfo(const AmdGpu::DepthBuffer& depth_buffer, AmdGpu::DepthView view,
                  AmdGpu::DepthControl ctl);

    AmdGpu::ImageType type = AmdGpu::ImageType::Color2D;
    Backend::Format format = Backend::Format::R8G8B8A8Unorm;
    SubresourceRange range;
    Backend::ComponentMapping mapping{};
    u32 min_lod = 0;
    bool is_storage = false;

    auto operator<=>(const ImageViewInfo&) const = default;
};

struct Image;

/// A view of an image's current backing, created through the render adapter
/// (docs/RENDER_ADAPTER.md, A2); its destruction is deferred by the device.
struct ImageView {
    ImageView(Backend::Device& device, const ImageViewInfo& info, const Image& image);
    ~ImageView();

    ImageView(const ImageView&) = delete;
    ImageView& operator=(const ImageView&) = delete;

    ImageView(ImageView&& other) noexcept
        : info{other.info}, device{other.device}, handle{std::exchange(other.handle, {})},
          native{other.native} {}
    ImageView& operator=(ImageView&& other) noexcept {
        std::swap(info, other.info);
        std::swap(device, other.device);
        std::swap(handle, other.handle);
        std::swap(native, other.native);
        return *this;
    }

    /// The backend's view (for Vulkan code until it records through the adapter).
    [[nodiscard]] vk::ImageView Handle() const noexcept {
        return vk::ImageView{reinterpret_cast<VkImageView>(native)};
    }

    ImageViewInfo info;

private:
    Backend::Device* device;
    Backend::ImageViewHandle handle;
    u64 native{};
};

} // namespace VideoCore
