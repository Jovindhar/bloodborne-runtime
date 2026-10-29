// The render adapter's Vulkan backend: objects of the renderer's own passes (A6).
// SPDX-License-Identifier: GPL-2.0-or-later

#include <utility>

#include "common/assert.h"
#include "video_core/renderer_vulkan/vk_backend_device.h"
#include "video_core/renderer_vulkan/vk_backend_readback.h"
#include "video_core/renderer_vulkan/vk_format.h"
#include "video_core/renderer_vulkan/vk_host_objects.h"

namespace Vulkan {

namespace Backend = VideoCore::Backend;

HostPipeline::~HostPipeline() {
    if (device) {
        device->DestroyPipeline(allocation.handle);
    }
}

HostPipeline::HostPipeline(HostPipeline&& other) noexcept
    : device{std::exchange(other.device, nullptr)}, allocation{std::exchange(other.allocation, {})} {}

HostPipeline& HostPipeline::operator=(HostPipeline&& other) noexcept {
    if (this != &other) {
        if (device) {
            device->DestroyPipeline(allocation.handle);
        }
        device = std::exchange(other.device, nullptr);
        allocation = std::exchange(other.allocation, {});
    }
    return *this;
}

vk::PipelineLayout HostPipeline::Layout() const noexcept {
    const auto* record = static_cast<const BackendDevice::PipelineRecord*>(allocation.binding);
    return record ? *record->layout : vk::PipelineLayout{};
}

HostPipeline MakeHostCompute(BackendDevice& device, std::span<const u32> spirv,
                             std::span<const Backend::BindingSlot> slots, u32 push_constant_size,
                             std::string_view name, std::span<const u32> specialization) {
    const auto shader = device.CreateShader(spirv, name);
    ASSERT_MSG(shader.handle, "Failed to compile the {} shader", name);
    const auto allocation = device.CreateComputePipeline(
        {.shader = shader.handle,
         .layout = {.slots = slots, .push_constant_size = push_constant_size},
         .host_pass = true,
         .specialization = specialization},
        name);
    device.DestroyShader(shader.handle); // the pipeline holds it
    ASSERT_MSG(allocation.handle, "Failed to create the {} pipeline", name);
    return {device, allocation};
}

HostSampler::HostSampler(BackendDevice& device_, const Backend::SamplerDesc& desc)
    : device{&device_}, sampler{device_.CreateSampler(desc)},
      native{NativeSampler(device_.NativeHandle(sampler))} {}

HostSampler::~HostSampler() {
    if (device) {
        device->DestroySampler(sampler);
    }
}

HostSampler::HostSampler(HostSampler&& other) noexcept
    : device{std::exchange(other.device, nullptr)}, sampler{std::exchange(other.sampler, {})},
      native{std::exchange(other.native, {})} {}

HostSampler& HostSampler::operator=(HostSampler&& other) noexcept {
    if (this != &other) {
        if (device) {
            device->DestroySampler(sampler);
        }
        device = std::exchange(other.device, nullptr);
        sampler = std::exchange(other.sampler, {});
        native = std::exchange(other.native, {});
    }
    return *this;
}

HostImage::~HostImage() {
    Destroy();
}

HostImage::HostImage(HostImage&& other) noexcept
    : image_ci{other.image_ci}, device{std::exchange(other.device, nullptr)},
      allocation{std::exchange(other.allocation, {})} {}

HostImage& HostImage::operator=(HostImage&& other) noexcept {
    if (this != &other) {
        Destroy();
        image_ci = other.image_ci;
        device = std::exchange(other.device, nullptr);
        allocation = std::exchange(other.allocation, {});
    }
    return *this;
}

void HostImage::Create(const vk::ImageCreateInfo& create_info) {
    ASSERT(device && create_info.tiling == vk::ImageTiling::eOptimal &&
           create_info.initialLayout == vk::ImageLayout::eUndefined);
    Destroy();
    image_ci = create_info;
    // The adapter's types have Vulkan's values (vk_state.h).
    allocation = device->CreateImage(
        {
            .type = Backend::ImageType(s32(create_info.imageType)),
            .format = FromVk(create_info.format),
            .width = create_info.extent.width,
            .height = create_info.extent.height,
            .depth = create_info.extent.depth,
            .levels = create_info.mipLevels,
            .layers = create_info.arrayLayers,
            .samples = u32(create_info.samples),
            .usage = Backend::ImageUse(u32(create_info.usage)),
            .views_of_3d_slices = bool(create_info.flags & vk::ImageCreateFlagBits::e2DArrayCompatible),
            .uncompressed_block_views =
                bool(create_info.flags & vk::ImageCreateFlagBits::eBlockTexelViewCompatible),
            .mutable_format = bool(create_info.flags & vk::ImageCreateFlagBits::eMutableFormat),
        },
        {});
    ASSERT_MSG(allocation.handle, "Failed to create an image of a host pass");
}

void HostImage::Destroy() {
    if (device && allocation.handle) {
        device->DestroyImage(allocation.handle);
    }
    allocation = {};
}

} // namespace Vulkan
