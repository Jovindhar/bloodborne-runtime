// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: depth/stencil formats without blit support (D32S8 on RADV) resampled by a fullscreen
// draw writing gl_FragDepth and, with stencil export, the stencil value (the Vulkan backend's
// Transfers::ResampleDepth, for scene resolution's reduced proxies).
#pragma once

#include <unordered_map>
#include <utility>
#include <vector>

#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {

class Instance;
class Scheduler;

class DepthResampler {
public:
    DepthResampler(const Instance& instance, Scheduler& scheduler);
    ~DepthResampler();

    /// Whether `format` can be resampled (sampled and a depth target; with `stencil`, the
    /// stencil value exported or rebuilt bit by bit).
    [[nodiscard]] bool Supported(vk::Format format, bool stencil) const;

    /// `src`'s depth (and stencil) drawn into `dst` at `dst_size`: `src` in `src_layout`
    /// (sampled), `dst` in `dst_layout` (a depth target), both single-level 2D images.
    void Resample(vk::Image src, vk::ImageLayout src_layout, vk::Image dst,
                  vk::ImageLayout dst_layout, vk::Format format, bool stencil,
                  vk::Extent2D dst_size);

private:
    vk::Pipeline Pipeline(vk::Format format, bool stencil);

    const Instance& instance;
    Scheduler& scheduler;
    bool force_stencil_bits = false; ///< BB_SCENE_STENCIL_BITS=1: the portable resampler
    vk::UniqueShaderModule fs_tri_vert, depth_frag, depth_stencil_frag, stencil_bits_frag;
    vk::UniqueDescriptorSetLayout set_layout;
    vk::UniquePipelineLayout layout;
    std::vector<std::pair<std::pair<vk::Format, bool>, vk::UniquePipeline>> pipelines;
    /// Supported is asked for depth targets of every draw: the driver's answer, kept.
    mutable std::unordered_map<vk::Format, bool> supported;
};

} // namespace Vulkan
