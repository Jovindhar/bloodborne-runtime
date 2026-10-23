//  SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
//  SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/renderer_vulkan/host_passes/pp_pass.h"

#include "common/assert.h"
#include "core/emulator_settings.h"
#include "video_core/host_shaders/fs_tri_vert.h"
#include "video_core/host_shaders/post_process_frag.h"
#include "video_core/renderer_vulkan/vk_backend_device.h"
#include "video_core/renderer_vulkan/vk_presenter.h"
#include "video_core/renderer_vulkan/vk_state.h"

namespace Vulkan::HostPasses {

PostProcessingPass::~PostProcessingPass() {
    if (device) {
        device->DestroyPipeline(pipeline.handle);
        device->DestroySampler(sampler);
    }
}

void PostProcessingPass::Create(BackendDevice& device_, const vk::Format surface_format) {
    namespace Backend = VideoCore::Backend;
    device = &device_;
    const auto vs = device->CreateShader(FS_TRI_VERT, "fs_tri.vert");
    const auto fs = device->CreateShader(POST_PROCESS_FRAG, "post_process.frag");
    ASSERT(vs.handle && fs.handle);

    const std::array slots{
        Backend::BindingSlot{.kind = Backend::BindingKind::CombinedImageSampler,
                             .stages = Backend::ShaderStage::Fragment},
    };
    const std::array stages{
        Backend::PipelineStage{Backend::ShaderStage::Vertex, vs.handle},
        Backend::PipelineStage{Backend::ShaderStage::Fragment, fs.handle},
    };
    const std::array targets{
        Backend::ColorTarget{.format = FromVk(surface_format)},
    };
    pipeline = device->CreateGraphicsPipeline(
        {
            .host_pass = true,
            .stages = stages,
            .layout = {.slots = slots,
                       .push_constant_size = sizeof(Settings),
                       .push_stages = Backend::ShaderStage::Fragment},
            .topology = Backend::PrimitiveTopology::TriangleList,
            .polygon_mode = Backend::PolygonMode::Fill,
            .color_targets = targets,
            .cull_mode = Backend::CullMode::Back,
            .front_face = Backend::FrontFace::Clockwise,
        },
        "post process");
    ASSERT_MSG(pipeline.handle, "Failed to create the post process pipeline");
    // The pipeline holds its shaders.
    device->DestroyShader(vs.handle);
    device->DestroyShader(fs.handle);

    sampler = device->CreateSampler({
        .mag_filter = Backend::Filter::Linear,
        .min_filter = Backend::Filter::Linear,
        .mipmap_mode = Backend::MipmapMode::Nearest,
        .address_u = Backend::AddressMode::ClampToEdge,
        .address_v = Backend::AddressMode::ClampToEdge,
    });
}

void PostProcessingPass::Render(vk::CommandBuffer cmdbuf, vk::ImageView input,
                                vk::Extent2D input_size, Frame& frame, Settings settings) {
    if (EmulatorSettings.IsVkHostMarkersEnabled()) {
        cmdbuf.beginDebugUtilsLabelEXT(vk::DebugUtilsLabelEXT{
            .pLabelName = "Host/Post processing",
        });
    }

    constexpr vk::ImageSubresourceRange simple_subresource = {
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .levelCount = 1,
        .layerCount = 1,
    };
    const std::array<vk::RenderingAttachmentInfo, 1> attachments{{
        {
            .imageView = frame.image_view,
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eStore,
        },
    }};
    const vk::RenderingInfo rendering_info{
        .renderArea{
            .extent{
                .width = frame.width,
                .height = frame.height,
            },
        },
        .layerCount = 1,
        .colorAttachmentCount = attachments.size(),
        .pColorAttachments = attachments.data(),
    };

    const auto& record = *static_cast<const BackendDevice::PipelineRecord*>(pipeline.binding);
    vk::DescriptorImageInfo image_info{
        .sampler = NativeSampler(device->NativeHandle(sampler)),
        .imageView = input,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };

    const std::array set_writes{
        vk::WriteDescriptorSet{
            .dstSet = VK_NULL_HANDLE,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .pImageInfo = &image_info,
        },
    };

    cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, *record.pipeline);

    const std::array viewports = {
        vk::Viewport{
            .width = static_cast<float>(frame.width),
            .height = static_cast<float>(frame.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        },
    };

    cmdbuf.setViewport(0, viewports);
    cmdbuf.setScissor(0, vk::Rect2D{
                             .extent{
                                 .width = frame.width,
                                 .height = frame.height,
                             },
                         });

    cmdbuf.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, *record.layout, 0, set_writes);
    cmdbuf.pushConstants(*record.layout, record.push_stages, 0, sizeof(Settings), &settings);

    cmdbuf.beginRendering(rendering_info);
    cmdbuf.draw(3, 1, 0, 0);
    cmdbuf.endRendering();

    const auto post_barrier = vk::ImageMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
        .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .newLayout = vk::ImageLayout::eGeneral,
        .image = frame.image,
        .subresourceRange = simple_subresource,
    };

    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &post_barrier,
    });

    if (EmulatorSettings.IsVkHostMarkersEnabled()) {
        cmdbuf.endDebugUtilsLabelEXT();
    }
}

} // namespace Vulkan::HostPasses
