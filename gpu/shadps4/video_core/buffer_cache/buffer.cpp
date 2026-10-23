// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <bit>
#include <array>
#include <cstdlib>
#include <numeric>

#include <fmt/format.h>

#include "common/alignment.h"
#include "common/assert.h"
#include "video_core/buffer_cache/buffer.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

namespace VideoCore {

std::string_view BufferTypeName(MemoryType type) {
    switch (type) {
    case MemoryType::HostUncached:
        return "HostUncached";
    case MemoryType::HostCached:
        return "HostCached";
    case MemoryType::Stream:
        return "Stream";
    case MemoryType::DeviceLocal:
        return "DeviceLocal";
    case MemoryType::Sparse:
        return "Sparse";
    default:
        return "Invalid";
    }
}

Buffer::Buffer(Backend::Device& device_, u64 size_bytes_, NativeMemory memory,
               std::string_view debug_name)
    : size_bytes{size_bytes_}, mem_type{MemoryType::HostCached}, device{&device_} {
    Adopt(device->ImportGuestMemoryChunk(memory.value, size_bytes, debug_name));
}

Buffer::Buffer(Backend::Device& device_, void* host_memory, u64 size_bytes_,
               std::string_view debug_name)
    : size_bytes{size_bytes_}, mem_type{MemoryType::HostCached}, device{&device_} {
    Adopt(device->ImportHostMemory(host_memory, size_bytes, debug_name));
}

Buffer::Buffer(Backend::Device& device_, VAddr cpu_addr_, u64 size_bytes_, MemoryType mem_type_,
               std::string_view debug_name)
    : cpu_addr{cpu_addr_}, size_bytes{size_bytes_}, mem_type{mem_type_}, device{&device_} {
    using Backend::BufferUse;
    constexpr auto uses = BufferUse::TransferSource | BufferUse::TransferDestination |
                          BufferUse::Uniform | BufferUse::Storage | BufferUse::Vertex |
                          BufferUse::Index | BufferUse::Indirect | BufferUse::DeviceAddress;
    // bbport: arenas are also read by the readback queue (BufferCache::DownloadMemory).
    const Backend::BufferDesc desc{.size_bytes = size_bytes,
                                   .uses = uses,
                                   .memory = mem_type,
                                   .concurrent_readback = mem_type == MemoryType::Sparse};
    const std::string name = debug_name.empty()
                                 ? fmt::format("Buffer {:#x}:{:#x}", cpu_addr, size_bytes)
                                 : std::string{debug_name};
    Adopt(device->CreateBuffer(desc, name));
}

Buffer::~Buffer() {
    if (handle) {
        device->DestroyBuffer(handle);
    }
}

void Buffer::Adopt(const Backend::BufferAllocation& allocation) {
    handle = allocation.handle;
    native = allocation.native;
    device_address = allocation.device_address;
    mapped_data = allocation.mapped;
    is_coherent = allocation.coherent;
}

void Buffer::Flush(u64 offset, u64 size) {
    if (mapped_data.empty() || is_coherent) {
        return;
    }
    device->FlushBuffer(handle, offset, size);
}

void Buffer::Invalidate(u64 offset, u64 size) {
    if (mapped_data.empty() || is_coherent) {
        return;
    }
    device->InvalidateBuffer(handle, offset, size);
}

constexpr u64 WATCHES_INITIAL_RESERVE = 0x100;
constexpr u64 WATCHES_RESERVE_CHUNK = 0x100;

StreamBuffer::StreamBuffer(Backend::Device& device, Vulkan::Scheduler& scheduler_,
                           MemoryType mem_type, u64 size_bytes)
    : Buffer{device, 0, size_bytes, mem_type,
             fmt::format("StreamBuffer({}):{:#x}", BufferTypeName(mem_type), size_bytes)},
      scheduler{scheduler_},
      non_coherent_atom_size{device.GetCapabilities().non_coherent_atom_size} {
    ReserveWatches(current_watches, WATCHES_INITIAL_RESERVE);
    ReserveWatches(previous_watches, WATCHES_INITIAL_RESERVE);
}

bool StreamBuffer::PrepareMap(u64 size, u64 alignment, bool allow_wait) {
    if (!mapped_data.empty() && !is_coherent) {
        size = Common::AlignUp(size, non_coherent_atom_size);
        alignment =
            alignment > 0 ? std::lcm(alignment, non_coherent_atom_size) : non_coherent_atom_size;
    }

    if (size > this->size_bytes) {
        return false;
    }

    if (alignment > 0) {
        offset = Common::AlignUp(offset, alignment);
    }

    if (offset + size > this->size_bytes) {
        // The buffer would overflow, save the amount of used watches and reset the state.
        invalidation_mark = current_watch_cursor;
        current_watch_cursor = 0;
        offset = 0;

        // Swap watches and reset waiting cursors.
        std::swap(previous_watches, current_watches);
        wait_cursor = 0;
        wait_bound = 0;
    }

    if (!WaitPendingOperations(offset + size, allow_wait)) {
        return false;
    }

    mapped_size = size;
    return true;
}

std::pair<u8*, u64> StreamBuffer::Map(u64 size, u64 alignment, bool allow_wait) {
    if (!PrepareMap(size, alignment, allow_wait)) {
        return {nullptr, 0};
    }
    u8* const data = mapped_data.empty() ? nullptr : mapped_data.data() + offset;
    return {data, offset};
}

void StreamBuffer::Commit() {
    if (mem_type == MemoryType::HostCached) {
        Invalidate(offset, mapped_size);
    } else {
        Flush(offset, mapped_size);
    }
    AdvanceAndWatch();
}

std::optional<u64> StreamBuffer::Reserve(u64 size, u64 alignment, bool allow_wait) {
    if (!PrepareMap(size, alignment, allow_wait)) {
        return std::nullopt;
    }
    const u64 reserved_offset = offset;
    AdvanceAndWatch();
    return reserved_offset;
}

void StreamBuffer::AdvanceAndWatch() {
    offset += mapped_size;
    const u64 tick = scheduler.CurrentTick();
    last_tick = tick;

    // Extend the last watch if it belongs to the same tick.
    if (current_watch_cursor != 0 && current_watches[current_watch_cursor - 1].tick == tick) {
        current_watches[current_watch_cursor - 1].upper_bound = offset;
        return;
    }

    if (current_watch_cursor + 1 >= current_watches.size()) {
        // Ensure that there are enough watches.
        ReserveWatches(current_watches, WATCHES_RESERVE_CHUNK);
    }

    auto& watch = current_watches[current_watch_cursor++];
    watch.upper_bound = offset;
    watch.tick = tick;
}

void StreamBuffer::ReserveWatches(std::vector<Watch>& watches, std::size_t grow_size) {
    watches.resize(watches.size() + grow_size);
}

bool StreamBuffer::WaitPendingOperations(u64 requested_upper_bound, bool allow_wait) {
    if (!invalidation_mark) {
        return true;
    }
    while (requested_upper_bound > wait_bound && wait_cursor < *invalidation_mark) {
        auto& watch = previous_watches[wait_cursor];
        if (!scheduler.IsFree(watch.tick) && !allow_wait) {
            return false;
        }
        scheduler.Wait(watch.tick);
        wait_bound = watch.upper_bound;
        ++wait_cursor;
    }
    return true;
}

} // namespace VideoCore
