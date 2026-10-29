// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/vk_camera_motion.h"

#include <chrono>
#include <boost/container/static_vector.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "bbport_toggles.h"

#include "video_core/host_shaders/camera_motion_comp.h"
#include "video_core/host_shaders/camera_motion_debug_comp.h"
#include "video_core/renderer_backend/commands.h"
#include "video_core/renderer_backend/transfers.h"
#include "video_core/renderer_vulkan/vk_object_motion.h"
#include "video_core/texture_cache/texture_cache.h"

namespace Vulkan {

namespace {

struct PushConstants {
    std::array<float, 12> reproject;
    std::array<float, 4> proj;
    std::array<float, 4> prev_proj;
    std::array<float, 2> size;
    std::array<float, 2> jitter;
    std::array<float, 2> previous_jitter;
    u32 mode;
};

/// a * b for 3x4 affine matrices (rows [R | t]).
std::array<float, 12> Multiply(const std::array<float, 12>& a, const std::array<float, 12>& b) {
    std::array<float, 12> out{};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 4; ++c) {
            float v = c == 3 ? a[r * 4 + 3] : 0.0f;
            for (int k = 0; k < 3; ++k) {
                v += a[r * 4 + k] * b[k * 4 + c];
            }
            out[r * 4 + c] = v;
        }
    }
    return out;
}

/// The inverse of a 3x4 affine matrix [M | t]: [M^-1 | -M^-1 t].
std::array<float, 12> InverseAffine(const std::array<float, 12>& m) {
    const float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9],
                i = m[10];
    const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (std::abs(det) < 1e-12f) {
        return m;
    }
    const float s = 1.0f / det;
    const std::array<float, 9> r = {(e * i - f * h) * s, (c * h - b * i) * s, (b * f - c * e) * s,
                                    (f * g - d * i) * s, (a * i - c * g) * s, (c * d - a * f) * s,
                                    (d * h - e * g) * s, (b * g - a * h) * s, (a * e - b * d) * s};
    std::array<float, 12> out{};
    for (int row = 0; row < 3; ++row) {
        out[row * 4 + 0] = r[row * 3 + 0];
        out[row * 4 + 1] = r[row * 3 + 1];
        out[row * 4 + 2] = r[row * 3 + 2];
        out[row * 4 + 3] = -(r[row * 3 + 0] * m[3] + r[row * 3 + 1] * m[7] + r[row * 3 + 2] * m[11]);
    }
    return out;
}

} // namespace

CameraMotion::CameraMotion(VideoCore::Backend::Device& device_,
                           VideoCore::TextureCache& texture_cache_,
                           VideoCore::Backend::Transfers& runtime_)
    : device{device_}, texture_cache{texture_cache_}, runtime{runtime_} {
    const char* env = std::getenv("BB_DEBUG_MOTION");
    debug_overlay = env && env[0] == '1';
    const char* upscaler = std::getenv("BB_UPSCALER");
    // The upscaler can be switched on from the menu at any time: the camera is always tracked
    // unless BB_UPSCALER=none.
    for_upscaler = !(upscaler && std::strcmp(upscaler, "none") == 0);
    namespace Backend = VideoCore::Backend;
    const auto compute_slots = [](std::initializer_list<Backend::BindingKind> kinds) {
        boost::container::static_vector<Backend::BindingSlot, 4> result;
        for (const auto kind : kinds) {
            result.push_back({.kind = kind, .stages = Backend::ShaderStage::Compute});
        }
        return result;
    };
    if (for_upscaler) {
        const auto slots = compute_slots({Backend::BindingKind::SampledImage,
                                          Backend::BindingKind::StorageImage,
                                          Backend::BindingKind::SampledImage});
        motion_pipeline = Backend::MakeCompute(device, CAMERA_MOTION_COMP,
                                               {slots.data(), slots.size()}, sizeof(PushConstants),
                                               "camera motion");
    }
    if (!debug_overlay) {
        return;
    }
    const auto slots =
        compute_slots({Backend::BindingKind::SampledImage, Backend::BindingKind::StorageImage,
                       Backend::BindingKind::StorageBuffer, Backend::BindingKind::StorageBuffer});
    overlay_pipeline = Backend::MakeCompute(device, CAMERA_MOTION_DEBUG_COMP,
                                            {slots.data(), slots.size()}, sizeof(PushConstants),
                                            "camera motion overlay");
    for (auto& frame : frames) {
        frame = std::make_unique<VideoCore::Buffer>(device, 0, 3840ull * 2160 * 4,
                                                    VideoCore::MemoryType::DeviceLocal);
    }
    std::printf("GPU: camera motion debug overlay on\n");
}

