//  SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
//  SPDX-License-Identifier: GPL-2.0-or-later

#include "common/assert.h"
#include "core/emulator_settings.h"
#include "video_core/host_shaders/fsr_easu_comp.h"
#include "video_core/host_shaders/fsr_rcas_comp.h"
#include "video_core/renderer_vulkan/host_passes/fsr_pass.h"
#include "video_core/renderer_vulkan/vk_backend_device.h"

#define A_CPU
#include "core/debug_state.h"
#include "video_core/host_shaders/fsr/ffx_a.h"
#include "video_core/host_shaders/fsr/ffx_fsr1.h"

typedef u32 uvec4[4];

struct FSRConstants {
    uvec4 Const0;
    uvec4 Const1;
    uvec4 Const2;
    uvec4 Const3;
    uvec4 Sample;
};

namespace Vulkan::HostPasses {

namespace {
vk::ImageView NativeView(const VideoCore::Backend::ImageViewAllocation& view) {
    return vk::ImageView{reinterpret_cast<VkImageView>(view.native)};
}
} // namespace

FsrPass::~FsrPass() {
    if (!device) {
        return;
    }
    for (auto& img : available_imgs) {
        DestroyImages(img);
    }
    device->DestroyPipeline(easu_pipeline.handle);
    device->DestroyPipeline(rcas_pipeline.handle);
    device->DestroySampler(sampler);
}

void FsrPass::Create(BackendDevice& device_, u32 num_images) {
    namespace Backend = VideoCore::Backend;
    device = &device_;
    this->num_images = num_images;

    sampler = device->CreateSampler({
        .mag_filter = Backend::Filter::Linear,
        .min_filter = Backend::Filter::Linear,
        .mipmap_mode = Backend::MipmapMode::Nearest,
        .address_u = Backend::AddressMode::ClampToEdge,
        .address_v = Backend::AddressMode::ClampToEdge,
        .address_w = Backend::AddressMode::ClampToEdge,
        .min_lod = -1000.0f,
        .max_lod = 1000.0f,
    });

    // The sampler is written with the images (it was an immutable sampler of the layout).
    constexpr std::array slots{
        Backend::BindingSlot{.kind = Backend::BindingKind::SampledImage,
                             .stages = Backend::ShaderStage::Compute},
        Backend::BindingSlot{.kind = Backend::BindingKind::StorageImage,
                             .stages = Backend::ShaderStage::Compute},
        Backend::BindingSlot{.kind = Backend::BindingKind::Sampler,
                             .stages = Backend::ShaderStage::Compute},
    };
    const auto make = [&](std::span<const u32> spirv, const char* name) {
        const auto shader = device->CreateShader(spirv, name);
        ASSERT(shader.handle);
        const auto pipeline = device->CreateComputePipeline(
            {.shader = shader.handle,
             .layout = {.slots = slots, .push_constant_size = sizeof(FSRConstants)},
             .host_pass = true},
            name);
        ASSERT_MSG(pipeline.handle, "Failed to create the {} pipeline", name);
        device->DestroyShader(shader.handle); // the pipeline holds it
        return pipeline;
    };
    easu_pipeline = make(FSR_EASU_COMP, "fsr easu");
    rcas_pipeline = make(FSR_RCAS_COMP, "fsr rcas");

    available_imgs.resize(num_images);
    for (int i = 0; i < num_images; ++i) {
        available_imgs[i].id = i;
    }
}

vk::ImageView FsrPass::Render(vk::CommandBuffer cmdbuf, vk::ImageView input,
                              vk::Extent2D input_size, vk::Extent2D output_size, Settings settings,
                              bool hdr) {
    if (!settings.enable) {
        DebugState.is_using_fsr = false;
        return input;
    }
    if (input_size.width >= output_size.width && input_size.height >= output_size.height) {
        DebugState.is_using_fsr = false;
        return input;
    }

    DebugState.is_using_fsr = true;

    if (output_size != cur_size) {
        ResizeAndInvalidate(output_size.width, output_size.height);
    }
    auto [width, height] = cur_size;

    auto& img = available_imgs[cur_image];
    if (++cur_image >= available_imgs.size()) {
        cur_image = 0;
    }

    if (img.dirty) {
        CreateImages(img);
    }

    if (EmulatorSettings.IsVkHostMarkersEnabled()) {
        cmdbuf.beginDebugUtilsLabelEXT(vk::DebugUtilsLabelEXT{
            .pLabelName = "Host/FSR",
        });
    }
    // Both pipelines have the same layout.
    const auto& easu = *static_cast<const BackendDevice::PipelineRecord*>(easu_pipeline.binding);
    const auto& rcas = *static_cast<const BackendDevice::PipelineRecord*>(rcas_pipeline.binding);
    const vk::Sampler native_sampler = NativeSampler(device->NativeHandle(sampler));

    static const int thread_group_work_region_dim = 16;
    int dispatch_x = (width + (thread_group_work_region_dim - 1)) / thread_group_work_region_dim;
    int dispatch_y = (height + (thread_group_work_region_dim - 1)) / thread_group_work_region_dim;

    constexpr vk::ImageSubresourceRange simple_subresource = {
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .levelCount = 1,
        .layerCount = 1,
    };
    const std::array enter_barrier{
        vk::ImageMemoryBarrier2{
            .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .srcAccessMask = vk::AccessFlagBits2::eShaderRead,
            .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eGeneral,
            .image = NativeImage(img.intermediary.native),
            .subresourceRange = simple_subresource,
        },
    };
    cmdbuf.pipelineBarrier2({
        .imageMemoryBarrierCount = enter_barrier.size(),
        .pImageMemoryBarriers = enter_barrier.data(),
    });

    FSRConstants consts{};
    FsrEasuCon(reinterpret_cast<AU1*>(&consts.Const0), reinterpret_cast<AU1*>(&consts.Const1),
               reinterpret_cast<AU1*>(&consts.Const2), reinterpret_cast<AU1*>(&consts.Const3),
               static_cast<AF1>(input_size.width), static_cast<AF1>(input_size.height),
               static_cast<AF1>(input_size.width), static_cast<AF1>(input_size.height), (AF1)width,
               (AF1)height);
    consts.Sample[0] = hdr && !settings.use_rcas ? 1 : 0;

    if (settings.use_rcas) {

        { // easu
            std::array<vk::DescriptorImageInfo, 3> img_info{{
                {
                    .imageView = input,
                    .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                },
                {
                    .imageView = NativeView(img.intermediary_view),
                    .imageLayout = vk::ImageLayout::eGeneral,
                },
                {
                    .sampler = native_sampler,
                },
            }};

            std::array<vk::WriteDescriptorSet, 3> set_writes{{
                {
                    .dstBinding = 0,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eSampledImage,
                    .pImageInfo = &img_info[0],
                },
                {
                    .dstBinding = 1,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eStorageImage,
                    .pImageInfo = &img_info[1],
                },
                {
                    .dstBinding = 2,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eSampler,
                    .pImageInfo = &img_info[2],
                },
            }};

            cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, *easu.pipeline);
            cmdbuf.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, *easu.layout, 0,
                                        set_writes);
            cmdbuf.pushConstants(*easu.layout, vk::ShaderStageFlagBits::eCompute, 0,
                                 sizeof(FSRConstants), &consts);
            cmdbuf.dispatch(dispatch_x, dispatch_y, 1);
        }

