// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/vk_object_motion.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "bbport_settings.h"
#include "common/assert.h"
#include "bbport_toggles.h"
#include "shader_recompiler/runtime_info.h"
#include "video_core/renderer_backend/commands.h"

namespace Vulkan {

namespace Backend = VideoCore::Backend;

ObjectMotion::ObjectMotion(Backend::Device& device_) : device{device_} {
    // Pipeline selection limits the extra attachment and vertex stores to likely
    // animated draws. A setting or environment override can still disable the path.
    if (!BbSettings::Get().object_motion ||
        BbSettings::Get().upscaler == BbSettings::UpscalerOff) {
        std::puts("Object motion: off (enable in menu or BB_OBJECT_MOTION=1)");
        return;
    }
    if (!device.GetCapabilities().vertex_stores) {
        std::printf("Object motion: vertex shader stores unsupported, off\n");
        return;
    }
    constexpr auto uses = Backend::BufferUse::Storage | Backend::BufferUse::DeviceAddress;
    // The parameters are written per draw without flushes: host-coherent memory only.
    params_buffer = device.CreateBuffer({.size_bytes = u64(1 + FrameSlots * ParamsPerFrame) * 32,
                                         .uses = uses,
                                         .memory = Backend::MemoryClass::HostUncached},
                                        "Object motion parameters");
    positions_buffer = device.CreateBuffer({.size_bytes = u64(1 + 2 * PositionsPerFrame) * 16,
                                            .uses = uses,
                                            .memory = Backend::MemoryClass::DeviceLocal},
                                           "Object motion positions");
    if (!params_buffer.handle || !positions_buffer.handle || params_buffer.mapped.empty() ||
        !params_buffer.coherent || !params_buffer.device_address ||
        !positions_buffer.device_address) {
        std::printf("Object motion: buffer creation failed, off\n");
        return;
    }
    params_mapped = reinterpret_cast<u32*>(params_buffer.mapped.data());
    std::memset(params_mapped, 0, 32);
    Shader::MotionVectors::params_address = params_buffer.device_address;
    Shader::MotionVectors::positions_address = positions_buffer.device_address;
    Shader::MotionVectors::positions_count = 1 + 2 * PositionsPerFrame;
    enabled = true;
    std::printf("Object motion: on (%u vertices per frame)\n", PositionsPerFrame);
}

ObjectMotion::~ObjectMotion() {
    // The device defers each destruction past the work that may use it.
    for (auto& target : targets) {
        Destroy(target);
    }
    if (params_buffer.handle) {
        device.DestroyBuffer(params_buffer.handle);
    }
    if (positions_buffer.handle) {
        device.DestroyBuffer(positions_buffer.handle);
    }
}

void ObjectMotion::Destroy(Target& target) {
    if (target.view.handle) {
        device.DestroyImageView(target.view.handle);
    }
    if (target.image.handle) {
        device.DestroyImage(target.image.handle);
    }
    target.view = {};
    target.image = {};
}

void ObjectMotion::OnFrameStart() {
    if (!enabled) {
        return;
    }
    if (BbStats::enabled && (frame % 600) == 0 && history.stats.draws) {
        const auto& s = history.stats;
        std::printf("Object motion: %llu draws, %llu stored, %llu with history, %llu unmatched, "
                    "%llu capacity skips, %llu invalid, %llu still (600 frames); last frame %u/%u vertices; "
                    "index ranges %llu reused, %llu scanned (%llu found changed), %zu cached\n",
                    (unsigned long long)s.draws, (unsigned long long)s.stored,
                    (unsigned long long)s.loaded, (unsigned long long)s.unmatched,
                    (unsigned long long)s.exhausted, (unsigned long long)s.invalid,
                    (unsigned long long)s.still,
                    history.Used(), PositionsPerFrame, (unsigned long long)index_ranges.stats.hits,
                    (unsigned long long)index_ranges.stats.scans,
                    (unsigned long long)index_ranges.stats.stale, index_ranges.Size());
        index_ranges.stats = {};
        history.stats = {};
    }
    history.NextFrame();
    ++frame;
    if (frame % Motion::IndexRangeCache::Unused == 0) {
        index_ranges.Trim(frame);
    }
    const u32 slot = u32(frame % FrameSlots);
    // Do not overwrite host parameters still referenced by queued GPU work.
    if (params_ticks[slot]) {
        device.Wait(params_ticks[slot]);
        params_ticks[slot] = 0;
    }
    params_used = 0;
    for (auto& target : targets) {
        target.written = false;
    }
    auto& recorder = device.Recorder();
    recorder.EndPass();
    // Last frame's stores must be visible to this frame's loads. Also finish old reads
    // before the ping-pong half they reference is reused for writes.
    using Use = VideoCore::Backend::MemoryUse;
    recorder.Barrier(Use::VertexRead | Use::VertexWrite, Use::VertexRead | Use::VertexWrite);
}

u32 ObjectMotion::PrepareDraw(const DrawInfo& draw) {
    if (!enabled || BbToggle::Disabled(1u << 29) || params_used >= ParamsPerFrame) {
        return 0;
    }
    const auto allocation = history.Prepare(draw);
    u32 flags = (allocation.store ? Shader::MotionVectors::FlagStore : 0) |
                (allocation.load ? Shader::MotionVectors::FlagLoad : 0);
    if (!flags) {
        return 0;
    }
    const u32 slot = u32(frame % FrameSlots);
    const u32 index = 1 + slot * ParamsPerFrame + params_used++;
    u32* param = params_mapped + index * 8;
    param[0] = allocation.store;
    param[1] = allocation.load;
    param[2] = allocation.vertices;
    param[3] = flags;
    param[4] = allocation.first_vertex;
    param[5] = allocation.first_instance;
    param[6] = allocation.instances;
    param[7] = 0;
    params_ticks[slot] = device.CurrentTick();
    return index;
}

ObjectMotion::Target& ObjectMotion::GetTarget(u32 width, u32 height) {
    for (auto& target : targets) {
        if (target.image.handle && target.width == width && target.height == height) {
            target.used_frame = frame;
            return target;
        }
    }
    // A free slot, else the image used longest ago (not the one being read).
    Target* slot = nullptr;
    for (auto& target : targets) {
        if (!target.image.handle) {
            slot = &target;
            break;
        }
        if (&target != read_target && (!slot || target.used_frame < slot->used_frame)) {
            slot = &target;
        }
    }
    // Destroyed once the GPU is past the work recorded so far (it may still use it).
    Destroy(*slot);
    static u32 reports = 0;
    if (BbStats::enabled && reports < 16) {
        ++reports;
        std::printf("Object motion: motion image %ux%u%s\n", width, height,
                    slot->width ? " (replacing one)" : "");
    }
    constexpr auto format = Backend::Format::R32G32B32A32Sfloat;
    slot->image = device.CreateImage(
        {.format = format,
         .width = width,
         .height = height,
         .usage = Backend::ImageUse::ColorAttachment | Backend::ImageUse::Sampled |
                  Backend::ImageUse::TransferSource,
         .mutable_format = false},
        "Object motion");
    ASSERT_MSG(slot->image.handle, "Object motion: image creation failed");
    slot->view = device.CreateImageView(
        slot->image.handle,
        {.format = format,
         .usage = Backend::ImageUse::ColorAttachment | Backend::ImageUse::Sampled},
        "Object motion");
    ASSERT_MSG(slot->view.handle, "Object motion: image view creation failed");
    slot->width = width;
    slot->height = height;
    slot->written = false;
    slot->attached = false;
    slot->used_frame = frame;
    return *slot;
}

void ObjectMotion::Attach(RenderState& state, u32 width, u32 height) {
    constexpr u32 slot = Shader::MotionVectors::Output;
    Target& target = GetTarget(width, height);
    auto& attachment = state.color_attachments[slot];
    attachment = {};
    attachment.image_view = target.view.native;
    auto& recorder = device.Recorder();
    if (!target.written || !target.attached) {
        // Clear once per frame; later reads must not erase the validity of this history.
        recorder.EndPass();
        recorder.TransitImage(target.image, Backend::ImageAspect::Color,
                              Backend::ImageAccess::ColorAttachment);
        target.attached = true;
        attachment.is_clear = !target.written;
        target.written = true;
    }
    attachment.image_layout = recorder.StateOf(target.image);
    state.num_color_attachments = std::max<u16>(state.num_color_attachments, slot + 1);
}

u64 ObjectMotion::PrepareRead(u32 width, u32 height, bool& valid) {
    // The image of this size; otherwise any image (the pass binds one, its vectors unused).
    Target* target = const_cast<Target*>(Find(width, height));
    if (!target) {
        for (auto& candidate : targets) {
            if (candidate.image.handle && (!target || candidate.used_frame > target->used_frame)) {
                target = &candidate;
            }
        }
    }
    if (!target) {
        target = &GetTarget(1, 1);
    }
    valid = target->written && width == target->width && height == target->height;
    device.Recorder().TransitImage(target->image, Backend::ImageAspect::Color,
                                   Backend::ImageAccess::ComputeRead);
    target->attached = false;
    read_target = target;
    return target->view.native;
}

} // namespace Vulkan
