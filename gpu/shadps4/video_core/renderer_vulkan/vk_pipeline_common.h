// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "shader_recompiler/profile.h"
#include "shader_recompiler/runtime_info.h"
#include "video_core/renderer_backend/binding.h"
#include "video_core/renderer_backend/device.h"
#include "video_core/renderer_backend/pipeline.h"
#include "video_core/renderer_vulkan/vk_common.h"

#include <boost/container/small_vector.hpp>

namespace Shader {
struct Info;
struct PushData;
} // namespace Shader

namespace Vulkan {

static constexpr auto AllGraphicsStageBits =
    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eTessellationControl |
    vk::ShaderStageFlagBits::eTessellationEvaluation | vk::ShaderStageFlagBits::eGeometry |
    vk::ShaderStageFlagBits::eFragment;

class Instance;

/// A guest pipeline, made by the render adapter's device from its description (A5).
class Pipeline {
public:
    Pipeline(VideoCore::Backend::Device& device, const Shader::Profile& profile, bool is_compute = false);
    virtual ~Pipeline();

    /// Empty when the driver rejected the pipeline: its draws are skipped.
    vk::Pipeline Handle() const noexcept {
        return vk::Pipeline{reinterpret_cast<VkPipeline>(allocation.native)};
    }

    /// What the device made, for CommandRecorder::BindResources.
    const VideoCore::Backend::PipelineAllocation& Allocation() const noexcept {
        return allocation;
    }

    auto GetStages() const {
        static_assert(static_cast<u32>(Shader::SwStage::Compute) == Shader::MaxStageTypes - 1);
        if (is_compute) {
            return std::span{stages.cend() - 1, stages.cend()};
        } else {
            return std::span{stages.cbegin(), stages.cend() - 1};
        }
    }

    const Shader::Info& GetStage(Shader::SwStage stage) const noexcept {
        return *stages[u32(stage)];
    }

    bool IsCompute() const {
        return is_compute;
    }

    /// The render adapter's slot writes (docs/RENDER_ADAPTER.md, A4).
    using DescriptorWrites = std::vector<VideoCore::Backend::SlotWrite>;

protected:
    using BindingSlots = boost::container::small_vector<VideoCore::Backend::BindingSlot, 32>;

    [[nodiscard]] std::string GetDebugString() const;
    /// The binding slots of a stage's resources, in the order the recompiler numbers them:
    /// buffers, images, samplers.
    static void AppendBindingSlots(const Shader::Info& info, VideoCore::Backend::ShaderStage stage,
                                   BindingSlots& slots);
    VideoCore::Backend::Device& device;
    const Shader::Profile& profile;
    VideoCore::Backend::PipelineAllocation allocation; ///< destroyed with the pipeline
    std::array<const Shader::Info*, Shader::MaxStageTypes> stages{};
    bool is_compute;
};

} // namespace Vulkan
