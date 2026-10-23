// The render adapter's Vulkan backend: its draw and dispatch commands (docs/RENDER_ADAPTER.md, A4).
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "video_core/renderer_backend/commands.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {

class Scheduler;
class DescriptorHeap;

/// Records into the scheduler's current submission; each draw or dispatch is one recorded
/// command with its pipeline bind, as before the adapter.
class CommandRecorder final : public VideoCore::Backend::CommandRecorder {
public:
    CommandRecorder(Scheduler& scheduler, DescriptorHeap& descriptor_heap, vk::Device device)
        : scheduler{scheduler}, descriptor_heap{descriptor_heap}, device{device} {}

    void Draw(const VideoCore::Backend::DrawCommand& draw,
              const VideoCore::Backend::Crumb& crumb) override;
    void Dispatch(const VideoCore::Backend::DispatchCommand& dispatch,
                  const VideoCore::Backend::Crumb& crumb) override;
    void BindIndexBuffer(u64 buffer, u64 offset, VideoCore::Backend::IndexType type) override;
    void BindVertexBuffers(std::span<const u64> buffers, std::span<const u64> offsets,
                           std::span<const u64> sizes, std::span<const u64> strides) override;
    /// Pushes the descriptors (or, past the push limit, writes a set from the heap: a device
    /// call, made on this thread).
    void BindResources(const VideoCore::Backend::PipelineAllocation& pipeline,
                       std::span<const VideoCore::Backend::SlotWrite> writes,
                       std::span<const u8> push_constants,
                       std::span<const VideoCore::Backend::ImageBinding> images,
                       std::span<const VideoCore::Backend::BufferBinding> buffers) override;
    void SetVertexInput(std::span<const VideoCore::Backend::VertexBinding> bindings,
                        std::span<const VideoCore::Backend::VertexAttribute> attributes) override;
    void WriteLabel(u64 buffer, u64 offset, u64 value, u32 num_bytes) override;
    void BeginLabel(const char* name) override;
    void EndLabel() override;
    void InsertLabel(const char* name, u32 color) override;

    /// Labels as AMD buffer markers (end of pipe, inside render passes too) instead of
    /// portable transfer updates, which need no render pass open.
    void SetBufferMarkers(bool enabled) noexcept {
        buffer_markers = enabled;
    }

private:
    Scheduler& scheduler;
    DescriptorHeap& descriptor_heap;
    vk::Device device;
    bool buffer_markers{};
};

} // namespace Vulkan
