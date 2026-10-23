//  SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
//  SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"
#include "video_core/renderer_backend/pipeline.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {
class BackendDevice;
class Frame;
}

namespace Vulkan::HostPasses {

class PostProcessingPass {
public:
    struct Settings {
        float gamma = 1.0f;
        u32 hdr = 0;
        u32 srgb_input = 0;
    };

    ~PostProcessingPass();

    /// Its pipeline and sampler, made by the render adapter's device.
    void Create(BackendDevice& device, vk::Format surface_format);

    void Render(vk::CommandBuffer cmdbuf, vk::ImageView input, vk::Extent2D input_size,
                Frame& output, Settings settings);

private:
    BackendDevice* device{};
    VideoCore::Backend::PipelineAllocation pipeline;
    VideoCore::Backend::SamplerHandle sampler;
};

} // namespace Vulkan::HostPasses
