// The render adapter's pipelines (docs/RENDER_ADAPTER.md, A5).
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>

#include "common/types.h"
#include "video_core/renderer_backend/binding.h"
#include "video_core/renderer_backend/commands.h"
#include "video_core/renderer_backend/resource.h"
#include "video_core/renderer_backend/state.h"

namespace VideoCore::Backend {

/// Shader stages, as bits (Vulkan's, checked in renderer_vulkan/vk_state.h).
enum class ShaderStage : u32 {
    None = 0,
    Vertex = 1u << 0,
    TessellationControl = 1u << 1,
    TessellationEvaluation = 1u << 2,
    Geometry = 1u << 3,
    Fragment = 1u << 4,
    Compute = 1u << 5,
};
constexpr ShaderStage operator|(ShaderStage a, ShaderStage b) noexcept {
    return static_cast<ShaderStage>(static_cast<u32>(a) | static_cast<u32>(b));
}

struct PipelineStage {
    ShaderStage stage{};
    ShaderHandle shader;
};

/// A slot of a pipeline's binding layout. Slots are numbered in order; a slot of `count`
/// elements takes that many numbers (SlotWrite::slot is the first).
struct BindingSlot {
    BindingKind kind{BindingKind::StorageBuffer};
    u32 count{1};
    ShaderStage stages{}; ///< the stages that use it
};

/// What a pipeline binds: its slots, and push constants of `push_constant_size` bytes visible to
/// `push_stages` (None: all its stages).
struct BindingLayout {
    std::span<const BindingSlot> slots;
    u32 push_constant_size{};
    ShaderStage push_stages{};
};

constexpr u32 MaxColorTargets = 8;

/// A color target of a graphics pipeline: its format (Undefined for a slot the pass leaves
/// empty), sample count, blending and written components.
struct ColorTarget {
    Format format{Format::Undefined};
    u32 samples{1};
    bool blend{};
    BlendFactor src_color{}, dst_color{}, src_alpha{}, dst_alpha{};
    BlendOp color_op{}, alpha_op{};
    ColorWriteMask write_mask{ColorWriteMask::All};
};

/// A graphics pipeline. A guest pipeline's viewports, scissors, depth, stencil, culling, depth
/// bias and blend constants are set per draw (the rasterizer's draw state). A host pass (the
/// renderer's own: presentation, upscaling) sets one viewport and one scissor per draw and has
/// no depth or stencil test; its culling is `cull_mode` and `front_face`.
struct GraphicsPipelineDesc {
    bool host_pass{};
    std::span<const PipelineStage> stages;
    BindingLayout layout;
    /// The vertex input, unless the device sets it per draw (Capabilities::dynamic_vertex_input).
    std::span<const VertexBinding> vertex_bindings;
    std::span<const VertexAttribute> vertex_attributes;
    PrimitiveTopology topology{};
    u32 patch_control_points{}; ///< tessellation
    PolygonMode polygon_mode{};
    bool depth_clamp{};
    bool depth_clip{true};
    bool provoking_vertex_last{};
    bool clip_negative_one_to_one{}; ///< clip space depth from -w to w, not 0 to w
    u32 samples{1};
    bool sample_shading{};
    std::span<const ColorTarget> color_targets;
    Format depth_format{Format::Undefined};   ///< Undefined: no depth target
    Format stencil_format{Format::Undefined}; ///< Undefined: no stencil target
    u32 depth_samples{1};
    bool logic_op_enable{};
    LogicOp logic_op{};
    CullMode cull_mode{}; ///< host passes
    FrontFace front_face{};
};

/// A compute pipeline. Guest shaders run with the guest's subgroup size where the device can
/// require it; host passes (`host_pass`) with the device's own.
struct ComputePipelineDesc {
    ShaderHandle shader;
    BindingLayout layout;
    bool host_pass{};
};

/// A pipeline the device made; an empty handle when the driver rejected it.
struct PipelineAllocation {
    PipelineHandle handle;
    u64 native{};          ///< Device::NativeHandle(handle)
    const void* binding{}; ///< the backend's state for CommandRecorder::BindResources
};

} // namespace VideoCore::Backend
