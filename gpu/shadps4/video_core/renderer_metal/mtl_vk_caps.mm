// SPDX-License-Identifier: GPL-2.0-or-later
// The Metal backend's answers to the renderer's start-up Vulkan queries (mtl_vk_caps.h). ARC.

#import <Metal/Metal.h>

#include <algorithm>
#include <cstdio>
#include <sys/sysctl.h>
#include <cstdlib>
#include <cstring>
#include <set>
#include <tuple>
#include <mutex>
#include <string>
#include <vector>

#include "common/types.h"
#include "video_core/renderer_metal/mtl_vk_caps.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace MetalCaps {
namespace {

// ---- The answers ----------------------------------------------------------------------------

struct Field {
    const char* structure;
    const char* name;
    size_t offset, size;
    char kind; ///< B VkBool32, U 32-bit, F float, Q 64-bit, S text, X bytes
};

/// The members of every answered structure (tools/vk_caps_fields.py).
const std::vector<Field>& Fields() {
    static const std::vector<Field> fields = [] {
        std::vector<Field> list;
#define BB_STRUCT(S)
#define BB_FIELD(S, M, K) list.push_back({#S, #M, offsetof(S, M), sizeof(S::M), #K[0]});
#include "video_core/renderer_metal/mtl_vk_caps_fields.inc"
#undef BB_FIELD
#undef BB_STRUCT
        return list;
    }();
    return fields;
}

const Field* FindField(const char* structure, const char* name) {
    for (const Field& f : Fields()) {
        if (!std::strcmp(f.structure, structure) && !std::strcmp(f.name, name)) {
            return &f;
        }
    }
    return nullptr;
}

/// Writes a profile value (its text as the comparison printed it) into a member.
void Store(const Field& f, u8* p, const char* text) {
    switch (f.kind) {
    case 'B':
    case 'U': {
        const u32 v = u32(std::strtoul(text, nullptr, 10));
        std::memcpy(p, &v, 4);
        break;
    }
    case 'F': {
        const float v = std::strtof(text, nullptr);
        std::memcpy(p, &v, 4);
        break;
    }
    case 'Q': {
        const u64 v = std::strtoull(text, nullptr, 10);
        std::memcpy(p, &v, 8);
        break;
    }
    case 'S': {
        std::memset(p, 0, f.size);
        std::string value = text;
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        std::memcpy(p, value.data(), std::min(value.size(), f.size - 1));
        break;
    }
    default:
        for (size_t i = 0; i < f.size && text[2 * i] && text[2 * i + 1]; ++i) {
            const char byte[3] = {text[2 * i], text[2 * i + 1], 0};
            p[i] = u8(std::strtoul(byte, nullptr, 16));
        }
        break;
    }
}

/// The profile's values for one structure (tools/vk_caps_profile.py), written over `base`.
void ApplyProfile(const char* structure, void* base) {
    struct Value {
        const char* structure;
        const char* field;
        const char* text;
    };
    static const Value values[] = {
#define BB_VALUE(S, F, V) {#S, #F, V},
#define BB_FORMAT(...)
#define BB_MEMORY_TYPE(...)
#define BB_MEMORY_HEAP(...)
#define BB_QUEUE_FAMILY(...)
#define BB_EXTENSION(...)
#include "video_core/renderer_metal/mtl_vk_caps_profile.inc"
#undef BB_VALUE
#undef BB_FORMAT
#undef BB_MEMORY_TYPE
#undef BB_MEMORY_HEAP
#undef BB_QUEUE_FAMILY
#undef BB_EXTENSION
    };
    for (const Value& v : values) {
        if (std::strcmp(v.structure, structure) != 0) {
            continue;
        }
        if (const Field* f = FindField(v.structure, v.field)) {
            Store(*f, static_cast<u8*>(base) + f->offset, v.text);
        }
    }
}

id<MTLDevice> Device() {
    static id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    return device;
}

/// The structures of a features or properties chain the Metal backend answers, by type.
struct Answered {
    VkStructureType type;
    const char* name;
    size_t size;
};
template <typename T>
constexpr Answered Entry(const char* name) {
    return {VkStructureType(T::structureType), name, sizeof(T)};
}
const Answered answered[] = {
    Entry<vk::PhysicalDeviceVulkan11Features>("VkPhysicalDeviceVulkan11Features"),
    Entry<vk::PhysicalDeviceVulkan12Features>("VkPhysicalDeviceVulkan12Features"),
    Entry<vk::PhysicalDeviceVulkan13Features>("VkPhysicalDeviceVulkan13Features"),
    Entry<vk::PhysicalDeviceRobustness2FeaturesEXT>("VkPhysicalDeviceRobustness2FeaturesKHR"),
    Entry<vk::PhysicalDeviceExtendedDynamicState3FeaturesEXT>("VkPhysicalDeviceExtendedDynamicState3FeaturesEXT"),
    Entry<vk::PhysicalDevicePrimitiveTopologyListRestartFeaturesEXT>("VkPhysicalDevicePrimitiveTopologyListRestartFeaturesEXT"),
    Entry<vk::PhysicalDeviceShaderAtomicFloat2FeaturesEXT>("VkPhysicalDeviceShaderAtomicFloat2FeaturesEXT"),
    Entry<vk::PhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR>("VkPhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR"),
    Entry<vk::PhysicalDeviceImage2DViewOf3DFeaturesEXT>("VkPhysicalDeviceImage2DViewOf3DFeaturesEXT"),
    Entry<vk::PhysicalDeviceShaderClockFeaturesKHR>("VkPhysicalDeviceShaderClockFeaturesKHR"),
    Entry<vk::PhysicalDeviceComputeShaderDerivativesFeaturesKHR>("VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR"),
    Entry<vk::PhysicalDeviceShaderMixedFloatDotProductFeaturesVALVE>("VkPhysicalDeviceShaderMixedFloatDotProductFeaturesVALVE"),
    Entry<vk::PhysicalDeviceCooperativeMatrixFeaturesKHR>("VkPhysicalDeviceCooperativeMatrixFeaturesKHR"),
    Entry<vk::PhysicalDeviceShaderFloat8FeaturesEXT>("VkPhysicalDeviceShaderFloat8FeaturesEXT"),
    Entry<vk::PhysicalDeviceVulkan11Properties>("VkPhysicalDeviceVulkan11Properties"),
    Entry<vk::PhysicalDeviceVulkan12Properties>("VkPhysicalDeviceVulkan12Properties"),
    Entry<vk::PhysicalDeviceVulkan13Properties>("VkPhysicalDeviceVulkan13Properties"),
    Entry<vk::PhysicalDevicePushDescriptorPropertiesKHR>("VkPhysicalDevicePushDescriptorProperties"),
    Entry<vk::PhysicalDeviceDriverProperties>("VkPhysicalDeviceDriverProperties"),
};

/// Every structure of the chain after `head` that the Metal backend answers.
void ApplyChain(void* head) {
    for (auto* s = static_cast<VkBaseOutStructure*>(head)->pNext; s; s = s->pNext) {
        for (const Answered& a : answered) {
            if (a.type == s->sType) {
                // Cleared after sType and pNext, then the profile's values.
                std::memset(reinterpret_cast<u8*>(s) + sizeof(VkBaseOutStructure), 0,
                            a.size - sizeof(VkBaseOutStructure));
                ApplyProfile(a.name, s);
            }
        }
    }
}

void FillFeatures(VkPhysicalDeviceFeatures2* features) {
    features->features = {};
    ApplyProfile("VkPhysicalDeviceFeatures", &features->features);
    ApplyChain(features);
}

void FillProperties(VkPhysicalDeviceProperties2* properties) {
    auto& p = properties->properties;
    p = {};
    ApplyProfile("VkPhysicalDeviceProperties", &p);
    ApplyProfile("VkPhysicalDeviceLimits", &p.limits);
    ApplyProfile("VkPhysicalDeviceSparseProperties", &p.sparseProperties);
    ApplyChain(properties);
    // What depends on the device.
    id<MTLDevice> device = Device();
    std::memset(p.deviceName, 0, sizeof(p.deviceName));
    std::strncpy(p.deviceName, device.name.UTF8String, sizeof(p.deviceName) - 1);
    p.limits.maxComputeSharedMemorySize = u32(device.maxThreadgroupMemoryLength);
    p.limits.maxComputeWorkGroupInvocations = u32(device.maxThreadsPerThreadgroup.width);
    for (auto* s = static_cast<VkBaseOutStructure*>(static_cast<void*>(properties))->pNext; s; s = s->pNext) {
        if (s->sType == VkStructureType(vk::PhysicalDeviceVulkan11Properties::structureType)) {
            reinterpret_cast<VkPhysicalDeviceVulkan11Properties*>(s)->maxMemoryAllocationSize = device.maxBufferLength;
        } else if (s->sType == VkStructureType(vk::PhysicalDeviceVulkan13Properties::structureType)) {
            reinterpret_cast<VkPhysicalDeviceVulkan13Properties*>(s)->maxBufferSize = device.maxBufferLength;
        }
    }
}

void FillFormat(VkFormat format, VkFormatProperties2* properties) {
    properties->formatProperties = {};
    for (auto* s = static_cast<VkBaseOutStructure*>(static_cast<void*>(properties))->pNext; s; s = s->pNext) {
        if (s->sType == VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3) {
            auto* p3 = reinterpret_cast<VkFormatProperties3*>(s);
            p3->linearTilingFeatures = p3->optimalTilingFeatures = p3->bufferFeatures = 0;
        }
    }
    struct Masks {
        u32 format;
        u64 linear, optimal, buffer;
    };
    static const Masks masks[] = {
#define BB_FORMAT(F, L, O, B) {F, L, O, B},
#define BB_VALUE(...)
#define BB_MEMORY_TYPE(...)
#define BB_MEMORY_HEAP(...)
#define BB_QUEUE_FAMILY(...)
#define BB_EXTENSION(...)
#include "video_core/renderer_metal/mtl_vk_caps_profile.inc"
#undef BB_VALUE
#undef BB_FORMAT
#undef BB_MEMORY_TYPE
#undef BB_MEMORY_HEAP
#undef BB_QUEUE_FAMILY
#undef BB_EXTENSION
    };
    for (const Masks& m : masks) {
        if (m.format != u32(format)) {
            continue;
        }
        properties->formatProperties = {VkFormatFeatureFlags(m.linear), VkFormatFeatureFlags(m.optimal),
                                         VkFormatFeatureFlags(m.buffer)};
        for (auto* s = static_cast<VkBaseOutStructure*>(static_cast<void*>(properties))->pNext; s; s = s->pNext) {
            if (s->sType == VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3) {
                auto* p3 = reinterpret_cast<VkFormatProperties3*>(s);
                p3->linearTilingFeatures = m.linear;
                p3->optimalTilingFeatures = m.optimal;
                p3->bufferFeatures = m.buffer;
            }
        }
    }
}

/// vkGetPhysicalDeviceImageFormatProperties2: whether an image of these parameters can be made,
/// and its limits, from the format's features (FillFormat) and the device's limits.
VkResult ImageFormat(const VkPhysicalDeviceImageFormatInfo2* info, VkImageFormatProperties2* out) {
    VkFormatProperties3 p3{VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3};
    VkFormatProperties2 p2{VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2, &p3};
    FillFormat(info->format, &p2);
    const u64 features = info->tiling == VK_IMAGE_TILING_LINEAR ? p3.linearTilingFeatures
                                                                : p3.optimalTilingFeatures;
    out->imageFormatProperties = {};
    // Each usage needs its feature (Vulkan 1.3, "Image Creation Limits").
    const struct {
        VkImageUsageFlags usage;
        u64 feature;
    } needs[] = {
        {VK_IMAGE_USAGE_SAMPLED_BIT, VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT},
        {VK_IMAGE_USAGE_STORAGE_BIT, VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT},
        {VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT},
        {VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT},
        {VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT},
        {VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT},
    };
    // With extended usage the image's own format need not support every usage (its views do).
    if (!(info->flags & VK_IMAGE_CREATE_EXTENDED_USAGE_BIT)) {
        for (const auto& n : needs) {
            if ((info->usage & n.usage) && !(features & n.feature)) {
                return VK_ERROR_FORMAT_NOT_SUPPORTED;
            }
        }
    }
    if (!features) {
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }
    // Metal has no uncompressed views of block-compressed textures.
    if (info->flags & VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT) {
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }
    // Metal cannot write a block-compressed texture, extended usage or not.
    if ((info->usage & VK_IMAGE_USAGE_STORAGE_BIT) && info->format >= VK_FORMAT_BC1_RGB_UNORM_BLOCK &&
        info->format <= VK_FORMAT_ASTC_12x12_SRGB_BLOCK) {
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }
    VkImageFormatProperties& p = out->imageFormatProperties;
    switch (info->type) {
    case VK_IMAGE_TYPE_1D:
        p.maxExtent = {16384, 1, 1};
        p.maxArrayLayers = 2048;
        break;
    case VK_IMAGE_TYPE_3D:
        p.maxExtent = {2048, 2048, 2048};
        p.maxArrayLayers = 1;
        break;
    default:
        p.maxExtent = {16384, 16384, 1};
        p.maxArrayLayers = 2048;
        break;
    }
    u32 levels = 1;
    for (u32 size = std::max({p.maxExtent.width, p.maxExtent.height, p.maxExtent.depth}); size > 1; size >>= 1) {
        ++levels;
    }
    p.maxMipLevels = info->tiling == VK_IMAGE_TILING_LINEAR ? 1 : levels;
    p.sampleCounts = VK_SAMPLE_COUNT_1_BIT;
    if (info->type == VK_IMAGE_TYPE_2D && info->tiling == VK_IMAGE_TILING_OPTIMAL &&
        !(info->flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) &&
        (features & (VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT))) {
        p.sampleCounts = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT;
    }
    p.maxResourceSize = u64(1) << 62; // as MoltenVK reports it: no limit of its own
    return VK_SUCCESS;
}

void FillMemory(VkPhysicalDeviceMemoryProperties* memory) {
    *memory = {};
#define BB_MEMORY_TYPE(F, H) memory->memoryTypes[memory->memoryTypeCount++] = {VkMemoryPropertyFlags(F), H};
#define BB_MEMORY_HEAP(S, F) memory->memoryHeaps[memory->memoryHeapCount++] = {S, VkMemoryHeapFlags(F)};
#define BB_VALUE(...)
#define BB_FORMAT(...)
#define BB_QUEUE_FAMILY(...)
#define BB_EXTENSION(...)
#include "video_core/renderer_metal/mtl_vk_caps_profile.inc"
#undef BB_VALUE
#undef BB_FORMAT
#undef BB_MEMORY_TYPE
#undef BB_MEMORY_HEAP
#undef BB_QUEUE_FAMILY
#undef BB_EXTENSION
    // One heap, the Mac's memory (as MoltenVK reports it).
    u64 bytes = 0;
    size_t size = sizeof(bytes);
    if (memory->memoryHeapCount && sysctlbyname("hw.memsize", &bytes, &size, nullptr, 0) == 0) {
        memory->memoryHeaps[0].size = bytes;
    }
}

std::vector<VkQueueFamilyProperties> QueueFamilies() {
    std::vector<VkQueueFamilyProperties> families;
#define BB_QUEUE_FAMILY(F, C, T, W, H, D) families.push_back({VkQueueFlags(F), C, T, {W, H, D}});
#define BB_VALUE(...)
#define BB_FORMAT(...)
#define BB_MEMORY_TYPE(...)
#define BB_MEMORY_HEAP(...)
#define BB_EXTENSION(...)
#include "video_core/renderer_metal/mtl_vk_caps_profile.inc"
#undef BB_VALUE
#undef BB_FORMAT
#undef BB_MEMORY_TYPE
#undef BB_MEMORY_HEAP
#undef BB_QUEUE_FAMILY
#undef BB_EXTENSION
    return families;
}

std::vector<std::string> Extensions() {
    return {
#define BB_EXTENSION(E) E,
#define BB_VALUE(...)
#define BB_FORMAT(...)
#define BB_MEMORY_TYPE(...)
#define BB_MEMORY_HEAP(...)
#define BB_QUEUE_FAMILY(...)
#include "video_core/renderer_metal/mtl_vk_caps_profile.inc"
#undef BB_VALUE
#undef BB_FORMAT
#undef BB_MEMORY_TYPE
#undef BB_MEMORY_HEAP
#undef BB_QUEUE_FAMILY
#undef BB_EXTENSION
    };
}


// ---- The entry points (step 4.2) --------------------------------------------------------------

VKAPI_ATTR void VKAPI_CALL GetFeatures2(VkPhysicalDevice, VkPhysicalDeviceFeatures2* features) {
    FillFeatures(features);
}
VKAPI_ATTR void VKAPI_CALL GetFeatures(VkPhysicalDevice, VkPhysicalDeviceFeatures* features) {
    *features = {};
    ApplyProfile("VkPhysicalDeviceFeatures", features);
}
VKAPI_ATTR void VKAPI_CALL GetProperties2(VkPhysicalDevice, VkPhysicalDeviceProperties2* properties) {
    @autoreleasepool {
        FillProperties(properties);
    }
}
VKAPI_ATTR void VKAPI_CALL GetProperties(VkPhysicalDevice, VkPhysicalDeviceProperties* properties) {
    VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    @autoreleasepool {
        FillProperties(&p2);
    }
    *properties = p2.properties;
}
VKAPI_ATTR void VKAPI_CALL GetFormatProperties2(VkPhysicalDevice, VkFormat format, VkFormatProperties2* p) {
    FillFormat(format, p);
}
VKAPI_ATTR void VKAPI_CALL GetFormatProperties(VkPhysicalDevice, VkFormat format, VkFormatProperties* p) {
    VkFormatProperties2 p2{VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2};
    FillFormat(format, &p2);
    *p = p2.formatProperties;
}
VKAPI_ATTR void VKAPI_CALL GetMemoryProperties(VkPhysicalDevice, VkPhysicalDeviceMemoryProperties* memory) {
    FillMemory(memory);
}
VKAPI_ATTR void VKAPI_CALL GetMemoryProperties2(VkPhysicalDevice, VkPhysicalDeviceMemoryProperties2* memory) {
    FillMemory(&memory->memoryProperties);
    for (auto* s = static_cast<VkBaseOutStructure*>(static_cast<void*>(memory))->pNext; s; s = s->pNext) {
        if (s->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT) {
            // As MoltenVK reports it: the device's working set and what it has allocated.
            auto* budget = reinterpret_cast<VkPhysicalDeviceMemoryBudgetPropertiesEXT*>(s);
            std::memset(budget->heapBudget, 0, sizeof(budget->heapBudget));
            std::memset(budget->heapUsage, 0, sizeof(budget->heapUsage));
            budget->heapBudget[0] = Device().recommendedMaxWorkingSetSize;
            budget->heapUsage[0] = Device().currentAllocatedSize;
        }
    }
}
VKAPI_ATTR void VKAPI_CALL GetQueueFamilies(VkPhysicalDevice, u32* count, VkQueueFamilyProperties* out) {
    const auto families = QueueFamilies();
    if (!out) {
        *count = u32(families.size());
        return;
    }
    *count = std::min(*count, u32(families.size()));
    std::copy_n(families.begin(), *count, out);
}
VKAPI_ATTR void VKAPI_CALL GetQueueFamilies2(VkPhysicalDevice, u32* count, VkQueueFamilyProperties2* out) {
    const auto families = QueueFamilies();
    if (!out) {
        *count = u32(families.size());
        return;
    }
    *count = std::min(*count, u32(families.size()));
    for (u32 i = 0; i < *count; ++i) {
        out[i].queueFamilyProperties = families[i];
    }
}
VKAPI_ATTR VkResult VKAPI_CALL EnumerateExtensions(VkPhysicalDevice, const char* layer, u32* count,
                                                  VkExtensionProperties* out) {
    if (layer) {
        *count = 0;
        return VK_ERROR_LAYER_NOT_PRESENT;
    }
    const auto names = Extensions();
    if (!out) {
        *count = u32(names.size());
        return VK_SUCCESS;
    }
    const u32 n = std::min(*count, u32(names.size()));
    for (u32 i = 0; i < n; ++i) {
        out[i] = {};
        std::strncpy(out[i].extensionName, names[i].c_str(), VK_MAX_EXTENSION_NAME_SIZE - 1);
        out[i].specVersion = 1;
    }
    *count = n;
    return n < names.size() ? VK_INCOMPLETE : VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL GetImageFormat2(VkPhysicalDevice, const VkPhysicalDeviceImageFormatInfo2* info,
                                              VkImageFormatProperties2* out) {
    @autoreleasepool {
        return ImageFormat(info, out);
    }
}
VKAPI_ATTR VkResult VKAPI_CALL GetToolProperties(VkPhysicalDevice, u32* count, VkPhysicalDeviceToolProperties*) {
    *count = 0;
    return VK_SUCCESS;
}

/// MoltenVK's own entry points, kept for the comparison once ours are in the dispatcher.
struct MoltenVkQueries {
    PFN_vkGetPhysicalDeviceFeatures2 features2;
    PFN_vkGetPhysicalDeviceProperties2 properties2;
    PFN_vkGetPhysicalDeviceFormatProperties2 format2;
    PFN_vkGetPhysicalDeviceMemoryProperties memory;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties families;
    PFN_vkEnumerateDeviceExtensionProperties extensions;
    PFN_vkGetPhysicalDeviceImageFormatProperties2 image_format2;
} mvk;

// ---- The comparison ---------------------------------------------------------------------------

std::string Value(const Field& f, const u8* p) {
    char text[256];
    switch (f.kind) {
    case 'B':
    case 'U': {
        u32 v;
        std::memcpy(&v, p, 4);
        std::snprintf(text, sizeof text, "%u", v);
        return text;
    }
    case 'F': {
        float v;
        std::memcpy(&v, p, 4);
        std::snprintf(text, sizeof text, "%g", v);
        return text;
    }
    case 'Q': {
        u64 v;
        std::memcpy(&v, p, 8);
        std::snprintf(text, sizeof text, "%llu", (unsigned long long)v);
        return text;
    }
    case 'S':
        return std::string{"\""} + std::string(reinterpret_cast<const char*>(p), strnlen(reinterpret_cast<const char*>(p), f.size)) + "\"";
    default: {
        std::string hex;
        for (size_t i = 0; i < f.size && i < 32; ++i) {
            std::snprintf(text, sizeof text, "%02x", p[i]);
            hex += text;
        }
        return hex;
    }
    }
}

/// Compares one structure's members; returns how many differ.
int CompareStructure(const char* structure, const void* moltenvk, const void* metal) {
    int differences = 0;
    for (const Field& f : Fields()) {
        if (std::strcmp(f.structure, structure) != 0) {
            continue;
        }
        const u8* a = static_cast<const u8*>(moltenvk) + f.offset;
        const u8* b = static_cast<const u8*>(metal) + f.offset;
        // Text up to its terminator (drivers leave the rest of the array as it was).
        const bool differ = f.kind == 'S' ? strncmp(reinterpret_cast<const char*>(a),
                                                     reinterpret_cast<const char*>(b), f.size) != 0
                                          : std::memcmp(a, b, f.size) != 0;
        if (differ) {
            std::printf("Metal caps: %s.%s: MoltenVK %s, Metal %s\n", structure, f.name,
                        Value(f, a).c_str(), Value(f, b).c_str());
            ++differences;
        }
    }
    return differences;
}

int CompareFeatures(VkPhysicalDevice physical) {
    using Chain = vk::StructureChain<
        vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features,
        vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan13Features,
        vk::PhysicalDeviceRobustness2FeaturesEXT, vk::PhysicalDeviceExtendedDynamicState3FeaturesEXT,
        vk::PhysicalDevicePrimitiveTopologyListRestartFeaturesEXT,
        vk::PhysicalDeviceShaderAtomicFloat2FeaturesEXT,
        vk::PhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR,
        vk::PhysicalDeviceImage2DViewOf3DFeaturesEXT, vk::PhysicalDeviceShaderClockFeaturesKHR,
        vk::PhysicalDeviceComputeShaderDerivativesFeaturesKHR,
        vk::PhysicalDeviceShaderMixedFloatDotProductFeaturesVALVE,
        vk::PhysicalDeviceCooperativeMatrixFeaturesKHR, vk::PhysicalDeviceShaderFloat8FeaturesEXT>;
    Chain moltenvk, metal;
    mvk.features2(
        physical, reinterpret_cast<VkPhysicalDeviceFeatures2*>(&moltenvk.get<vk::PhysicalDeviceFeatures2>()));
    FillFeatures(reinterpret_cast<VkPhysicalDeviceFeatures2*>(&metal.get<vk::PhysicalDeviceFeatures2>()));
    int n = CompareStructure("VkPhysicalDeviceFeatures", &moltenvk.get<vk::PhysicalDeviceFeatures2>().features,
                             &metal.get<vk::PhysicalDeviceFeatures2>().features);
#define BB_COMPARE(T, S) n += CompareStructure(#S, &moltenvk.get<T>(), &metal.get<T>());
    BB_COMPARE(vk::PhysicalDeviceVulkan11Features, VkPhysicalDeviceVulkan11Features)
    BB_COMPARE(vk::PhysicalDeviceVulkan12Features, VkPhysicalDeviceVulkan12Features)
    BB_COMPARE(vk::PhysicalDeviceVulkan13Features, VkPhysicalDeviceVulkan13Features)
    BB_COMPARE(vk::PhysicalDeviceRobustness2FeaturesEXT, VkPhysicalDeviceRobustness2FeaturesKHR)
    BB_COMPARE(vk::PhysicalDeviceExtendedDynamicState3FeaturesEXT, VkPhysicalDeviceExtendedDynamicState3FeaturesEXT)
    BB_COMPARE(vk::PhysicalDevicePrimitiveTopologyListRestartFeaturesEXT, VkPhysicalDevicePrimitiveTopologyListRestartFeaturesEXT)
    BB_COMPARE(vk::PhysicalDeviceShaderAtomicFloat2FeaturesEXT, VkPhysicalDeviceShaderAtomicFloat2FeaturesEXT)
    BB_COMPARE(vk::PhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR, VkPhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR)
    BB_COMPARE(vk::PhysicalDeviceImage2DViewOf3DFeaturesEXT, VkPhysicalDeviceImage2DViewOf3DFeaturesEXT)
    BB_COMPARE(vk::PhysicalDeviceShaderClockFeaturesKHR, VkPhysicalDeviceShaderClockFeaturesKHR)
    BB_COMPARE(vk::PhysicalDeviceComputeShaderDerivativesFeaturesKHR, VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR)
    BB_COMPARE(vk::PhysicalDeviceShaderMixedFloatDotProductFeaturesVALVE, VkPhysicalDeviceShaderMixedFloatDotProductFeaturesVALVE)
    BB_COMPARE(vk::PhysicalDeviceCooperativeMatrixFeaturesKHR, VkPhysicalDeviceCooperativeMatrixFeaturesKHR)
    BB_COMPARE(vk::PhysicalDeviceShaderFloat8FeaturesEXT, VkPhysicalDeviceShaderFloat8FeaturesEXT)
#undef BB_COMPARE
    return n;
}

int CompareProperties(VkPhysicalDevice physical) {
    using Chain = vk::StructureChain<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceVulkan11Properties,
                                     vk::PhysicalDeviceVulkan12Properties, vk::PhysicalDeviceVulkan13Properties,
                                     vk::PhysicalDevicePushDescriptorPropertiesKHR,
                                     vk::PhysicalDeviceDriverProperties>;
    Chain moltenvk, metal;
    mvk.properties2(
        physical, reinterpret_cast<VkPhysicalDeviceProperties2*>(&moltenvk.get<vk::PhysicalDeviceProperties2>()));
    FillProperties(reinterpret_cast<VkPhysicalDeviceProperties2*>(&metal.get<vk::PhysicalDeviceProperties2>()));
    const auto& a = moltenvk.get<vk::PhysicalDeviceProperties2>().properties;
    const auto& b = metal.get<vk::PhysicalDeviceProperties2>().properties;
    int n = CompareStructure("VkPhysicalDeviceProperties", &a, &b);
    n += CompareStructure("VkPhysicalDeviceLimits", &a.limits, &b.limits);
    n += CompareStructure("VkPhysicalDeviceSparseProperties", &a.sparseProperties, &b.sparseProperties);
#define BB_COMPARE(T, S) n += CompareStructure(#S, &moltenvk.get<T>(), &metal.get<T>());
    BB_COMPARE(vk::PhysicalDeviceVulkan11Properties, VkPhysicalDeviceVulkan11Properties)
    BB_COMPARE(vk::PhysicalDeviceVulkan12Properties, VkPhysicalDeviceVulkan12Properties)
    BB_COMPARE(vk::PhysicalDeviceVulkan13Properties, VkPhysicalDeviceVulkan13Properties)
    BB_COMPARE(vk::PhysicalDevicePushDescriptorPropertiesKHR, VkPhysicalDevicePushDescriptorProperties)
    BB_COMPARE(vk::PhysicalDeviceDriverProperties, VkPhysicalDeviceDriverProperties)
#undef BB_COMPARE
    return n;
}

/// Every core format (1 .. VK_FORMAT_ASTC_12x12_SRGB_BLOCK): the three feature masks.
int CompareFormats(VkPhysicalDevice physical) {
    int n = 0;
    for (u32 f = 1; f <= VK_FORMAT_ASTC_12x12_SRGB_BLOCK; ++f) {
        VkFormatProperties3 a3{VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3}, b3{VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3};
        VkFormatProperties2 a{VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2, &a3}, b{VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2, &b3};
        mvk.format2(physical, VkFormat(f), &a);
        FillFormat(VkFormat(f), &b);
        const auto report = [&](const char* what, u64 x, u64 y) {
            if (x != y) {
                std::printf("Metal caps: format %u (%s) %s: MoltenVK %#llx, Metal %#llx (MoltenVK only "
                            "%#llx, Metal only %#llx)\n",
                            f, vk::to_string(vk::Format(f)).c_str(), what, (unsigned long long)x,
                            (unsigned long long)y, (unsigned long long)(x & ~y), (unsigned long long)(y & ~x));
                ++n;
            }
        };
        report("linear", a3.linearTilingFeatures, b3.linearTilingFeatures);
        report("optimal", a3.optimalTilingFeatures, b3.optimalTilingFeatures);
        report("buffer", a3.bufferFeatures, b3.bufferFeatures);
    }
    return n;
}

int CompareMemoryAndQueues(VkPhysicalDevice physical) {
    int n = 0;
    VkPhysicalDeviceMemoryProperties a{}, b{};
    mvk.memory(physical, &a);
    FillMemory(&b);
    if (a.memoryTypeCount != b.memoryTypeCount || a.memoryHeapCount != b.memoryHeapCount) {
        std::printf("Metal caps: memory: MoltenVK %u types in %u heaps, Metal %u in %u\n",
                    a.memoryTypeCount, a.memoryHeapCount, b.memoryTypeCount, b.memoryHeapCount);
        ++n;
    }
    for (u32 i = 0; i < a.memoryTypeCount; ++i) {
        if (i >= b.memoryTypeCount || a.memoryTypes[i].propertyFlags != b.memoryTypes[i].propertyFlags ||
            a.memoryTypes[i].heapIndex != b.memoryTypes[i].heapIndex) {
            std::printf("Metal caps: memory type %u: MoltenVK flags %#x heap %u\n", i,
                        a.memoryTypes[i].propertyFlags, a.memoryTypes[i].heapIndex);
            ++n;
        }
    }
    for (u32 i = 0; i < a.memoryHeapCount; ++i) {
        if (i >= b.memoryHeapCount || a.memoryHeaps[i].size != b.memoryHeaps[i].size ||
            a.memoryHeaps[i].flags != b.memoryHeaps[i].flags) {
            std::printf("Metal caps: memory heap %u: MoltenVK %llu bytes flags %#x\n", i,
                        (unsigned long long)a.memoryHeaps[i].size, a.memoryHeaps[i].flags);
            ++n;
        }
    }
    u32 count = 0;
    mvk.families(physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    mvk.families(physical, &count, families.data());
    const auto ours = QueueFamilies();
    for (u32 i = 0; i < count; ++i) {
        const bool same = i < ours.size() && std::memcmp(&families[i], &ours[i], sizeof(families[i])) == 0;
        if (!same) {
            std::printf("Metal caps: queue family %u: MoltenVK flags %#x count %u timestamp bits %u "
                        "granularity %ux%ux%u\n",
                        i, families[i].queueFlags, families[i].queueCount, families[i].timestampValidBits,
                        families[i].minImageTransferGranularity.width,
                        families[i].minImageTransferGranularity.height,
                        families[i].minImageTransferGranularity.depth);
            ++n;
        }
    }
    if (ours.size() > count) {
        std::printf("Metal caps: Metal has %zu queue families, MoltenVK %u\n", ours.size(), count);
        ++n;
    }
    return n;
}

int CompareExtensions(VkPhysicalDevice physical) {
    u32 count = 0;
    mvk.extensions(physical, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> list(count);
    mvk.extensions(physical, nullptr, &count, list.data());
    std::set<std::string> moltenvk, metal;
    for (const auto& e : list) {
        moltenvk.insert(e.extensionName);
    }
    for (const auto& e : Extensions()) {
        metal.insert(e);
    }
    std::string only_moltenvk, only_metal;
    int missing = 0, extra = 0;
    for (const auto& e : moltenvk) {
        if (!metal.contains(e)) {
            only_moltenvk += " " + e;
            ++missing;
        }
    }
    for (const auto& e : metal) {
        if (!moltenvk.contains(e)) {
            only_metal += " " + e;
            ++extra;
        }
    }
    if (missing) {
        std::printf("Metal caps: extensions only MoltenVK has (%d of %zu):%s\n", missing,
                    moltenvk.size(), only_moltenvk.c_str());
    }
    if (extra) {
        std::printf("Metal caps: extensions only Metal has (%d):%s\n", extra, only_metal.c_str());
    }
    const int n = missing + extra;
    return n;
}

PFN_vkGetPhysicalDeviceImageFormatProperties2 moltenvk_image_format;

/// MoltenVK's entry points from the dispatcher, before any of ours replace them.
void SaveMoltenVk() {
    if (mvk.features2) {
        return;
    }
    auto& d = VULKAN_HPP_DEFAULT_DISPATCHER;
    mvk = {d.vkGetPhysicalDeviceFeatures2, d.vkGetPhysicalDeviceProperties2,
                d.vkGetPhysicalDeviceFormatProperties2, d.vkGetPhysicalDeviceMemoryProperties,
                d.vkGetPhysicalDeviceQueueFamilyProperties, d.vkEnumerateDeviceExtensionProperties,
                d.vkGetPhysicalDeviceImageFormatProperties2};
}

/// In compare mode the renderer's image format queries go through here: MoltenVK answers (as
/// before) and each distinct query whose Metal answer differs is logged once.
VKAPI_ATTR VkResult VKAPI_CALL CompareImageFormat(VkPhysicalDevice physical,
                                                  const VkPhysicalDeviceImageFormatInfo2* info,
                                                  VkImageFormatProperties2* out) {
    const VkResult result = moltenvk_image_format(physical, info, out);
    VkImageFormatProperties2 ours{VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};
    const VkResult our_result = ImageFormat(info, &ours);
    const auto& a = out->imageFormatProperties;
    const auto& b = ours.imageFormatProperties;
    const bool same = result == our_result &&
                      (result != VK_SUCCESS ||
                       (a.maxExtent.width == b.maxExtent.width && a.maxExtent.height == b.maxExtent.height &&
                        a.maxExtent.depth == b.maxExtent.depth && a.maxMipLevels == b.maxMipLevels &&
                        a.maxArrayLayers == b.maxArrayLayers && a.sampleCounts == b.sampleCounts &&
                        a.maxResourceSize == b.maxResourceSize));
    static std::mutex mutex;
    static std::set<std::tuple<int, int, int, u32, u32>> logged;
    std::scoped_lock lock{mutex};
    if (!same && logged.emplace(info->format, info->type, info->tiling, info->usage, info->flags).second) {
        std::printf("Metal caps: image format %d type %d tiling %d usage %#x flags %#x: MoltenVK %d "
                    "%ux%ux%u, %u levels, %u layers, samples %#x, %llu bytes; Metal %d %ux%ux%u, %u "
                    "levels, %u layers, samples %#x, %llu bytes\n",
                    info->format, info->type, info->tiling, info->usage, info->flags, result,
                    a.maxExtent.width, a.maxExtent.height, a.maxExtent.depth, a.maxMipLevels,
                    a.maxArrayLayers, a.sampleCounts, (unsigned long long)a.maxResourceSize,
                    our_result, b.maxExtent.width, b.maxExtent.height, b.maxExtent.depth,
                    b.maxMipLevels, b.maxArrayLayers, b.sampleCounts,
                    (unsigned long long)b.maxResourceSize);
    }
    return result;
}

} // namespace

void CompareIfAsked(void* physical_device) {
    const char* env = std::getenv("BB_METAL_CAPS");
    if (!env || std::strcmp(env, "compare") != 0 || !physical_device) {
        return;
    }
    const auto physical = static_cast<VkPhysicalDevice>(physical_device);
    SaveMoltenVk();
    @autoreleasepool {
        const int features = CompareFeatures(physical);
        const int properties = CompareProperties(physical);
        const int formats = CompareFormats(physical);
        const int memory = CompareMemoryAndQueues(physical);
        const int extensions = CompareExtensions(physical);
        // The image format queries happen later (the texture cache): compared as they come.
        moltenvk_image_format = mvk.image_format2;
        VULKAN_HPP_DEFAULT_DISPATCHER.vkGetPhysicalDeviceImageFormatProperties2 = CompareImageFormat;
        {
            // Instance::CollectImageFormatInfo's block-texel-view query, made before this.
            VkPhysicalDeviceImageFormatInfo2 info{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2};
            info.format = VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
            info.type = VK_IMAGE_TYPE_2D;
            info.tiling = VK_IMAGE_TILING_OPTIMAL;
            info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
            info.flags = VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT;
            VkImageFormatProperties2 props{VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};
            CompareImageFormat(physical, &info, &props);
        }
        std::printf("Metal caps: %d differences from MoltenVK (features %d, properties %d, formats %d, "
                    "memory and queues %d, extensions %d)\n",
                    features + properties + formats + memory + extensions, features, properties,
                    formats, memory, extensions);
    }
}

void UseMetalAnswers() {
    const char* frame = std::getenv("BB_METAL_FRAME");
    const char* caps = std::getenv("BB_METAL_CAPS");
    if (!frame || frame[0] != '1' || (caps && std::strcmp(caps, "moltenvk") == 0)) {
        return;
    }
    SaveMoltenVk();
    auto& d = VULKAN_HPP_DEFAULT_DISPATCHER;
    d.vkGetPhysicalDeviceFeatures2 = d.vkGetPhysicalDeviceFeatures2KHR = GetFeatures2;
    d.vkGetPhysicalDeviceFeatures = GetFeatures;
    d.vkGetPhysicalDeviceProperties2 = d.vkGetPhysicalDeviceProperties2KHR = GetProperties2;
    d.vkGetPhysicalDeviceProperties = GetProperties;
    d.vkGetPhysicalDeviceFormatProperties2 = d.vkGetPhysicalDeviceFormatProperties2KHR = GetFormatProperties2;
    d.vkGetPhysicalDeviceFormatProperties = GetFormatProperties;
    d.vkGetPhysicalDeviceMemoryProperties = GetMemoryProperties;
    d.vkGetPhysicalDeviceMemoryProperties2 = d.vkGetPhysicalDeviceMemoryProperties2KHR = GetMemoryProperties2;
    d.vkGetPhysicalDeviceQueueFamilyProperties = GetQueueFamilies;
    d.vkGetPhysicalDeviceQueueFamilyProperties2 = d.vkGetPhysicalDeviceQueueFamilyProperties2KHR = GetQueueFamilies2;
    d.vkEnumerateDeviceExtensionProperties = EnumerateExtensions;
    d.vkGetPhysicalDeviceImageFormatProperties2 = d.vkGetPhysicalDeviceImageFormatProperties2KHR = GetImageFormat2;
    d.vkGetPhysicalDeviceToolProperties = d.vkGetPhysicalDeviceToolPropertiesEXT = GetToolProperties;
    std::printf("Metal caps: the device's features, properties, formats, memory, queues and "
                "extensions are the Metal backend's (BB_METAL_CAPS=moltenvk: MoltenVK's)\n");
}

void* GetProc(const char* name) {
    if (!name) return nullptr;
#define BB_CAP_PROC(symbol, function) \
    if (std::strcmp(name, "vk" #symbol) == 0) \
        return reinterpret_cast<void*>(function);
    BB_CAP_PROC(GetPhysicalDeviceFeatures2, GetFeatures2)
    BB_CAP_PROC(GetPhysicalDeviceFeatures2KHR, GetFeatures2)
    BB_CAP_PROC(GetPhysicalDeviceFeatures, GetFeatures)
    BB_CAP_PROC(GetPhysicalDeviceProperties2, GetProperties2)
    BB_CAP_PROC(GetPhysicalDeviceProperties2KHR, GetProperties2)
    BB_CAP_PROC(GetPhysicalDeviceProperties, GetProperties)
    BB_CAP_PROC(GetPhysicalDeviceFormatProperties2, GetFormatProperties2)
    BB_CAP_PROC(GetPhysicalDeviceFormatProperties2KHR, GetFormatProperties2)
    BB_CAP_PROC(GetPhysicalDeviceFormatProperties, GetFormatProperties)
    BB_CAP_PROC(GetPhysicalDeviceMemoryProperties, GetMemoryProperties)
    BB_CAP_PROC(GetPhysicalDeviceMemoryProperties2, GetMemoryProperties2)
    BB_CAP_PROC(GetPhysicalDeviceMemoryProperties2KHR, GetMemoryProperties2)
    BB_CAP_PROC(GetPhysicalDeviceQueueFamilyProperties, GetQueueFamilies)
    BB_CAP_PROC(GetPhysicalDeviceQueueFamilyProperties2, GetQueueFamilies2)
    BB_CAP_PROC(GetPhysicalDeviceQueueFamilyProperties2KHR, GetQueueFamilies2)
    BB_CAP_PROC(EnumerateDeviceExtensionProperties, EnumerateExtensions)
    BB_CAP_PROC(GetPhysicalDeviceImageFormatProperties2, GetImageFormat2)
    BB_CAP_PROC(GetPhysicalDeviceImageFormatProperties2KHR, GetImageFormat2)
    BB_CAP_PROC(GetPhysicalDeviceToolProperties, GetToolProperties)
    BB_CAP_PROC(GetPhysicalDeviceToolPropertiesEXT, GetToolProperties)
#undef BB_CAP_PROC
    return nullptr;
}

} // namespace MetalCaps
