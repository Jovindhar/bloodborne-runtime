// The render adapter's device: one per process, one graphics API (docs/RENDER_ADAPTER.md).
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <span>
#include <string_view>

#include "common/types.h"
#include "common/unique_function.h"
#include "shader_recompiler/profile.h"
#include "video_core/renderer_backend/commands.h"
#include "video_core/renderer_backend/pipeline.h"
#include "video_core/renderer_backend/resource.h"

namespace VideoCore::Backend {

enum class Api : u8 {
    Vulkan,
    Metal,
};

/// The API the process renders with: BB_GFX_BACKEND=vulkan|metal, read once. Metal (macOS only)
/// makes resources in Metal and records through MoltenVK so far (step M1).
Api SelectedApi();

class Device {
public:
    virtual ~Device() = default;

    [[nodiscard]] virtual Api GetApi() const noexcept = 0;
    /// What the device's shaders can do, for the shader recompiler (the fields that depend on
    /// the game or the memory model are the caller's).
    [[nodiscard]] virtual Shader::Profile ShaderProfile() const = 0;
    [[nodiscard]] virtual const Capabilities& GetCapabilities() const noexcept = 0;
    [[nodiscard]] virtual MemoryReport ReportMemory() const = 0;

    /// The tick of the submission now being recorded.
    [[nodiscard]] virtual u64 CurrentTick() const noexcept = 0;
    /// The GPU has completed every submission up to `tick` (asks the GPU when not known yet).
    [[nodiscard]] virtual bool IsComplete(u64 tick) noexcept = 0;
    /// The last tick known to be complete, without asking the GPU.
    [[nodiscard]] virtual u64 CompletedTick() const noexcept = 0;
    /// Asks the GPU how far it got (a system call: callers on hot paths throttle it).
    virtual void RefreshTicks() = 0;
    virtual void Wait(u64 tick) = 0;
    enum class WaitResult : u8 { Complete, Timeout, DeviceLost };
    /// Waits up to `timeout_ns` for `tick` (it may not be submitted yet).
    [[nodiscard]] virtual WaitResult WaitFor(u64 tick, u64 timeout_ns) = 0;

    /// The command stream the renderer records into.
    [[nodiscard]] virtual CommandRecorder& Recorder() noexcept = 0;

    /// Runs `release` once no submission that could use an object released now is pending,
    /// including work prepared before this call and recorded into a later submission. Callers
    /// never wait for the GPU to free something.
    virtual void DeferRelease(Common::UniqueFunction<void>&& release) = 0;
    /// Runs `work` on another thread as soon as the GPU has finished what was recorded so far
    /// (reading back what it wrote).
    virtual void AfterCompletion(Common::UniqueFunction<void>&& work) = 0;

    /// Guest memory the GPU uses in place where the platform maps it for the device
    /// (BB_GUEST_GPU_MEMORY, Linux): set up before the runtime maps direct memory.
    virtual void InstallGuestMemory() = 0;

    // Thread-safe. Destroy* is deferred like DeferRelease; the handle is stale at once.
    [[nodiscard]] virtual BufferAllocation CreateBuffer(const BufferDesc& desc,
                                                        std::string_view name) = 0;
    /// Host memory used where it is (macOS unified memory): every use, a device address, mapped.
    /// The memory stays the caller's; it must outlive the buffer.
    [[nodiscard]] virtual BufferAllocation ImportHostMemory(void* host, u64 size,
                                                            std::string_view name) = 0;
    /// A buffer over a guest memory chunk the backend exported (BB_GUEST_IN_PLACE, Linux):
    /// `native_memory` is the backend's memory object (a VkDeviceMemory).
    [[nodiscard]] virtual BufferAllocation ImportGuestMemoryChunk(u64 native_memory, u64 size,
                                                                  std::string_view name) = 0;
    virtual void DestroyBuffer(BufferHandle buffer) = 0;
    /// Makes host writes visible to the GPU / GPU writes visible to the host, for memory that is
    /// not coherent.
    virtual void FlushBuffer(BufferHandle buffer, u64 offset, u64 size) = 0;
    virtual void InvalidateBuffer(BufferHandle buffer, u64 offset, u64 size) = 0;

