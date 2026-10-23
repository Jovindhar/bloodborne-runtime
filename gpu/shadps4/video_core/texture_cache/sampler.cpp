// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include "common/logging/log.h"
#include "video_core/renderer_vulkan/liverpool_to_vk.h"
#include "video_core/texture_cache/sampler.h"

namespace VideoCore {

Sampler::Sampler(Backend::Device& device_, const AmdGpu::Sampler& sampler,
                 const AmdGpu::BorderColorBuffer border_color_base, const bool is_depth,
                 float extra_lod_bias)
    : device{&device_} {
    using namespace Vulkan;
    const auto& caps = device->GetCapabilities();
    const bool anisotropy_enable = caps.max_sampler_anisotropy > 1.0f &&
                                   (AmdGpu::IsAnisoFilter(sampler.xy_mag_filter) ||
                                    AmdGpu::IsAnisoFilter(sampler.xy_min_filter));

    Backend::SamplerDesc desc{
        .mag_filter = LiverpoolToVK::Filter(sampler.xy_mag_filter),
        .min_filter = LiverpoolToVK::Filter(sampler.xy_min_filter),
        .mipmap_mode = LiverpoolToVK::MipFilter(sampler.mip_filter),
        .address_u = LiverpoolToVK::ClampMode(sampler.clamp_x),
        .address_v = LiverpoolToVK::ClampMode(sampler.clamp_y),
        .address_w = LiverpoolToVK::ClampMode(sampler.clamp_z),
        .lod_bias = std::clamp(sampler.LodBias() + extra_lod_bias, -caps.max_sampler_lod_bias,
                               caps.max_sampler_lod_bias),
        .min_lod = sampler.MinLod(),
        .max_lod = sampler.MaxLod(),
        .max_anisotropy = anisotropy_enable
                              ? std::clamp(sampler.MaxAniso(), 1.0f, caps.max_sampler_anisotropy)
                              : 1.0f,
        // GCN compares per instruction; a plain read of a compare sampler is undefined in Vulkan.
        .compare_enable = is_depth,
        .compare_op = LiverpoolToVK::DepthCompare(sampler.depth_compare_func),
        .border_color = LiverpoolToVK::BorderColor(sampler.border_color_type),
    };
    if (desc.border_color == Backend::BorderColor::FloatCustom) {
        if (caps.custom_border_color) {
            const auto border_color_index = sampler.border_color_ptr.Value();
            const auto border_color_buffer = border_color_base.Address<std::array<float, 4>*>();
            desc.border_color_value = border_color_buffer[border_color_index];
        } else {
            LOG_WARNING(Render_Vulkan,
                        "Custom border color is not supported, falling back to black");
            desc.border_color = Backend::BorderColor::FloatOpaqueBlack;
        }
    }
    handle = device->CreateSampler(desc);
    native = device->NativeHandle(handle);
}

Sampler::~Sampler() {
    if (handle) {
        device->DestroySampler(handle);
    }
}

} // namespace VideoCore
