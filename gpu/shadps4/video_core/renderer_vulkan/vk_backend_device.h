// The render adapter's Vulkan backend: its device (docs/RENDER_ADAPTER.md, step A0).
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <memory>
#include <map>
#include <mutex>
#include <tuple>
#include <unordered_map>
#include <boost/container/static_vector.hpp>

#include "video_core/renderer_backend/device.h"
#include "video_core/renderer_backend/handle_table.h"
#include "video_core/renderer_vulkan/vk_command_recorder.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_resource_backend.h"
#include "video_core/renderer_vulkan/vk_resource_pool.h"
#include "video_core/texture_cache/image.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

namespace Frontend {
class WindowSDL;
}

namespace Vulkan {

/// Owns the Vulkan instance (device, queues, VMA) and the draw scheduler, whose submissions
/// are the adapter's ticks. Code not yet moved onto the adapter reaches them through
/// GetInstance() and GetScheduler().
class BackendDevice final : public VideoCore::Backend::Device {
public:
    BackendDevice(Frontend::WindowSDL& window, s32 physical_device_index, bool enable_validation,
                  bool enable_crash_diagnostic);
    ~BackendDevice() override;

    [[nodiscard]] bool HasReadbackQueue() const noexcept override;
    [[nodiscard]] bool ReadBackAfter(u64 buffer,
                                     std::span<const VideoCore::Backend::BufferCopy> copies,
                                     u64 total, u64 tick,
                                     const std::function<void(const u8*)>& use) override;

    [[nodiscard]] VideoCore::Backend::Api GetApi() const noexcept override {
        return VideoCore::Backend::Api::Vulkan;
    }
    [[nodiscard]] Shader::Profile ShaderProfile() const override;
    [[nodiscard]] VideoCore::Backend::MemoryReport ReportMemory() const override;
    [[nodiscard]] const VideoCore::Backend::Capabilities& GetCapabilities() const noexcept override {
        return capabilities;
    }

    [[nodiscard]] u64 CurrentTick() const noexcept override {
        return scheduler.CurrentTick();
    }
    [[nodiscard]] bool IsComplete(u64 tick) noexcept override {
        return scheduler.IsFree(tick);
    }
    [[nodiscard]] u64 CompletedTick() const noexcept override {
        return const_cast<Scheduler&>(scheduler).GetWorkSemaphore()->KnownGpuTick();
    }
    void RefreshTicks() override {
        scheduler.GetWorkSemaphore()->Refresh();
    }
    [[nodiscard]] WaitResult WaitFor(u64 tick, u64 timeout_ns) override;
    void Wait(u64 tick) override {
        scheduler.Wait(tick);
    }
    [[nodiscard]] CommandRecorder& Recorder() noexcept override {
        return *recorder;
    }

    void DeferRelease(Common::UniqueFunction<void>&& release) override {
        // Past the next submission: the draw pipeline records draws prepared earlier.
        scheduler.DeferOperationPastNext(std::move(release));
    }
    void AfterCompletion(Common::UniqueFunction<void>&& work) override {
        scheduler.DeferPriorityOperation(std::move(work));
    }
    void InstallGuestMemory() override;

    [[nodiscard]] VideoCore::Backend::BufferAllocation CreateBuffer(
        const VideoCore::Backend::BufferDesc& desc, std::string_view name) override;
    [[nodiscard]] VideoCore::Backend::BufferAllocation ImportHostMemory(
        void* host, u64 size, std::string_view name) override;
    [[nodiscard]] VideoCore::Backend::BufferAllocation ImportGuestMemoryChunk(
        u64 native_memory, u64 size, std::string_view name) override;
    void DestroyBuffer(VideoCore::Backend::BufferHandle buffer) override;
    void FlushBuffer(VideoCore::Backend::BufferHandle buffer, u64 offset, u64 size) override;
    void InvalidateBuffer(VideoCore::Backend::BufferHandle buffer, u64 offset, u64 size) override;
    [[nodiscard]] u64 NativeHandle(VideoCore::Backend::BufferHandle buffer) override;

