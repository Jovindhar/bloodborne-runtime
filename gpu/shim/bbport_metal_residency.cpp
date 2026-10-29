// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_metal_residency.h"

#ifdef __APPLE__
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <objc/message.h>
#include <objc/runtime.h>
#include <vulkan/vulkan_metal.h>
#include "video_core/renderer_vulkan/vk_instance.h"
#if BB_METAL_BACKEND
#include "video_core/renderer_metal/mtl_vk_layer.h"
#endif
#endif

namespace BbMetalResidency {

#ifdef __APPLE__
namespace {

id Send(id object, const char* selector) {
    return reinterpret_cast<id (*)(id, SEL)>(objc_msgSend)(object, sel_registerName(selector));
}

void SendObject(id object, const char* selector, id argument) {
    reinterpret_cast<void (*)(id, SEL, id)>(objc_msgSend)(object, sel_registerName(selector),
                                                          argument);
}

struct State {
    std::mutex mutex;
    VkDevice device = VK_NULL_HANDLE;
    PFN_vkExportMetalObjectsEXT export_objects = nullptr;
    id set = nullptr;
    /// Allocations in the set: memory (MTLBuffer and its count) and images (MTLTexture).
    std::unordered_map<VkDeviceMemory, std::pair<id, u32>> memories;
    std::unordered_map<VkBuffer, id> metal_buffers; ///< buffers only Metal has (M4)
    std::unordered_map<VkImage, id> images;
    /// Removed objects, retained until the GPU completes the tick open at their removal.
    std::deque<std::pair<id, u64>> retired;
    u64 (*current_tick)(void*) = nullptr;
    u64 (*completed_tick)(void*) = nullptr;
    void* tick_context = nullptr;
    u64 released = 0; ///< removed objects released so far (the stats line)
};

State& Get() {
    static State state;
    return state;
}

id ExportBuffer(State& s, VkDeviceMemory memory) {
    VkExportMetalBufferInfoEXT info{
        .sType = VK_STRUCTURE_TYPE_EXPORT_METAL_BUFFER_INFO_EXT,
        .memory = memory,
    };
    VkExportMetalObjectsInfoEXT objects{
        .sType = VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECTS_INFO_EXT,
        .pNext = &info,
    };
    s.export_objects(s.device, &objects);
    return static_cast<id>(info.mtlBuffer);
}

id ExportTexture(State& s, VkImage image) {
    VkExportMetalTextureInfoEXT info{
        .sType = VK_STRUCTURE_TYPE_EXPORT_METAL_TEXTURE_INFO_EXT,
        .image = image,
        .plane = VK_IMAGE_ASPECT_PLANE_0_BIT,
    };
    VkExportMetalObjectsInfoEXT objects{
        .sType = VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECTS_INFO_EXT,
        .pNext = &info,
    };
    s.export_objects(s.device, &objects);
    return static_cast<id>(info.mtlTexture);
}

void Commit(State& s) {
    Send(s.set, "commit");
}

/// Keeps a removed object alive past the command buffers already committed (see SetTicks).
void Retire(State& s, id object) {
    if (!s.current_tick) {
        return;
    }
    Send(object, "retain");
    s.retired.emplace_back(object, s.current_tick(s.tick_context));
}

void ReleaseCompleted(State& s) {
    if (s.retired.empty() || !s.completed_tick) {
        return;
    }
    const u64 completed = s.completed_tick(s.tick_context);
    while (!s.retired.empty() && s.retired.front().second <= completed) {
        Send(s.retired.front().first, "release");
        s.retired.pop_front();
        ++s.released;
    }
}

} // namespace

void Init(const Vulkan::Instance& instance) {
    auto& s = Get();
    std::scoped_lock lock{s.mutex};
    const char* env = std::getenv("BB_RESIDENCY_SET");
    const bool native = MetalLayer::NativeBoot();
    if ((env && env[0] == '0') || (!native && !instance.IsMetalObjectsSupported()) || s.set) {
        return;
    }
    s.device = instance.GetDevice();
    id device = nullptr, queue = nullptr;
    if (native) {
        device = static_cast<id>(MetalLayer::MtlDevice());
        queue = static_cast<id>(MetalLayer::MtlQueue());
    } else {
        s.export_objects = reinterpret_cast<PFN_vkExportMetalObjectsEXT>(
            instance.GetDevice().getProcAddr("vkExportMetalObjectsEXT"));
        if (!s.export_objects) return;
        VkExportMetalDeviceInfoEXT device_info{
            .sType = VK_STRUCTURE_TYPE_EXPORT_METAL_DEVICE_INFO_EXT,
        };
        VkExportMetalCommandQueueInfoEXT queue_info{
            .sType = VK_STRUCTURE_TYPE_EXPORT_METAL_COMMAND_QUEUE_INFO_EXT,
            .pNext = &device_info,
            .queue = instance.GetGraphicsQueue(),
        };
        VkExportMetalObjectsInfoEXT objects{
            .sType = VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECTS_INFO_EXT,
            .pNext = &queue_info,
        };
        s.export_objects(s.device, &objects);
        device = static_cast<id>(device_info.mtlDevice);
        queue = static_cast<id>(queue_info.mtlCommandQueue);
    }
    const SEL new_set = sel_registerName("newResidencySetWithDescriptor:error:");
    if (!device || !queue || !class_respondsToSelector(object_getClass(device), new_set)) {
        std::printf("GPU: no Metal residency sets (macOS 15 or newer needed)\n");
        return;
    }
    const id descriptor =
        Send(reinterpret_cast<id>(objc_getClass("MTLResidencySetDescriptor")), "new");
    reinterpret_cast<void (*)(id, SEL, unsigned long)>(objc_msgSend)(
        descriptor, sel_registerName("setInitialCapacity:"), 4096);
    id error = nullptr;
    s.set = reinterpret_cast<id (*)(id, SEL, id, id*)>(objc_msgSend)(device, new_set, descriptor,
                                                                    &error);
    Send(descriptor, "release");
    if (!s.set) {
        std::printf("GPU: Metal residency set not created\n");
        return;
    }
    SendObject(queue, "addResidencySet:", s.set);
    Send(s.set, "requestResidency");
    std::printf("GPU: GPU memory kept resident (Metal residency set; BB_RESIDENCY_SET=0: off)\n");
}

bool Active() {
    return Get().set != nullptr;
}

bool AddMemory(VkDeviceMemory memory) {
    auto& s = Get();
    if (!s.set || !memory) {
        return false;
    }
    std::scoped_lock lock{s.mutex};
    if (auto it = s.memories.find(memory); it != s.memories.end()) {
        ++it->second.second;
        return true;
    }
    const id buffer = ExportBuffer(s, memory);
    if (!buffer) {
        return false;
    }
    s.memories.emplace(memory, std::pair{buffer, 1u});
    SendObject(s.set, "addAllocation:", buffer);
    Commit(s);
    return true;
}

void RemoveMemory(VkDeviceMemory memory) {
    auto& s = Get();
    if (!s.set || !memory) {
        return;
    }
    std::scoped_lock lock{s.mutex};
    const auto it = s.memories.find(memory);
    if (it == s.memories.end() || --it->second.second != 0) {
        return;
    }
    SendObject(s.set, "removeAllocation:", it->second.first);
    Commit(s);
    Retire(s, it->second.first);
    s.memories.erase(it);
    ReleaseCompleted(s);
}

void AddImage(VkImage image) {
    auto& s = Get();
    if (!s.set || !image) {
        return;
    }
    std::scoped_lock lock{s.mutex};
    if (s.images.contains(image)) {
        return;
    }
    const id texture = ExportTexture(s, image);
    if (!texture) {
        return;
    }
    s.images.emplace(image, texture);
    SendObject(s.set, "addAllocation:", texture);
    Commit(s);
}

void AddImageTexture(VkImage image, void* texture) {
    auto& s = Get();
    if (!s.set || !image || !texture) {
        return;
    }
    std::scoped_lock lock{s.mutex};
    if (s.images.contains(image)) {
        return;
    }
    const id object = static_cast<id>(texture);
    s.images.emplace(image, object);
    SendObject(s.set, "addAllocation:", object);
    Commit(s);
}

void AddMetalBuffer(VkBuffer key, void* buffer) {
    auto& s = Get();
    if (!s.set || !key || !buffer) {
        return;
    }
    std::scoped_lock lock{s.mutex};
    if (s.metal_buffers.emplace(key, static_cast<id>(buffer)).second) {
        SendObject(s.set, "addAllocation:", static_cast<id>(buffer));
        Commit(s);
    }
}

void RemoveMetalBuffer(VkBuffer key) {
    auto& s = Get();
    if (!s.set || !key) {
        return;
    }
    std::scoped_lock lock{s.mutex};
    const auto it = s.metal_buffers.find(key);
    if (it == s.metal_buffers.end()) {
        return;
    }
    SendObject(s.set, "removeAllocation:", it->second);
    Commit(s);
    Retire(s, it->second);
    s.metal_buffers.erase(it);
    ReleaseCompleted(s);
}

void RemoveImage(VkImage image) {
    auto& s = Get();
    if (!s.set || !image) {
        return;
    }
    std::scoped_lock lock{s.mutex};
    const auto it = s.images.find(image);
    if (it == s.images.end()) {
        return;
    }
    SendObject(s.set, "removeAllocation:", it->second);
    Commit(s);
    Retire(s, it->second);
    s.images.erase(it);
    ReleaseCompleted(s);
}

void SetTicks(std::uint64_t (*current)(void*), std::uint64_t (*completed)(void*), void* context) {
    auto& s = Get();
    std::scoped_lock lock{s.mutex};
    s.current_tick = current;
    s.completed_tick = completed;
    s.tick_context = context;
}

void ReleaseRetired() {
    auto& s = Get();
    if (!s.set) {
        return;
    }
    std::scoped_lock lock{s.mutex};
    ReleaseCompleted(s);
    // Every 5 s: removed objects still waiting for their submission, and released so far.
    static auto last = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    static const bool stats = std::getenv("BB_FRAME_STATS") != nullptr;
    if (stats && now - last >= std::chrono::seconds(5)) {
        last = now;
        std::printf("Residency: %zu removed objects waiting, %llu released; set holds %zu "
                    "memories and %zu images\n",
                    s.retired.size(), static_cast<unsigned long long>(s.released),
                    s.memories.size(), s.images.size());
    }
}
#else
void Init(const Vulkan::Instance&) {}
bool Active() {
    return false;
}
bool AddMemory(VkDeviceMemory) {
    return false;
}
void RemoveMemory(VkDeviceMemory) {}
void AddImage(VkImage) {}
void AddImageTexture(VkImage, void*) {}
void AddMetalBuffer(VkBuffer, void*) {}
void RemoveMetalBuffer(VkBuffer) {}
void RemoveImage(VkImage) {}
void SetTicks(std::uint64_t (*)(void*), std::uint64_t (*)(void*), void*) {}
void ReleaseRetired() {}
#endif

} // namespace BbMetalResidency
