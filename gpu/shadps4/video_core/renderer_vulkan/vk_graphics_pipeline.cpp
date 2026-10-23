// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <utility>
#include <boost/container/small_vector.hpp>

#include "common/serdes.h"
#include "common/assert.h"
#include "shader_recompiler/backend/spirv/emit_spirv_discard_frag.h"
#include "shader_recompiler/backend/spirv/emit_spirv_quad_rect.h"
#include "video_core/renderer_vulkan/vk_backend_device.h"
#include "video_core/renderer_vulkan/liverpool_to_vk.h"
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"

namespace Vulkan {

using Shader::Backend::SPIRV::AuxShaderType;

static constexpr std::array LogicalStageToStage = {
    Backend::ShaderStage::Fragment,
    Backend::ShaderStage::TessellationControl,
    Backend::ShaderStage::TessellationEvaluation,
    Backend::ShaderStage::Vertex,
    Backend::ShaderStage::Geometry,
    Backend::ShaderStage::Compute,
};

AttachmentBlend AttachmentBlendState(const GraphicsPipelineKey& key, u32 i) {
    const auto& control = key.blend_controls[i];

    const auto src_color = LiverpoolToVK::BlendFactor(control.color_src_factor);
    const auto dst_color = LiverpoolToVK::BlendFactor(control.color_dst_factor);
    const auto color_blend = LiverpoolToVK::BlendOp(control.color_func);

    const auto src_alpha = control.separate_alpha_blend
                               ? LiverpoolToVK::BlendFactor(control.alpha_src_factor)
                               : src_color;
    const auto dst_alpha = control.separate_alpha_blend
                               ? LiverpoolToVK::BlendFactor(control.alpha_dst_factor)
                               : dst_color;
    const auto alpha_blend =
        control.separate_alpha_blend ? LiverpoolToVK::BlendOp(control.alpha_func) : color_blend;

    // Vulkan ignores blend factors for min/max, but a factor that zeroes one operand
    // makes the operation collapse to a plain selection: min(s, 0) is 0 and max(s, 0)
    // is s for normalized alpha. Rewrite those to the equivalent add so the result is
    // exact instead of leaving the other operand to survive.
    auto eff_src_alpha = src_alpha;
    auto eff_dst_alpha = dst_alpha;
    auto eff_alpha_blend = alpha_blend;
    if (alpha_blend == Backend::BlendOp::Min || alpha_blend == Backend::BlendOp::Max) {
        const bool takes_max = alpha_blend == Backend::BlendOp::Max;
        if (src_alpha == Backend::BlendFactor::One && dst_alpha == Backend::BlendFactor::Zero) {
            eff_alpha_blend = Backend::BlendOp::Add;
            eff_src_alpha = takes_max ? Backend::BlendFactor::One : Backend::BlendFactor::Zero;
            eff_dst_alpha = Backend::BlendFactor::Zero;
        } else if (src_alpha == Backend::BlendFactor::Zero && dst_alpha == Backend::BlendFactor::One) {
            eff_alpha_blend = Backend::BlendOp::Add;
            eff_src_alpha = Backend::BlendFactor::Zero;
            eff_dst_alpha = takes_max ? Backend::BlendFactor::One : Backend::BlendFactor::Zero;
        }
    }

    AttachmentBlend blend{
        .enable = bool(control.enable),
        .src_color = src_color,
        .dst_color = dst_color,
        .src_alpha = eff_src_alpha,
        .dst_alpha = eff_dst_alpha,
        .color_op = color_blend,
        .alpha_op = eff_alpha_blend,
    };
    const auto color_scaled_min_max =
        (color_blend == Backend::BlendOp::Min || color_blend == Backend::BlendOp::Max) &&
        (src_color != Backend::BlendFactor::One || dst_color != Backend::BlendFactor::One) &&
        !key.color_buffers[i].blend_self_scale;
    const auto alpha_scaled_min_max =
        (eff_alpha_blend == Backend::BlendOp::Min || eff_alpha_blend == Backend::BlendOp::Max) &&
        (eff_src_alpha != Backend::BlendFactor::One || eff_dst_alpha != Backend::BlendFactor::One);
    blend.unimplemented_min_max = color_scaled_min_max || alpha_scaled_min_max;

    // The shader squares its color output for this attachment (see PsColorBuffer), so the
    // factors must not scale the operands again.
    if (key.color_buffers[i].blend_self_scale) {
        blend.self_scaled = true;
        blend.src_color = Backend::BlendFactor::One;
        blend.dst_color = Backend::BlendFactor::One;
    }

    // On GCN GPU there is an additional mask which allows to control color components exported
    // from a pixel shader. A situation possible, when the game may mask out the alpha channel,
    // while it is still need to be used in blending ops. For such cases, HW will default alpha
    // to 1 and perform the blending, while shader normally outputs 0 in the last component.
    // Unfortunatelly, Vulkan doesn't provide any control on blend inputs, so below we detecting
    // such cases and override alpha value in order to emulate HW behaviour.
    const auto has_alpha_masked_out =
        (key.cb_shader_mask.GetMask(i) & AmdGpu::ColorBufferMask::ComponentA) == 0;
    const auto has_src_alpha_in_src_blend = src_color == Backend::BlendFactor::SrcAlpha ||
                                            src_color == Backend::BlendFactor::OneMinusSrcAlpha;
    const auto has_src_alpha_in_dst_blend = dst_color == Backend::BlendFactor::SrcAlpha ||
                                            dst_color == Backend::BlendFactor::OneMinusSrcAlpha;
    if (has_alpha_masked_out && has_src_alpha_in_src_blend) {
        blend.src_color = src_color == Backend::BlendFactor::SrcAlpha ? Backend::BlendFactor::One
                                                                   : Backend::BlendFactor::Zero; // 1-A
    }
    if (has_alpha_masked_out && has_src_alpha_in_dst_blend) {
        blend.dst_color = dst_color == Backend::BlendFactor::SrcAlpha ? Backend::BlendFactor::One
                                                                   : Backend::BlendFactor::Zero; // 1-A
    }
    return blend;
}

// The pipeline's own auxiliary stages (rect and quad lists, clip-distance emulation), made
// through the render adapter. Released once the pipeline is made: it holds them.
static Backend::ShaderHandle AuxShader(const Instance& instance, std::span<const u32> spirv,
                                       std::span<Backend::ShaderHandle> made, u32& num_made) {
    const auto shader = BackendDevice::From(instance).CreateShader(spirv, {});
    ASSERT_MSG(shader.handle, "Failed to compile an auxiliary SPIR-V shader");
    made[num_made++] = shader.handle;
    return shader.handle;
}

GraphicsPipeline::GraphicsPipeline(
    const Instance& instance, const Shader::Profile& profile, const GraphicsPipelineKey& key_,
    std::span<const Shader::Info*, MaxShaderStages> infos,
    std::span<const Shader::RuntimeInfo, MaxShaderStages> runtime_infos,
    std::optional<const Shader::Gcn::FetchShaderData> fetch_shader_,
    std::span<const Backend::ShaderHandle> shaders, SerializationSupport& sdata, bool preloading)
    : Pipeline{instance, profile}, key{key_},
      fetch_shader{std::move(fetch_shader_)} {
    auto& device = BackendDevice::From(instance);
    const auto& caps = device.GetCapabilities();
    std::ranges::copy(infos, stages.begin());

    // The stages' resources, in stage order.
    BindingSlots slots;
    for (const auto* stage : stages) {
        if (!stage) {
            continue;
        }
        const auto visible = key.fixed_quad_outer &&
                                     stage->sw_stage == Shader::SwStage::TessellationEval
                                 ? Backend::ShaderStage::Vertex
                                 : LogicalStageToStage[u32(stage->sw_stage)];
        AppendBindingSlots(*stage, visible, slots);
    }

    if (!preloading && !caps.dynamic_vertex_input) {
        VertexInputs<AmdGpu::Buffer> guest_buffers;
        const auto& vs_info = runtime_infos[u32(Shader::SwStage::Vertex)].sw.vs;
        GetVertexInputs(sdata.vertex_attributes, sdata.vertex_bindings, sdata.divisors,
                        guest_buffers, vs_info.step_rate_0, vs_info.step_rate_1);
    }
    // The vertex input as the shader cache stores it (Vulkan's descriptions), in the adapter's
    // terms.
    VertexInputs<Backend::VertexBinding> vertex_bindings;
    VertexInputs<Backend::VertexAttribute> vertex_attributes;
    for (const auto& stored : sdata.vertex_bindings) {
        Backend::VertexBinding binding{
            .binding = stored.binding,
            .stride = stored.stride,
            .input_rate = stored.inputRate == vk::VertexInputRate::eInstance
                              ? Backend::VertexInputRate::Instance
                              : Backend::VertexInputRate::Vertex,
        };
        for (const auto& divisor : sdata.divisors) {
            if (divisor.binding == stored.binding) {
                binding.divisor = divisor.divisor;
            }
        }
        vertex_bindings.push_back(binding);
    }
    for (const auto& stored : sdata.vertex_attributes) {
        vertex_attributes.push_back({
            .location = stored.location,
            .binding = stored.binding,
            .format = FromVk(stored.format),
            .offset = stored.offset,
        });
    }

    if (!preloading) {
        const auto& fs_info = runtime_infos[u32(Shader::SwStage::Fragment)].hw.fs;
        sdata.multisampling = {
            .rasterizationSamples = LiverpoolToVK::NumSamples(
                key.num_samples,
                vk::SampleCountFlags{caps.color_sample_counts & caps.depth_sample_counts}),
            .sampleShadingEnable =
                fs_info.addr_flags.persp_sample_ena || fs_info.addr_flags.linear_sample_ena,
        };
    }

    const bool is_rect_list = key.prim_type == AmdGpu::PrimitiveType::RectList;
    const bool is_quad_list = key.prim_type == AmdGpu::PrimitiveType::QuadList;
    boost::container::static_vector<Backend::PipelineStage, MaxShaderStages> pipeline_stages;
    std::array<Backend::ShaderHandle, 3> aux{};
    u32 num_aux = 0;
    auto stage = u32(Shader::SwStage::Vertex);
    if (key.fixed_quad_outer) {
        const auto domain = u32(Shader::SwStage::TessellationEval);
        pipeline_stages.push_back({Backend::ShaderStage::Vertex, shaders[domain]});
    } else if (infos[stage]) {
        pipeline_stages.push_back({Backend::ShaderStage::Vertex, shaders[stage]});
    }
    stage = u32(Shader::SwStage::Geometry);
    if (infos[stage]) {
        pipeline_stages.push_back({Backend::ShaderStage::Geometry, shaders[stage]});
    }
    stage = u32(Shader::SwStage::TessellationControl);
    if (infos[stage] && !key.fixed_quad_outer) {
        pipeline_stages.push_back({Backend::ShaderStage::TessellationControl, shaders[stage]});
    } else if (is_rect_list || is_quad_list) {
        const auto type = is_quad_list ? AuxShaderType::QuadListTCS : AuxShaderType::RectListTCS;
        if (!preloading) {
            const auto& fs_info = runtime_infos[u32(Shader::SwStage::Fragment)].hw.fs;
            sdata.tcs = Shader::Backend::SPIRV::EmitAuxilaryTessShader(type, fs_info);
        }
        pipeline_stages.push_back(
            {Backend::ShaderStage::TessellationControl, AuxShader(instance, sdata.tcs, aux, num_aux)});
    }
    stage = u32(Shader::SwStage::TessellationEval);
    if (infos[stage] && !key.fixed_quad_outer) {
        pipeline_stages.push_back({Backend::ShaderStage::TessellationEvaluation, shaders[stage]});
    } else if (is_rect_list || is_quad_list) {
        if (!preloading) {
            const auto& fs_info = runtime_infos[u32(Shader::SwStage::Fragment)].hw.fs;
            sdata.tes = Shader::Backend::SPIRV::EmitAuxilaryTessShader(
                AuxShaderType::PassthroughTES, fs_info);
        }
        pipeline_stages.push_back(
            {Backend::ShaderStage::TessellationEvaluation, AuxShader(instance, sdata.tes, aux, num_aux)});
    }
    stage = u32(Shader::SwStage::Fragment);
    if (infos[stage]) {
        pipeline_stages.push_back({Backend::ShaderStage::Fragment, shaders[stage]});
    } else if (runtime_infos[u32(Shader::SwStage::Fragment)].hw.fs.clip_distance_emulation) {
        if (!preloading) {
            const auto& vs = runtime_infos[static_cast<u32>(Shader::SwStage::Vertex)].hw.vs;
            sdata.fragment = Shader::Backend::SPIRV::EmitDiscardFragmentShader(vs.outputs);
        }
        pipeline_stages.push_back(
            {Backend::ShaderStage::Fragment, AuxShader(instance, sdata.fragment, aux, num_aux)});
    }

    const auto color_counts = vk::SampleCountFlags{caps.color_sample_counts};
    std::array<Backend::ColorTarget, Backend::MaxColorTargets> targets{};
    for (u32 i = 0; i < key.num_color_attachments; i++) {
        auto& target = targets[i];
        // bbport: a slot the pass leaves empty (the motion vector target is slot 7 after a
        // 6-target G-buffer) has no format: Metal requires the pipeline's to be invalid there.
        if (key.mrt_mask & (1u << i)) {
            const auto& col_buf = key.color_buffers[i];
            target.format = device.SupportedFormat(
                LiverpoolToVK::SurfaceFormat(col_buf.data_format, col_buf.num_format),
                Backend::ImageUse::ColorAttachment);
        }
        target.samples = key.color_samples[i]
                             ? u32(LiverpoolToVK::NumSamples(key.color_samples[i], color_counts))
                             : 1;
        const AttachmentBlend blend = AttachmentBlendState(key, i);
        if (blend.unimplemented_min_max) {
            LOG_WARNING(
                Render_Vulkan,
                "Unimplemented use of min/max blend op with blend factor not equal to one.");
        }
        if (blend.self_scaled) {
            LOG_WARNING(
                Render_Vulkan,
                "Emulating scaled min/max blend with squared shader output on attachment {}", i);
        }
        target.blend = blend.enable;
        target.src_color = blend.src_color;
        target.dst_color = blend.dst_color;
        target.src_alpha = blend.src_alpha;
        target.dst_alpha = blend.dst_alpha;
        target.color_op = blend.color_op;
        target.alpha_op = blend.alpha_op;
        target.write_mask = key.write_masks[i];
    }

    const auto depth_format =
        device.SupportedFormat(LiverpoolToVK::DepthFormat(key.z_format, key.stencil_format),
                               Backend::ImageUse::DepthStencilAttachment);
    const Backend::GraphicsPipelineDesc desc{
        .stages = pipeline_stages,
        .layout = {.slots = slots, .push_constant_size = sizeof(Shader::PushData)},
        .vertex_bindings = vertex_bindings,
        .vertex_attributes = vertex_attributes,
        .topology = LiverpoolToVK::PrimitiveType(key.prim_type),
        .patch_control_points = is_rect_list ? 3U : (is_quad_list ? 4U : key.patch_control_points),
        .polygon_mode = LiverpoolToVK::PolygonMode(key.polygon_mode),
        .depth_clamp = bool(key.depth_clamp_enable),
        .depth_clip = bool(key.depth_clip_enable),
        .provoking_vertex_last = key.provoking_vtx_last != AmdGpu::ProvokingVtxLast::First,
        .clip_negative_one_to_one = key.clip_space == AmdGpu::ClipSpace::MinusWToW,
        .samples = u32(sdata.multisampling.rasterizationSamples),
        .sample_shading = bool(sdata.multisampling.sampleShadingEnable),
        .color_targets = std::span{targets.data(), key.num_color_attachments},
        .depth_format = key.z_format != AmdGpu::DepthBuffer::ZFormat::Invalid
                            ? depth_format
                            : Backend::Format::Undefined,
        .stencil_format = key.stencil_format != AmdGpu::DepthBuffer::StencilFormat::Invalid
                              ? depth_format
                              : Backend::Format::Undefined,
        .depth_samples = u32(LiverpoolToVK::NumSamples(
            key.depth_samples, vk::SampleCountFlags{caps.depth_sample_counts})),
        .logic_op_enable = key.logic_op != AmdGpu::ColorControl::LogicOp::Copy,
        .logic_op = LiverpoolToVK::LogicOp(key.logic_op),
    };
    const auto debug_str = GetDebugString();
    const auto created = device.CreateGraphicsPipeline(desc, debug_str);
    for (u32 i = 0; i < num_aux; ++i) {
        device.DestroyShader(aux[i]);
    }
    if (!created.handle) {
        if (preloading) {
            // bbport: a pipeline from the cache the driver rejects is a damaged cache entry: the
            // cache is rebuilt (PipelineCache::WarmUp) instead of stopping the game at every
            // start.
            throw Serialization::CorruptData{"cached graphics pipeline rejected by the driver"};
        }
        // bbport: the pipeline stays empty: its draws are skipped (PipelineCache returns no
        // pipeline) instead of ending the game.
        LOG_ERROR(Render_Vulkan, "Graphics pipeline {} rejected; its draws are skipped", debug_str);
        return;
    }
    allocation = created;
}

GraphicsPipeline::~GraphicsPipeline() = default;

namespace {
// The vertex input as Vulkan's descriptions (pipeline creation, dynamic state) or the render
// adapter's (the rasterizer's draws, renderer_backend/commands.h).
template <typename Attribute>
Attribute MakeAttribute(u32 location, VideoCore::Backend::Format format) {
    if constexpr (std::is_same_v<Attribute, VideoCore::Backend::VertexAttribute>) {
        return {.location = location, .binding = location, .format = format, .offset = 0};
    } else {
        return {.location = location, .binding = location, .format = ToVk(format), .offset = 0};
    }
}
template <typename Binding>
Binding MakeBinding(u32 binding, u32 stride, bool per_instance, u32 divisor) {
    if constexpr (std::is_same_v<Binding, VideoCore::Backend::VertexBinding>) {
        return {.binding = binding,
                .stride = stride,
                .input_rate = per_instance ? VideoCore::Backend::VertexInputRate::Instance
                                           : VideoCore::Backend::VertexInputRate::Vertex,
                .divisor = divisor};
    } else {
        Binding b{.binding = binding,
                  .stride = stride,
                  .inputRate = per_instance ? vk::VertexInputRate::eInstance
                                            : vk::VertexInputRate::eVertex};
        if constexpr (std::is_same_v<Binding, vk::VertexInputBindingDescription2EXT>) {
            b.divisor = divisor;
        }
        return b;
    }
}
} // namespace

template <typename Attribute, typename Binding>
void GraphicsPipeline::GetVertexInputs(
    VertexInputs<Attribute>& attributes, VertexInputs<Binding>& bindings,
    VertexInputs<vk::VertexInputBindingDivisorDescriptionEXT>& divisors,
    VertexInputs<AmdGpu::Buffer>& guest_buffers, u32 step_rate_0, u32 step_rate_1,
    std::span<const AmdGpu::Buffer> sharps) const {
    using InstanceIdType = Shader::Gcn::VertexAttribute::InstanceIdType;
    if (key.fixed_quad_outer) {
        const u32 count = stages[u32(Shader::SwStage::Vertex)]->fixed_quad_output_count;
        for (u32 location = 0; location <= count; ++location) {
            const bool coord = location == count;
            attributes.push_back(MakeAttribute<Attribute>(
                location, coord ? VideoCore::Backend::Format::R32G32Sfloat
                                : VideoCore::Backend::Format::R32G32B32A32Sfloat));
            bindings.push_back(MakeBinding<Binding>(location, coord ? 8u : 16u, !coord, 1));
            guest_buffers.emplace_back();
        }
        return;
    }
    if (!fetch_shader || fetch_shader->attributes.empty()) {
        return;
    }
    const auto& vs_info = GetStage(Shader::SwStage::Vertex);
    const bool given = sharps.size() == fetch_shader->attributes.size();
    for (u32 index = 0; const auto& attrib : fetch_shader->attributes) {
        const auto step_rate = attrib.GetStepRate();
        const auto buffer = given ? sharps[index] : attrib.GetSharp(vs_info);
        ++index;
        attributes.push_back(MakeAttribute<Attribute>(
            attrib.semantic,
            LiverpoolToVK::SurfaceFormat(buffer.GetDataFmt(), buffer.GetNumberFmt())));
        const u32 divisor = step_rate == InstanceIdType::OverStepRate0
                                ? step_rate_0
                                : (step_rate == InstanceIdType::OverStepRate1 ? step_rate_1 : 1);
        bindings.push_back(MakeBinding<Binding>(attrib.semantic, buffer.GetStride(),
                                                step_rate != InstanceIdType::None, divisor));
        if constexpr (std::is_same_v<Binding, vk::VertexInputBindingDescription>) {
            if (step_rate == InstanceIdType::None) {
                guest_buffers.emplace_back(buffer);
                continue;
            }
            divisors.push_back(vk::VertexInputBindingDivisorDescriptionEXT{
                .binding = attrib.semantic,
                .divisor = divisor,
            });
        }
        guest_buffers.emplace_back(buffer);
    }
}

// Declare templated GetVertexInputs for necessary types.
template void GraphicsPipeline::GetVertexInputs(
    VertexInputs<vk::VertexInputAttributeDescription>& attributes,
    VertexInputs<vk::VertexInputBindingDescription>& bindings,
    VertexInputs<vk::VertexInputBindingDivisorDescriptionEXT>& divisors,
    VertexInputs<AmdGpu::Buffer>& guest_buffers, u32 step_rate_0, u32 step_rate_1,
    std::span<const AmdGpu::Buffer> sharps) const;
template void GraphicsPipeline::GetVertexInputs(
    VertexInputs<VideoCore::Backend::VertexAttribute>& attributes,
    VertexInputs<VideoCore::Backend::VertexBinding>& bindings,
    VertexInputs<vk::VertexInputBindingDivisorDescriptionEXT>& divisors,
    VertexInputs<AmdGpu::Buffer>& guest_buffers, u32 step_rate_0, u32 step_rate_1,
    std::span<const AmdGpu::Buffer> sharps) const;


} // namespace Vulkan
