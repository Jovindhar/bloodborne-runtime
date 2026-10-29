// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/vk_indirect_guard.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "video_core/buffer_cache/buffer.h"
#include "video_core/host_shaders/indirect_guard_comp.h"

namespace Vulkan {

namespace {
struct Push {
    u32 max_count[3];
    u32 first;
    u32 max_total;
    u32 slot;
};

struct Skipped {
    u32 count;
    u32 pad[3];
    u32 last[3];
};

s64 NowNs() {
    return std::chrono::steady_clock::now().time_since_epoch().count();
}
} // namespace

bool IndirectGuard::Enabled() {
    static const bool enabled = [] {
        const char* v = std::getenv("BB_INDIRECT_GUARD");
        return !(v && v[0] == '0');
    }();
    return enabled;
}

IndirectGuard::IndirectGuard(VideoCore::Backend::Device& device_) : device{device_} {
    checked = std::make_unique<VideoCore::Buffer>(device, 0, NumSlots * 16,
                                                  VideoCore::MemoryType::DeviceLocal,
                                                  "Indirect dispatch counts");
    skipped = std::make_unique<VideoCore::Buffer>(device, 0, sizeof(Skipped),
                                                  VideoCore::MemoryType::HostCached,
                                                  "Indirect dispatches skipped");
    std::memset(skipped->mapped_data.data(), 0, sizeof(Skipped));
    skipped->Flush(0, sizeof(Skipped));

    for (u32 i = 0; i < 3; ++i) {
        max_count[i] = device.GetCapabilities().max_compute_groups[i];
    }
    const char* max = std::getenv("BB_INDIRECT_GUARD_MAX");
    max_total = max && *max ? static_cast<u32>(std::strtoul(max, nullptr, 0)) : 1u << 22;

    namespace Backend = VideoCore::Backend;
    constexpr Backend::BindingSlot storage{.kind = Backend::BindingKind::StorageBuffer,
                                           .stages = Backend::ShaderStage::Compute};
    constexpr std::array slots{storage, storage, storage};
    pipeline = Backend::MakeCompute(device, INDIRECT_GUARD_COMP, slots, sizeof(Push),
                                    "indirect guard");
    last_report_ns = NowNs();
}

IndirectGuard::~IndirectGuard() = default;

std::pair<u64, u64> IndirectGuard::CheckArgs(const VideoCore::Buffer& args, u64 offset) {
    // A storage binding starts at a multiple of the device's alignment: the arguments are
    // `first` dwords into it.
    const u64 alignment = device.GetCapabilities().storage_min_alignment;
    const u64 bind_offset = offset / alignment * alignment;
    const u32 first = static_cast<u32>((offset - bind_offset) / sizeof(u32));
    const u64 bind_size = (offset - bind_offset) + 3 * sizeof(u32);
    const u32 slot = next_slot.fetch_add(1, std::memory_order_relaxed) % NumSlots;
    const Push push{{max_count[0], max_count[1], max_count[2]}, first, max_total, slot * 4};
    namespace Backend = VideoCore::Backend;
    using Use = Backend::MemoryUse;
    auto& recorder = device.Recorder();
    recorder.Barrier(Use::Any, Use::ComputeRead | Use::ComputeWrite);
    const std::array buffers{
        VideoCore::BindingOf(args, bind_offset, bind_size),
        VideoCore::BindingOf(*checked, 0, NumSlots * 16),
        VideoCore::BindingOf(*skipped, 0, sizeof(Skipped)),
    };
    std::array<Backend::SlotWrite, 3> writes{};
    for (u32 i = 0; i < writes.size(); ++i) {
        writes[i] = {.slot = i,
                     .count = 1,
                     .kind = Backend::BindingKind::StorageBuffer,
                     .buffers = &buffers[i]};
    }
    recorder.BindResources(pipeline.Allocation(), writes,
                           {reinterpret_cast<const u8*>(&push), sizeof(push)}, {}, buffers);
    recorder.Dispatch({.pipeline = pipeline.Allocation().native, .groups = {1, 1, 1}},
                      {.kind = Backend::CrumbKind::Dispatch,
                       .name = "indirect guard",
                       .count = {1, 1, 1}});
    recorder.Barrier(Use::ComputeWrite, Use::IndirectRead | Use::ComputeRead);
    Report();
    return {checked->Native(), u64(slot) * 16};
}

void IndirectGuard::Report() {
    const s64 now = NowNs();
    s64 last = last_report_ns.load(std::memory_order_relaxed);
    if (now - last < 5'000'000'000 ||
        !last_report_ns.compare_exchange_strong(last, now, std::memory_order_relaxed)) {
        return;
    }
    skipped->Invalidate(0, sizeof(Skipped));
    const auto* s = reinterpret_cast<const volatile Skipped*>(skipped->mapped_data.data());
    const u32 count = s->count;
    const u32 before = reported.exchange(count, std::memory_order_relaxed);
    if (count != before) {
        std::printf("Indirect dispatches: %u skipped in all (garbage group counts, the last "
                    "%ux%ux%u; BB_INDIRECT_GUARD)\n",
                    count, s->last[0], s->last[1], s->last[2]);
    }
}

} // namespace Vulkan
