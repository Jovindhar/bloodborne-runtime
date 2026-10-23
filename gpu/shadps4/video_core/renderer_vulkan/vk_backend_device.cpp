// SPDX-License-Identifier: GPL-2.0-or-later

#include <bit>
#include <mutex>
#include <set>
#include <cstdio>
#include <string>

#include "bbport_guest_memory.h"
#include "bbport_metal_residency.h"
#include "video_core/renderer_vulkan/vk_backend_device.h"
#include "video_core/renderer_vulkan/vk_metal_shadow.h"
#include "video_core/renderer_vulkan/vk_platform.h"
#include "video_core/renderer_vulkan/vk_resource_backend.h"
#include "video_core/renderer_vulkan/vk_state.h"

#include <vk_mem_alloc.h>

namespace Vulkan {

namespace {
constexpr std::array DescriptorHeapSizes = {
    vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, 512},
    vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, 8192},
    vk::DescriptorPoolSize{vk::DescriptorType::eSampledImage, 8192},
    vk::DescriptorPoolSize{vk::DescriptorType::eStorageImage, 1024},
    vk::DescriptorPoolSize{vk::DescriptorType::eSampler, 1024},
};
} // namespace

BackendDevice::BackendDevice(Frontend::WindowSDL& window, s32 physical_device_index,
                             bool enable_validation, bool enable_crash_diagnostic)
    : instance{window, physical_device_index, enable_validation, enable_crash_diagnostic},
      scheduler{instance, true}, capabilities{QueryResourceCapabilities(instance)},
      descriptor_heap{instance, scheduler.GetWorkSemaphore(), DescriptorHeapSizes} {
    instance.SetBackendDevice(this);
    capabilities.non_coherent_atom_size = instance.NonCoherentAtomSize();
    VideoCore::Backend::SelectedApi();
}

namespace Backend = VideoCore::Backend;

Backend::BufferAllocation BackendDevice::Insert(UniqueBuffer&& buffer, bool sparse) {
    Backend::BufferAllocation result{
        .native = reinterpret_cast<u64>(static_cast<VkBuffer>(buffer.buffer)),
        .device_address = buffer.bda_addr,
        .coherent = !sparse, // imports are coherent; sparse buffers have no host mapping
    };
    if (buffer.allocation && !sparse) {
        VmaAllocationInfo alloc_info{};
        vmaGetAllocationInfo(buffer.allocator, buffer.allocation, &alloc_info);
        VkMemoryPropertyFlags property_flags{};
        vmaGetAllocationMemoryProperties(buffer.allocator, buffer.allocation, &property_flags);
        if (alloc_info.pMappedData) {
            result.mapped = std::span<u8>{static_cast<u8*>(alloc_info.pMappedData),
                                          static_cast<size_t>(alloc_info.size)};
        }
        result.coherent = property_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }
    std::scoped_lock lock{objects_mutex};
    result.handle = buffers.Insert(std::move(buffer));
    return result;
}

Backend::BufferAllocation BackendDevice::CreateBuffer(const Backend::BufferDesc& desc,
                                                      std::string_view name) {
    UniqueBuffer buffer{instance.GetDevice(), instance.GetAllocator()};
    CreateBufferFromDesc(instance, buffer, desc, nullptr);
    SetObjectName(instance.GetDevice(), buffer.buffer, name);
    auto result = Insert(std::move(buffer), desc.memory == Backend::MemoryClass::Sparse);
    if (!result.mapped.empty()) {
        result.mapped = result.mapped.first(desc.size_bytes);
    }
    return result;
}

