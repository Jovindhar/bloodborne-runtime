// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: stand-ins for null descriptors (vk_null_resources.h).

#include <cstring>

#include "common/assert.h"
#include "common/logging/log.h"
#include "video_core/buffer_cache/buffer.h"
#include "video_core/renderer_backend/commands.h"
#include "video_core/renderer_vulkan/vk_null_resources.h"

namespace Vulkan {

namespace Backend = VideoCore::Backend;

NullResources::NullResources(Backend::Device& device_) : device{device_} {
    // The buffer: host-visible and cleared here, no transfer needed.
    buffer = std::make_unique<VideoCore::Buffer>(device, 0, BufferSize,
                                                 VideoCore::MemoryType::HostUncached,
                                                 "Null descriptor stand-in");
    ASSERT_MSG(!buffer->mapped_data.empty(), "Null descriptor stand-in: buffer not mapped");
    std::memset(buffer->mapped_data.data(), 0, BufferSize);
    buffer->Flush(0, BufferSize);

    // The images: 1x1 (x1), one layer, float and unsigned integer formats.
    auto& recorder = device.Recorder();
    for (u32 integer = 0; integer < 2; ++integer) {
        const Backend::Format format =
            integer ? Backend::Format::R8G8B8A8Uint : Backend::Format::R8G8B8A8Unorm;
        for (u32 shape = 0; shape < NumShapes; ++shape) {
            const bool msaa = shape == Shape2DMsaa;
            const Backend::ImageDesc desc{
                .type = shape == Shape1D   ? Backend::ImageType::Image1D
                        : shape == Shape3D ? Backend::ImageType::Image3D
                                           : Backend::ImageType::Image2D,
                .format = format,
                .samples = msaa ? 4u : 1u,
                // Multisampled storage images need shaderStorageImageMultisample: sampled only.
                .usage = Backend::ImageUse::Sampled | Backend::ImageUse::TransferDestination |
                         (msaa ? Backend::ImageUse::None : Backend::ImageUse::Storage),
                .mutable_format = false,
            };
            images[integer][shape] = device.CreateImage(desc, "Null descriptor stand-in");
            ASSERT_MSG(images[integer][shape].handle,
                       "Null descriptor stand-in: image allocation failed");
            // To the general layout, cleared to zero, ahead of any draw in the command stream.
            const auto& image = images[integer][shape];
            recorder.TransitImage(image, Backend::ImageAspect::Color,
                                  Backend::ImageAccess::TransferDestination, true);
            recorder.ClearImage(image, Backend::ClearColor{}); // 0.0 and 0 alike
            recorder.TransitImage(image, Backend::ImageAspect::Color, Backend::ImageAccess::Storage);
        }
        const auto make_view = [&](Shape shape, Backend::ImageViewType type) {
            const auto view = device.CreateImageView(
                images[integer][shape].handle,
                {.type = type,
                 .format = format,
                 .usage = Backend::ImageUse::Sampled |
                          (shape == Shape2DMsaa ? Backend::ImageUse::None
                                                : Backend::ImageUse::Storage)},
                "Null descriptor stand-in");
            ASSERT_MSG(view.handle, "Null descriptor stand-in: image view failed");
            return view;
        };
        using ViewType = Backend::ImageViewType;
        views[integer][View1D] = make_view(Shape1D, ViewType::View1D);
        views[integer][View1DArray] = make_view(Shape1D, ViewType::View1DArray);
        views[integer][View2D] = make_view(Shape2D, ViewType::View2D);
        views[integer][View2DArray] = make_view(Shape2D, ViewType::View2DArray);
        views[integer][View2DMsaa] = make_view(Shape2DMsaa, ViewType::View2D);
        views[integer][View3D] = make_view(Shape3D, ViewType::View3D);
    }
    LOG_INFO(Render_Vulkan, "No nullDescriptor: empty bindings use zeroed stand-in resources");
}

NullResources::~NullResources() {
    for (u32 integer = 0; integer < 2; ++integer) {
        for (const auto& view : views[integer]) {
            device.DestroyImageView(view.handle);
        }
        for (const auto& image : images[integer]) {
            device.DestroyImage(image.handle);
        }
    }
}

u64 NullResources::NativeBuffer() const {
    return buffer->Native();
}

u64 NullResources::NativeImageView(AmdGpu::ImageType type, bool integer) const {
    const auto& set = views[integer ? 1 : 0];
    switch (type) {
    case AmdGpu::ImageType::Color1D:
        return set[View1D].native;
    case AmdGpu::ImageType::Color1DArray:
        return set[View1DArray].native;
    case AmdGpu::ImageType::Color2DArray:
    case AmdGpu::ImageType::Cube: // the recompiler declares cubes as 2D arrays
        return set[View2DArray].native;
    case AmdGpu::ImageType::Color2DMsaa:
    case AmdGpu::ImageType::Color2DMsaaArray:
        return set[View2DMsaa].native;
    case AmdGpu::ImageType::Color3D:
        return set[View3D].native;
    default:
        return set[View2D].native;
    }
}

} // namespace Vulkan
