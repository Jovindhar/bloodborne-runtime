//  SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
//  SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"
#include "video_core/renderer_backend/pipeline.h"
#include "video_core/renderer_backend/resource.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {
class BackendDevice;
}

namespace Vulkan::HostPasses {

class FsrPass {
public:
    struct Settings {
        bool enable{true};
        bool use_rcas{true};
        float rcas_attenuation{0.25f};
    };

    ~FsrPass();

    /// Its pipelines, sampler and images, made by the render adapter's device.
    void Create(BackendDevice& device, u32 num_images);

    vk::ImageView Render(vk::CommandBuffer cmdbuf, vk::ImageView input, vk::Extent2D input_size,
                         vk::Extent2D output_size, Settings settings, bool hdr);

private:
    struct Img {
        u8 id{};
        bool dirty{true};

        VideoCore::Backend::ImageAllocation intermediary;
        VideoCore::Backend::ImageViewAllocation intermediary_view;
        VideoCore::Backend::ImageAllocation output;
        VideoCore::Backend::ImageViewAllocation output_view;
    };

    void ResizeAndInvalidate(u32 width, u32 height);
    void CreateImages(Img& img) const;
    void DestroyImages(Img& img) const;

    BackendDevice* device{};
    u32 num_images{};

    VideoCore::Backend::SamplerHandle sampler;
    VideoCore::Backend::PipelineAllocation easu_pipeline;
    VideoCore::Backend::PipelineAllocation rcas_pipeline;

    vk::Extent2D cur_size{};
    u32 cur_image{};
    std::vector<Img> available_imgs;
};

} // namespace Vulkan::HostPasses
