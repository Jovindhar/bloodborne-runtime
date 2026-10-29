// The Vulkan backend's stream commands (docs/RENDER_ADAPTER.md, M2 step 2): how the scheduler's
// passes and the runtime's copies, clears and barriers encode. As Vulkan commands, or, with the
// Metal frame (BB_METAL_FRAME=1), straight into its Metal encoders (renderer_metal/
// mtl_recorder.mm) instead of as Vulkan calls the layer translates.
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {

/// Called inside recorded commands with the command buffer they run on; arrays live in the
/// recording chunk (or on the caller's stack when recorded at once).
class StreamEncoder {
public:
    virtual ~StreamEncoder() = default;

    virtual void CopyBuffer(vk::CommandBuffer cmd, vk::Buffer src, vk::Buffer dst,
                            std::span<const vk::BufferCopy> regions) const = 0;
    virtual void FillBuffer(vk::CommandBuffer cmd, vk::Buffer dst, u64 offset, u64 size,
                            u32 value) const = 0;
    virtual void UpdateBuffer(vk::CommandBuffer cmd, vk::Buffer dst, u64 offset,
                              std::span<const u8> data) const = 0;
    /// Images in the transfer layouts (destination, source) the runtime transitioned them to.
    virtual void CopyBufferToImage(vk::CommandBuffer cmd, vk::Buffer src, vk::Image dst,
                                   std::span<const vk::BufferImageCopy> regions) const = 0;
    virtual void CopyImageToBuffer(vk::CommandBuffer cmd, vk::Image src, vk::Buffer dst,
                                   std::span<const vk::BufferImageCopy> regions,
                                   vk::ImageLayout src_layout) const = 0;
    virtual void CopyImage(vk::CommandBuffer cmd, vk::Image src, vk::Image dst,
                           std::span<const vk::ImageCopy> regions, vk::ImageLayout src_layout,
                           vk::ImageLayout dst_layout) const = 0;
    virtual void BlitImage(vk::CommandBuffer cmd, vk::Image src, vk::ImageLayout src_layout,
                           vk::Image dst, vk::ImageLayout dst_layout,
                           std::span<const vk::ImageBlit> regions, vk::Filter filter) const = 0;
    virtual void ClearDepthStencilImage(vk::CommandBuffer cmd, vk::Image image,
                                        vk::ImageLayout layout,
                                        const vk::ClearDepthStencilValue& value,
                                        const vk::ImageSubresourceRange& range) const = 0;
    virtual void ResolveImage(vk::CommandBuffer cmd, vk::Image src, vk::Image dst,
                              const vk::ImageResolve& region) const = 0;
    virtual void ClearColorImage(vk::CommandBuffer cmd, vk::Image image,
                                 const vk::ClearColorValue& color,
                                 const vk::ImageSubresourceRange& range) const = 0;
    virtual void Barrier(vk::CommandBuffer cmd, const vk::DependencyInfo& info) const = 0;
    virtual void BeginRendering(vk::CommandBuffer cmd, const vk::RenderingInfo& info) const = 0;
    virtual void EndRendering(vk::CommandBuffer cmd) const = 0;
};

/// The Vulkan commands themselves.
[[nodiscard]] const StreamEncoder& VulkanStreamEncoder();

/// The device's encoder (the Metal frame's, or the Vulkan commands), for the Vulkan backend's
/// own passes that record on a command buffer (the upscaler). Set once, when the device is made.
[[nodiscard]] const StreamEncoder& ActiveStreamEncoder();
void SetActiveStreamEncoder(const StreamEncoder& encoder);

} // namespace Vulkan
