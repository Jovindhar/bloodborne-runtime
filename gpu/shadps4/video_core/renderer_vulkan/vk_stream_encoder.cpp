// The Vulkan backend's stream commands as Vulkan commands (vk_stream_encoder.h).
// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/renderer_vulkan/vk_stream_encoder.h"

namespace Vulkan {
namespace {

class VulkanEncoder final : public StreamEncoder {
public:
    void CopyBuffer(vk::CommandBuffer cmd, vk::Buffer src, vk::Buffer dst,
                    std::span<const vk::BufferCopy> regions) const override {
        cmd.copyBuffer(src, dst, u32(regions.size()), regions.data());
    }
    void FillBuffer(vk::CommandBuffer cmd, vk::Buffer dst, u64 offset, u64 size,
                    u32 value) const override {
        cmd.fillBuffer(dst, offset, size, value);
    }
    void UpdateBuffer(vk::CommandBuffer cmd, vk::Buffer dst, u64 offset,
                      std::span<const u8> data) const override {
        cmd.updateBuffer(dst, offset, data.size_bytes(), data.data());
    }
    void CopyBufferToImage(vk::CommandBuffer cmd, vk::Buffer src, vk::Image dst,
                           std::span<const vk::BufferImageCopy> regions) const override {
        cmd.copyBufferToImage(src, dst, vk::ImageLayout::eTransferDstOptimal, u32(regions.size()),
                              regions.data());
    }
    void CopyImageToBuffer(vk::CommandBuffer cmd, vk::Image src, vk::Buffer dst,
                           std::span<const vk::BufferImageCopy> regions,
                           vk::ImageLayout src_layout) const override {
        cmd.copyImageToBuffer(src, src_layout, dst, u32(regions.size()), regions.data());
    }
    void CopyImage(vk::CommandBuffer cmd, vk::Image src, vk::Image dst,
                   std::span<const vk::ImageCopy> regions, vk::ImageLayout src_layout,
                   vk::ImageLayout dst_layout) const override {
        cmd.copyImage(src, src_layout, dst, dst_layout, u32(regions.size()), regions.data());
    }
    void BlitImage(vk::CommandBuffer cmd, vk::Image src, vk::ImageLayout src_layout, vk::Image dst,
                   vk::ImageLayout dst_layout, std::span<const vk::ImageBlit> regions,
                   vk::Filter filter) const override {
        cmd.blitImage(src, src_layout, dst, dst_layout, u32(regions.size()), regions.data(),
                      filter);
    }
    void ClearDepthStencilImage(vk::CommandBuffer cmd, vk::Image image, vk::ImageLayout layout,
                                const vk::ClearDepthStencilValue& value,
                                const vk::ImageSubresourceRange& range) const override {
        cmd.clearDepthStencilImage(image, layout, value, range);
    }
    void ResolveImage(vk::CommandBuffer cmd, vk::Image src, vk::Image dst,
                      const vk::ImageResolve& region) const override {
        cmd.resolveImage(src, vk::ImageLayout::eTransferSrcOptimal, dst,
                         vk::ImageLayout::eTransferDstOptimal, region);
    }
    void ClearColorImage(vk::CommandBuffer cmd, vk::Image image, const vk::ClearColorValue& color,
                         const vk::ImageSubresourceRange& range) const override {
        cmd.clearColorImage(image, vk::ImageLayout::eTransferDstOptimal, color, range);
    }
    void Barrier(vk::CommandBuffer cmd, const vk::DependencyInfo& info) const override {
        cmd.pipelineBarrier2(info);
    }
    void BeginRendering(vk::CommandBuffer cmd, const vk::RenderingInfo& info) const override {
        cmd.beginRendering(info);
    }
    void EndRendering(vk::CommandBuffer cmd) const override {
        cmd.endRendering();
    }
};

} // namespace

const StreamEncoder& VulkanStreamEncoder() {
    static const VulkanEncoder encoder;
    return encoder;
}

namespace {
const StreamEncoder* active_encoder{};
}

const StreamEncoder& ActiveStreamEncoder() {
    return active_encoder ? *active_encoder : VulkanStreamEncoder();
}

void SetActiveStreamEncoder(const StreamEncoder& encoder) {
    active_encoder = &encoder;
}

} // namespace Vulkan
