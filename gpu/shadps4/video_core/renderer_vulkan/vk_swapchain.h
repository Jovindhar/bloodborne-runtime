// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include "common/types.h"
#include "video_core/renderer_backend/swapchain.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Frontend {
class WindowSDL;
}

namespace Vulkan {

class Instance;
class Scheduler;

/// The render adapter's swapchain on Vulkan; the semaphores stay Vulkan's (the presenter's
/// submissions wait on them). With the Metal frame (BB_METAL_FRAME=1) the window's CAMetalLayer
/// is presented through Metal instead (renderer_metal/mtl_present.h, PresentFrame): no images
/// of its own and no semaphores (the present is ordered on the queue).
class Swapchain final : public VideoCore::Backend::Swapchain {
public:
    explicit Swapchain(const Instance& instance, const Frontend::WindowSDL& window);
    ~Swapchain() override;

    void Resize(u32 width, u32 height) override {
        Recreate(width, height);
    }
    bool Acquire() override {
        return AcquireNextImage();
    }
    bool Present() override;
    [[nodiscard]] VideoCore::Backend::Format ImageFormat() const override;
    [[nodiscard]] u32 Width() const override {
        return width;
    }
    [[nodiscard]] u32 Height() const override {
        return height;
    }
    [[nodiscard]] u32 ImageCount() const override {
        return image_count;
    }
    [[nodiscard]] u64 NativeImage() const override {
        return reinterpret_cast<u64>(static_cast<VkImage>(Image()));
    }
    [[nodiscard]] u64 NativeImageView() const override {
        return reinterpret_cast<u64>(static_cast<VkImageView>(ImageView()));
    }

    /// Creates (or recreates) the swapchain with a given size.
    void Create(u32 width, u32 height);

    /// Recreates the swapchain with a given size and current surface.
    void Recreate(u32 width, u32 height);

    /// Acquires the next image in the swapchain.
    bool AcquireNextImage();


    vk::SurfaceKHR GetSurface() const {
        return surface;
    }

    vk::Image Image() const {
        return images[image_index];
    }

    vk::ImageView ImageView() const {
        return images_view[image_index];
    }

    vk::SurfaceFormatKHR GetSurfaceFormat() const {
        return surface_format;
    }

    vk::SwapchainKHR GetHandle() const {
        return swapchain;
    }

    /// The window is presented through Metal (the Metal frame).
    [[nodiscard]] bool IsMetal() const noexcept {
        return metal_layer != nullptr;
    }
    /// With the Metal frame: the next present also writes the drawable to `path`
    /// (MetalPresent::Present).
    void DumpNextPresent(std::string path) {
        dump_path = std::move(path);
    }
    /// With the Metal frame: draws `frame` into the window's next drawable where `fit` puts it
    /// (its destination offsets), `overlay` over it, and presents it, after the frame's work on
    /// the queue; `done` once the GPU has read the frame. False when nothing was shown.
    bool PresentFrame(vk::Image frame, const vk::ImageBlit& fit,
                      std::function<void(void*, void*, u32, u32)> overlay,
                      std::function<void()> done);

    /// bbport: false while the window is minimised (0x0 surface): frames are not presented.
    bool IsPresentable() const override {
        return !zero_extent && (swapchain || metal_layer);
    }

    u32 GetWidth() const {
        return width;
    }

    u32 GetHeight() const {
        return height;
    }

    u32 GetImageCount() const {
        return image_count;
    }

    u32 GetFrameIndex() const {
        return frame_index;
    }

    vk::Extent2D GetExtent() const {
        return extent;
    }

    /// Null with the Metal frame.
    [[nodiscard]] vk::Semaphore GetImageAcquiredSemaphore() const {
        return image_acquired.empty() ? vk::Semaphore{} : image_acquired[frame_index];
    }

    [[nodiscard]] vk::Semaphore GetPresentReadySemaphore() const {
        return present_ready.empty() ? vk::Semaphore{} : present_ready[image_index];
    }

    bool HasHDR() const override {
        return supports_hdr;
    }

    void SetHDR(bool hdr) override;

    bool GetHDR() const override {
        return needs_hdr;
    }

private:
    /// Selects the best available swapchain image format
    void FindPresentFormat();

    /// Selects the best available present mode
    void FindPresentMode();

    /// Sets the surface properties according to device capabilities
    void SetSurfaceProperties();

    /// Destroys current swapchain resources
    void Destroy();

    /// Performs creation of image views and framebuffers from the swapchain images
    void SetupImages();

    /// Creates the image acquired and present ready semaphores
    void RefreshSemaphores();

    // The Metal frame's layer (metal_layer set).
    void CreateMetal();

private:
    const Instance& instance;
    const Frontend::WindowSDL& window;
    vk::SwapchainKHR swapchain{};
    vk::SurfaceKHR surface{};
    vk::SurfaceFormatKHR surface_format;
    vk::Format view_format;
    vk::PresentModeKHR present_mode;
    vk::Extent2D extent;
    vk::SurfaceTransformFlagBitsKHR transform;
    vk::CompositeAlphaFlagBitsKHR composite_alpha;
    std::vector<vk::Image> images;
    std::vector<vk::ImageView> images_view;
    std::vector<vk::Semaphore> image_acquired;
    std::vector<vk::Semaphore> present_ready;
    u32 width = 0;
    u32 height = 0;
    u32 image_count = 0;
    u32 image_index = 0;
    u32 frame_index = 0;
    bool needs_recreation = true;
    bool zero_extent = false; ///< bbport: the surface was 0x0 (minimised) at the last Create
    bool needs_hdr = false;    // The game requested HDR swapchain
    bool supports_hdr = false; // SC supports HDR output

    void* metal_layer{}; ///< the window's CAMetalLayer, with the Metal frame
    std::string dump_path; ///< DumpNextPresent
};

} // namespace Vulkan
