// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: depth/stencil resampling by a fullscreen draw (vk_depth_resampler.h).

#include <array>
#include <cstdlib>

#include "video_core/host_shaders/depth_resample_frag.h"
#include "video_core/host_shaders/depth_stencil_bits_frag.h"
#include "video_core/host_shaders/depth_stencil_resample_frag.h"
#include "video_core/host_shaders/fs_tri_vert.h"
#include "video_core/renderer_vulkan/vk_depth_resampler.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"

namespace Vulkan {

DepthResampler::DepthResampler(const Instance& instance_, Scheduler& scheduler_)
    : instance{instance_}, scheduler{scheduler_} {
    const char* force = std::getenv("BB_SCENE_STENCIL_BITS");
    force_stencil_bits = force && force[0] == '1';
    const auto device = instance.GetDevice();
    fs_tri_vert = vk::UniqueShaderModule(CompileSPV(FS_TRI_VERT, device), device);
    depth_frag = vk::UniqueShaderModule(CompileSPV(DEPTH_RESAMPLE_FRAG, device), device);
    if (instance.IsShaderStencilExportSupported() && !force_stencil_bits) {
        depth_stencil_frag =
            vk::UniqueShaderModule(CompileSPV(DEPTH_STENCIL_RESAMPLE_FRAG, device), device);
    }
    if (!depth_stencil_frag) {
        stencil_bits_frag =
            vk::UniqueShaderModule(CompileSPV(DEPTH_STENCIL_BITS_FRAG, device), device);
    }
    const std::array bindings{
        vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eSampledImage, 1,
                                       vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eSampledImage, 1,
                                       vk::ShaderStageFlagBits::eFragment},
    };
    set_layout = Check(device.createDescriptorSetLayoutUnique({
        .flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
        .bindingCount = u32(bindings.size()), .pBindings = bindings.data(),
    }));
    const auto raw_set_layout = *set_layout;
    const vk::PushConstantRange push{vk::ShaderStageFlagBits::eFragment, 0, sizeof(u32)};
    layout = Check(device.createPipelineLayoutUnique({
        .setLayoutCount = 1, .pSetLayouts = &raw_set_layout,
        .pushConstantRangeCount = 1, .pPushConstantRanges = &push,
    }));
}

DepthResampler::~DepthResampler() = default;

bool DepthResampler::Supported(vk::Format format, bool stencil) const {
    auto it = supported.find(format);
    if (it == supported.end()) {
        const auto features =
            instance.GetPhysicalDevice().getFormatProperties(format).optimalTilingFeatures;
        const auto required = vk::FormatFeatureFlagBits::eSampledImage |
                              vk::FormatFeatureFlagBits::eDepthStencilAttachment;
        it = supported.emplace(format, (features & required) == required).first;
    }
    // Without shader stencil export (e.g. Pascal) depth/stencil stays native: the eight-draw
    // stencil resampler costs too much on such GPUs. BB_SCENE_STENCIL_BITS=1 forces it (tests).
    return it->second &&
           (!stencil || instance.IsShaderStencilExportSupported() || force_stencil_bits);
}

void DepthResampler::Resample(vk::Image src, vk::ImageLayout src_layout, vk::Image dst,
                              vk::ImageLayout dst_layout, vk::Format format, bool stencil,
                              vk::Extent2D dst_size) {
    const auto device = instance.GetDevice();
    const bool stencil_bits = stencil && !depth_stencil_frag;
    const auto make_view = [&](vk::Image image, vk::ImageAspectFlags aspect,
                               vk::ImageUsageFlags usage) {
        const vk::ImageViewUsageCreateInfo usage_ci{.usage = usage};
        return Check(device.createImageView({
            .pNext = &usage_ci, .image = image, .viewType = vk::ImageViewType::e2D,
            .format = format, .subresourceRange = {aspect, 0, 1, 0, 1},
        }));
    };
    const auto target_aspect = stencil ? vk::ImageAspectFlagBits::eDepth |
                                             vk::ImageAspectFlagBits::eStencil
                                       : vk::ImageAspectFlags(vk::ImageAspectFlagBits::eDepth);
    const auto depth_view = make_view(src, vk::ImageAspectFlagBits::eDepth,
                                      vk::ImageUsageFlagBits::eSampled);
    const auto stencil_view = stencil ? make_view(src, vk::ImageAspectFlagBits::eStencil,
                                                  vk::ImageUsageFlagBits::eSampled)
                                      : vk::ImageView{};
    const auto target_view =
        make_view(dst, target_aspect, vk::ImageUsageFlagBits::eDepthStencilAttachment);
    scheduler.DeferOperation([device, depth_view, stencil_view, target_view] {
        device.destroyImageView(depth_view);
        if (stencil_view) device.destroyImageView(stencil_view);
        device.destroyImageView(target_view);
    });
    const auto pipeline = Pipeline(format, stencil);
    const auto pipeline_layout = *layout;
    scheduler.EndRendering();
    scheduler.RecordCrumb({.name = "scene resample"}, [=](vk::CommandBuffer cmd) {
        const vk::RenderingAttachmentInfo attachment{
            .imageView = target_view,
            .imageLayout = dst_layout,
            .loadOp = vk::AttachmentLoadOp::eDontCare,
            .storeOp = vk::AttachmentStoreOp::eStore,
        };
        auto stencil_attachment = attachment;
        if (stencil_bits) {
            stencil_attachment.loadOp = vk::AttachmentLoadOp::eClear;
            stencil_attachment.clearValue.depthStencil = vk::ClearDepthStencilValue{0.f, 0};
        }
        cmd.beginRendering({
            .renderArea = {{0, 0}, dst_size}, .layerCount = 1,
            .pDepthAttachment = &attachment,
            .pStencilAttachment = stencil ? &stencil_attachment : nullptr,
        });
        const std::array infos{
            vk::DescriptorImageInfo{{}, depth_view, src_layout},
            vk::DescriptorImageInfo{{}, stencil_view, src_layout},
        };
        const std::array writes{
            vk::WriteDescriptorSet{.dstBinding = 0, .descriptorCount = 1,
                                   .descriptorType = vk::DescriptorType::eSampledImage,
                                   .pImageInfo = &infos[0]},
            vk::WriteDescriptorSet{.dstBinding = 1, .descriptorCount = 1,
                                   .descriptorType = vk::DescriptorType::eSampledImage,
                                   .pImageInfo = &infos[1]},
        };
        cmd.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, pipeline_layout, 0,
                                 vk::ArrayProxy<const vk::WriteDescriptorSet>(
                                     stencil ? 2u : 1u, writes.data()));
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
        cmd.setViewportWithCount(vk::Viewport{0.f, 0.f, float(dst_size.width),
                                              float(dst_size.height), 0.f, 1.f});
        cmd.setScissorWithCount(vk::Rect2D{{0, 0}, dst_size});
        if (stencil_bits) {
            // Copy depth first, then reconstruct all eight stencil bits without needing
            // VK_EXT_shader_stencil_export (e.g. Pascal). Discard keeps unset bits clear.
            for (u32 bit : {0u, 1u, 2u, 4u, 8u, 16u, 32u, 64u, 128u}) {
                cmd.pushConstants(pipeline_layout, vk::ShaderStageFlagBits::eFragment, 0,
                                  sizeof(bit), &bit);
                cmd.setStencilWriteMask(vk::StencilFaceFlagBits::eFrontAndBack, bit);
                cmd.setStencilReference(vk::StencilFaceFlagBits::eFrontAndBack, bit);
                cmd.draw(3, 1, 0, 0);
            }
        } else {
            cmd.draw(3, 1, 0, 0);
        }
        cmd.endRendering();
    });
    // The guest pipelines' dynamic state must be emitted again after this pipeline.
    scheduler.GetDynamicState().Invalidate();
}

