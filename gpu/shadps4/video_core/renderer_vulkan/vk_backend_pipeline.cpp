// The render adapter's Vulkan backend: pipelines (docs/RENDER_ADAPTER.md, A5).
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <boost/container/small_vector.hpp>
#include <boost/container/static_vector.hpp>

#include "common/assert.h"
#include "common/logging/log.h"
#include "video_core/renderer_vulkan/vk_backend_device.h"
#include "video_core/renderer_vulkan/vk_state.h"

namespace Vulkan {

namespace Backend = VideoCore::Backend;

namespace {
constexpr auto AllGraphicsStages =
    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eTessellationControl |
    vk::ShaderStageFlagBits::eTessellationEvaluation | vk::ShaderStageFlagBits::eGeometry |
    vk::ShaderStageFlagBits::eFragment;
} // namespace

std::unique_ptr<BackendDevice::PipelineRecord> BackendDevice::MakePipelineLayout(
    const Backend::BindingLayout& layout, vk::ShaderStageFlags push_stages, std::string_view name) {
    const vk::Device device = instance.GetDevice();
    boost::container::small_vector<vk::DescriptorSetLayoutBinding, 32> bindings;
    u32 binding{};
    for (const auto& slot : layout.slots) {
        bindings.push_back({
            .binding = binding,
            .descriptorType = vk::DescriptorType(slot.kind),
            .descriptorCount = slot.count,
            .stageFlags = ToVk(slot.stages),
        });
        binding += slot.count;
    }
    auto record = std::make_unique<PipelineRecord>();
    record->bind_point = push_stages == vk::ShaderStageFlagBits::eCompute
                             ? vk::PipelineBindPoint::eCompute
                             : vk::PipelineBindPoint::eGraphics;
    if (layout.push_stages != Backend::ShaderStage::None) {
        push_stages = ToVk(layout.push_stages);
    }
    record->push_stages = push_stages;
    record->push_descriptors = binding < instance.MaxPushDescriptors();
    const vk::DescriptorSetLayoutCreateInfo set_layout_ci = {
        .flags = record->push_descriptors
                     ? vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR
                     : vk::DescriptorSetLayoutCreateFlagBits{},
        .bindingCount = static_cast<u32>(bindings.size()),
        .pBindings = bindings.data(),
    };
    auto [set_result, set_layout] = device.createDescriptorSetLayoutUnique(set_layout_ci);
    ASSERT_MSG(set_result == vk::Result::eSuccess, "Failed to create descriptor set layout: {}",
               vk::to_string(set_result));
    record->set_layout = std::move(set_layout);

    const vk::PushConstantRange push_constants = {
        .stageFlags = push_stages,
        .offset = 0,
        .size = layout.push_constant_size,
    };
    const vk::DescriptorSetLayout set = *record->set_layout;
    const vk::PipelineLayoutCreateInfo layout_ci = {
        .setLayoutCount = 1U,
        .pSetLayouts = &set,
        .pushConstantRangeCount = layout.push_constant_size ? 1U : 0U,
        .pPushConstantRanges = &push_constants,
    };
    auto [layout_result, pipeline_layout] = device.createPipelineLayoutUnique(layout_ci);
    ASSERT_MSG(layout_result == vk::Result::eSuccess, "Failed to create pipeline layout: {}",
               vk::to_string(layout_result));
    record->layout = std::move(pipeline_layout);
    if (!name.empty()) {
        SetObjectName(device, *record->layout, "PipelineLayout {}", name);
    }
    return record;
}

Backend::PipelineAllocation BackendDevice::InsertPipeline(std::unique_ptr<PipelineRecord> record) {
    Backend::PipelineAllocation allocation{
        .native = reinterpret_cast<u64>(static_cast<VkPipeline>(*record->pipeline)),
        .binding = record.get(),
    };
    std::scoped_lock lock{objects_mutex};
    allocation.handle = pipelines.Insert(std::move(record));
    return allocation;
}

Backend::PipelineAllocation BackendDevice::CreateGraphicsPipeline(
    const Backend::GraphicsPipelineDesc& desc, std::string_view name) {
    const vk::Device device = instance.GetDevice();
    auto record = MakePipelineLayout(desc.layout, AllGraphicsStages, name);

    // The vertex input, when it is not set per draw. A divisor for each binding stepped per
    // instance.
    boost::container::small_vector<vk::VertexInputBindingDescription, 16> bindings;
    boost::container::small_vector<vk::VertexInputAttributeDescription, 16> attributes;
    boost::container::small_vector<vk::VertexInputBindingDivisorDescriptionEXT, 16> divisors;
    const bool host = desc.host_pass;
    const bool dynamic_vertex_input = !host && instance.IsVertexInputDynamicState();
    if (!dynamic_vertex_input) {
        for (const auto& binding : desc.vertex_bindings) {
            const bool per_instance = binding.input_rate == Backend::VertexInputRate::Instance;
            bindings.push_back({
                .binding = binding.binding,
                .stride = binding.stride,
                .inputRate =
                    per_instance ? vk::VertexInputRate::eInstance : vk::VertexInputRate::eVertex,
            });
            if (per_instance && binding.divisor != 1) {
                divisors.push_back({.binding = binding.binding, .divisor = binding.divisor});
            }
        }
        for (const auto& attribute : desc.vertex_attributes) {
            attributes.push_back({
                .location = attribute.location,
                .binding = attribute.binding,
                .format = ToVk(attribute.format),
                .offset = attribute.offset,
            });
        }
    }
    const vk::PipelineVertexInputDivisorStateCreateInfo divisor_state = {
        .vertexBindingDivisorCount = static_cast<u32>(divisors.size()),
        .pVertexBindingDivisors = divisors.data(),
    };
    const vk::PipelineVertexInputStateCreateInfo vertex_input_info = {
        .pNext = divisors.empty() ? nullptr : &divisor_state,
        .vertexBindingDescriptionCount = static_cast<u32>(bindings.size()),
        .pVertexBindingDescriptions = bindings.data(),
        .vertexAttributeDescriptionCount = static_cast<u32>(attributes.size()),
        .pVertexAttributeDescriptions = attributes.data(),
    };

    const vk::PipelineInputAssemblyStateCreateInfo input_assembly = {
        .topology = ToVk(desc.topology),
    };
    const vk::PipelineTessellationStateCreateInfo tessellation_state = {
        .patchControlPoints = desc.patch_control_points,
    };

    vk::StructureChain raster_chain = {
        vk::PipelineRasterizationStateCreateInfo{
            .depthClampEnable = !host && desc.depth_clamp &&
                                (!desc.depth_clip || instance.IsDepthClipEnableSupported()),
            .rasterizerDiscardEnable = false,
            .polygonMode = ToVk(desc.polygon_mode),
            .cullMode = host ? ToVk(desc.cull_mode) : vk::CullModeFlags{},
            .frontFace = host ? ToVk(desc.front_face) : vk::FrontFace{},
            .lineWidth = 1.0f,
        },
        vk::PipelineRasterizationProvokingVertexStateCreateInfoEXT{
            .provokingVertexMode = desc.provoking_vertex_last
                                       ? vk::ProvokingVertexModeEXT::eLastVertex
                                       : vk::ProvokingVertexModeEXT::eFirstVertex,
        },
        vk::PipelineRasterizationDepthClipStateCreateInfoEXT{
            .depthClipEnable = desc.depth_clip,
        },
    };
    if (host || !instance.IsProvokingVertexSupported()) {
        raster_chain.unlink<vk::PipelineRasterizationProvokingVertexStateCreateInfoEXT>();
    }
    if (host || !instance.IsDepthClipEnableSupported()) {
        raster_chain.unlink<vk::PipelineRasterizationDepthClipStateCreateInfoEXT>();
    }

    const vk::PipelineMultisampleStateCreateInfo multisampling = {
        .rasterizationSamples = vk::SampleCountFlagBits(desc.samples),
        .sampleShadingEnable = desc.sample_shading,
    };

    const vk::PipelineViewportDepthClipControlCreateInfoEXT clip_control = {
        .negativeOneToOne = desc.clip_negative_one_to_one,
    };
    const vk::PipelineViewportStateCreateInfo viewport_info = {
        .pNext = !host && instance.IsDepthClipControlSupported() ? &clip_control : nullptr,
        .viewportCount = host ? 1U : 0U,
        .scissorCount = host ? 1U : 0U,
    };

    // A host pass: one viewport and one scissor per draw.
    constexpr std::array host_dynamic_states = {vk::DynamicState::eViewport,
                                                vk::DynamicState::eScissor};
    boost::container::static_vector<vk::DynamicState, 32> dynamic_states = {
        vk::DynamicState::eViewportWithCount,  vk::DynamicState::eScissorWithCount,
        vk::DynamicState::eBlendConstants,     vk::DynamicState::eDepthTestEnable,
        vk::DynamicState::eDepthWriteEnable,   vk::DynamicState::eDepthCompareOp,
        vk::DynamicState::eDepthBiasEnable,    vk::DynamicState::eDepthBias,
        vk::DynamicState::eStencilTestEnable,  vk::DynamicState::eStencilReference,
        vk::DynamicState::eStencilCompareMask, vk::DynamicState::eStencilWriteMask,
        vk::DynamicState::eStencilOp,          vk::DynamicState::eCullMode,
        vk::DynamicState::eFrontFace,          vk::DynamicState::eRasterizerDiscardEnable,
        vk::DynamicState::eLineWidth,          vk::DynamicState::ePrimitiveRestartEnable,
    };
    if (instance.IsDepthBoundsSupported()) {
        dynamic_states.push_back(vk::DynamicState::eDepthBoundsTestEnable);
        dynamic_states.push_back(vk::DynamicState::eDepthBounds);
    }
    if (instance.IsDynamicColorWriteMaskSupported()) {
        dynamic_states.push_back(vk::DynamicState::eColorWriteMaskEXT);
    }
    if (dynamic_vertex_input) {
        dynamic_states.push_back(vk::DynamicState::eVertexInputEXT);
    } else if (!bindings.empty()) {
        dynamic_states.push_back(vk::DynamicState::eVertexInputBindingStride);
    }
    const vk::PipelineDynamicStateCreateInfo dynamic_info = {
        .dynamicStateCount =
            static_cast<u32>(host ? host_dynamic_states.size() : dynamic_states.size()),
        .pDynamicStates = host ? host_dynamic_states.data() : dynamic_states.data(),
    };

    const vk::PipelineShaderStageRequiredSubgroupSizeCreateInfo subgroup_size_ci = {
        .requiredSubgroupSize = 64,
    };
    boost::container::static_vector<vk::PipelineShaderStageCreateInfo, 5> shader_stages;
    {
        // Held from here: the caller may release a shader while the pipeline is made.
        std::scoped_lock lock{objects_mutex};
        for (const auto& stage : desc.stages) {
            ShaderRecord* shader = shaders.Get(stage.shader);
            ASSERT_MSG(shader, "A graphics pipeline's shader is stale");
            ++shader->holders;
            record->shaders.push_back(stage.shader);
            shader_stages.push_back({
                .pNext = !host && instance.IsSubgroupSize64Supported() ? &subgroup_size_ci
                                                                       : nullptr,
                .stage = vk::ShaderStageFlagBits(u32(stage.stage)),
                .module = *shader->module,
                .pName = "main",
            });
        }
    }

    const u32 num_targets = static_cast<u32>(desc.color_targets.size());
    std::array<vk::Format, Backend::MaxColorTargets> color_formats{};
    std::array<vk::SampleCountFlagBits, Backend::MaxColorTargets> color_samples{};
    std::array<vk::PipelineColorBlendAttachmentState, Backend::MaxColorTargets> blend_states{};
    for (u32 i = 0; i < num_targets; ++i) {
        const auto& target = desc.color_targets[i];
        color_formats[i] = ToVk(target.format);
        if (target.format != Backend::Format::Undefined &&
            !instance.IsFormatSupported(color_formats[i],
                                        vk::FormatFeatureFlagBits2::eColorAttachment)) {
            LOG_WARNING(Render_Vulkan, "color buffer format {} does not support COLOR_ATTACHMENT_BIT",
                        vk::to_string(color_formats[i]));
        }
        color_samples[i] = vk::SampleCountFlagBits(target.samples);
        blend_states[i] = vk::PipelineColorBlendAttachmentState{
            .blendEnable = target.blend,
            .srcColorBlendFactor = ToVk(target.src_color),
            .dstColorBlendFactor = ToVk(target.dst_color),
            .colorBlendOp = ToVk(target.color_op),
            .srcAlphaBlendFactor = ToVk(target.src_alpha),
            .dstAlphaBlendFactor = ToVk(target.dst_alpha),
            .alphaBlendOp = ToVk(target.alpha_op),
            // With a dynamic write mask, the draw's mask applies (the rasterizer sets it).
            .colorWriteMask = !host && instance.IsDynamicColorWriteMaskSupported()
                                  ? ToVk(Backend::ColorWriteMask::All)
                                  : ToVk(target.write_mask),
        };
    }
    const vk::AttachmentSampleCountInfoAMD mixed_samples = {
        .colorAttachmentCount = num_targets,
        .pColorAttachmentSamples = color_samples.data(),
        .depthStencilAttachmentSamples = vk::SampleCountFlagBits(desc.depth_samples),
    };
    const vk::PipelineRenderingCreateInfo rendering_ci = {
        .pNext = !host && instance.IsMixedDepthSamplesSupported() ? &mixed_samples : nullptr,
        .colorAttachmentCount = num_targets,
        .pColorAttachmentFormats = color_formats.data(),
        .depthAttachmentFormat = ToVk(desc.depth_format),
        .stencilAttachmentFormat = ToVk(desc.stencil_format),
    };
    const vk::PipelineColorBlendStateCreateInfo color_blending = {
        .logicOpEnable = !host && instance.IsLogicOpSupported() && desc.logic_op_enable,
        .logicOp = ToVk(desc.logic_op),
        .attachmentCount = num_targets,
        .pAttachments = blend_states.data(),
        .blendConstants = std::array{1.0f, 1.0f, 1.0f, 1.0f},
    };
    // Required by spec unless VK_EXT_extended_dynamic_state3 is supported. In practice, we use
    // dynamic state for all of it.
    constexpr vk::PipelineDepthStencilStateCreateInfo depth_stencil_info = {};

    const vk::GraphicsPipelineCreateInfo pipeline_ci = {
        .pNext = &rendering_ci,
        .stageCount = static_cast<u32>(shader_stages.size()),
        .pStages = shader_stages.data(),
        .pVertexInputState = dynamic_vertex_input ? nullptr : &vertex_input_info,
        .pInputAssemblyState = &input_assembly,
        .pTessellationState = host ? nullptr : &tessellation_state,
        .pViewportState = &viewport_info,
        .pRasterizationState = &raster_chain.get(),
        .pMultisampleState = &multisampling,
        .pDepthStencilState = !host && !instance.IsExtendedDynamicState3Supported()
                                  ? &depth_stencil_info
                                  : nullptr,
        .pColorBlendState = &color_blending,
        .pDynamicState = &dynamic_info,
        .layout = *record->layout,
    };
    auto [result, pipeline] = device.createGraphicsPipelineUnique({}, pipeline_ci);
    if (result != vk::Result::eSuccess) {
        // bbport: Metal (MoltenVK) rejects some pipelines Vulkan drivers accept, such as fragment
        // inputs the vertex shader does not write.
        LOG_ERROR(Render_Vulkan, "Failed to create graphics pipeline {}: {}", name,
                  vk::to_string(result));
        ReleasePipelineShaders(*record);
        return {}; // its layouts were never used: freed now
    }
    record->pipeline = std::move(pipeline);
    if (!name.empty()) {
        SetObjectName(device, *record->pipeline, "Graphics Pipeline {}", name);
    }
    return InsertPipeline(std::move(record));
}

Backend::PipelineAllocation BackendDevice::CreateComputePipeline(
    const Backend::ComputePipelineDesc& desc, std::string_view name) {
    const vk::Device device = instance.GetDevice();
    auto record = MakePipelineLayout(desc.layout, vk::ShaderStageFlagBits::eCompute, name);
    vk::ShaderModule module;
    {
        std::scoped_lock lock{objects_mutex};
        ShaderRecord* shader = shaders.Get(desc.shader);
        ASSERT_MSG(shader, "A compute pipeline's shader is stale");
        ++shader->holders;
        record->shaders.push_back(desc.shader);
        module = *shader->module;
    }
    const vk::PipelineShaderStageRequiredSubgroupSizeCreateInfo subgroup_size_ci = {
        .requiredSubgroupSize = 64,
    };
    const vk::ComputePipelineCreateInfo pipeline_ci = {
        .stage =
            {
                .pNext = !desc.host_pass && instance.IsSubgroupSize64Supported()
                             ? &subgroup_size_ci
                             : nullptr,
                .stage = vk::ShaderStageFlagBits::eCompute,
                .module = module,
                .pName = "main",
            },
        .layout = *record->layout,
    };
    auto [result, pipeline] = device.createComputePipelineUnique({}, pipeline_ci);
    if (result != vk::Result::eSuccess) {
        LOG_ERROR(Render_Vulkan, "Failed to create compute pipeline {}: {}", name,
                  vk::to_string(result));
        ReleasePipelineShaders(*record);
        return {}; // its layouts were never used: freed now
    }
    record->pipeline = std::move(pipeline);
    if (!name.empty()) {
        SetObjectName(device, *record->pipeline, "Compute Pipeline {}", name);
    }
    return InsertPipeline(std::move(record));
}

void BackendDevice::ReleasePipelineShaders(PipelineRecord& record) {
    boost::container::static_vector<vk::UniqueShaderModule, 5> freed;
    {
        std::scoped_lock lock{objects_mutex};
        for (const auto shader : record.shaders) {
            if (auto module = ReleaseShaderLocked(shader)) {
                freed.push_back(std::move(module));
            }
        }
    }
    record.shaders.clear();
    for (auto& module : freed) {
        DeferRelease([module = std::move(module)] {});
    }
}

void BackendDevice::DestroyPipeline(Backend::PipelineHandle pipeline) {
    if (!pipeline) {
        return;
    }
    std::unique_ptr<PipelineRecord> record;
    {
        std::scoped_lock lock{objects_mutex};
        std::unique_ptr<PipelineRecord>* entry = pipelines.Get(pipeline);
        if (!entry) {
            return;
        }
        record = std::move(*entry);
        pipelines.Erase(pipeline);
    }
    ReleasePipelineShaders(*record);
    DeferRelease([record = std::move(record)] {});
}

u64 BackendDevice::NativeHandle(Backend::PipelineHandle pipeline) {
    const PipelineRecord* record = GetPipelineRecord(pipeline);
    return record ? reinterpret_cast<u64>(static_cast<VkPipeline>(*record->pipeline)) : 0;
}

const BackendDevice::PipelineRecord* BackendDevice::GetPipelineRecord(
    Backend::PipelineHandle pipeline) {
    std::scoped_lock lock{objects_mutex};
    std::unique_ptr<PipelineRecord>* entry = pipelines.Get(pipeline);
    return entry ? entry->get() : nullptr;
}

} // namespace Vulkan