    [[nodiscard]] VideoCore::Backend::Format SupportedFormat(
        VideoCore::Backend::Format format, VideoCore::Backend::ImageUse usage) override;
    [[nodiscard]] u32 SupportedSampleCounts(const VideoCore::Backend::ImageDesc& desc) override;
    [[nodiscard]] bool CanBlit(VideoCore::Backend::Format format) override;
    [[nodiscard]] VideoCore::Backend::ImageAllocation CreateImage(
        const VideoCore::Backend::ImageDesc& desc, std::string_view name) override;
    void DestroyImage(VideoCore::Backend::ImageHandle image) override;
    [[nodiscard]] u64 NativeHandle(VideoCore::Backend::ImageHandle image) override;
    [[nodiscard]] VideoCore::Backend::ImageViewAllocation CreateImageView(
        VideoCore::Backend::ImageHandle image, const VideoCore::Backend::ImageViewDesc& desc,
        std::string_view name) override;
    void DestroyImageView(VideoCore::Backend::ImageViewHandle view) override;
    [[nodiscard]] u64 NativeHandle(VideoCore::Backend::ImageViewHandle view) override;

    /// The create info the backend builds for `desc` (for Vulkan code that makes images like it).
    [[nodiscard]] vk::ImageCreateInfo ImageCreateInfo(const VideoCore::Backend::ImageDesc& desc) const;

    [[nodiscard]] VideoCore::Backend::ShaderAllocation CreateShader(
        std::span<const u32> spirv, std::string_view name) override;
    void DestroyShader(VideoCore::Backend::ShaderHandle shader) override;
    [[nodiscard]] u64 NativeHandle(VideoCore::Backend::ShaderHandle shader) override;

    [[nodiscard]] VideoCore::Backend::PipelineAllocation CreateGraphicsPipeline(
        const VideoCore::Backend::GraphicsPipelineDesc& desc, std::string_view name) override;
    [[nodiscard]] VideoCore::Backend::PipelineAllocation CreateComputePipeline(
        const VideoCore::Backend::ComputePipelineDesc& desc, std::string_view name) override;
    void DestroyPipeline(VideoCore::Backend::PipelineHandle pipeline) override;
    [[nodiscard]] u64 NativeHandle(VideoCore::Backend::PipelineHandle pipeline) override;

    /// A pipeline with the layouts it was made with, for binding its resources.
    struct PipelineRecord {
        vk::UniqueDescriptorSetLayout set_layout;
        vk::UniquePipelineLayout layout;
        vk::UniquePipeline pipeline;
        vk::PipelineBindPoint bind_point{};
        vk::ShaderStageFlags push_stages{}; ///< the push constants' stages
        bool push_descriptors{};            ///< the set is pushed, not allocated
        boost::container::static_vector<VideoCore::Backend::ShaderHandle, 5> shaders; ///< held
    };
    /// Valid until the pipeline is destroyed; null for a stale handle.
    [[nodiscard]] const PipelineRecord* GetPipelineRecord(VideoCore::Backend::PipelineHandle pipeline);

    [[nodiscard]] VideoCore::Backend::QueryPoolAllocation CreateOcclusionQueries(
        u32 count, std::string_view name) override;
    [[nodiscard]] VideoCore::Backend::QueryPoolAllocation CreateTimestampQueries(
        u32 count, std::string_view name) override;
    void DestroyQueryPool(VideoCore::Backend::QueryPoolHandle pool) override;
    void ResetQueries(VideoCore::Backend::QueryPoolHandle pool, u32 first, u32 count) override;
    [[nodiscard]] bool ReadQueryResults(VideoCore::Backend::QueryPoolHandle pool, u32 first,
                                        std::span<u64> values, bool wait) override;

    [[nodiscard]] VideoCore::Backend::SamplerHandle CreateSampler(
        const VideoCore::Backend::SamplerDesc& desc) override;
    void DestroySampler(VideoCore::Backend::SamplerHandle sampler) override;
    [[nodiscard]] u64 NativeHandle(VideoCore::Backend::SamplerHandle sampler) override;

    /// The device that owns `instance`, for Vulkan code that creates resources through the
    /// adapter but is not handed the device yet.
    [[nodiscard]] static BackendDevice& From(const Instance& instance) {
        return *instance.GetBackendDevice();
    }