    /// `format`, or the closest format the device supports for `usage`.
    [[nodiscard]] virtual Format SupportedFormat(Format format, ImageUse usage) = 0;
    /// The sample counts `desc` can have, as a mask of counts (bit n: 2^n samples).
    [[nodiscard]] virtual u32 SupportedSampleCounts(const ImageDesc& desc) = 0;
    /// Images of `format` can be blitted (CommandRecorder::BlitImage). Cheap: asked per draw.
    [[nodiscard]] virtual bool CanBlit(Format format) = 0;
    [[nodiscard]] virtual ImageAllocation CreateImage(const ImageDesc& desc,
                                                      std::string_view name) = 0;
    virtual void DestroyImage(ImageHandle image) = 0;
    [[nodiscard]] virtual ImageViewAllocation CreateImageView(ImageHandle image,
                                                              const ImageViewDesc& desc,
                                                              std::string_view name) = 0;
    virtual void DestroyImageView(ImageViewHandle view) = 0;

    /// A shader from SPIR-V (the shader recompiler's output; the Metal backend converts it).
    [[nodiscard]] virtual ShaderAllocation CreateShader(std::span<const u32> spirv,
                                                        std::string_view name) = 0;
    /// Releases the caller's shader. Pipelines made with it keep it: it is freed when the last
    /// of them is. Once per CreateShader.
    virtual void DestroyShader(ShaderHandle shader) = 0;

    /// Pipelines from their descriptions. A pipeline holds its shaders until it is destroyed.
    [[nodiscard]] virtual PipelineAllocation CreateGraphicsPipeline(
        const GraphicsPipelineDesc& desc, std::string_view name) = 0;
    [[nodiscard]] virtual PipelineAllocation CreateComputePipeline(
        const ComputePipelineDesc& desc, std::string_view name) = 0;
    virtual void DestroyPipeline(PipelineHandle pipeline) = 0;

    [[nodiscard]] virtual QueryPoolAllocation CreateOcclusionQueries(u32 count,
                                                                     std::string_view name) = 0;
    [[nodiscard]] virtual QueryPoolAllocation CreateTimestampQueries(u32 count,
                                                                     std::string_view name) = 0;
    virtual void DestroyQueryPool(QueryPoolHandle pool) = 0;
    /// From the CPU, for queries no submission in flight uses.
    virtual void ResetQueries(QueryPoolHandle pool, u32 first, u32 count) = 0;
    /// The results of `values.size()` queries from `first` (64 bits each); false when one is not
    /// available (`wait`: until they are).
    [[nodiscard]] virtual bool ReadQueryResults(QueryPoolHandle pool, u32 first,
                                                std::span<u64> values, bool wait) = 0;

    [[nodiscard]] virtual SamplerHandle CreateSampler(const SamplerDesc& desc) = 0;
    virtual void DestroySampler(SamplerHandle sampler) = 0;

    /// The device copies buffers out on a queue of its own (ReadBackAfter can).
    [[nodiscard]] virtual bool HasReadbackQueue() const noexcept = 0;
    /// Copies `copies` of `buffer` (NativeHandle; destination offsets from 0, `total` bytes)
    /// once submission `tick` is complete, on the readback queue, and hands the host copy to
    /// `use`; false when it cannot (the caller copies in stream order).
    [[nodiscard]] virtual bool ReadBackAfter(u64 buffer, std::span<const BufferCopy> copies,
                                             u64 total, u64 tick,
                                             const std::function<void(const u8*)>& use) = 0;

    /// The backend's own object (a VkSampler, an id<MTLSamplerState>) as an integer, for code
    /// that still calls the API directly (docs/RENDER_ADAPTER.md, "native interop"). Zero for a
    /// stale handle. Valid until the object is destroyed.
    [[nodiscard]] virtual u64 NativeHandle(SamplerHandle sampler) = 0;
    [[nodiscard]] virtual u64 NativeHandle(BufferHandle buffer) = 0;
    [[nodiscard]] virtual u64 NativeHandle(ImageHandle image) = 0;
    [[nodiscard]] virtual u64 NativeHandle(ImageViewHandle view) = 0;
    [[nodiscard]] virtual u64 NativeHandle(ShaderHandle shader) = 0;
    [[nodiscard]] virtual u64 NativeHandle(PipelineHandle pipeline) = 0;
};

} // namespace VideoCore::Backend