Backend::BufferAllocation BackendDevice::ImportHostMemory(void* host, u64 size,
                                                          std::string_view name) {
    const auto device = instance.GetDevice();
    UniqueBuffer buffer{device, instance.GetAllocator()};
    const vk::ExternalMemoryBufferCreateInfo external{
        .handleTypes = vk::ExternalMemoryHandleTypeFlagBits::eHostAllocationEXT,
    };
    const vk::BufferCreateInfo buffer_ci = {
        .pNext = &external,
        .size = size,
        .usage = VideoCore::AllFlags | vk::BufferUsageFlagBits::eUniformTexelBuffer |
                 vk::BufferUsageFlagBits::eStorageTexelBuffer,
        .sharingMode = vk::SharingMode::eExclusive,
    };
    buffer.buffer = Check(device.createBuffer(buffer_ci));
    const auto [props_result, props] = device.getMemoryHostPointerPropertiesEXT(
        vk::ExternalMemoryHandleTypeFlagBits::eHostAllocationEXT, host);
    ASSERT_MSG(props_result == vk::Result::eSuccess && props.memoryTypeBits != 0,
               "Host memory cannot be imported: {}", vk::to_string(props_result));
    const auto requirements = device.getBufferMemoryRequirements(buffer.buffer);
    const u32 types = props.memoryTypeBits & requirements.memoryTypeBits;
    ASSERT_MSG(types != 0, "No memory type for imported host memory");
    const vk::ImportMemoryHostPointerInfoEXT import_info{
        .handleType = vk::ExternalMemoryHandleTypeFlagBits::eHostAllocationEXT,
        .pHostPointer = host,
    };
    const vk::MemoryAllocateFlagsInfo flags_info{
        .pNext = &import_info,
        .flags = vk::MemoryAllocateFlagBits::eDeviceAddress,
    };
    const auto [memory_result, memory] = device.allocateMemory({
        .pNext = &flags_info,
        .allocationSize = size,
        .memoryTypeIndex = static_cast<u32>(std::countr_zero(types)),
    });
    ASSERT_MSG(memory_result == vk::Result::eSuccess, "Importing host memory failed: {}",
               vk::to_string(memory_result));
    const auto bind_result = device.bindBufferMemory(buffer.buffer, memory, 0);
    ASSERT_MSG(bind_result == vk::Result::eSuccess, "Binding imported host memory failed: {}",
               vk::to_string(bind_result));
    buffer.bda_addr = device.getBufferAddress({.buffer = buffer.buffer});
    ASSERT_MSG(buffer.bda_addr != 0, "Failed to get buffer device address");
    SetObjectName(device, buffer.buffer, name);
    // bbport: wired once instead of for every command buffer that uses it (BB_RESIDENCY_SET).
    if (BbMetalResidency::AddMemory(memory)) {
        std::printf("GPU: %s (%llu MiB) kept resident (Metal residency set)\n",
                    std::string{name}.c_str(), static_cast<unsigned long long>(size >> 20));
    }
    // The memory lives for the process: it is not freed with the buffer.
    auto result = Insert(std::move(buffer), false);
    result.mapped = std::span<u8>{static_cast<u8*>(host), size};
    return result;
}

Backend::BufferAllocation BackendDevice::ImportGuestMemoryChunk(u64 native_memory, u64 size,
                                                                std::string_view name) {
    const auto device = instance.GetDevice();
    UniqueBuffer buffer{device, instance.GetAllocator()};
    const vk::ExternalMemoryBufferCreateInfo external{
        .handleTypes = BbGuestMemory::HandleType(),
    };
    // bbport BB_LAYER_MEMORY: what the GPU binds for the game's memory in place (no arena):
    // vertex, index and indirect data and device addresses too (the memory has them).
    const bool layer = BbGuestMemory::LayerMemory();
    vk::BufferUsageFlags usage =
        vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst |
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eUniformBuffer |
        vk::BufferUsageFlagBits::eUniformTexelBuffer | vk::BufferUsageFlagBits::eStorageTexelBuffer;
    if (layer) {
        usage |= vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eIndexBuffer |
                 vk::BufferUsageFlagBits::eIndirectBuffer |
                 vk::BufferUsageFlagBits::eShaderDeviceAddress;
    }
    const vk::BufferCreateInfo buffer_ci = {
        .pNext = &external,
        .size = size,
        .usage = usage,
        .sharingMode = vk::SharingMode::eExclusive,
    };
    buffer.buffer = Check(device.createBuffer(buffer_ci));
    const vk::DeviceMemory memory{reinterpret_cast<VkDeviceMemory>(native_memory)};
    const auto result = device.bindBufferMemory(buffer.buffer, memory, 0);
    ASSERT_MSG(result == vk::Result::eSuccess, "Binding a guest memory chunk failed: {}",
               vk::to_string(result));
    if (layer) {
        buffer.bda_addr = device.getBufferAddress({.buffer = buffer.buffer});
    }
    SetObjectName(device, buffer.buffer, name);
    return Insert(std::move(buffer), false);
}

