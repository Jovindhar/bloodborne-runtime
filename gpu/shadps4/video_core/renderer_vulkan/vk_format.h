// The Vulkan backend's view of the render adapter's formats (docs/RENDER_ADAPTER.md, step A1).
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "video_core/renderer_backend/format.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {

namespace Backend = VideoCore::Backend;

#define BB_BACKEND_FORMAT_CHECK(name, value)                                                       \
    static_assert(static_cast<s32>(vk::Format::e##name) == value, "VkFormat value of " #name);
BB_BACKEND_FORMATS(BB_BACKEND_FORMAT_CHECK)
#undef BB_BACKEND_FORMAT_CHECK
// Pipeline keys hold formats; the serialized shader cache depends on their size.
static_assert(sizeof(Backend::Format) == sizeof(vk::Format));

// The values are Vulkan's, so the conversions are casts.
[[nodiscard]] constexpr vk::Format ToVk(Backend::Format format) noexcept {
    return static_cast<vk::Format>(format);
}
[[nodiscard]] constexpr Backend::Format FromVk(vk::Format format) noexcept {
    return static_cast<Backend::Format>(format);
}

} // namespace Vulkan