CameraMotion::~CameraMotion() = default;

float CameraMotion::VerticalFov() const noexcept {
    return 2.0f * std::atan(1.0f / std::abs(current.proj[1]));
}

float CameraMotion::Near() const noexcept {
    // depth = zs + zo / z is 0 at the near plane.
    return -current.proj[3] / current.proj[2];
}

void CameraMotion::PrintState(int frame) const {
    const auto print = [](const char* name, const float* v, int n) {
        std::printf(" %s", name);
        for (int i = 0; i < n; ++i) {
            std::printf(" %.9g", v[i]);
        }
    };
    std::printf("Dump camera: frame %d", frame);
    print("view", current.view.data(), 12);
    print("prev_view", previous.view.data(), 12);
    print("proj", current.proj.data(), 4);
    print("prev_proj", previous.proj.data(), 4);
    print("jitter", jitter.data(), 2);
    print("prev_jitter", previous_jitter.data(), 2);
    std::printf("\n");
}

std::array<std::array<float, 4>, 3> CameraMotion::TaaDepthParameters() const noexcept {
    const auto transform = Multiply(previous.view, current.inv_view);
    return {current.proj, previous.proj,
            {transform[8], transform[9], transform[10], transform[11]}};
}

u64 CameraMotion::ObjectMotionView() const noexcept {
    return object_motion && object_motion->Enabled() ? object_motion->View() : 0;
}

u64 CameraMotion::ObjectMotionImage(u32 width, u32 height) const noexcept {
    return object_motion && object_motion->Enabled() ? object_motion->Image(width, height) : 0;
}

void CameraMotion::RecordMotion(u64 depth_view, u64 motion_view, u32 width, u32 height) {
    CommitFrameCamera();
    bool object_valid = false;
    const u64 object_view = object_motion && object_motion->Enabled()
        ? object_motion->PrepareRead(width, height, object_valid) : depth_view;
    const PushConstants push{
        .reproject = Multiply(previous.view, current.inv_view),
        .proj = current.proj,
        .prev_proj = previous.proj,
        .size = {float(width), float(height)},
        .jitter = jitter,
        .previous_jitter = previous_jitter,
        .mode = object_valid ? 1u : 0u,
    };
    // bbport: everything the pass reads is captured: it may be recorded on a recording thread
    // while this thread goes on with the next frame's camera.
    namespace Backend = VideoCore::Backend;
    constexpr u32 General = Backend::GeneralLayout;
    const std::array images{
        Backend::ImageBinding{.view = depth_view, .layout = General},
        Backend::ImageBinding{.view = motion_view, .layout = General},
        Backend::ImageBinding{.view = object_view, .layout = General},
    };
    auto& recorder = device.Recorder();
    const std::array writes{
        Backend::SlotWrite{.slot = 0, .count = 1, .kind = Backend::BindingKind::SampledImage,
                           .images = &images[0]},
        Backend::SlotWrite{.slot = 1, .count = 1, .kind = Backend::BindingKind::StorageImage,
                           .images = &images[1]},
        Backend::SlotWrite{.slot = 2, .count = 1, .kind = Backend::BindingKind::SampledImage,
                           .images = &images[2]},
    };
    recorder.BindResources(motion_pipeline.Allocation(), writes,
                           {reinterpret_cast<const u8*>(&push), sizeof(push)}, images, {});
    const u32 groups_x = (width + 7) / 8, groups_y = (height + 7) / 8;
    recorder.Dispatch({.pipeline = motion_pipeline.Allocation().native,
                       .groups = {groups_x, groups_y, 1}},
                      {.kind = Backend::CrumbKind::Dispatch,
                       .name = "camera motion",
                       .count = {groups_x, groups_y, 1}});
}

