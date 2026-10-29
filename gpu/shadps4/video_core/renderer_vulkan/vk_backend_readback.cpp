// The render adapter's Vulkan backend: copies out of buffers on the readback queue
// (Device::ReadBackAfter).
// SPDX-License-Identifier: GPL-2.0-or-later

#include <bit>
#include <boost/container/small_vector.hpp>

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_backend_device.h"
#include "video_core/renderer_vulkan/vk_backend_readback.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_platform.h"
#include "video_core/renderer_vulkan/vk_state.h"
#if BB_METAL_BACKEND
#include "video_core/renderer_metal/mtl_vk_layer.h"
#endif

#include <vk_mem_alloc.h>

namespace Vulkan {

namespace Backend = VideoCore::Backend;

bool BackendDevice::HasReadbackQueue() const noexcept {
#if BB_METAL_BACKEND
    if (MetalLayer::FrameEnabled()) {
        // This Vulkan queue and its VMA staging buffer are not part of the Metal frame.
        return false;
    }
#endif
    return bool(instance.GetReadbackQueue());
}

bool BackendDevice::ReadBackAfter(u64 buffer, std::span<const Backend::BufferCopy> copies, u64 total,
                                  u64 tick, const std::function<void(const u8*)>& use) {
#if BB_METAL_BACKEND
    if (MetalLayer::FrameEnabled()) {
        return false; // The caller records its ordinary Metal-frame readback instead.
    }
#endif
    std::scoped_lock lock{readback_mutex};
    if (!readback) {
        readback = std::make_unique<FastReadback>(instance);
    }
    if (!readback->Available()) {
        return false;
    }
    const u8* data = readback->Copy(vk::Buffer{reinterpret_cast<VkBuffer>(buffer)}, copies, total,
                                    scheduler.GetWorkSemaphore()->Handle(), tick);
    if (!data) {
        return false;
    }
    use(data);
    return true;
}

BackendDevice::~BackendDevice() = default;

} // namespace Vulkan
