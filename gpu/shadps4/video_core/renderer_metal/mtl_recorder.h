// SPDX-License-Identifier: GPL-2.0-or-later
// The render adapter's Metal command stream (docs/RENDER_ADAPTER.md, M2 step 2), while the Vulkan
// scheduler still carries the stream: with the Metal frame (BB_METAL_FRAME=1) every command
// buffer is encoded in Metal (mtl_vk_layer.mm), and this recorder encodes its commands there
// directly instead of as Vulkan calls the layer translates. Commands it does not cover yet go
// the Vulkan recorder's way.

#pragma once

#include <memory>

#include "video_core/renderer_vulkan/vk_command_recorder.h"
#include "video_core/renderer_vulkan/vk_stream_encoder.h"

namespace Metal {

/// The recorder the device uses with the Metal frame.
[[nodiscard]] std::unique_ptr<Vulkan::CommandRecorder> MakeFrameRecorder(
    Vulkan::Scheduler& scheduler, Vulkan::DescriptorHeap& descriptor_heap, vk::Device device);

/// The Vulkan runtime's transfers encoded by the Metal frame.
[[nodiscard]] const Vulkan::StreamEncoder& FrameStreamEncoder();

} // namespace Metal
