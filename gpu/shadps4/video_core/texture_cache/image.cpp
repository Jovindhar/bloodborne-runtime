// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <set>
#include <mutex>
#include "bbport_toggles.h"
#include <ranges>
#include "bbport_metal_residency.h"
#include "common/assert.h"
#include "video_core/renderer_vulkan/liverpool_to_vk.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_resource_backend.h"
#include "video_core/renderer_vulkan/vk_state.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/blit_helper.h"
#include "video_core/texture_cache/image.h"

#include <vk_mem_alloc.h>

namespace VideoCore {

using namespace Vulkan;

Common::IncrementalIdProvider<u64> Image::global_image_uid{};

static Backend::ImageUse ImageUsage(const Backend::Capabilities& caps, const ImageInfo& info) {
    using Backend::ImageUse;
    ImageUse usage = ImageUse::TransferSource | ImageUse::TransferDestination | ImageUse::Sampled;
    if (!info.props.is_block) {
        if (info.props.is_depth) {
            usage = usage | ImageUse::DepthStencilAttachment;
        } else {
            usage = usage | ImageUse::ColorAttachment;
            if (caps.attachment_feedback_loop) {
                usage = usage | ImageUse::AttachmentFeedbackLoop;
            }
            // Always create images with storage flag to avoid needing re-creation in case of e.g
            // compute clears This sacrifices a bit of performance but is less work. ExtendedUsage
            // flag is also used.
            usage = usage | ImageUse::Storage;
        }
    } else {
        // Similarly to above, we specify storage usage. This is typically not supported by
        // compressed formats, but may be used for uncompressed views. In order to satisfy this,
        // we will also specify the extended usage bit.
        usage = usage | ImageUse::Storage;
    }

    return usage;
}

static Backend::ImageType ConvertImageType(AmdGpu::ImageType type) noexcept {
    switch (type) {
    case AmdGpu::ImageType::Color1D:
    case AmdGpu::ImageType::Color1DArray:
        return Backend::ImageType::Image1D;
    case AmdGpu::ImageType::Color2D:
    case AmdGpu::ImageType::Color2DMsaa:
    case AmdGpu::ImageType::Color2DArray:
        return Backend::ImageType::Image2D;
    case AmdGpu::ImageType::Color3D:
        return Backend::ImageType::Image3D;
    default:
        UNREACHABLE();
    }
}


Image::Image(Backend::Device& device_, const Vulkan::Instance& instance,
             Vulkan::Runtime& runtime_, Common::SlotVector<ImageView>& slot_image_views_,
             const ImageInfo& info_)
    : guest_begin{info_.guest_address}, guest_end{info_.guest_address + info_.guest_size},
      info{info_}, device{&device_}, runtime{&runtime_}, slot_image_views{&slot_image_views_} {
    BbStats::Timer timer{BbStats::t_image_create};
    if (info.pixel_format == Backend::Format::Undefined) {
        return;
    }

    image_uid = global_image_uid.Next();
    mip_hashes.resize(info.resources.levels);

    const Backend::ImageUse usage = ImageUsage(device->GetCapabilities(), info);
    usage_flags = ToVk(usage);
    format_features = Vulkan::FormatFeatureFlags(usage_flags);
    if (info.props.is_depth) {
        aspect_mask = vk::ImageAspectFlagBits::eDepth;
        if (info.props.has_stencil) {
            aspect_mask |= vk::ImageAspectFlagBits::eStencil;
        }
    }

    Backend::ImageDesc desc{
        .type = ConvertImageType(info.type),
        .format = device->SupportedFormat(info.pixel_format, usage),
        .width = info.size.width,
        .height = info.size.height,
        .depth = info.size.depth,
        .levels = static_cast<u32>(info.resources.levels),
        .layers = static_cast<u32>(info.resources.layers),
        .usage = usage,
        .views_of_3d_slices = bool(info.props.is_volume),
        .uncompressed_block_views = bool(info.props.is_block),
    };
    supported_samples = vk::SampleCountFlags(device->SupportedSampleCounts(desc));
    desc.samples = static_cast<u32>(LiverpoolToVK::NumSamples(info.num_samples, supported_samples));

    backing = &backing_images.emplace_back();
    backing->num_samples = info.num_samples;
    backing->desc = desc;
    const auto allocation = device->CreateImage(
        desc, fmt::format("Image {}x{}x{} {} {} {:#x}:{:#x} L:{} M:{} S:{}", info.size.width,
                          info.size.height, info.size.depth, AmdGpu::NameOf(info.tile_mode),
                          Backend::FormatName(info.pixel_format), info.guest_address,
                          info.guest_size, info.resources.layers, info.resources.levels,
                          info.num_samples));
    backing->handle = allocation.handle;
    backing->native = allocation.native;
    backing->size_bytes = allocation.size_bytes;
    backing->backend_state = allocation.backend_state;
}

Image::~Image() {
    // Stale handles (a moved-from image's) are ignored by the device.
    for (const BackingImage& b : backing_images) {
        if (b.handle) {
            device->DestroyImage(b.handle);
        }
    }
}


ImageView& Image::FindView(const ImageViewInfo& view_info, bool ensure_guest_samples) {
    if (ensure_guest_samples && backing->num_samples > 1 != info.num_samples > 1) {
        runtime->SetBackingSamples(this, info.num_samples);
    }
    const auto& view_infos = backing->image_view_infos;
    if (backing->last_view < view_infos.size() && view_infos[backing->last_view] == view_info) {
        return (*slot_image_views)[backing->image_view_ids[backing->last_view]];
    }
    const auto it = std::ranges::find(view_infos, view_info);
    if (it != view_infos.end()) {
        backing->last_view = static_cast<u32>(std::distance(view_infos.begin(), it));
        const auto view_id = backing->image_view_ids[backing->last_view];
        return (*slot_image_views)[view_id];
    }
    const auto view_id = slot_image_views->insert(*device, view_info, *this);
    backing->image_view_infos.emplace_back(view_info);
    backing->image_view_ids.emplace_back(view_id);
    return (*slot_image_views)[view_id];
}


} // namespace VideoCore
