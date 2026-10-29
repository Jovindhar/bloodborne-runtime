// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/serdes.h"
#include "shader_recompiler/info.h"
#include "video_core/renderer_vulkan/vk_backend_device.h"
#include "video_core/renderer_vulkan/vk_compute_pipeline.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

namespace Vulkan {

ComputePipeline::ComputePipeline(VideoCore::Backend::Device& device, const Shader::Profile& profile,
                                 ComputePipelineKey compute_key_, const Shader::Info& info_,
                                 VideoCore::Backend::ShaderHandle shader,
                                 SerializationSupport& sdata, bool preloading /*=false*/)
    : Pipeline{device, profile, true}, compute_key{compute_key_} {
    stages[int(Shader::SwStage::Compute)] = &info_;
    BindingSlots slots;
    AppendBindingSlots(info_, VideoCore::Backend::ShaderStage::Compute, slots);
    const VideoCore::Backend::GuestCompute guest{.key = &compute_key};
    const auto created = device.CreateComputePipeline(
        {.shader = shader,
         .layout = {.slots = slots, .push_constant_size = sizeof(Shader::PushData)},
         .guest = &guest},
        GetDebugString());
    if (preloading && !created.handle) {
        // bbport: a pipeline from the cache the driver rejects is a damaged cache entry: the
        // cache is rebuilt (PipelineCache::WarmUp) instead of stopping the game at every start.
        throw Serialization::CorruptData{"cached compute pipeline rejected by the driver"};
    }
    ASSERT_MSG(created.handle, "Failed to create compute pipeline");
    allocation = created;
}

ComputePipeline::~ComputePipeline() = default;

} // namespace Vulkan
