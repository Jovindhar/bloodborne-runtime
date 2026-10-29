// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>
#include <span>

#include <boost/container/small_vector.hpp>
#include <boost/container/static_vector.hpp>

#include "shader_recompiler/info.h"
#include "shader_recompiler/resource.h"
#include "video_core/renderer_vulkan/vk_backend_device.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/renderer_vulkan/vk_pipeline_common.h"

namespace Vulkan {

Pipeline::Pipeline(VideoCore::Backend::Device& device_, const Shader::Profile& profile_,
                   bool is_compute_ /*= false*/)
    : device{device_}, profile{profile_}, is_compute{is_compute_} {}

Pipeline::~Pipeline() {
    device.DestroyPipeline(allocation.handle);
}

void Pipeline::AppendBindingSlots(const Shader::Info& info, VideoCore::Backend::ShaderStage stage,
                                  BindingSlots& slots) {
    using VideoCore::Backend::BindingKind;
    for (size_t i = 0; i < info.buffers.size(); ++i) {
        slots.push_back({.kind = BindingKind::StorageBuffer, .count = 1, .stages = stage});
    }
    for (const auto& image : info.images) {
        slots.push_back({
            .kind = image.is_written ? BindingKind::StorageImage : BindingKind::SampledImage,
            .count = image.NumBindings(info),
            .stages = stage,
        });
    }
    for (size_t i = 0; i < info.samplers.size(); ++i) {
        slots.push_back({.kind = BindingKind::Sampler, .count = 1, .stages = stage});
    }
}

std::string Pipeline::GetDebugString() const {
    std::string stage_desc;
    for (const auto& stage : stages) {
        if (stage) {
            const auto shader_name = PipelineCache::GetShaderName(stage->hw_stage, stage->pgm_hash);
            if (stage_desc.empty()) {
                stage_desc = shader_name;
            } else {
                stage_desc = fmt::format("{},{}", stage_desc, shader_name);
            }
        }
    }
    return stage_desc;
}

} // namespace Vulkan
