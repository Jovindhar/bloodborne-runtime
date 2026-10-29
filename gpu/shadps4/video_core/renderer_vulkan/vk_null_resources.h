// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: stand-ins for null descriptors on drivers without VK_EXT_robustness2's nullDescriptor
// (MoltenVK). A binding the game leaves empty gets a zeroed buffer, or a zeroed 1x1 image whose
// view matches what the shader recompiler declares for it (dimension, float or integer), kept in
// the general layout. Reads return zero as they would from a null descriptor. Made by the render
// adapter's device.

#pragma once

#include <array>
#include <memory>
#include "common/types.h"
#include "video_core/amdgpu/resource.h"
#include "video_core/renderer_backend/device.h"

namespace VideoCore {
struct Buffer;
}

namespace Vulkan {

class NullResources {
public:
    static constexpr u64 BufferSize = 16384;

    explicit NullResources(VideoCore::Backend::Device& device);
    ~NullResources();

    NullResources(const NullResources&) = delete;
    NullResources& operator=(const NullResources&) = delete;

    /// The buffer and a view as the backend's objects (Device::NativeHandle).
    [[nodiscard]] u64 NativeBuffer() const;
    /// A view of the given recompiler view type (AmdGpu::Image::GetViewType).
    [[nodiscard]] u64 NativeImageView(AmdGpu::ImageType type, bool integer) const;

private:
    enum Shape : u32 { Shape1D, Shape2D, Shape2DMsaa, Shape3D, NumShapes };
    enum ViewKind : u32 { View1D, View1DArray, View2D, View2DArray, View2DMsaa, View3D, NumViews };

    VideoCore::Backend::Device& device;
    std::unique_ptr<VideoCore::Buffer> buffer;
    /// [integer][shape]
    std::array<std::array<VideoCore::Backend::ImageAllocation, NumShapes>, 2> images{};
    /// [integer][view kind]
    std::array<std::array<VideoCore::Backend::ImageViewAllocation, NumViews>, 2> views{};
};

} // namespace Vulkan