        std::array img_barrier{
            vk::ImageMemoryBarrier2{
                .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
                .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
                .oldLayout = vk::ImageLayout::eGeneral,
                .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                .image = NativeImage(img.intermediary.native),
                .subresourceRange = simple_subresource,
            },
            vk::ImageMemoryBarrier2{
                .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
                .srcAccessMask = vk::AccessFlagBits2::eNone,
                .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                .dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
                .oldLayout = vk::ImageLayout::eUndefined,
                .newLayout = vk::ImageLayout::eGeneral,
                .image = NativeImage(img.output.native),
                .subresourceRange = simple_subresource,
            },
        };
        cmdbuf.pipelineBarrier2(vk::DependencyInfo{
            .imageMemoryBarrierCount = img_barrier.size(),
            .pImageMemoryBarriers = img_barrier.data(),
        });

        { // rcas
            consts = {};
            FsrRcasCon(reinterpret_cast<AU1*>(&consts.Const0), settings.rcas_attenuation);
            consts.Sample[0] = hdr ? 1 : 0;

            std::array<vk::DescriptorImageInfo, 3> img_info{{
                {
                    .imageView = NativeView(img.intermediary_view),
                    .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                },
                {
                    .imageView = NativeView(img.output_view),
                    .imageLayout = vk::ImageLayout::eGeneral,
                },
                {
                    .sampler = native_sampler,
                },
            }};

            std::array<vk::WriteDescriptorSet, 3> set_writes{{
                {
                    .dstBinding = 0,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eSampledImage,
                    .pImageInfo = &img_info[0],
                },
                {
                    .dstBinding = 1,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eStorageImage,
                    .pImageInfo = &img_info[1],
                },
                {
                    .dstBinding = 2,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eSampler,
                    .pImageInfo = &img_info[2],
                },
            }};

            cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, *rcas.pipeline);
            cmdbuf.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, *easu.layout, 0,
                                        set_writes);
            cmdbuf.pushConstants(*easu.layout, vk::ShaderStageFlagBits::eCompute, 0,
                                 sizeof(FSRConstants), &consts);
            cmdbuf.dispatch(dispatch_x, dispatch_y, 1);
        }

    } else {
        // only easu
        std::array<vk::DescriptorImageInfo, 3> img_info{{
            {
                .imageView = input,
                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            },
            {
                .imageView = NativeView(img.output_view),
                .imageLayout = vk::ImageLayout::eGeneral,
            },
            {
                .sampler = native_sampler,
            },
        }};

        std::array<vk::WriteDescriptorSet, 3> set_writes{{
            {
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eSampledImage,
                .pImageInfo = &img_info[0],
            },
            {
                .dstBinding = 1,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eStorageImage,
                .pImageInfo = &img_info[1],
            },
            {
                .dstBinding = 2,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eSampler,
                .pImageInfo = &img_info[2],
            },
        }};

        cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, *easu.pipeline);
        cmdbuf.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, *easu.layout, 0,
                                    set_writes);
        cmdbuf.pushConstants(*easu.layout, vk::ShaderStageFlagBits::eCompute, 0,
                             sizeof(FSRConstants), &consts);
        cmdbuf.dispatch(dispatch_x, dispatch_y, 1);
    }

    const std::array return_barrier{
        vk::ImageMemoryBarrier2{
            .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
            .oldLayout = vk::ImageLayout::eGeneral,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .image = NativeImage(img.output.native),
            .subresourceRange = simple_subresource,
        },
    };
    cmdbuf.pipelineBarrier2({
        .imageMemoryBarrierCount = return_barrier.size(),
        .pImageMemoryBarriers = return_barrier.data(),
    });

    if (EmulatorSettings.IsVkHostMarkersEnabled()) {
        cmdbuf.endDebugUtilsLabelEXT();
    }

    return NativeView(img.output_view);
}

