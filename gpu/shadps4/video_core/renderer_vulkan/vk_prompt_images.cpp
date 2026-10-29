// SPDX-License-Identifier: GPL-2.0-or-later
// Adapted from bbport 0.6 (deadinside28/bloodborne_pc, 8185b88). Its generated key guides
// pass through the render adapter so both native Metal and MoltenVK show the active device.
#include "video_core/renderer_vulkan/vk_prompt_images.h"

#include <algorithm>
#include <cstdio>

#include "bbport_button_prompts.h"
#include "video_core/buffer_cache/buffer_cache.h"
#include "video_core/renderer_backend/commands.h"
#include "video_core/renderer_backend/device.h"
#include "video_core/renderer_backend/transfers.h"

namespace Vulkan {
namespace {
using VideoCore::Backend::Format;
using VideoCore::Backend::ImageAccess;

Format PromptFormat(Format source) {
    return source == Format::Bc7SrgbBlock ? Format::R8G8B8A8Srgb : Format::R8G8B8A8Unorm;
}

VideoCore::ImageViewInfo ViewInfo(const VideoCore::ImageViewInfo& source, bool array) {
    VideoCore::ImageViewInfo result{};
    result.type = array ? AmdGpu::ImageType::Color2DArray : AmdGpu::ImageType::Color2D;
    result.format = PromptFormat(source.format);
    result.mapping = source.mapping;
    return result;
}
} // namespace

PromptImages::PromptImages(VideoCore::Backend::Device& device_, VideoCore::Backend::Transfers& runtime_,
                           VideoCore::BufferCache& buffer_cache_)
    : device{device_}, runtime{runtime_}, buffer_cache{buffer_cache_} {}

std::unique_ptr<VideoCore::Image> PromptImages::MakeImage(u32 width, u32 height, bool array) {
    VideoCore::ImageInfo info{};
    info.pixel_format = Format::R8G8B8A8Unorm;
    info.type = array ? AmdGpu::ImageType::Color2DArray : AmdGpu::ImageType::Color2D;
    info.size = {width, height, 1};
    info.pitch = width;
    info.num_bits = 32;
    info.guest_size = width * height * 4;
    return std::make_unique<VideoCore::Image>(device, runtime, views, info);
}

void PromptImages::Update() {
    if (!BbPrompts::Enabled()) return;
    BbPrompts::Set set;
    if (!BbPrompts::Update(generation, set)) return;
    generation = set.generation;
    drawn = std::move(set.drawn);
    active = std::find(drawn.begin(), drawn.end(), true) != drawn.end();
    if (!active) return;
    icons.resize(set.count);
    constexpr u32 side = BbPrompts::IconSize;
    constexpr u32 small_side = BbPrompts::GameIconSize;
    constexpr u32 factor = side / small_side;
    small.assign(std::size_t(set.count) * small_side * small_side, 0);
    auto& stream = buffer_cache.GetStreamBuffer();
    for (u32 i = 0; i < set.count; ++i) {
        if (!drawn[i]) continue;
        const u32* pixels = set.pixels.data() + std::size_t(i) * side * side;
        for (u32 y = 0; y < small_side; ++y) {
            for (u32 x = 0; x < small_side; ++x) {
                u32 channels[4]{};
                for (u32 dy = 0; dy < factor; ++dy) for (u32 dx = 0; dx < factor; ++dx) {
                    const u32 texel = pixels[(y * factor + dy) * side + x * factor + dx];
                    for (u32 c = 0; c < 4; ++c) channels[c] += (texel >> (c * 8)) & 0xff;
                }
                u32 texel = 0;
                for (u32 c = 0; c < 4; ++c) {
                    texel |= ((channels[c] + factor * factor / 2) / (factor * factor)) << (c * 8);
                }
                small[(std::size_t(i) * small_side + y) * small_side + x] = texel;
            }
        }
        for (bool array : {false, true}) {
            auto& image = icons[i][array ? 1 : 0];
            if (!image) image = MakeImage(side, side, array);
            const u64 offset = stream.Copy(pixels, side * side * sizeof(u32), 16);
            const VideoCore::Backend::BufferImageCopy copy{
                .buffer_offset = offset, .width = side, .height = side};
            runtime.UploadImage(image.get(), &stream, std::span{&copy, 1});
        }
    }
}

VideoCore::Backend::ImageBinding PromptImages::View(u32 icon, const VideoCore::ImageViewInfo& info) {
    if (!active || !icon || icon > icons.size() || !drawn[icon - 1]) return {};
    const bool array = info.type == AmdGpu::ImageType::Color2DArray;
    if (!array && info.type != AmdGpu::ImageType::Color2D) return {};
    auto& image = icons[icon - 1][array ? 1 : 0];
    if (!image) return {};
    runtime.Transit(image.get(), ImageAccess::Sampled);
    return {.view = image->FindView(ViewInfo(info, array), false).Native(),
            .layout = runtime.BindingLayout(*image)};
}

VideoCore::Backend::ImageBinding PromptImages::AtlasView(VideoCore::Image& source,
                                                          const VideoCore::ImageViewInfo& info) {
    if (!active || !source.prompt_atlas || info.type != AmdGpu::ImageType::Color2D ||
        source.info.resources.levels != 1 || source.info.resources.layers != 1 ||
        (source.backing->desc.format != Format::Bc7UnormBlock &&
         source.backing->desc.format != Format::Bc7SrgbBlock)) return {};
    const auto cells = BbPrompts::AtlasCells(source.prompt_atlas);
    const auto needed = [&](const BbPrompts::AtlasCell& cell) {
        return cell.icon && cell.icon <= drawn.size() && drawn[cell.icon - 1];
    };
    if (std::none_of(cells.begin(), cells.end(), needed)) return {};
    // Atlases have stable image UIDs. Bound this cache if a menu repeatedly recreates them.
    if (atlases.size() > 32) atlases.clear();
    Atlas& atlas = atlases[source.image_uid];
    const u32 width = source.info.size.width, height = source.info.size.height;
    if (!atlas.image) atlas.image = MakeImage(width, height, false);
    if (atlas.upload != source.prompt_upload || atlas.generation != generation) {
        atlas.upload = source.prompt_upload;
        atlas.generation = generation;
        runtime.Transit(&source, ImageAccess::TransferSource);
        runtime.Transit(atlas.image.get(), ImageAccess::TransferDestination);
        runtime.FlushBarriers();
        device.Recorder().EndPass();
        device.Recorder().BlitImage(source.backing->native, atlas.image->backing->native,
                                    {.src_width = width, .src_height = height,
                                     .dst_width = width, .dst_height = height});
        constexpr u32 side = BbPrompts::GameIconSize;
        auto& stream = buffer_cache.GetStreamBuffer();
        for (const auto& cell : cells) {
            if (!needed(cell) || cell.x + side > width || cell.y + side > height) continue;
            const u32* pixels = small.data() + std::size_t(cell.icon - 1) * side * side;
            const u64 offset = stream.Copy(pixels, side * side * sizeof(u32), 16);
            const VideoCore::Backend::BufferImageCopy copy{
                .buffer_offset = offset, .x = s32(cell.x), .y = s32(cell.y),
                .width = side, .height = side};
            runtime.UploadImage(atlas.image.get(), &stream, std::span{&copy, 1});
        }
    }
    runtime.Transit(atlas.image.get(), ImageAccess::Sampled);
    return {.view = atlas.image->FindView(ViewInfo(info, false), false).Native(),
            .layout = runtime.BindingLayout(*atlas.image)};
}

} // namespace Vulkan
