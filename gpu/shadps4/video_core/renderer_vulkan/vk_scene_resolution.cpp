// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_backend/commands.h"
#include "video_core/renderer_vulkan/vk_scene_resolution.h"
#include "video_core/renderer_vulkan/vk_state.h"
#include "video_core/texture_cache/texture_cache.h"
#include "bbport_toggles.h"
#include <cstdio>
#include <cstdlib>

namespace Vulkan {
namespace Backend = VideoCore::Backend;
using Backend::ImageAccess;

SceneTargets::SceneTargets(Backend::Device& d, Backend::Transfers& r,
                           VideoCore::TextureCache& t)
    : SceneTargets(d, r, [&t](VideoCore::ImageId id, u64 uid) {
        return t.TryGetImage(id, uid);
    }) {}
SceneTargets::SceneTargets(Backend::Device& d, Backend::Transfers& r, Lookup get)
    : device{d}, runtime{r}, lookup{std::move(get)} {
    runtime.SetNativeAccessHook(&SceneTargets::OnNativeAccess, this);
    const char* force = std::getenv("BB_SCENE_STENCIL_BITS");
    force_stencil_bits = force && force[0] == '1';
}
SceneTargets::~SceneTargets() {
    runtime.SetNativeAccessHook(nullptr, nullptr);
    // The device defers each destruction past the work that may use it.
    for (auto& [key, entry] : entries) Destroy(*entry);
}
void SceneTargets::Destroy(Entry& e) {
    for (auto& [info, view] : e.views) device.DestroyImageView(view.handle);
    e.views.clear();
    if (e.image.handle) device.DestroyImage(e.image.handle);
    e.image = {};
}
bool SceneTargets::Blittable(Backend::Format format) const {
    // Called for every attachment of every draw: the device keeps its answers.
    return device.CanBlit(format);
}
bool SceneTargets::ShaderResampled(const VideoCore::Image& image) const {
    const auto format = image.backing->desc.format;
    if (!image.info.props.is_depth || (Blittable(format) && !force_stencil_bits)) return false;
    return runtime.CanResampleDepth(
        format, bool(image.aspect_mask & vk::ImageAspectFlagBits::eStencil));
}
// 1 for the scene size, 2 for half resolution (the game's post-processing chain), else 0.
// Half resolution needs a scene size that halves exactly: the proxy keeps the scene's factor,
// so the shaders' FragCoord conversion (1920 / scene width) stays correct for it.
static u32 SizeDivisor(const VideoCore::ImageInfo& i, SceneResolution::Size scene) {
    if (i.size.width == 1920 && i.size.height == 1080) return 1;
    if (i.size.width == 960 && i.size.height == 540 && scene.width % 2 == 0 &&
        scene.height % 2 == 0 && !BbToggle::Disabled(BbToggle::SceneHalfRes)) {
        return 2;
    }
    return 0;
}
bool SceneTargets::Eligible(const VideoCore::Image& image) const {
    const auto& i = image.info;
    const u32 div = SizeDivisor(i, size);
    if (!div || i.size.depth != 1 || i.resources.layers != 1 || i.num_samples != 1 ||
        i.props.is_block || !image.backing || image.backing->num_samples != 1) {
        return false;
    }
    if (i.resources.levels != 1) {
        // Mip chains (the bloom pyramid at half resolution): one blitted proxy per level.
        return div == 2 && i.resources.levels <= 16 && !i.props.is_depth &&
               Blittable(image.backing->desc.format);
    }
    return Blittable(image.backing->desc.format) || ShaderResampled(image);
}
bool SceneTargets::EligibleScene(const VideoCore::Image& image) const {
    return image.info.size.width == 1920 && image.info.size.height == 1080 && Eligible(image);
}
SceneResolution::Size SceneTargets::ProxySize(const VideoCore::Image& image, u32 level) const {
    const u32 div = std::max(1u, SizeDivisor(image.info, size));
    return {std::max(1u, size.width / div >> level), std::max(1u, size.height / div >> level)};
}
bool SceneTargets::SetSize(SceneResolution::Size next) {
    if (next == size) return false;
    ResolveAll();
    device.Recorder().Finish();
    for (auto& [key, entry] : entries) Destroy(*entry);
    entries.clear(); // no command buffer can still reference these images/views
    tracked.clear();
    ++generation;
    recent = {};
    size = next;
    std::printf("Scene resolution: raster %ux%u, half-resolution targets %ux%u, UI 1920x1080 "
                "(live)\n", size.width, size.height, size.width / 2, size.height / 2);
    return true;
}
void SceneTargets::ResolveAll() {
    for (auto& [key, entry] : entries) {
        if (entry->state.dirty) {
            if (auto* image = lookup(entry->source, entry->uid)) Copy(*entry, *image, true);
        }
    }
}
void SceneTargets::NativeAccess(VideoCore::Image& image, bool writes) {
    if (copying || !image.scene_proxy) return; // most images never had a proxy
    const u32 levels = std::min(16u, image.info.resources.levels);
    for (u32 level = 0; level < levels; ++level) {
        const auto it = entries.find(Key(image.image_uid, level));
        if (it == entries.end()) continue;
        auto& entry = *it->second;
        if (entry.state.dirty) Copy(entry, image, true);
        if (writes) entry.state.NativeWrite();
    }
}
void SceneTargets::Transition(Entry& e, Backend::ImageAspect aspect, ImageAccess access) {
    auto& recorder = device.Recorder();
    recorder.EndPass();
    recorder.TransitImage(e.image, aspect, access);
    e.access = access;
}
void SceneTargets::Copy(Entry& e, VideoCore::Image& original, bool to_native) {
    copying = true;
    auto& recorder = device.Recorder();
    recorder.EndPass();
    const Backend::ScopedTransferMark mark{
        runtime, to_native ? "scene proxy resolve" : "scene proxy fill", original};
    if (debug) {
        std::printf("Scene %s: %s %ux%u level %u\n", to_native ? "resolve" : "fill",
                    Backend::FormatName(original.info.pixel_format), original.info.size.width,
                    original.info.size.height, e.level);
    }
    runtime.ResolvePendingClear(&original, Backend::Transfers::ClearResolveReason::Copy);
    const auto aspect = FromVk(original.aspect_mask);
    const u64 native = original.backing->native;
    const u32 native_width = std::max(1u, original.info.size.width >> e.level);
    const u32 native_height = std::max(1u, original.info.size.height >> e.level);
    if (ShaderResampled(original)) {
        runtime.Transit(&original, to_native ? ImageAccess::DepthAttachment : ImageAccess::Sampled);
        runtime.FlushBarriers();
        Transition(e, aspect, to_native ? ImageAccess::Sampled : ImageAccess::DepthAttachment);
        const u32 proxy_state = recorder.StateOf(e.image);
        const u32 native_state = runtime.BindingLayout(original);
        const bool stencil = Backend::Has(aspect, Backend::ImageAspect::Stencil);
        const auto format = original.backing->desc.format;
        if (to_native) {
            runtime.ResampleDepth(e.image.native, proxy_state, native, native_state, format,
                                  stencil, original.info.size.width, original.info.size.height);
        } else {
            runtime.ResampleDepth(native, native_state, e.image.native, proxy_state, format,
                                  stencil, e.desc.width, e.desc.height);
        }
        // (Depth proxies are single-level: Eligible.)
        if (to_native) e.state.Resolved(); else e.state.CopiedToProxy();
        copying = false;
        return;
    }
    const VideoCore::SubresourceRange level_range{.base = {e.level, 0}, .extent = {1, 1}};
    runtime.Transit(&original,
                    to_native ? ImageAccess::TransferDestination : ImageAccess::TransferSource,
                    level_range);
    runtime.FlushBarriers();
    Transition(e, aspect,
               to_native ? ImageAccess::TransferSource : ImageAccess::TransferDestination);
    for (const auto plane : {Backend::ImageAspect::Color, Backend::ImageAspect::Depth,
                             Backend::ImageAspect::Stencil}) {
        if (!Backend::Has(aspect, plane)) continue;
        // Nearest preserves depth/stencil and avoids assuming linear blit support for
        // integer G-buffer formats. FSR reconstructs color at the HDR scene boundary.
        if (to_native) {
            recorder.BlitImage(e.image.native, native,
                               {.aspect = plane, .src_level = 0, .src_width = e.desc.width,
                                .src_height = e.desc.height, .dst_level = e.level,
                                .dst_width = native_width, .dst_height = native_height});
        } else {
            recorder.BlitImage(native, e.image.native,
                               {.aspect = plane, .src_level = e.level, .src_width = native_width,
                                .src_height = native_height, .dst_level = 0,
                                .dst_width = e.desc.width, .dst_height = e.desc.height});
        }
    }
    if (to_native) e.state.Resolved(); else e.state.CopiedToProxy();
    copying = false;
}
SceneTargets::Entry& SceneTargets::Get(VideoCore::ImageId id, u32 level) {
    auto& original = *lookup(id, 0);
    const u64 key = Key(original.image_uid, level);
    // A draw has up to six targets, mostly the same as the previous draw's.
    for (const auto& [recent_key, recent_entry] : recent) {
        if (recent_entry && recent_key == key) {
            if (!recent_entry->state.valid) Copy(*recent_entry, original, false);
            return *recent_entry;
        }
    }
    auto& entry = entries[key];
    original.scene_proxy = true;
    if (!entry) {
        entry = std::make_unique<Entry>();
        entry->source = id;
        entry->uid = original.image_uid;
        entry->level = level;
        entry->desc = original.backing->desc;
        const auto proxy = ProxySize(original, level);
        entry->desc.width = proxy.width;
        entry->desc.height = proxy.height;
        entry->desc.levels = 1;
        entry->image = device.CreateImage(entry->desc, "Scene proxy");
        ASSERT_MSG(entry->image.handle, "Scene resolution: proxy creation failed");
        tracked.insert(original.image_uid);
    }
    recent[recent_next++ % recent.size()] = {key, entry.get()};
    if (!entry->state.valid) Copy(*entry, original, false);
    return *entry;
}
u64 SceneTargets::View(Entry& e, const VideoCore::Image& original,
                       const VideoCore::ImageViewInfo& info) {
    for (const auto& [key, view] : e.views) if (key == info) return view.native;
    const auto aspect = original.info.props.is_depth
        ? (info.is_storage ? FromVk(original.aspect_mask) : Backend::ImageAspect::Depth)
        : Backend::ImageAspect::Color;
    const auto format = original.info.props.is_depth ? original.backing->desc.format : info.format;
    // The proxy's uses, as views without a usage of their own had them.
    const auto view = device.CreateImageView(
        e.image.handle,
        {.format = format, .aspect = aspect, .mapping = info.mapping, .usage = e.desc.usage},
        "Scene proxy");
    ASSERT_MSG(view.handle, "Scene resolution: proxy view creation failed");
    e.views.emplace_back(info, view);
    return view.native;
}
SceneTargets::Target SceneTargets::Attachment(VideoCore::ImageId id,
                                               const VideoCore::ImageViewInfo& info) {
    auto& original = *lookup(id, 0);
    auto& e = Get(id, info.range.base.level);
    const bool depth = original.info.props.is_depth;
    const auto access = depth ? ImageAccess::DepthAttachment : ImageAccess::ColorAttachment;
    // Consecutive draws can stay in the same render pass.
    if (e.access != access) Transition(e, FromVk(original.aspect_mask), access);
    e.state.ProxyWrite();
    auto attachment_info = info;
    attachment_info.is_storage = true; // include stencil in attachment views
    return {e.image.native, View(e, original, attachment_info), device.Recorder().StateOf(e.image)};
}
bool SceneTargets::ProxyCurrent(const VideoCore::Image& image, u32 level, u32 levels) const {
    if (copying || !image.scene_proxy || image.info.props.is_depth || levels != 1) {
        return false;
    }
    const auto it = entries.find(Key(image.image_uid, level));
    return it != entries.end() && it->second->state.valid;
}
u32 SceneTargets::PrepareSampleLayout(const VideoCore::Image& image, u32 level) {
    auto& e = *entries.find(Key(image.image_uid, level))->second;
    if (e.access != ImageAccess::Sampled) {
        Transition(e, FromVk(image.aspect_mask), ImageAccess::Sampled);
    }
    return device.Recorder().StateOf(e.image);
}
std::optional<SceneTargets::Target> SceneTargets::SampleProxy(
    const VideoCore::Image& image, const VideoCore::ImageViewInfo& info) {
    const u32 level = info.range.base.level;
    if (!ProxyCurrent(image, level, info.range.extent.levels)) {
        return std::nullopt;
    }
    auto& e = *entries.find(Key(image.image_uid, level))->second;
    const u32 layout = PrepareSampleLayout(image, level);
    return Target{e.image.native, View(e, image, info), layout};
}
SceneTargets::Target SceneTargets::Read(VideoCore::ImageId id,
                                       const VideoCore::ImageViewInfo& info,
                                       ImageAccess access) {
    auto& original = *lookup(id, 0);
    auto& e = Get(id);
    Transition(e, FromVk(original.aspect_mask), access);
    return {e.image.native, View(e, original, info), device.Recorder().StateOf(e.image)};
}
} // namespace Vulkan