void FsrPass::ResizeAndInvalidate(u32 width, u32 height) {
    this->cur_size = vk::Extent2D{
        .width = width,
        .height = height,
    };
    for (int i = 0; i < num_images; ++i) {
        available_imgs[i].dirty = true;
    }
}

void FsrPass::DestroyImages(Img& img) const {
    device->DestroyImageView(img.intermediary_view.handle);
    device->DestroyImageView(img.output_view.handle);
    device->DestroyImage(img.intermediary.handle);
    device->DestroyImage(img.output.handle);
    img.intermediary_view = {};
    img.output_view = {};
    img.intermediary = {};
    img.output = {};
}

void FsrPass::CreateImages(Img& img) const {
    namespace Backend = VideoCore::Backend;
    img.dirty = false;
    // The previous size's images are freed once the GPU is past them.
    DestroyImages(img);

    Backend::ImageDesc desc{
        .format = Backend::Format::R16G16B16A16Sfloat,
        .width = cur_size.width,
        .height = cur_size.height,
        .usage = Backend::ImageUse::Sampled | Backend::ImageUse::Storage,
        .mutable_format = false,
    };
    img.intermediary = device->CreateImage(desc, fmt::format("FSR Intermediary Image #{}", img.id));
    desc.usage = Backend::ImageUse::TransferSource | Backend::ImageUse::Sampled |
                 Backend::ImageUse::Storage | Backend::ImageUse::ColorAttachment;
    img.output = device->CreateImage(desc, fmt::format("FSR Output Image #{}", img.id));

    const Backend::ImageViewDesc view_desc{.format = Backend::Format::R16G16B16A16Sfloat};
    img.intermediary_view = device->CreateImageView(
        img.intermediary.handle, view_desc, fmt::format("FSR Intermediary ImageView #{}", img.id));
    img.output_view = device->CreateImageView(img.output.handle, view_desc,
                                              fmt::format("FSR Output ImageView #{}", img.id));
}

} // namespace Vulkan::HostPasses