void CameraMotion::OnConstants(const float* data) {
    // Scene constants: far plane 3000, 1/far, and the render size.
    if (data[0] != 3000.0f || data[4] < 64.0f || data[5] < 64.0f ||
        std::abs(data[1] * data[0] - 1.0f) > 1e-3f) {
        return;
    }
    // bbport: the first constants of each G-buffer pass are its camera. A frame may have more
    // than one such pass (another camera before the main scene): the camera goes with the pass
    // whose depth the motion vectors use (CommitFrameCamera), not with the frame's first pass.
    if (pass_has_camera) {
        return;
    }
    std::memcpy(pass_camera.view.data(), data + 8, 12 * sizeof(float));
    std::memcpy(pass_camera.inv_view.data(), data + 180, 12 * sizeof(float));
    // bbport: BB_CAMERA_INVERSE=own: the inverse of the view matrix itself instead of the one the
    // game stores beside it (A/B; they agree to float precision). BB_CAMERA_LOG prints the gap.
    static const bool own_inverse = [] {
        const char* env = std::getenv("BB_CAMERA_INVERSE");
        return env && std::strcmp(env, "own") == 0;
    }();
    const auto computed_inverse = InverseAffine(pass_camera.view);
    float inverse_gap = 0.0f;
    for (int i = 0; i < 12; ++i) {
        inverse_gap = std::max(inverse_gap, std::abs(computed_inverse[i] - pass_camera.inv_view[i]));
    }
    const auto game_inverse = pass_camera.inv_view;
    if (own_inverse != BbToggle::Disabled(BbToggle::CameraOwnInverse)) {
        pass_camera.inv_view = computed_inverse;
    }
    // bbport: BB_CAMERA_LOG=1 prints the camera position every 100 ms (scripted tests of how far
    // the player moves in a given time at different frame rates).
    static const bool camera_log = std::getenv("BB_CAMERA_LOG") != nullptr;
    if (camera_log) {
        using Clock = std::chrono::steady_clock;
        static const auto start = Clock::now();
        static auto next = start;
        if (const auto now = Clock::now(); now >= next) {
            next = now + std::chrono::milliseconds(100);
            std::printf("Camera: %.3f s at %.3f %.3f %.3f (inverse of view: %.3f %.3f %.3f, largest "
                        "difference %.4f)\n",
                        std::chrono::duration<double>(now - start).count(), game_inverse[3],
                        game_inverse[7], game_inverse[11], computed_inverse[3], computed_inverse[7],
                        computed_inverse[11], inverse_gap);
        }
    }
    // bbport: the projection as the motion shaders use it: ndc +y down the screen. 0.2 assumed the
    // viewport flips y (ndc +y up, as at Yahar'gul: yscale -540), 0.3 that it does not; the
    // viewport of the frame's G-buffer pass decides (BB_CAMERA_Y=up/down forces one, for tests).
    static const float forced_y = [] {
        const char* env = std::getenv("BB_CAMERA_Y");
        return !env ? 0.0f : std::strcmp(env, "up") == 0 ? -1.0f : std::strcmp(env, "down") == 0 ? 1.0f : 0.0f;
    }();
    const float y_sign = (forced_y != 0.0f ? forced_y : gbuffer_y_sign) *
                         (BbToggle::Disabled(BbToggle::CameraYFlip) ? -1.0f : 1.0f);
    pass_camera.proj = {data[52] * gbuffer_x_sign, data[57] * y_sign,
                    data[62], data[63]};
    {
        // bbport: the projection's y scale sign, printed when it changes (see the G-buffer
        // viewport line): which way view +y goes on the screen.
        static int last_sign = 0;
        const int sign = (data[57] < 0.0f ? -2 : 2) + (gbuffer_y_sign < 0.0f ? -1 : 1);
        if (sign != last_sign) {
            last_sign = sign;
            std::printf("Camera motion: projection x %.4f y %.4f z %.6f %.6f, G-buffer viewport y "
                        "%s: screen y follows view %s\n",
                        data[52], data[57], data[62], data[63],
                        gbuffer_y_sign < 0.0f ? "flipped" : "not flipped",
                        pass_camera.proj[1] < 0.0f ? "-y" : "+y");
        }
    }
    pass_camera.valid = pass_camera.proj[0] != 0.0f && pass_camera.proj[1] != 0.0f;
    const std::array<u32, 2> size{u32(data[4]), u32(data[5])};
    if (size != render_size) {
        std::printf("Camera motion: scene render size %ux%u\n", size[0], size[1]);
        render_size = size;
    }
    pass_has_camera = true;
    frame_has_camera = true;
    if (frame_gbuffer_passes <= pass_positions.size() && frame_gbuffer_passes > 0) {
        // World position of this pass's camera (diagnostics: frames with several passes).
        const auto& v = pass_camera.view;
        pass_positions[frame_gbuffer_passes - 1] = {
            -(v[0] * v[3] + v[4] * v[7] + v[8] * v[11]),
            -(v[1] * v[3] + v[5] * v[7] + v[9] * v[11]),
            -(v[2] * v[3] + v[6] * v[7] + v[10] * v[11])};
    }
}

