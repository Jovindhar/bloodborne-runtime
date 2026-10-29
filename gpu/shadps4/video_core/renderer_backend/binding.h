// The render adapter's resource bindings: flat, typed slots (docs/RENDER_ADAPTER.md, A4).
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"

namespace VideoCore::Backend {

/// What a slot holds. The values are Vulkan's descriptor types (renderer_vulkan/vk_state.h).
enum class BindingKind : s32 {
    Sampler = 0,
    CombinedImageSampler = 1, ///< ImageBinding with both a sampler and a view
    SampledImage = 2,
    StorageImage = 3,
    UniformBuffer = 6,
    StorageBuffer = 7,
};

/// The image state value any use may bind with (Vulkan's general layout; backends without
/// layouts ignore it).
constexpr u32 GeneralLayout = 1;

/// A binding's range to the end of the buffer (Vulkan's VK_WHOLE_SIZE).
constexpr u64 WholeRange = ~0ull;

/// A buffer range in a slot: the backend's buffer (NativeHandle), offset and size in bytes.
/// Laid out like VkDescriptorBufferInfo, so the Vulkan backend reads it in place.
struct BufferBinding {
    u64 buffer{};
    u64 offset{};
    u64 range{};
};

/// A sampler or an image view in a slot (NativeHandle values). `layout` is the backend's own
/// state of the image when bound (the Vulkan backend's image layout); laid out like
/// VkDescriptorImageInfo.
struct ImageBinding {
    u64 sampler{};
    u64 view{};
    u32 layout{};
};

/// `count` consecutive elements bound at `slot`: `buffers` for buffer kinds, `images` for the
/// others.
struct SlotWrite {
    u32 slot{};
    u32 count{};
    BindingKind kind{BindingKind::StorageBuffer};
    const BufferBinding* buffers{};
    const ImageBinding* images{};
};

} // namespace VideoCore::Backend
