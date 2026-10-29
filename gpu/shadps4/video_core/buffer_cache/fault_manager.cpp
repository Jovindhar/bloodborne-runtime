// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/div_ceil.h"
#include "video_core/renderer_vulkan/vk_backend_device.h"
#include "video_core/buffer_cache/buffer_cache.h"
#include "video_core/buffer_cache/fault_manager.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_platform.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_state.h"

#include "video_core/host_shaders/fault_buffer_process_comp.h"

namespace VideoCore {

static constexpr size_t MaxPageFaults = 1024;
static constexpr size_t PageFaultAreaSize = MaxPageFaults * sizeof(u64);

FaultManager::FaultManager(const Vulkan::Instance& instance, Vulkan::Scheduler& scheduler_,
                           BufferCache& buffer_cache_, u32 sparse_pagebits, u64 sparse_num_pages_)
    : scheduler{scheduler_}, buffer_cache{buffer_cache_}, sparse_pagesize{1ULL << sparse_pagebits},
      sparse_num_pages{sparse_num_pages_}, fault_buffer_size{sparse_num_pages_ / 8},
      fault_buffer{Vulkan::BackendDevice::From(instance), 0, fault_buffer_size, MemoryType::DeviceLocal},
      download_buffer{Vulkan::BackendDevice::From(instance), 0, MaxPendingFaults * PageFaultAreaSize, MemoryType::HostCached},
      recorder{scheduler_, Vulkan::BackendDevice::From(instance).GetDescriptorHeap(),
               instance.GetDevice()} {
    Vulkan::SetObjectName(instance.GetDevice(), fault_buffer.Handle(), "Fault Buffer");
    namespace Backend = VideoCore::Backend;
    constexpr std::array slots{
        Backend::BindingSlot{.kind = Backend::BindingKind::StorageBuffer,
                             .stages = Backend::ShaderStage::Compute},
        Backend::BindingSlot{.kind = Backend::BindingKind::StorageBuffer,
                             .stages = Backend::ShaderStage::Compute},
    };
    const std::array<u32, 2> spec_data{sparse_pagebits, MaxPageFaults};
    fault_process_pipeline =
        Vulkan::MakeHostCompute(Vulkan::BackendDevice::From(instance), FAULT_BUFFER_PROCESS_COMP,
                                slots, 0, "Fault Buffer Parser", spec_data);
}

void FaultManager::ProcessFaultBuffer() {
    if (u64 wait_tick = fault_areas[current_area]) {
        scheduler.Wait(wait_tick);
        scheduler.PopPendingOperations();
    }

    const u32 offset = current_area * PageFaultAreaSize;
    u8* mapped = download_buffer.mapped_data.data() + offset;
    std::memset(mapped, 0, PageFaultAreaSize);

    namespace Backend = VideoCore::Backend;
    const u64 fault = Vulkan::ToNative(fault_buffer.Handle());
    const std::array buffers{
        Vulkan::MakeBufferBinding(fault_buffer.Handle(), 0, fault_buffer_size),
        Vulkan::MakeBufferBinding(download_buffer.Handle(), offset, PageFaultAreaSize),
    };
    const std::array writes{
        Backend::SlotWrite{.slot = 0, .count = 1, .kind = Backend::BindingKind::StorageBuffer,
                           .buffers = &buffers[0]},
        Backend::SlotWrite{.slot = 1, .count = 1, .kind = Backend::BindingKind::StorageBuffer,
                           .buffers = &buffers[1]},
    };
    recorder.EndPass();
    recorder.BufferBarrier(fault, 0, fault_buffer_size, Backend::MemoryUse::Any,
                           Backend::MemoryUse::ComputeRead);
    recorder.BindResources(fault_process_pipeline.Allocation(), writes, {}, {}, buffers);
    // 1 bit per page, 32 pages per workgroup
    const u32 num_threads = sparse_num_pages / 32;
    const u32 num_workgroups = Common::DivCeil(num_threads, 64u);
    recorder.Dispatch({.pipeline = fault_process_pipeline.Allocation().native,
                       .groups = {num_workgroups, 1, 1}},
                      {.kind = Backend::CrumbKind::Dispatch,
                       .name = "fault buffer",
                       .count = {num_workgroups, 1, 1}});
    recorder.BufferBarrier(fault, 0, fault_buffer_size, Backend::MemoryUse::ComputeWrite,
                           Backend::MemoryUse::Any);

    scheduler.DeferOperation([this, mapped, area = current_area] {
        fault_ranges.Clear();
        const u64* fault_buf = std::bit_cast<const u64*>(mapped);
        const u32 fault_count = fault_buf[0];
        for (u32 i = 1; i <= fault_count; ++i) {
            fault_ranges.Add(fault_buf[i], sparse_pagesize);
            LOG_INFO(Render_Vulkan, "Accessed non-GPU cached memory at {:#x}", fault_buf[i]);
        }
        fault_ranges.ForEach([&](VAddr start, VAddr end) {
            ASSERT_MSG((end - start) <= std::numeric_limits<u32>::max(),
                       "Buffer size is too large");
            (void)buffer_cache.ObtainBuffer(start, end - start, false);
        });
        fault_areas[area] = 0;
    });

    fault_areas[current_area++] = scheduler.CurrentTick();
    current_area %= MaxPendingFaults;
}

} // namespace VideoCore