void CameraMotion::OnGBufferPass(VideoCore::ImageId depth, float x_sign, float y_sign) {
    if (depth != depth_id) {
        // Another depth target: another G-buffer pass, with its own camera.
        depth_id = depth;
        pass_has_camera = false;
        ++frame_gbuffer_passes;
    }
    gbuffer_x_sign = x_sign;
    gbuffer_y_sign = y_sign;
}

void CameraMotion::CommitFrameCamera() {
    if (committed || !pass_has_camera) {
        return;
    }
    previous = current;
    current = pass_camera;
    committed = true;
}

void CameraMotion::OnDisplayPass(VideoCore::ImageId frame) {
    CommitFrameCamera();
    if (debug_overlay && frame && depth_id && current.valid && previous.valid) {
        Overlay(frame);
    }
    if (frame_gbuffer_passes > 1) {
        static auto next = std::chrono::steady_clock::now();
        if (const auto now = std::chrono::steady_clock::now(); now >= next) {
            next = now + std::chrono::seconds(2);
            std::printf("Camera motion: %u G-buffer passes this frame, cameras at", frame_gbuffer_passes);
            for (u32 i = 0; i < std::min<u32>(frame_gbuffer_passes, pass_positions.size()); ++i) {
                std::printf(" (%.2f %.2f %.2f)", pass_positions[i][0], pass_positions[i][1],
                            pass_positions[i][2]);
            }
            std::printf("; the last pass's camera goes with its depth\n");
        }
    }
    if (!frame_has_camera) InvalidateHistory();
    frame_has_camera = false;
    pass_has_camera = false;
    committed = false;
    frame_gbuffer_passes = 0;
    depth_id = {};
}

