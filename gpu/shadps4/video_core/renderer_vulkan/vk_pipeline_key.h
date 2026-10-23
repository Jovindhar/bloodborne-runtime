// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The pipeline keys, free of Vulkan objects: every backend's pipelines key on them (the Vulkan
// pipelines, the Metal pipeline states of renderer_metal/mtl_pso_cache.h). Formats and state are
// the render adapter's vocabulary (renderer_backend/format.h, state.h), with Vulkan's values, so
// the layout, the hash and the serialized cache are unchanged; no handles, no instance or
// scheduler. The split follows Wooloomooloo2/bloodborne_mac_port (0b50589, GPL-2.0-or-later).

#include <array>
#include <cstddef>
#include <cstring>
#include <xxhash.h>

#include "shader_recompiler/runtime_info.h"
#include "video_core/amdgpu/regs_color.h"
#include "video_core/amdgpu/regs_depth.h"
#include "video_core/amdgpu/regs_primitive.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/renderer_vulkan/vk_format.h"
#include "video_core/renderer_vulkan/vk_state.h"

namespace Serialization {
struct Archive;
}

namespace Vulkan {

static constexpr u32 MaxShaderStages = static_cast<u32>(Shader::SwStage::NumLogicalStages);
static constexpr u32 MaxVertexBufferCount = 32;

struct GraphicsPipelineKey {
    std::array<size_t, MaxShaderStages> stage_hashes;
    std::array<Backend::Format, MaxVertexBufferCount> vertex_buffer_formats;
    u32 patch_control_points;
    u8 fixed_quad_outer;
    u32 num_color_attachments;
    std::array<Shader::PsColorBuffer, AmdGpu::NUM_COLOR_BUFFERS> color_buffers;
    std::array<AmdGpu::BlendControl, AmdGpu::NUM_COLOR_BUFFERS> blend_controls;
    std::array<Backend::ColorWriteMask, AmdGpu::NUM_COLOR_BUFFERS> write_masks;
    AmdGpu::ColorBufferMask cb_shader_mask;
    AmdGpu::ColorControl::LogicOp logic_op;
    u8 num_samples;
    u8 depth_samples;
    std::array<u8, AmdGpu::NUM_COLOR_BUFFERS> color_samples;
    u32 mrt_mask;
    u32 motion_vectors = 0;
    struct {
        AmdGpu::DepthBuffer::ZFormat z_format : 2;
        AmdGpu::DepthBuffer::StencilFormat stencil_format : 1;
        u32 depth_clamp_enable : 1;
    };
    struct {
        AmdGpu::PrimitiveType prim_type : 5;
        AmdGpu::PolygonMode polygon_mode : 2;
        AmdGpu::ClipSpace clip_space : 1;
        AmdGpu::ProvokingVtxLast provoking_vtx_last : 1;
        u32 depth_clip_enable : 1;
    };

    GraphicsPipelineKey() {
        std::memset(this, 0, sizeof(*this));
    }

    bool operator==(const GraphicsPipelineKey& key) const noexcept {
        return std::memcmp(this, &key, sizeof(key)) == 0;
    }

    void Serialize(Serialization::Archive& ar) const;
    bool Deserialize(Serialization::Archive& ar);
};

/// The blend state of color attachment `i` as the key gives it, with the GCN rules applied
/// (alpha-masked exports, min/max with zeroing factors, self-scaled outputs). Shared by the
/// backends so they blend alike.
struct AttachmentBlend {
    bool enable{};
    Backend::BlendFactor src_color{}, dst_color{}, src_alpha{}, dst_alpha{};
    Backend::BlendOp color_op{}, alpha_op{};
    bool unimplemented_min_max{}; ///< min/max with factors other than one (not emulated)
    bool self_scaled{};           ///< the shader squares its output (blend_self_scale)
};
AttachmentBlend AttachmentBlendState(const GraphicsPipelineKey& key, u32 i);

struct ComputePipelineKey {
    size_t value;

    friend bool operator==(const ComputePipelineKey& lhs, const ComputePipelineKey& rhs) {
        return lhs.value == rhs.value;
    }
    friend bool operator!=(const ComputePipelineKey& lhs, const ComputePipelineKey& rhs) {
        return !(lhs == rhs);
    }

    void Serialize(Serialization::Archive& ar) const;
    bool Deserialize(Serialization::Archive& ar);
};

} // namespace Vulkan

template <>
struct std::hash<Vulkan::GraphicsPipelineKey> {
    std::size_t operator()(const Vulkan::GraphicsPipelineKey& key) const noexcept {
        return XXH3_64bits(&key, sizeof(key));
    }
};

template <>
struct std::hash<Vulkan::ComputePipelineKey> {
    std::size_t operator()(const Vulkan::ComputePipelineKey& key) const noexcept {
        return std::hash<size_t>{}(key.value);
    }
};