vk::Pipeline DepthResampler::Pipeline(vk::Format format, bool stencil) {
    const bool stencil_bits = stencil && !depth_stencil_frag;
    const std::pair key{format, stencil};
    for (const auto& [k, pipeline] : pipelines) if (k == key) return *pipeline;
    const vk::PipelineInputAssemblyStateCreateInfo input_assembly{
        .topology = vk::PrimitiveTopology::eTriangleList,
    };
    const vk::PipelineMultisampleStateCreateInfo multisampling{
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
    };
    // Reference, compare and write masks are static; the exported value replaces the reference.
    const vk::StencilOpState stencil_op{
        .failOp = vk::StencilOp::eReplace, .passOp = vk::StencilOp::eReplace,
        .depthFailOp = vk::StencilOp::eReplace, .compareOp = vk::CompareOp::eAlways,
        .compareMask = 0xff, .writeMask = 0xff, .reference = 0,
    };
    const vk::PipelineDepthStencilStateCreateInfo depth_state{
        .depthTestEnable = true, .depthWriteEnable = true,
        .depthCompareOp = vk::CompareOp::eAlways,
        .stencilTestEnable = stencil, .front = stencil_op, .back = stencil_op,
    };
    const std::array dynamic_states{vk::DynamicState::eViewportWithCount,
                                    vk::DynamicState::eScissorWithCount,
                                    vk::DynamicState::eStencilWriteMask,
                                    vk::DynamicState::eStencilReference};
    const vk::PipelineDynamicStateCreateInfo dynamic_info{
        .dynamicStateCount = stencil_bits ? 4u : 2u, .pDynamicStates = dynamic_states.data(),
    };
    const std::array stages{
        vk::PipelineShaderStageCreateInfo{.stage = vk::ShaderStageFlagBits::eVertex,
                                          .module = *fs_tri_vert, .pName = "main"},
        vk::PipelineShaderStageCreateInfo{.stage = vk::ShaderStageFlagBits::eFragment,
                                          .module = stencil ? (stencil_bits ? *stencil_bits_frag :
                                                             *depth_stencil_frag) : *depth_frag,
                                          .pName = "main"},
    };
    const vk::PipelineRenderingCreateInfo rendering{
        .depthAttachmentFormat = format,
        .stencilAttachmentFormat = stencil ? format : vk::Format::eUndefined,
    };
    const vk::PipelineColorBlendStateCreateInfo color_blending{};
    const vk::PipelineViewportStateCreateInfo viewport_info{};
    const vk::PipelineVertexInputStateCreateInfo vertex_input{};
    const vk::PipelineRasterizationStateCreateInfo raster{.lineWidth = 1.f};
    auto pipeline = Check(instance.GetDevice().createGraphicsPipelineUnique(VK_NULL_HANDLE, {
        .pNext = &rendering, .stageCount = u32(stages.size()), .pStages = stages.data(),
        .pVertexInputState = &vertex_input, .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_info, .pRasterizationState = &raster,
        .pMultisampleState = &multisampling, .pDepthStencilState = &depth_state,
        .pColorBlendState = &color_blending, .pDynamicState = &dynamic_info,
        .layout = *layout,
    }));
    const auto result = *pipeline;
    pipelines.emplace_back(key, std::move(pipeline));
    return result;
}

} // namespace Vulkan
