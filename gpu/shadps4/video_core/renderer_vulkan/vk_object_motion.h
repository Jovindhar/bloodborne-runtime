// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: object motion vectors for temporal upscaling (bbport's docs/ROADMAP.md, step 1).
//
// G-buffer shaders preserve clip positions for the exact indexed range of each draw.
// Matching includes vertex streams, index contents and base offsets. A missing match falls
// back to camera motion. See motion_history.h for CPU bookkeeping and its regression test.

#pragma once

#include <array>
#include "video_core/renderer_backend/device.h"
#include "video_core/renderer_backend/pass.h"
#include "video_core/renderer_vulkan/motion_history.h"

#include "common/types.h"

namespace Vulkan {

using VideoCore::Backend::RenderState;

class ObjectMotion {
public:
    explicit ObjectMotion(VideoCore::Backend::Device& device);
    ~ObjectMotion();

    [[nodiscard]] bool Enabled() const noexcept {
        return enabled;
    }

    using DrawInfo = Motion::Draw;
    u32 PrepareDraw(const DrawInfo& draw);
    /// Small skeletons: whether the bone palette changed (Motion::History::Moving).
    bool Moving(const Motion::History::GateKey& key, u64 palette) {
        return history.Moving(key, palette);
    }
    /// Index range and topology hash of an indexed draw, reused across frames.
    template <class Scan>
    Motion::IndexRangeCache::Result IndexRange(const Motion::IndexRangeCache::Key& key,
                                               Scan&& scan) {
        return index_ranges.Get(key, frame, scan);
    }

    /// Rendering of a motion pipeline: the motion attachment (render-target size).
    void Attach(RenderState& state, u32 width, u32 height);

    /// Start of a frame (display pass).
    void OnFrameStart();
    void InvalidateHistory() { history.NextFrame(); history.NextFrame(); }

    /// The motion image's view for the compose pass (layout General after this call; a
    /// NativeHandle) and whether it holds this frame's vectors at `width` x `height`.
    u64 PrepareRead(u32 width, u32 height, bool& valid);
    /// The view of the last PrepareRead (layout General), or 0.
    [[nodiscard]] u64 View() const noexcept {
        return read_target ? read_target->view.native : 0;
    }
    /// The image of this size holding this frame's vectors (NativeHandle), or 0.
    [[nodiscard]] u64 Image(u32 width, u32 height) const noexcept {
        const Target* target = Find(width, height);
        return target && target->written ? target->image.native : 0;
    }

private:
    /// bbport: a motion image per render-target size. Scenes with G-buffer passes at two sizes
    /// (a reduced proxy and the full target) shared one image: every switch recreated it after
    /// a full GPU wait (Scheduler::Finish), twice a frame — ~70% of the draw recording thread's
    /// time and an idle GPU — and threw this frame's vectors away.
    struct Target {
        VideoCore::Backend::ImageAllocation image;
        VideoCore::Backend::ImageViewAllocation view;
        u32 width = 0, height = 0;
        bool written = false; ///< this frame's vectors are in the image
        bool attached = false; ///< last used as the pass's colour target

        u64 used_frame = 0;
    };
    static constexpr u32 MaxTargets = 4;

    [[nodiscard]] const Target* Find(u32 width, u32 height) const noexcept {
        for (const auto& target : targets) {
            if (target.image.handle && target.width == width && target.height == height) {
                return &target;
            }
        }
        return nullptr;
    }
    /// The image of this size, created when missing (replacing the one used longest ago; it is
    /// destroyed once the GPU is done with it, without waiting).
    Target& GetTarget(u32 width, u32 height);

    void Destroy(Target& target);

    VideoCore::Backend::Device& device;
    bool enabled = false;

    // Parameter ring (host visible): FrameSlots frames of ParamsPerFrame entries (two u32x4),
    // element 0 all zero (motion off).
    static constexpr u32 FrameSlots = 4;
    static constexpr u32 ParamsPerFrame = 8192;
    // Positions (device local): two halves (current/previous frame) of vec4; element 0 is reserved.
    static constexpr u32 PositionsPerFrame = 4u << 20;
    Motion::History history{PositionsPerFrame};
    Motion::IndexRangeCache index_ranges;
    std::array<u64, FrameSlots> params_ticks{};
    VideoCore::Backend::BufferAllocation params_buffer{};
    u32* params_mapped{};
    VideoCore::Backend::BufferAllocation positions_buffer{};

    u64 frame = 0;
    u32 params_used = 0;
    std::array<Target, MaxTargets> targets;
    const Target* read_target = nullptr; ///< of the last PrepareRead
};

} // namespace Vulkan
