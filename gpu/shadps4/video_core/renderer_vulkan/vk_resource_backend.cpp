// Vulkan allocation behind the N4 resource-description boundary.
// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/renderer_vulkan/vk_resource_backend.h"
#include "bbport_guest_memory.h"
#include "bbport_metal_residency.h"
#include "bbport_toggles.h"
#include "video_core/buffer_cache/buffer.h"
#include "video_core/renderer_metal/mtl_resources.h"
#if BB_METAL_BACKEND
#include "video_core/renderer_metal/mtl_vk_layer.h"
#endif
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_platform.h"

#include <array>
#include <cstdio>
#include <vk_mem_alloc.h>
#ifdef __APPLE__
#include <vulkan/vulkan_metal.h>
#endif

namespace Vulkan {

using VideoCore::MemoryType;

[[nodiscard]] static VkMemoryPropertyFlags
MemoryUsagePreferredVmaFlags(MemoryType type) {
  return type != MemoryType::DeviceLocal ? VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                         : VkMemoryPropertyFlagBits{};
}

[[nodiscard]] static VmaAllocationCreateFlags
MemoryUsageVmaFlags(MemoryType type) {
  switch (type) {
  case MemoryType::HostUncached:
  case MemoryType::Stream:
    return VMA_ALLOCATION_CREATE_MAPPED_BIT |
           VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
  case MemoryType::HostCached:
    return VMA_ALLOCATION_CREATE_MAPPED_BIT |
           VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
  case MemoryType::DeviceLocal:
  default:
    return {};
  }
}

[[nodiscard]] static VmaMemoryUsage MemoryUsageVma(MemoryType type) {
  switch (type) {
  case MemoryType::DeviceLocal:
  case MemoryType::Stream:
    return VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
  case MemoryType::HostUncached:
  case MemoryType::HostCached:
    return VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
  default:
    return VMA_MEMORY_USAGE_UNKNOWN;
  }
}

UniqueBuffer::UniqueBuffer(vk::Device device_, VmaAllocator allocator_)
    : device{device_}, allocator{allocator_} {}

UniqueBuffer::~UniqueBuffer() { Destroy(); }

void UniqueBuffer::Destroy() {
  if (metal_only) {
#if BB_METAL_BACKEND
    BbMetalResidency::RemoveMetalBuffer(static_cast<VkBuffer>(buffer));
    MetalLayer::BufferGone(static_cast<VkBuffer>(buffer));
#endif
    MetalResources::Release(metal_buffer);
    metal_only = false;
  } else if (allocation) {
    vmaDestroyBuffer(allocator, buffer, allocation);
  } else if (buffer) {
    device.destroyBuffer(buffer);
  }
  if (metal_memory) {
    BbMetalResidency::RemoveMemory(metal_memory);
    device.freeMemory(metal_memory);
    MetalResources::Release(metal_buffer);
  }
  buffer = VK_NULL_HANDLE;
  allocation = VK_NULL_HANDLE;
  metal_memory = VK_NULL_HANDLE;
  metal_buffer = nullptr;
  metal_contents = nullptr;
  metal_size = 0;
  bda_addr = 0;
}

void UniqueBuffer::CreateInMetal(vk::BufferCreateInfo &buffer_ci,
                                 MemoryType mem_type) {
#ifdef __APPLE__
  const bool host = mem_type != MemoryType::DeviceLocal;
  const auto storage =
      mem_type == MemoryType::DeviceLocal ? MetalResources::Storage::Private
      : mem_type == MemoryType::HostCached
          ? MetalResources::Storage::Shared
          : MetalResources::Storage::SharedWriteCombined;
#if BB_METAL_BACKEND
  if (MetalLayer::FrameEnabled()) {
    // The Metal buffer alone (M4): MoltenVK makes no buffer and imports no memory.
    const auto metal = MetalResources::NewBuffer((buffer_ci.size + 255) & ~vk::DeviceSize{255},
                                                 storage);
    metal_buffer = metal.object;
    metal_contents = metal.contents;
    metal_size = buffer_ci.size;
    metal_only = true;
    buffer = vk::Buffer{static_cast<VkBuffer>(MetalLayer::MetalOnlyBuffer(metal_buffer))};
    BbMetalResidency::AddMetalBuffer(static_cast<VkBuffer>(buffer), metal_buffer);
    if (buffer_ci.usage & vk::BufferUsageFlagBits::eShaderDeviceAddress) {
      bda_addr = MetalLayer::MetalOnlyBufferAddress(static_cast<VkBuffer>(buffer));
    }
    return;
  }
#endif
  buffer = Vulkan::Check(device.createBuffer(buffer_ci));
  // The Metal buffer is the memory: as large as the buffer requires.
  const auto requirements = device.getBufferMemoryRequirements(buffer);
  const auto metal = MetalResources::NewBuffer(requirements.size, storage);
  metal_buffer = metal.object;
  metal_contents = metal.contents;
  metal_size = buffer_ci.size;

  // A memory type the buffer takes: host-visible and coherent for shared storage,
  // device-local only for private.
  const VkPhysicalDeviceMemoryProperties *props = nullptr;
  vmaGetMemoryProperties(allocator, &props);
  const VkMemoryPropertyFlags wanted =
      host ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
           : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  u32 type = ~0u;
  for (u32 i = 0; i < props->memoryTypeCount && type == ~0u; ++i) {
    const auto flags = props->memoryTypes[i].propertyFlags;
    if ((requirements.memoryTypeBits & (1u << i)) &&
        (flags & wanted) == wanted &&
        (host || !(flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))) {
      type = i;
    }
  }
  ASSERT_MSG(type != ~0u, "No memory type for a Metal buffer");

  const VkImportMetalBufferInfoEXT import_info{
      .sType = VK_STRUCTURE_TYPE_IMPORT_METAL_BUFFER_INFO_EXT,
      .mtlBuffer = static_cast<MTLBuffer_id>(metal_buffer),
  };
  const vk::MemoryAllocateFlagsInfo flags_info{
      .pNext = &import_info,
      .flags = buffer_ci.usage & vk::BufferUsageFlagBits::eShaderDeviceAddress
                   ? vk::MemoryAllocateFlagBits::eDeviceAddress
                   : vk::MemoryAllocateFlags{},
  };
  metal_memory = Vulkan::Check(device.allocateMemory({
      .pNext = &flags_info,
      .allocationSize = requirements.size,
      .memoryTypeIndex = type,
  }));
  Vulkan::Check(device.bindBufferMemory(buffer, metal_memory, 0));
  BbMetalResidency::AddMemory(metal_memory);
  if (buffer_ci.usage & vk::BufferUsageFlagBits::eShaderDeviceAddress) {
    bda_addr = device.getBufferAddress({.buffer = buffer});
    ASSERT_MSG(bda_addr != 0, "Failed to get buffer device address");
  }
#else
  UNREACHABLE_MSG("Metal buffers need macOS");
#endif
}

void UniqueBuffer::Create(vk::BufferCreateInfo &buffer_ci, MemoryType mem_type,
                          VmaAllocationInfo *out_alloc_info) {
  const bool with_bda =
      bool(buffer_ci.usage & vk::BufferUsageFlagBits::eShaderDeviceAddress);
  if (mem_type != MemoryType::Sparse) {
    const VmaAllocationCreateFlags bda_flag =
        with_bda ? VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT : 0;
    const VmaAllocationCreateInfo alloc_ci = {
        .flags = VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT | bda_flag |
                 MemoryUsageVmaFlags(mem_type),
        .usage = MemoryUsageVma(mem_type),
        .requiredFlags = 0,
        .preferredFlags = MemoryUsagePreferredVmaFlags(mem_type),
        .pool = VK_NULL_HANDLE,
        .pUserData = nullptr,
    };

    const VkBufferCreateInfo buffer_ci_unsafe =
        static_cast<VkBufferCreateInfo>(buffer_ci);
    VkBuffer unsafe_buffer{};
    VkResult result =
        vmaCreateBuffer(allocator, &buffer_ci_unsafe, &alloc_ci, &unsafe_buffer,
                        &allocation, out_alloc_info);
    ASSERT_MSG(result == VK_SUCCESS, "Failed allocating buffer with error {}",
               vk::to_string(vk::Result{result}));
    buffer = vk::Buffer{unsafe_buffer};
  } else {
    buffer_ci.flags |= vk::BufferCreateFlagBits::eSparseBinding |
                       vk::BufferCreateFlagBits::eSparseResidency;
    // Bound to exported guest memory chunks (BB_GUEST_IN_PLACE) as well as VRAM
    // blocks.
    const vk::ExternalMemoryBufferCreateInfo external{
        .handleTypes = BbGuestMemory::HandleType(),
    };
    if (VideoCore::GuestInPlace()) {
      buffer_ci.pNext = &external;
      if (const auto [result, created] = device.createBuffer(buffer_ci);
          result == vk::Result::eSuccess) {
        buffer = created;
        // bbport: VRAM blocks are bound into it too: the external memory type
        // must not keep device-local memory out (a driver may restrict it).
        const auto reqs = device.getBufferMemoryRequirements(created);
        const VkPhysicalDeviceMemoryProperties *props = nullptr;
        vmaGetMemoryProperties(allocator, &props);
        bool vram = false;
        for (u32 i = 0; i < props->memoryTypeCount; ++i) {
          vram |= (reqs.memoryTypeBits & (1u << i)) &&
                  (props->memoryTypes[i].propertyFlags &
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        }
        if (!vram) {
          std::printf("Guest memory: the arena for guest memory cannot take "
                      "VRAM; the GPU "
                      "uses copies in VRAM as before\n");
          device.destroyBuffer(created);
          buffer = vk::Buffer{};
          VideoCore::DisableGuestInPlace();
        }
      } else {
        std::printf(
            "Guest memory: the arena cannot take exported memory (%s); the GPU "
            "uses copies in VRAM as before\n",
            vk::to_string(result).c_str());
        VideoCore::DisableGuestInPlace();
      }
      buffer_ci.pNext = nullptr;
    }
    if (!buffer) {
      buffer = Vulkan::Check(device.createBuffer(buffer_ci));
    }
  }

  if (with_bda) {
    const vk::BufferDeviceAddressInfo bda_info = {
        .buffer = buffer,
    };
    auto bda_result = device.getBufferAddress(bda_info);
    ASSERT_MSG(bda_result != 0, "Failed to get buffer device address");
    bda_addr = bda_result;
  }
}

vk::FormatFeatureFlags2 FormatFeatureFlags(const vk::ImageUsageFlags usage_flags) {
    vk::FormatFeatureFlags2 feature_flags{};
    if (usage_flags & vk::ImageUsageFlagBits::eTransferSrc) {
        feature_flags |= vk::FormatFeatureFlagBits2::eTransferSrc;
    }
    if (usage_flags & vk::ImageUsageFlagBits::eTransferDst) {
        feature_flags |= vk::FormatFeatureFlagBits2::eTransferDst;
    }
    if (usage_flags & vk::ImageUsageFlagBits::eSampled) {
        feature_flags |= vk::FormatFeatureFlagBits2::eSampledImage;
    }
    if (usage_flags & vk::ImageUsageFlagBits::eColorAttachment) {
        feature_flags |= vk::FormatFeatureFlagBits2::eColorAttachment;
    }
    if (usage_flags & vk::ImageUsageFlagBits::eDepthStencilAttachment) {
        feature_flags |= vk::FormatFeatureFlagBits2::eDepthStencilAttachment;
    }
    // Note: StorageImage is intentionally ignored for now since it is always set, and can mess up
    // compatibility checks.
    return feature_flags;
}

UniqueImage::~UniqueImage() {
    // Destroy also gives up a Metal-made image's texture (CreateInMetal); destroying only the
    // VkImage left it alive.
    Destroy();
}

void UniqueImage::Destroy() {
    if (image) {
        BbStats::vk_image_bytes.fetch_sub(size_bytes, std::memory_order_relaxed);
        BbMetalResidency::RemoveImage(image);
        if (metal_only) {
#if BB_METAL_BACKEND
            MetalLayer::ImageGone(static_cast<VkImage>(image));
#endif
            MetalResources::Release(metal_texture);
            metal_texture = nullptr;
            metal_only = false;
        } else if (metal_texture) {
            device.destroyImage(image);
            MetalResources::Release(metal_texture);
            metal_texture = nullptr;
        } else {
            vmaDestroyImage(allocator, image, allocation);
        }
        image = vk::Image{};
        allocation = {};
    }
}

bool UniqueImage::CreateInMetal(const vk::ImageCreateInfo& image_ci) {
#ifdef __APPLE__
    if (!MetalResources::Enabled()) {
        return false;
    }
#if BB_METAL_BACKEND
    // With the Metal frame the image is the texture alone (M4): views are made in Metal, so
    // block-compressed images that might be viewed uncompressed qualify (the game was measured
    // making no such view; the device logs one if it does).
    const bool frame = MetalLayer::FrameEnabled();
#else
    const bool frame = false;
#endif
    vk::ImageCreateFlags unsupported =
        vk::ImageCreateFlagBits::eSparseBinding | vk::ImageCreateFlagBits::eSparseResidency |
        vk::ImageCreateFlagBits::eSparseAliased | vk::ImageCreateFlagBits::eCubeCompatible;
    if (!frame) {
        // MoltenVK makes the views then: it needs these images made by itself.
        unsupported |= vk::ImageCreateFlagBits::eBlockTexelViewCompatible |
                       vk::ImageCreateFlagBits::e2DArrayCompatible |
                       vk::ImageCreateFlagBits::e2DViewCompatibleEXT;
    }
    // With the Metal frame 3D images qualify too: the texture cache makes every 3D image
    // slice-viewable, but the game was measured viewing them only as 3D (Metal has no 2D view of
    // a 3D texture; the frame logs one if it is asked for).
    if ((image_ci.flags & unsupported) || image_ci.tiling != vk::ImageTiling::eOptimal) {
        return false;
    }
    using Usage = vk::ImageUsageFlagBits;
    const auto usage = image_ci.usage;
    const VkFormat format = static_cast<VkFormat>(image_ci.format);
    // Block-compressed formats are never render targets.
    const bool compressed =
        format >= VK_FORMAT_BC1_RGB_UNORM_BLOCK && format <= VK_FORMAT_ASTC_12x12_SRGB_BLOCK;
    const bool attachment =
        bool(usage & (Usage::eColorAttachment | Usage::eDepthStencilAttachment));
    const MetalResources::TextureDesc desc{
        .vk_format = u32(format),
        .type = image_ci.imageType == vk::ImageType::e1D   ? 1u
                : image_ci.imageType == vk::ImageType::e3D ? 3u
                                                           : 2u,
        .width = image_ci.extent.width,
        .height = image_ci.extent.height,
        .depth = image_ci.extent.depth,
        .levels = image_ci.mipLevels,
        .layers = image_ci.arrayLayers,
        .samples = u32(image_ci.samples),
        .shader_read = bool(usage & (Usage::eSampled | Usage::eInputAttachment | Usage::eStorage |
                                     Usage::eTransferSrc | Usage::eTransferDst)),
        .shader_write = !compressed && bool(usage & Usage::eStorage), // Metal: no compressed writes
        // MoltenVK clears and some copies through render passes.
        .render_target = attachment || (!compressed && bool(usage & Usage::eTransferDst)),
        .format_views = bool(image_ci.flags & vk::ImageCreateFlagBits::eMutableFormat) ||
                        image_ci.arrayLayers > 1,
    };
    const auto texture = MetalResources::NewTexture(desc);
    if (!texture.object) {
        return false;
    }
#if BB_METAL_BACKEND
    if (frame) {
        this->image_ci = image_ci;
        image = vk::Image{static_cast<VkImage>(MetalLayer::MetalOnlyImage(texture.object))};
        metal_texture = texture.object;
        metal_only = true;
        size_bytes = texture.size_bytes;
        BbMetalResidency::AddImageTexture(image, texture.object);
        BbStats::vk_image_bytes.fetch_add(size_bytes, std::memory_order_relaxed);
        return true;
    }
#endif
    const VkImportMetalTextureInfoEXT import_info{
        .sType = VK_STRUCTURE_TYPE_IMPORT_METAL_TEXTURE_INFO_EXT,
        .pNext = image_ci.pNext,
        .plane = VK_IMAGE_ASPECT_PLANE_0_BIT,
        .mtlTexture = static_cast<MTLTexture_id>(texture.object),
    };
    vk::ImageCreateInfo create_info = image_ci;
    create_info.pNext = &import_info;
    const auto [result, created] = device.createImage(create_info);
    if (result != vk::Result::eSuccess) {
        MetalResources::Release(texture.object);
        return false;
    }
    // The texture is the image's memory: MoltenVK binds none to an imported texture.
    this->image_ci = image_ci;
    image = created;
    metal_texture = texture.object;
    size_bytes = texture.size_bytes;
    BbMetalResidency::AddImage(image);
    BbStats::vk_image_bytes.fetch_add(size_bytes, std::memory_order_relaxed);
    return true;
#else
    return false;
#endif
}

void UniqueImage::Create(const vk::ImageCreateInfo& image_ci) {
    this->image_ci = image_ci;
    ASSERT(!image);
    // bbport: images of BB_VMA_DEDICATED_MB (4) or more get memory of their own, given back to
    // the driver when they are freed. In shared VMA blocks the texture collector's evictions left
    // holes (~0.9 GB on an RX 7800 XT) that a block only returns once wholly empty.
    static const vk::DeviceSize dedicated_from = [] {
        const char* env = std::getenv("BB_VMA_DEDICATED_MB");
        return vk::DeviceSize(env ? std::strtoull(env, nullptr, 10) : 4) << 20;
    }();
    VmaAllocationCreateFlags dedicated = 0;
    if (device && dedicated_from != 0) {
        const vk::DeviceImageMemoryRequirements query{.pCreateInfo = &image_ci};
        if (device.getImageMemoryRequirements(query).memoryRequirements.size >= dedicated_from) {
            dedicated = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        }
    }
    const VmaAllocationCreateInfo alloc_ci = {
        .flags = VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT | dedicated,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .requiredFlags = 0,
        .preferredFlags = 0,
        .pool = VK_NULL_HANDLE,
        .pUserData = nullptr,
    };

    const VkImageCreateInfo image_ci_unsafe = static_cast<VkImageCreateInfo>(image_ci);
    VkImage unsafe_image{};
    VmaAllocationInfo alloc_info{};
    VkResult result = vmaCreateImage(allocator, &image_ci_unsafe, &alloc_ci, &unsafe_image,
                                     &allocation, &alloc_info);
    ASSERT_MSG(result == VK_SUCCESS, "Failed allocating image with error {}",
               vk::to_string(vk::Result{result}));
    image = vk::Image{unsafe_image};
    size_bytes = alloc_info.size;
    BbMetalResidency::AddImage(image);
    BbStats::vk_image_bytes.fetch_add(size_bytes, std::memory_order_relaxed);
}

namespace Backend = VideoCore::Backend;

Backend::Capabilities QueryResourceCapabilities(const Instance &instance) {
  return {
      .sparse_buffers = instance.IsSparseBufferSupported(),
      .imported_host_memory = instance.IsHostMemoryImportSupported(),
      .exported_guest_memory = instance.IsGuestMemoryExportSupported(),
      .buffer_device_address = instance.IsBufferDeviceAddressSupported(),
      .custom_border_color = instance.IsCustomBorderColorSupported(),
      .max_sampler_anisotropy = instance.IsAnisotropicFilteringSupported()
                                    ? instance.MaxSamplerAnisotropy()
                                    : 1.0f,
      .max_sampler_lod_bias = instance.MaxSamplerLodBias(),
      .attachment_feedback_loop = instance.IsAttachmentFeedbackLoopLayoutSupported(),
      .dynamic_vertex_input = instance.IsVertexInputDynamicState(),
      .color_sample_counts = u32(instance.GetColorSampleCounts()),
      .depth_sample_counts = u32(instance.GetDepthSampleCounts()),
  };
}

static vk::BufferUsageFlags ToVulkanBufferUsage(Backend::BufferUse uses) {
  vk::BufferUsageFlags flags{};
  if (Backend::Has(uses, Backend::BufferUse::TransferSource))
    flags |= vk::BufferUsageFlagBits::eTransferSrc;
  if (Backend::Has(uses, Backend::BufferUse::TransferDestination))
    flags |= vk::BufferUsageFlagBits::eTransferDst;
  if (Backend::Has(uses, Backend::BufferUse::Uniform))
    flags |= vk::BufferUsageFlagBits::eUniformBuffer;
  if (Backend::Has(uses, Backend::BufferUse::Storage))
    flags |= vk::BufferUsageFlagBits::eStorageBuffer;
  if (Backend::Has(uses, Backend::BufferUse::Vertex))
    flags |= vk::BufferUsageFlagBits::eVertexBuffer;
  if (Backend::Has(uses, Backend::BufferUse::Index))
    flags |= vk::BufferUsageFlagBits::eIndexBuffer;
  if (Backend::Has(uses, Backend::BufferUse::Indirect))
    flags |= vk::BufferUsageFlagBits::eIndirectBuffer;
  if (Backend::Has(uses, Backend::BufferUse::DeviceAddress))
    flags |= vk::BufferUsageFlagBits::eShaderDeviceAddress;
  return flags;
}

void CreateBufferFromDesc(const Instance &instance,
                          UniqueBuffer &buffer,
                          const Backend::BufferDesc &desc,
                          VmaAllocationInfo *out_alloc_info) {
  const auto capabilities = QueryResourceCapabilities(instance);
  ASSERT_MSG(desc.memory != Backend::MemoryClass::Sparse ||
                 capabilities.sparse_buffers,
             "Sparse buffers are not supported by this Vulkan device");
  ASSERT_MSG(!Backend::Has(desc.uses, Backend::BufferUse::DeviceAddress) ||
                 capabilities.buffer_device_address,
             "Buffer device addresses are not supported by this Vulkan device");
  const std::array<u32, 2> families = {instance.GetGraphicsQueueFamilyIndex(),
                                       instance.GetReadbackQueueFamilyIndex()};
  vk::BufferCreateInfo create_info{
      .size = desc.size_bytes,
      .usage = ToVulkanBufferUsage(desc.uses),
      .sharingMode = vk::SharingMode::eExclusive,
  };
  if (desc.memory == Backend::MemoryClass::Sparse && desc.concurrent_readback &&
      instance.GetReadbackQueue()) {
    create_info.sharingMode = vk::SharingMode::eConcurrent;
    create_info.queueFamilyIndexCount = static_cast<u32>(families.size());
    create_info.pQueueFamilyIndices = families.data();
  }
  if (MetalResources::Enabled() && desc.memory != Backend::MemoryClass::Sparse &&
      !out_alloc_info) {
    buffer.CreateInMetal(create_info, desc.memory);
    return;
  }
  buffer.Create(create_info, desc.memory, out_alloc_info);
}

ImageState::State ImageAccessState(Backend::ImageAccess access, bool is_depth, bool has_stencil,
                                   bool feedback_loop) {
  using Backend::ImageAccess;
  using Stage = vk::PipelineStageFlagBits2;
  using Access = vk::AccessFlagBits2;
  using Layout = vk::ImageLayout;
  constexpr auto color_target =
      Access::eColorAttachmentWrite | Access::eColorAttachmentRead;
  constexpr auto depth_tests =
      Stage::eEarlyFragmentTests | Stage::eLateFragmentTests;
  constexpr auto depth_target = Access::eDepthStencilAttachmentWrite |
                                Access::eDepthStencilAttachmentRead;
  const auto state = [](vk::PipelineStageFlags2 stage, vk::AccessFlags2 bits,
                        Layout layout) {
    return ImageState::State{
        .pl_stage = stage, .access_mask = bits, .layout = layout};
  };
  switch (access) {
  case ImageAccess::Sampled:
    return state(Stage::eAllCommands, Access::eShaderRead,
                 is_depth ? Layout::eDepthStencilReadOnlyOptimal
                          : Layout::eShaderReadOnlyOptimal);
  case ImageAccess::Storage:
    return state(Stage::eAllCommands, Access::eShaderRead | Access::eShaderWrite,
                 Layout::eGeneral);
  case ImageAccess::SampledWhileTarget:
    return feedback_loop
               ? state(Stage::eAllGraphics, Access::eShaderRead,
                       Layout::eAttachmentFeedbackLoopOptimalEXT)
               : state(Stage::eAllCommands,
                       Access::eShaderRead | Access::eShaderWrite, Layout::eGeneral);
  case ImageAccess::ColorAttachment:
    return state(Stage::eColorAttachmentOutput, color_target,
                 Layout::eColorAttachmentOptimal);
  case ImageAccess::ColorAttachmentFeedback:
    return state(Stage::eColorAttachmentOutput, color_target,
                 feedback_loop ? Layout::eAttachmentFeedbackLoopOptimalEXT
                               : Layout::eGeneral);
  case ImageAccess::DepthAttachment:
    return state(depth_tests, depth_target,
                 has_stencil ? Layout::eDepthStencilAttachmentOptimal
                             : Layout::eDepthAttachmentOptimal);
  case ImageAccess::DepthReadOnlyStencilWrite:
    return state(depth_tests, depth_target,
                 Layout::eDepthReadOnlyStencilAttachmentOptimal);
  case ImageAccess::DepthReadOnly:
    return state(depth_tests, depth_target,
                 has_stencil ? Layout::eDepthStencilReadOnlyOptimal
                             : Layout::eDepthReadOnlyOptimal);
  case ImageAccess::ComputeRead:
    return state(Stage::eComputeShader, Access::eShaderRead, Layout::eGeneral);
  case ImageAccess::ComputeReadWrite:
    return state(Stage::eComputeShader, Access::eShaderRead | Access::eShaderWrite,
                 Layout::eGeneral);
  case ImageAccess::TransferSource:
    return state(Stage::eTransfer, Access::eTransferRead,
                 Layout::eTransferSrcOptimal);
  case ImageAccess::TransferDestination:
    return state(Stage::eTransfer, Access::eTransferWrite,
                 Layout::eTransferDstOptimal);
  }
  UNREACHABLE();
}

} // namespace Vulkan