void CameraMotion::Overlay(VideoCore::ImageId frame) {
    namespace Backend = VideoCore::Backend;
    auto& depth = texture_cache.GetImage(depth_id);
    auto& color = texture_cache.GetImage(frame);
    const auto depth_format = depth.info.pixel_format;
    if ((depth_format != Backend::Format::D32Sfloat &&
         depth_format != Backend::Format::D32SfloatS8Uint) ||
        color.info.pixel_format != Backend::Format::R8G8B8A8Unorm ||
        !Backend::Has(color.backing->desc.usage, Backend::ImageUse::Storage) ||
        color.info.size.width != depth.info.size.width ||
        color.info.size.height != depth.info.size.height) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            std::printf("Camera motion: overlay skipped (depth %s, frame %s %ux%u)\n",
                        Backend::FormatName(depth_format),
                        Backend::FormatName(color.info.pixel_format), color.info.size.width,
                        color.info.size.height);
        }
        return;
    }
    // The texture cache's views: it keeps them as long as the work recorded with them may run
    // (views made here and released after the dispatch were destroyed while the recording
    // thread's later command buffer still held them: Metal API validation).
    VideoCore::ImageViewInfo depth_info{};
    depth_info.format = Backend::Format::D32Sfloat; // the depth aspect
    VideoCore::ImageViewInfo color_info{};
    color_info.format = Backend::Format::R8G8B8A8Unorm;
    color_info.is_storage = true;
    const u64 depth_view = depth.FindView(depth_info).Native();
    const u64 color_view = color.FindView(color_info).Native();

    auto& recorder = device.Recorder();
    recorder.EndPass();
    runtime.Transit(&depth, Backend::ImageAccess::ComputeRead);
    runtime.Transit(&color, Backend::ImageAccess::ComputeReadWrite);
    runtime.FlushBarriers();

    const PushConstants push{
        .reproject = Multiply(previous.view, current.inv_view),
        .proj = current.proj,
        .prev_proj = previous.proj,
        .size = {float(color.info.size.width), float(color.info.size.height)},
        .jitter = jitter,
        .previous_jitter = previous_jitter,
        .mode = BbToggle::Disabled(1u << 20)   ? 1u
                : BbToggle::Disabled(1u << 21) ? 2u
                : BbToggle::Disabled(1u << 22) ? 3u
                : BbToggle::Disabled(1u << 23) ? 4u
                                               : 0u,
    };
    static u32 log_counter = 0;
    if (++log_counter % 200 == 0) {
        const auto& m = push.reproject;
        std::printf("Camera motion: proj %g %g %g %g prev %g %g %g %g\n"
                    "  view  %8.4f %8.4f %8.4f %9.3f | %8.4f %8.4f %8.4f %9.3f | %8.4f %8.4f %8.4f %9.3f\n"
                    "  reproj %8.4f %8.4f %8.4f %9.4f | %8.4f %8.4f %8.4f %9.4f | %8.4f %8.4f %8.4f %9.4f\n"
                    "  depth %s %ux%u, frame %ux%u\n",
                    push.proj[0], push.proj[1], push.proj[2], push.proj[3], push.prev_proj[0],
                    push.prev_proj[1], push.prev_proj[2], push.prev_proj[3], current.view[0],
                    current.view[1], current.view[2], current.view[3], current.view[4],
                    current.view[5], current.view[6], current.view[7], current.view[8],
                    current.view[9], current.view[10], current.view[11], m[0], m[1], m[2], m[3],
                    m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11],
                    Backend::FormatName(depth_format), depth.info.size.width,
                    depth.info.size.height, color.info.size.width, color.info.size.height);
    }
    const auto& prev_frame = *frames[frame_index ^ 1];
    const auto& next_frame = *frames[frame_index];
    frame_index ^= 1;
    const std::array images{
        Backend::ImageBinding{.view = depth_view, .layout = Backend::GeneralLayout},
        Backend::ImageBinding{.view = color_view, .layout = Backend::GeneralLayout},
    };
    const std::array buffers{
        VideoCore::BindingOf(prev_frame, 0, prev_frame.SizeBytes()),
        VideoCore::BindingOf(next_frame, 0, next_frame.SizeBytes()),
    };
    const std::array writes{
        Backend::SlotWrite{.slot = 0, .count = 1, .kind = Backend::BindingKind::SampledImage,
                           .images = &images[0]},
        Backend::SlotWrite{.slot = 1, .count = 1, .kind = Backend::BindingKind::StorageImage,
                           .images = &images[1]},
        Backend::SlotWrite{.slot = 2, .count = 1, .kind = Backend::BindingKind::StorageBuffer,
                           .buffers = &buffers[0]},
        Backend::SlotWrite{.slot = 3, .count = 1, .kind = Backend::BindingKind::StorageBuffer,
                           .buffers = &buffers[1]},
    };
    // The previous frame buffer was written by the last dispatch.
    using Use = Backend::MemoryUse;
    recorder.Barrier(Use::ComputeWrite, Use::ComputeRead | Use::ComputeWrite);
    recorder.BindResources(overlay_pipeline.Allocation(), writes,
                           {reinterpret_cast<const u8*>(&push), sizeof(push)}, images, buffers);
    const u32 groups_x = (color.info.size.width + 7) / 8;
    const u32 groups_y = (color.info.size.height + 7) / 8;
    recorder.Dispatch({.pipeline = overlay_pipeline.Allocation().native,
                       .groups = {groups_x, groups_y, 1}},
                      {.kind = Backend::CrumbKind::Dispatch,
                       .name = "motion overlay",
                       .count = {groups_x, groups_y, 1}});
}

} // namespace Vulkan
