// The render adapter's Vulkan backend: its draw and dispatch commands (docs/RENDER_ADAPTER.md, A4).
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "video_core/renderer_backend/commands.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {

class Scheduler;
class DescriptorHeap;

/// Records into the scheduler's current submission; each draw or dispatch is one recorded
/// command with its pipeline bind, as before the adapter. On macOS with the Metal frame
/// (BB_METAL_FRAME=1), renderer_metal/mtl_recorder.mm encodes some commands in Metal itself.
class CommandRecorder : public VideoCore::Backend::CommandRecorder {
public:
    CommandRecorder(Scheduler& scheduler, DescriptorHeap& descriptor_heap, vk::Device device)
        : scheduler{scheduler}, descriptor_heap{descriptor_heap}, device{device} {}

    void BeginPass(const VideoCore::Backend::RenderState& state) override;
    void EndPass() override;
    [[nodiscard]] bool InPass() const override;
    [[nodiscard]] bool DefersRecording() const override;
    void ClearImage(const VideoCore::Backend::ImageAllocation& image,
                    const VideoCore::Backend::ClearColor& value) override;
    [[nodiscard]] u32 StateOf(const VideoCore::Backend::ImageAllocation& image) const override;
    void BlitImage(u64 src, u64 dst, const VideoCore::Backend::ImageBlit& blit) override;
    [[nodiscard]] const VideoCore::Backend::RenderState& CurrentPass() const override;
    void SetCarriedScope(VideoCore::Backend::CarriedScope* scope) override;
    [[nodiscard]] bool InPassWith(const VideoCore::Backend::RenderState& state) const override;
    [[nodiscard]] bool WillBeginPass(const VideoCore::Backend::RenderState& state) const override;
    void ForceNewPass() override;
    void SetPassLabel(std::string label) override;

    [[nodiscard]] VideoCore::Backend::DrawState& GetDrawState() const override;
    void CommitDrawState() const override;

    void Draw(const VideoCore::Backend::DrawCommand& draw,
              const VideoCore::Backend::Crumb& crumb) override;
    void Dispatch(const VideoCore::Backend::DispatchCommand& dispatch,
                  const VideoCore::Backend::Crumb& crumb) override;
    void BindIndexBuffer(u64 buffer, u64 offset, VideoCore::Backend::IndexType type) override;
    void BindVertexBuffers(std::span<const u64> buffers, std::span<const u64> offsets,
                           std::span<const u64> sizes, std::span<const u64> strides) override;
    void TransitImage(const VideoCore::Backend::ImageAllocation& image,
                      VideoCore::Backend::ImageAspect aspect, VideoCore::Backend::ImageAccess access,
                      bool discard = false) override;
    void BeginQuery(u64 pool, u32 slot, bool precise) override;
    void EndQuery(u64 pool, u32 slot) override;
    void ResetQueries(u64 pool, u32 first, u32 count) override;
    void WriteTimestamp(u64 pool, u32 slot, bool end_of_pipe) override;
    void CopyQueryResults(u64 pool, u32 first, u32 count, u64 buffer, u64 offset) override;
    void FillBuffer(u64 buffer, u64 offset, u64 size, u32 value) override;
    void Barrier(VideoCore::Backend::MemoryUse before, VideoCore::Backend::MemoryUse after) override;
    void BufferBarrier(u64 buffer, u64 offset, u64 size, VideoCore::Backend::MemoryUse before,
                       VideoCore::Backend::MemoryUse after) override;
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
    void Submit() override;
    void Finish() override;
    void Kick(bool force = false) override;
    void CollectCompleted() override;
    void RecordHostCopy(Common::UniqueFunction<void>&& copy) override;
    void NoteHostCopySource(u64 address, u64 size) override;
    [[nodiscard]] bool HostCopiesPendingFor(u64 address, u64 size) override;
    void WaitHostCopies() override;
    void WaitHostCopiesFor(u64 address, u64 size) override;
    void SignalAfterHostCopies(std::function<void()> signal) override;
    void WaitDeferredSignals() override;
    void BeginLabel(const char* name) override;
    void EndLabel() override;
    void InsertLabel(const char* name, u32 color) override;

    /// Labels as AMD buffer markers (end of pipe, inside render passes too) instead of
    /// portable transfer updates, which need no render pass open.
    void SetBufferMarkers(bool enabled) noexcept override {
        buffer_markers = enabled;
    }

protected:
    /// How BindResources records its parts (`data` and `writes` already in the recording chunk);
    /// the Metal recorder encodes them instead.
    virtual void RecordPushConstants(vk::PipelineLayout layout, vk::ShaderStageFlags stages,
                                     std::span<const u8> data);
    virtual void RecordPushDescriptors(vk::PipelineBindPoint bind_point, vk::PipelineLayout layout,
                                       std::span<const vk::WriteDescriptorSet> writes);

    Scheduler& scheduler;
    DescriptorHeap& descriptor_heap;
    vk::Device device;
    bool buffer_markers{};
};

} // namespace Vulkan
