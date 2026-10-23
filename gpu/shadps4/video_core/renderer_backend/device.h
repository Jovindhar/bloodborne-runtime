// The render adapter's device: one per process, one graphics API (docs/RENDER_ADAPTER.md).
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>
#include <string_view>

#include "common/types.h"
#include "common/unique_function.h"
#include "video_core/renderer_backend/pipeline.h"
#include "video_core/renderer_backend/resource.h"

namespace VideoCore::Backend {

enum class Api : u8 {
    Vulkan,
    Metal,
};

/// The API the process renders with: BB_GFX_BACKEND=vulkan|metal, read once. Only Vulkan has a
/// backend so far (step A0); asking for Metal prints a note and returns Vulkan.
Api SelectedApi();

class Device {
public:
    virtual ~Device() = default;

    [[nodiscard]] virtual Api GetApi() const noexcept = 0;
    [[nodiscard]] virtual const Capabilities& GetCapabilities() const noexcept = 0;

    /// The tick of the submission now being recorded.
    [[nodiscard]] virtual u64 CurrentTick() const noexcept = 0;
    /// The GPU has completed every submission up to `tick`.
    [[nodiscard]] virtual bool IsComplete(u64 tick) noexcept = 0;
    virtual void Wait(u64 tick) = 0;

    /// Runs `release` once no submission that could use an object released now is pending,
    /// including work prepared before this call and recorded into a later submission. Callers
    /// never wait for the GPU to free something.
    virtual void DeferRelease(Common::UniqueFunction<void>&& release) = 0;

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

    [[nodiscard]] virtual SamplerHandle CreateSampler(const SamplerDesc& desc) = 0;
    virtual void DestroySampler(SamplerHandle sampler) = 0;

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