void BackendDevice::DestroyBuffer(Backend::BufferHandle buffer) {
    if (!buffer) {
        return;
    }
    UniqueBuffer object{instance.GetDevice(), instance.GetAllocator()};
    {
        std::scoped_lock lock{objects_mutex};
        UniqueBuffer* entry = buffers.Get(buffer);
        if (!entry) {
            return;
        }
        object = std::move(*entry);
        buffers.Erase(buffer);
    }
    DeferRelease([object = std::move(object)] {});
}

void BackendDevice::FlushBuffer(Backend::BufferHandle buffer, u64 offset, u64 size) {
    std::scoped_lock lock{objects_mutex};
    if (const UniqueBuffer* entry = buffers.Get(buffer); entry && entry->allocation) {
        vmaFlushAllocation(entry->allocator, entry->allocation, offset, size);
    }
}

void BackendDevice::InvalidateBuffer(Backend::BufferHandle buffer, u64 offset, u64 size) {
    std::scoped_lock lock{objects_mutex};
    if (const UniqueBuffer* entry = buffers.Get(buffer); entry && entry->allocation) {
        vmaInvalidateAllocation(entry->allocator, entry->allocation, offset, size);
    }
}

u64 BackendDevice::NativeHandle(Backend::BufferHandle buffer) {
    std::scoped_lock lock{objects_mutex};
    const UniqueBuffer* entry = buffers.Get(buffer);
    return entry ? reinterpret_cast<u64>(static_cast<VkBuffer>(entry->buffer)) : 0;
}

vk::ImageCreateInfo BackendDevice::ImageCreateInfo(const Backend::ImageDesc& desc) const {
    vk::ImageCreateFlags flags{};
    if (desc.mutable_format) {
        flags |= vk::ImageCreateFlagBits::eMutableFormat | vk::ImageCreateFlagBits::eExtendedUsage;
    }
    if (desc.views_of_3d_slices) {
        flags |= vk::ImageCreateFlagBits::e2DArrayCompatible;
        if (instance.Is2dViewOf3dSupported()) {
            flags |= vk::ImageCreateFlagBits::e2DViewCompatibleEXT;
        }
    }
    if (desc.uncompressed_block_views && desc.mutable_format &&
        instance.IsBlockTexelViewSupported()) {
        flags |= vk::ImageCreateFlagBits::eBlockTexelViewCompatible;
    }
    return vk::ImageCreateInfo{
        .flags = flags,
        .imageType = ToVk(desc.type),
        .format = ToVk(desc.format),
        .extent{.width = desc.width, .height = desc.height, .depth = desc.depth},
        .mipLevels = desc.levels,
        .arrayLayers = desc.layers,
        .samples = static_cast<vk::SampleCountFlagBits>(desc.samples),
        .tiling = vk::ImageTiling::eOptimal,
        .usage = ToVk(desc.usage),
        .initialLayout = vk::ImageLayout::eUndefined,
    };
}

Backend::Format BackendDevice::SupportedFormat(Backend::Format format, Backend::ImageUse usage) {
    return FromVk(instance.GetSupportedFormat(ToVk(format), FormatFeatureFlags(ToVk(usage))));
}

u32 BackendDevice::SupportedSampleCounts(const Backend::ImageDesc& desc) {
    const vk::ImageCreateInfo ci = ImageCreateInfo(desc);
    const vk::PhysicalDeviceImageFormatInfo2 format_info{
        .format = ci.format,
        .type = ci.imageType,
        .tiling = ci.tiling,
        .usage = ci.usage,
        .flags = ci.flags,
    };
    const auto properties = instance.GetPhysicalDevice().getImageFormatProperties2(format_info);
    if (properties.result == vk::Result::eErrorFormatNotSupported) {
        // bbport: once per format and type. MoltenVK reports block-compressed formats with storage
        // usage as unsupported yet creates the images; one line per texture flooded the log.
        static std::mutex reported_mutex;
        static std::set<std::pair<vk::Format, vk::ImageType>> reported;
        std::scoped_lock lk{reported_mutex};
        if (reported.emplace(format_info.format, format_info.type).second) {
            LOG_ERROR(Render_Vulkan, "image format {} type {} is not supported (flags {}, usage {})",
                      vk::to_string(format_info.format), vk::to_string(format_info.type),
                      vk::to_string(format_info.flags), vk::to_string(format_info.usage));
        }
    }
    return properties.result == vk::Result::eSuccess
               ? static_cast<u32>(properties.value.imageFormatProperties.sampleCounts)
               : 1u;
}

