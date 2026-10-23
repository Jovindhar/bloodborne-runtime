// The render adapter's render passes and draw state (docs/RENDER_ADAPTER.md, A4).
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstring>
#include <type_traits>

#include "common/types.h"
#include "video_core/renderer_backend/state.h"

namespace VideoCore::Backend {

/// Laid out like VkViewport (checked in renderer_vulkan/vk_state.h).
struct Viewport {
    float x{}, y{}, width{}, height{}, min_depth{}, max_depth{};

    bool operator==(const Viewport&) const = default;
};

/// Laid out like VkRect2D.
struct Rect2D {
    s32 x{}, y{};
    u32 width{}, height{};

    bool operator==(const Rect2D&) const = default;
};

struct StencilOps {
    StencilOp fail_op{};
    StencilOp pass_op{};
    StencilOp depth_fail_op{};
    CompareOp compare_op{};

    bool operator==(const StencilOps&) const = default;
};

/// A render pass's color or depth-stencil target. `image_view` is the backend's view
/// (NativeHandle) and `image_layout` its own state of the image (the Vulkan backend's layout).
/// A target with a clear takes it as its load action; others load.
struct RenderAttachment {
    u64 image_view;
    u32 image_layout;
    std::array<u32, 4> clear_value;
    union {
        u32 is_clear;
        struct {
            bool has_depth;
            bool depth_clear;
            bool has_stencil;
            bool stencil_clear;
        };
    };
};
static_assert(std::has_unique_object_representations_v<RenderAttachment>);

struct RenderState {
    std::array<RenderAttachment, 8> color_attachments;
    RenderAttachment depth_stencil_attachment;
    u16 width;
    u16 height;
    u16 num_layers;
    u16 num_color_attachments;

    bool operator==(const RenderState& other) const noexcept {
        return std::memcmp(this, &other, sizeof(RenderState)) == 0;
    }

    /// bbport: the same targets loaded instead of cleared.
    RenderState WithoutClears() const noexcept {
        RenderState state = *this;
        for (auto& attachment : state.color_attachments) {
            attachment.clear_value = {};
            attachment.is_clear = 0;
        }
        state.depth_stencil_attachment.clear_value = {};
        state.depth_stencil_attachment.depth_clear = false;
        state.depth_stencil_attachment.stencil_clear = false;
        return state;
    }
};
static_assert(std::has_unique_object_representations_v<RenderState>);

} // namespace VideoCore::Backend
