// The render adapter's Vulkan backend: objects of the renderer's own passes (presentation,
// upscaling) made by the device, for code that still records them itself (docs/RENDER_ADAPTER.md,
// A6).
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>
#include <string_view>

#include "video_core/renderer_backend/pipeline.h"
#include "video_core/renderer_backend/resource.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {

class BackendDevice;

/// A pipeline the device made, destroyed with this object.
class HostPipeline {
public:
    HostPipeline() = default;
    HostPipeline(BackendDevice& device, const VideoCore::Backend::PipelineAllocation& allocation)
        : device{&device}, allocation{allocation} {}
    ~HostPipeline();

    HostPipeline(HostPipeline&& other) noexcept;
    HostPipeline& operator=(HostPipeline&& other) noexcept;
    HostPipeline(const HostPipeline&) = delete;
    HostPipeline& operator=(const HostPipeline&) = delete;

    explicit operator bool() const noexcept {
        return bool(allocation.handle);
    }
    /// The Vulkan pipeline.
    vk::Pipeline operator*() const noexcept {
        return vk::Pipeline{reinterpret_cast<VkPipeline>(allocation.native)};
    }
    /// Its layout, for pushing descriptors and constants.
    [[nodiscard]] vk::PipelineLayout Layout() const noexcept;
    [[nodiscard]] const VideoCore::Backend::PipelineAllocation& Allocation() const noexcept {
        return allocation;
    }

private:
    BackendDevice* device{};
    VideoCore::Backend::PipelineAllocation allocation;
};

/// A compute pipeline of a host pass from its SPIR-V; fatal when the driver rejects it.
[[nodiscard]] HostPipeline MakeHostCompute(BackendDevice& device, std::span<const u32> spirv,
                                           std::span<const VideoCore::Backend::BindingSlot> slots,
                                           u32 push_constant_size, std::string_view name,
                                           std::span<const u32> specialization = {});

/// A sampler the device made, destroyed with this object.
class HostSampler {
public:
    HostSampler() = default;
    HostSampler(BackendDevice& device, const VideoCore::Backend::SamplerDesc& desc);
    ~HostSampler();

    HostSampler(HostSampler&& other) noexcept;
    HostSampler& operator=(HostSampler&& other) noexcept;
    HostSampler(const HostSampler&) = delete;
    HostSampler& operator=(const HostSampler&) = delete;

    /// The Vulkan sampler.
    vk::Sampler operator*() const noexcept {
        return native;
    }

private:
    BackendDevice* device{};
    VideoCore::Backend::SamplerHandle sampler;
    vk::Sampler native;
};

/// An image the device made from a Vulkan create info (optimal tiling, undefined initial layout),
/// for code that still records itself; freed once the GPU is past it.
class HostImage {
public:
    HostImage() = default;
    explicit HostImage(BackendDevice& device) : device{&device} {}
    ~HostImage();

    HostImage(HostImage&& other) noexcept;
    HostImage& operator=(HostImage&& other) noexcept;
    HostImage(const HostImage&) = delete;
    HostImage& operator=(const HostImage&) = delete;

    void Create(const vk::ImageCreateInfo& create_info);
    void Destroy();

    operator vk::Image() const noexcept {
        return vk::Image{reinterpret_cast<VkImage>(allocation.native)};
    }
    explicit operator bool() const noexcept {
        return bool(allocation.handle);
    }
    [[nodiscard]] const VideoCore::Backend::ImageAllocation& Allocation() const noexcept {
        return allocation;
    }

    vk::ImageCreateInfo image_ci{}; ///< as it was made

private:
    BackendDevice* device{};
    VideoCore::Backend::ImageAllocation allocation;
};

} // namespace Vulkan