Backend::ImageAllocation BackendDevice::CreateImage(const Backend::ImageDesc& desc,
                                                    std::string_view name) {
    auto record = std::make_unique<ImageRecord>();
    record->image = UniqueImage{instance.GetDevice(), instance.GetAllocator()};
    record->image.Create(ImageCreateInfo(desc));
    SetObjectName(instance.GetDevice(), record->image.image, name);
    Backend::ImageAllocation result{
        .native = reinterpret_cast<u64>(static_cast<VkImage>(record->image.image)),
        .size_bytes = record->image.size_bytes,
        .backend_state = &record->state,
    };
    std::scoped_lock lock{objects_mutex};
    result.handle = images.Insert(std::move(record));
    return result;
}

void BackendDevice::DestroyImage(Backend::ImageHandle image) {
    if (!image) {
        return;
    }
    std::unique_ptr<ImageRecord> object;
    {
        std::scoped_lock lock{objects_mutex};
        std::unique_ptr<ImageRecord>* entry = images.Get(image);
        if (!entry) {
            return;
        }
        object = std::move(*entry);
        images.Erase(image);
    }
    DeferRelease([object = std::move(object)] {});
}

u64 BackendDevice::NativeHandle(Backend::ImageHandle image) {
    std::scoped_lock lock{objects_mutex};
    const std::unique_ptr<ImageRecord>* entry = images.Get(image);
    return entry ? reinterpret_cast<u64>(static_cast<VkImage>((*entry)->image.image)) : 0;
}

Backend::ImageViewAllocation BackendDevice::CreateImageView(Backend::ImageHandle image,
                                                            const Backend::ImageViewDesc& desc,
                                                            std::string_view name) {
    vk::Image native_image{};
    {
        std::scoped_lock lock{objects_mutex};
        const std::unique_ptr<ImageRecord>* entry = images.Get(image);
        ASSERT_MSG(entry, "Creating a view of a destroyed image");
        native_image = (*entry)->image.image;
    }
    vk::ImageViewUsageCreateInfo usage_ci{.usage = ToVk(desc.usage)};
    vk::ImageViewMinLodCreateInfoEXT min_lod_ci{.minLod = desc.min_lod};
    if (desc.min_lod != 0.0f && instance.IsImageViewMinLodSupported()) {
        usage_ci.pNext = &min_lod_ci;
    }
    const vk::ImageViewCreateInfo image_view_ci = {
        .pNext = &usage_ci,
        .image = native_image,
        .viewType = ToVk(desc.type),
        .format = ToVk(desc.format),
        .components = ToVk(desc.mapping),
        .subresourceRange{
            .aspectMask = ToVk(desc.aspect),
            .baseMipLevel = desc.base_level,
            .levelCount = desc.levels,
            .baseArrayLayer = desc.base_layer,
            .layerCount = desc.layers,
        },
    };
    auto [view_result, view] = instance.GetDevice().createImageViewUnique(image_view_ci);
    ASSERT_MSG(view_result == vk::Result::eSuccess, "Failed to create image view: {}",
               vk::to_string(view_result));
    SetObjectName(instance.GetDevice(), *view, name);
    Backend::ImageViewAllocation result{
        .native = reinterpret_cast<u64>(static_cast<VkImageView>(*view)),
    };
    std::scoped_lock lock{objects_mutex};
    result.handle = views.Insert(std::move(view));
    return result;
}

void BackendDevice::DestroyImageView(Backend::ImageViewHandle view) {
    if (!view) {
        return;
    }
    vk::UniqueImageView object;
    {
        std::scoped_lock lock{objects_mutex};
        vk::UniqueImageView* entry = views.Get(view);
        if (!entry) {
            return;
        }
        object = std::move(*entry);
        views.Erase(view);
    }
    DeferRelease([object = std::move(object)] {});
}

u64 BackendDevice::NativeHandle(Backend::ImageViewHandle view) {
    std::scoped_lock lock{objects_mutex};
    const vk::UniqueImageView* entry = views.Get(view);
    return entry ? reinterpret_cast<u64>(static_cast<VkImageView>(**entry)) : 0;
}

