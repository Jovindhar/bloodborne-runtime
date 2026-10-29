// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/vk_timestamps.h"

#include <array>
#include <cmath>
#include <cstdio>

#include "video_core/amdgpu/pm4_cmds.h"
#include "video_core/buffer_cache/buffer.h"
#include "video_core/buffer_cache/buffer_cache.h"
#include "video_core/host_shaders/gpu_timestamp_comp.h"

namespace Vulkan {

namespace {
struct Push {
    u32 address[2];
    u32 base_ticks[2];
    u32 base_clock[2];
    u32 mask[2];
    u32 ratio;
    u32 slot;
};

void Split(u64 value, u32 (&out)[2]) {
    out[0] = static_cast<u32>(value);
    out[1] = static_cast<u32>(value >> 32);
}

} // namespace

GpuTimestamps::GpuTimestamps(VideoCore::Backend::Device& device_,
                             VideoCore::BufferCache& buffer_cache_)
    : device{device_}, buffer_cache{buffer_cache_} {
    valid_bits = device.GetCapabilities().timestamp_valid_bits;
    period_ns = device.GetCapabilities().timestamp_period_ns;
    if (valid_bits == 0 || period_ns <= 0.0) {
        valid_bits = 0;
        std::printf("GPU: no timestamps on the graphics queue: the GPU clock is written by the "
                    "CPU after the GPU\n");
        return;
    }
    pool = device.CreateTimestampQueries(NumSlots, "GPU timestamps");
    results = std::make_unique<VideoCore::Buffer>(device, 0, NumSlots * sizeof(u64),
                                                  VideoCore::MemoryType::DeviceLocal,
                                                  "GPU timestamps");
    namespace Backend = VideoCore::Backend;
    constexpr std::array slots{Backend::BindingSlot{.kind = Backend::BindingKind::StorageBuffer,
                                                    .stages = Backend::ShaderStage::Compute}};
    pipeline = Backend::MakeCompute(device, GPU_TIMESTAMP_COMP, slots, sizeof(Push), "GPU timestamp");
}

GpuTimestamps::~GpuTimestamps() {
    device.DestroyQueryPool(pool.handle);
}

void GpuTimestamps::Calibrate() {
    calibrated = true;
    // A timestamp taken while the GPU has nothing else to do, between two host clock readings.
    auto& recorder = device.Recorder();
    recorder.EndPass();
    recorder.Finish();
    recorder.ResetQueries(pool.native, 0, 1);
    recorder.WriteTimestamp(pool.native, 0, true);
    const u64 before = AmdGpu::GetGpuClock64();
    recorder.Finish();
    const u64 after = AmdGpu::GetGpuClock64();
    u64 ticks = 0;
    if (!device.ReadQueryResults(pool.handle, 0, std::span{&ticks, 1}, false)) {
        valid_bits = 0;
        std::printf("GPU: the calibration timestamp was not available: the GPU clock is written "
                    "by the CPU after the GPU\n");
        return;
    }
    base_ticks = ticks;
    base_clock = before + (after - before) / 2;
    ratio = static_cast<u32>(std::llround(period_ns / 10.0 * double(1u << 24)));
    std::printf("GPU: the GPU clock (100 MHz) from Vulkan timestamps: %.3f ns a tick, %u valid "
                "bits, tied to the host clock within %.0f us\n",
                period_ns, valid_bits, double(after - before) * 10.0 / 1000.0 / 2.0);
}

bool GpuTimestamps::Write(VAddr address, bool end_of_pipe) {
    std::scoped_lock lk{mutex};
    if (!calibrated && Supported()) {
        Calibrate();
    }
    if (!Supported()) {
        return false;
    }
    auto& recorder = device.Recorder();
    recorder.EndPass(); // the copy and the dispatch are outside render passes
    // The value is read by the CPU: in place (a VRAM copy would show it a stale one).
    buffer_cache.force_writes_in_place = true;
    const auto [buffer, offset] = buffer_cache.ObtainBuffer(address, sizeof(u64), true);
    buffer_cache.force_writes_in_place = false;
    const u32 slot = next_slot;
    next_slot = next_slot + 1 == NumSlots ? 1 : next_slot + 1;
    Push push{};
    Split(buffer->BufferDeviceAddress() + offset, push.address);
    Split(base_ticks, push.base_ticks);
    Split(base_clock, push.base_clock);
    Split(valid_bits >= 64 ? ~0ull : (1ull << valid_bits) - 1, push.mask);
    push.ratio = ratio;
    push.slot = slot;
    namespace Backend = VideoCore::Backend;
    using Use = Backend::MemoryUse;
    recorder.ResetQueries(pool.native, slot, 1);
    // The end of the pipe: after all the work before it; else when the GPU gets here.
    recorder.WriteTimestamp(pool.native, slot, end_of_pipe);
    recorder.CopyQueryResults(pool.native, slot, 1, results->Native(), slot * sizeof(u64));
    // Everything before before everything after (a few timestamps a frame).
    recorder.Barrier(Use::Any, Use::Any);
    const std::array buffers{VideoCore::BindingOf(*results, 0, NumSlots * sizeof(u64))};
    const std::array writes{Backend::SlotWrite{.slot = 0,
                                               .count = 1,
                                               .kind = Backend::BindingKind::StorageBuffer,
                                               .buffers = &buffers[0]}};
    recorder.BindResources(pipeline.Allocation(), writes,
                           {reinterpret_cast<const u8*>(&push), sizeof(push)}, {}, buffers);
    recorder.Dispatch({.pipeline = pipeline.Allocation().native, .groups = {1, 1, 1}},
                      {.kind = Backend::CrumbKind::Dispatch,
                       .name = "GPU timestamp",
                       .count = {1, 1, 1}});
    recorder.Barrier(Use::Any, Use::Any);
    ++written;
    return true;
}

} // namespace Vulkan