    [[nodiscard]] Instance& GetInstance() noexcept {
        return instance;
    }
    [[nodiscard]] Scheduler& GetScheduler() noexcept {
        return scheduler;
    }
    /// Descriptor sets of pipelines with more slots than can be pushed.
    [[nodiscard]] DescriptorHeap& GetDescriptorHeap() noexcept {
        return descriptor_heap;
    }

private:
    // CanBlit: core formats index a flat table; extension formats (large values) use the map.
    std::array<u8, 256> blit_table{}; ///< 0 unknown, 1 no, 2 yes
    std::unordered_map<u32, bool> blit_formats;
    /// SupportedSampleCounts' answers by format, type, usage and flags: asked for every image the
    /// texture cache makes (about 2,300 driver queries in a run before).
    std::mutex sample_counts_mutex;
    std::map<std::tuple<u32, u32, u32, u32>, u32> sample_counts;
    Instance instance;
    Scheduler scheduler;
    VideoCore::Backend::Capabilities capabilities;
    DescriptorHeap descriptor_heap;
    /// The draw scheduler's command stream (the Metal recorder with the Metal frame).
    std::unique_ptr<CommandRecorder> recorder;

    VideoCore::Backend::BufferAllocation Insert(UniqueBuffer&& buffer, bool sparse);
    VideoCore::Backend::QueryPoolAllocation CreateQueries(vk::QueryType type, u32 count,
                                                          std::string_view name);
    [[nodiscard]] vk::QueryPool NativeQueryPool(VideoCore::Backend::QueryPoolHandle pool);
    /// The set and pipeline layouts of `layout`, in a new record.
    std::unique_ptr<PipelineRecord> MakePipelineLayout(const VideoCore::Backend::BindingLayout& layout,
                                                       vk::ShaderStageFlags push_stages,
                                                       std::string_view name);
    VideoCore::Backend::PipelineAllocation InsertPipeline(std::unique_ptr<PipelineRecord> record);

    /// A shader and its holders: the caller until DestroyShader, and each pipeline made with it.
    struct ShaderRecord {
        vk::UniqueShaderModule module; ///< none with the Metal frame (Metal builds its pipelines)
        u64 native{};                  ///< the module, or the Metal frame's placeholder for it
        u32 holders{1};

        [[nodiscard]] vk::ShaderModule Module() const noexcept {
            return vk::ShaderModule{reinterpret_cast<VkShaderModule>(native)};
        }
    };
    /// Drops a holder of `shader`; under objects_mutex. The module, when it was the last.
    [[nodiscard]] vk::UniqueShaderModule ReleaseShaderLocked(VideoCore::Backend::ShaderHandle shader);
    /// Drops `record`'s holds on its shaders.
    void ReleasePipelineShaders(PipelineRecord& record);

    // No VkPipelineCache: it was never saved, and pipelines loaded from the shader cache (what
    // persists) never had one.

    std::mutex readback_mutex;
    std::unique_ptr<class FastReadback> readback; ///< made on first use

    std::mutex objects_mutex;
    VideoCore::Backend::HandleTable<VideoCore::Backend::BufferTag, UniqueBuffer> buffers;
    VideoCore::Backend::HandleTable<VideoCore::Backend::ImageTag, std::unique_ptr<ImageRecord>>
        images;
    VideoCore::Backend::HandleTable<VideoCore::Backend::ImageViewTag, vk::UniqueImageView> views;
    VideoCore::Backend::HandleTable<VideoCore::Backend::SamplerTag, vk::UniqueSampler> samplers;
    VideoCore::Backend::HandleTable<VideoCore::Backend::QueryPoolTag, vk::UniqueQueryPool>
        query_pools;
    VideoCore::Backend::HandleTable<VideoCore::Backend::ShaderTag, ShaderRecord> shaders;
    VideoCore::Backend::HandleTable<VideoCore::Backend::PipelineTag, std::unique_ptr<PipelineRecord>>
        pipelines;
};

/// The barrier state of an image backing created by this backend.
[[nodiscard]] inline ImageState& ImageStateOf(const VideoCore::Image::BackingImage& backing) {
    return *static_cast<ImageState*>(backing.backend_state);
}

/// An image's NativeHandle as the Vulkan object.
[[nodiscard]] inline vk::Image NativeImage(u64 native) noexcept {
    return vk::Image{reinterpret_cast<VkImage>(native)};
}

/// An image view's NativeHandle as the Vulkan object.
[[nodiscard]] inline vk::ImageView NativeImageView(u64 native) noexcept {
    return vk::ImageView{reinterpret_cast<VkImageView>(native)};
}

/// A sampler's NativeHandle as the Vulkan object.
[[nodiscard]] inline vk::Sampler NativeSampler(u64 native) noexcept {
    return vk::Sampler{reinterpret_cast<VkSampler>(native)};
}

} // namespace Vulkan