Backend::ShaderAllocation BackendDevice::CreateShader(std::span<const u32> spirv,
                                                      std::string_view name) {
    auto [result, module] = instance.GetDevice().createShaderModuleUnique(
        {.codeSize = spirv.size_bytes(), .pCode = spirv.data()});
    if (result != vk::Result::eSuccess) {
        return {};
    }
    // The Metal path builds its pipeline states from the same SPIR-V.
    if (MetalShadow::Enabled()) {
        MetalShadow::RegisterModule(*module, spirv);
    }
    if (!name.empty()) {
        SetObjectName(instance.GetDevice(), *module, name);
    }
    Backend::ShaderAllocation allocation{
        .native = reinterpret_cast<u64>(static_cast<VkShaderModule>(*module)),
    };
    std::scoped_lock lock{objects_mutex};
    allocation.handle = shaders.Insert(ShaderRecord{.module = std::move(module)});
    return allocation;
}

void BackendDevice::DestroyShader(Backend::ShaderHandle shader) {
    if (!shader) {
        return;
    }
    vk::UniqueShaderModule object;
    {
        std::scoped_lock lock{objects_mutex};
        object = ReleaseShaderLocked(shader);
    }
    if (object) {
        DeferRelease([object = std::move(object)] {});
    }
}

vk::UniqueShaderModule BackendDevice::ReleaseShaderLocked(Backend::ShaderHandle shader) {
    ShaderRecord* entry = shaders.Get(shader);
    if (!entry || --entry->holders != 0) {
        return {};
    }
    vk::UniqueShaderModule object = std::move(entry->module);
    shaders.Erase(shader);
    return object;
}

u64 BackendDevice::NativeHandle(Backend::ShaderHandle shader) {
    std::scoped_lock lock{objects_mutex};
    const ShaderRecord* entry = shaders.Get(shader);
    return entry ? reinterpret_cast<u64>(static_cast<VkShaderModule>(*entry->module)) : 0;
}

Backend::SamplerHandle BackendDevice::CreateSampler(const Backend::SamplerDesc& desc) {
    const bool custom = desc.border_color == Backend::BorderColor::FloatCustom;
    const vk::SamplerCustomBorderColorCreateInfoEXT custom_color{
        .customBorderColor = vk::ClearColorValue{.float32 = desc.border_color_value},
        .format = vk::Format::eR32G32B32A32Sfloat,
    };
    const vk::SamplerCreateInfo sampler_ci = {
        .pNext = custom ? &custom_color : nullptr,
        .magFilter = ToVk(desc.mag_filter),
        .minFilter = ToVk(desc.min_filter),
        .mipmapMode = ToVk(desc.mipmap_mode),
        .addressModeU = ToVk(desc.address_u),
        .addressModeV = ToVk(desc.address_v),
        .addressModeW = ToVk(desc.address_w),
        .mipLodBias = desc.lod_bias,
        .anisotropyEnable = desc.max_anisotropy > 1.0f,
        .maxAnisotropy = desc.max_anisotropy,
        .compareEnable = desc.compare_enable,
        .compareOp = ToVk(desc.compare_op),
        .minLod = desc.min_lod,
        .maxLod = desc.max_lod,
        .borderColor = ToVk(desc.border_color),
        .unnormalizedCoordinates = false,
    };
    auto [result, sampler] = instance.GetDevice().createSamplerUnique(sampler_ci);
    ASSERT_MSG(result == vk::Result::eSuccess, "Failed to create sampler: {}",
               vk::to_string(result));
    std::scoped_lock lock{objects_mutex};
    return samplers.Insert(std::move(sampler));
}

void BackendDevice::DestroySampler(Backend::SamplerHandle sampler) {
    if (!sampler) {
        return;
    }
    vk::UniqueSampler object;
    {
        std::scoped_lock lock{objects_mutex};
        vk::UniqueSampler* entry = samplers.Get(sampler);
        if (!entry) {
            return;
        }
        object = std::move(*entry);
        samplers.Erase(sampler);
    }
    DeferRelease([object = std::move(object)] {});
}

u64 BackendDevice::NativeHandle(Backend::SamplerHandle sampler) {
    std::scoped_lock lock{objects_mutex};
    const vk::UniqueSampler* entry = samplers.Get(sampler);
    return entry ? reinterpret_cast<u64>(static_cast<VkSampler>(**entry)) : 0;
}

} // namespace Vulkan
