// Vulkan adapter for the N4 resource descriptions.
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <utility>
#include <vector>

#include "video_core/buffer_cache/buffer.h"
#include "video_core/renderer_backend/resource.h"
#include "video_core/renderer_vulkan/vk_common.h"

VK_DEFINE_HANDLE(VmaAllocation)
VK_DEFINE_HANDLE(VmaAllocator)

struct VmaAllocationInfo;

namespace Vulkan {

/// A VkImage and its VMA allocation: the texture cache's images (owned by the render adapter's
/// Vulkan backend) and the backend's own (upscalers, scene proxies).
struct UniqueImage {
    explicit UniqueImage() = default;
    explicit UniqueImage(vk::Device device, VmaAllocator allocator)
        : device{device}, allocator{allocator} {}
    ~UniqueImage();

    UniqueImage(const UniqueImage&) = delete;
    UniqueImage& operator=(const UniqueImage&) = delete;

    UniqueImage(UniqueImage&& other)
        : allocator{std::exchange(other.allocator, VK_NULL_HANDLE)},
          allocation{std::exchange(other.allocation, VK_NULL_HANDLE)},
          image{std::exchange(other.image, VK_NULL_HANDLE)}, image_ci{std::move(other.image_ci)},
          size_bytes{std::exchange(other.size_bytes, 0)} {}
    UniqueImage& operator=(UniqueImage&& other) {
        if (this == &other) return *this;
        Destroy();
        device = other.device;
        image = std::exchange(other.image, VK_NULL_HANDLE);
        allocator = std::exchange(other.allocator, VK_NULL_HANDLE);
        allocation = std::exchange(other.allocation, VK_NULL_HANDLE);
        image_ci = std::move(other.image_ci);
        size_bytes = std::exchange(other.size_bytes, 0);
        return *this;
    }

    void Create(const vk::ImageCreateInfo& image_ci);

    void Destroy();

    operator vk::Image() const {
        return image;
    }

    operator bool() const {
        return image;
    }

public:
    vk::Device device{};
    VmaAllocator allocator{};
    VmaAllocation allocation{};
    vk::Image image{};
    vk::ImageCreateInfo image_ci{};
    vk::DeviceSize size_bytes{};
};

/// The Vulkan backend's barrier state of one texture-cache image allocation: its layout and last
/// access, for the whole image or per subresource after a partial transition.
struct ImageState {
    struct State {
        vk::PipelineStageFlags2 pl_stage = vk::PipelineStageFlagBits2::eAllCommands;
        vk::AccessFlags2 access_mask = vk::AccessFlagBits2::eNone;
        vk::ImageLayout layout = vk::ImageLayout::eUndefined;
    };
    State state;
    std::vector<State> subresource_states;
};

/// A texture-cache image allocation as the backend keeps it: at a fixed address, so the image's
/// backing reaches its state without a lookup.
struct ImageRecord {
    UniqueImage image;
    ImageState state;
};

/// The format features an image with these usages needs (format substitution).
vk::FormatFeatureFlags2 FormatFeatureFlags(vk::ImageUsageFlags usage_flags);

/// A VkBuffer and its VMA allocation, owned by the render adapter's Vulkan backend.
struct UniqueBuffer {
    explicit UniqueBuffer(vk::Device device, VmaAllocator allocator);
    ~UniqueBuffer();

    UniqueBuffer(const UniqueBuffer&) = delete;
    UniqueBuffer& operator=(const UniqueBuffer&) = delete;

    UniqueBuffer(UniqueBuffer&& other) noexcept
        : device{other.device}, buffer{std::exchange(other.buffer, VK_NULL_HANDLE)},
          bda_addr{std::exchange(other.bda_addr, 0)},
          allocator{std::exchange(other.allocator, VK_NULL_HANDLE)},
          allocation{std::exchange(other.allocation, VK_NULL_HANDLE)} {}

    UniqueBuffer& operator=(UniqueBuffer&& other) noexcept {
        if (this != &other) {
            Destroy();
            device = other.device;
            buffer = std::exchange(other.buffer, VK_NULL_HANDLE);
            bda_addr = std::exchange(other.bda_addr, 0);
            allocator = std::exchange(other.allocator, VK_NULL_HANDLE);
            allocation = std::exchange(other.allocation, VK_NULL_HANDLE);
        }
        return *this;
    }

    void Create(vk::BufferCreateInfo& buffer_ci, VideoCore::MemoryType mem_type,
                VmaAllocationInfo* out_alloc_info);

    void Destroy();

    vk::Device device;
    vk::Buffer buffer{};
    vk::DeviceAddress bda_addr{};
    VmaAllocator allocator;
    VmaAllocation allocation{};
};


class Instance;

VideoCore::Backend::Capabilities
QueryResourceCapabilities(const Instance &instance);
void CreateBufferFromDesc(const Instance &instance,
                          UniqueBuffer &buffer,
                          const VideoCore::Backend::BufferDesc &desc,
                          VmaAllocationInfo *out_alloc_info);

} // namespace Vulkan
