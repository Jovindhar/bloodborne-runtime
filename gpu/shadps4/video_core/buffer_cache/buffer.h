// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "video_core/renderer_backend/binding.h"
#include "common/assert.h"
#include "common/types.h"
#include "video_core/renderer_backend/device.h"
#include "video_core/renderer_backend/resource.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {
class Instance;
class Scheduler;
} // namespace Vulkan

namespace VideoCore {

namespace Detail {
/// bbport: GuestInPlace() and WriteTracking() once known (every buffer binding asks): 0 not yet,
/// 1 on, 2 off.
inline std::atomic<u8> guest_in_place{0}, write_tracking{0};
bool ComputeGuestInPlace();
bool ComputeWriteTracking();
} // namespace Detail

/// bbport BB_GUEST_IN_PLACE=1: the GPU uses the game's direct memory where it is (the arena is bound to
/// the Vulkan chunks it lives in, gpu/shim/bbport_guest_memory.cpp) instead of copies in VRAM.
inline bool GuestInPlace() {
    const u8 state = Detail::guest_in_place.load(std::memory_order_relaxed);
    return state != 0 ? state == 1 : Detail::ComputeGuestInPlace();
}
/// The driver cannot bind guest memory to the arena: BB_GUEST_IN_PLACE stays off from now on.
void DisableGuestInPlace();
/// bbport (macOS, MoltenVK): the driver has no sparse buffers. The game's memory pool (direct and
/// flexible memory) is imported as one buffer and every guest buffer is used where it lives, as
/// Apple GPUs share the CPU's memory: no arenas, no uploads, no readbacks.
bool UnifiedGuestMemory();
/// Whether CPU writes to GPU memory are caught by page protection. Off with BB_GUEST_IN_PLACE: the
/// GPU side learns of writes from the writers (file reads, the game's resource loaders, its own
/// DMA and command writes); BB_WRITE_TRACKING=1 brings the protection back.
inline bool WriteTracking() {
    const u8 state = Detail::write_tracking.load(std::memory_order_relaxed);
    return state != 0 ? state == 1 : Detail::ComputeWriteTracking();
}
/// BB_WRITE_VERIFY=1 (diagnostics, without write tracking): pages the GPU side watches are protected as
/// well, so writes into them that nothing announced show up as write faults (their sites).
bool WriteVerify();

/// bbport BB_CONSTANTS_IN_PLACE=1 (with BB_GUEST_IN_PLACE): small read-only buffers (constants) are
/// read by the GPU where the game wrote them, when the draw executes, like everything else in
/// place, instead of being copied when the draw is decoded (constant ring) or recorded (stream
/// buffer). The copies kept a draw's values from command writes the CPU performed at decode time;
/// in place those writes are GPU commands in stream order. Experiment bit 1 of BB_TOGGLE_FILE
/// inverts it for an A/B while the game runs.
bool ConstantsInPlace();

/// Hints and requirements for the backing memory type of a commit.
using MemoryType = Backend::MemoryClass;

/// A backend memory object (a VkDeviceMemory) that a buffer is created over.
struct NativeMemory {
    u64 value{};
};

/// A buffer created through the render adapter (docs/RENDER_ADAPTER.md, A2); its destruction is
/// deferred by the device.
struct Buffer {
    explicit Buffer(Backend::Device& device, VAddr cpu_addr_, u64 size_bytes_,
                    MemoryType mem_type, std::string_view debug_name = "");
    /// bbport BB_GUEST_IN_PLACE: a buffer over memory owned elsewhere (a guest memory chunk,
    /// exported as a dma-buf): transfers and texel/storage reads, no device address.
    explicit Buffer(Backend::Device& device, u64 size_bytes_, NativeMemory memory,
                    std::string_view debug_name);
    /// bbport (macOS unified memory): a buffer over host memory imported as it is
    /// (VK_EXT_external_memory_host): every use, a device address, and its host mapping.
    explicit Buffer(Backend::Device& device, void* host_memory, u64 size_bytes_,
                    std::string_view debug_name);
    ~Buffer();

    Buffer& operator=(const Buffer&) = delete;
    Buffer(const Buffer&) = delete;

    Buffer(Buffer&& other) noexcept
        : cpu_addr{other.cpu_addr}, size_bytes{other.size_bytes},
          mapped_data{other.mapped_data}, is_coherent{other.is_coherent},
          mem_type{other.mem_type}, device{other.device},
          handle{std::exchange(other.handle, {})}, native{other.native},
          device_address{other.device_address} {}
    Buffer& operator=(Buffer&& other) noexcept {
        std::swap(cpu_addr, other.cpu_addr);
        std::swap(size_bytes, other.size_bytes);
        std::swap(mapped_data, other.mapped_data);
        std::swap(is_coherent, other.is_coherent);
        std::swap(mem_type, other.mem_type);
        std::swap(device, other.device);
        std::swap(handle, other.handle);
        std::swap(native, other.native);
        std::swap(device_address, other.device_address);
        return *this;
    }

    [[nodiscard]] bool IsInBounds(VAddr addr, u64 size) const noexcept {
        return addr >= cpu_addr && addr + size <= cpu_addr + SizeBytes();
    }

    [[nodiscard]] VAddr CpuAddr() const noexcept {
        return cpu_addr;
    }

    [[nodiscard]] u64 Offset(VAddr other_cpu_addr) const noexcept {
        return other_cpu_addr - cpu_addr;
    }

    size_t SizeBytes() const {
        return size_bytes;
    }

    /// The backend's buffer (for Vulkan code until it records through the adapter).
    vk::Buffer Handle() const noexcept {
        return vk::Buffer{reinterpret_cast<VkBuffer>(native)};
    }

    /// The backend's buffer (Device::NativeHandle).
    [[nodiscard]] u64 Native() const noexcept {
        return native;
    }

    [[nodiscard]] Backend::BufferHandle AdapterHandle() const noexcept {
        return handle;
    }

    u64 BufferDeviceAddress() const noexcept {
        ASSERT_MSG(device_address != 0, "Can't get BDA from a non BDA buffer");
        return device_address;
    }

    /// Makes host writes in provided range visible to the device.
    void Flush(u64 offset, u64 size);

    /// Makes device writes in provided range visible to the host.
    void Invalidate(u64 offset, u64 size);

    VAddr cpu_addr = 0;
    size_t size_bytes = 0;
    std::span<u8> mapped_data{};
    bool is_coherent{};
    MemoryType mem_type{MemoryType::DeviceLocal};

private:
    void Adopt(const Backend::BufferAllocation& allocation);

    Backend::Device* device;
    Backend::BufferHandle handle;
    u64 native{};
    u64 device_address{};
};

/// `range` bytes of `buffer` from `offset` as a slot binding.
[[nodiscard]] inline Backend::BufferBinding BindingOf(const Buffer& buffer, u64 offset,
                                                      u64 range) noexcept {
    return {buffer.Native(), offset, range};
}

struct StreamBuffer : public Buffer {
    explicit StreamBuffer(Backend::Device& device, MemoryType mem_type, u64 size_bytes);

    /// Reserves a region of memory from the stream buffer. Must be followed by Commit().
    std::pair<u8*, u64> Map(u64 size, u64 alignment = 0, bool allow_wait = true);

    /// Ensures that reserved bytes of memory are available to the GPU.
    void Commit();

    /// Reserves and commits a region in one step.
    std::optional<u64> Reserve(u64 size, u64 alignment = 0, bool allow_wait = true);

    /// Tick of the most recent commit into this buffer, 0 if never used.
    [[nodiscard]] u64 LastTick() const noexcept {
        return last_tick;
    }

    /// Maps and commits a memory region with user provided data
    u64 Copy(auto src, size_t size, size_t alignment = 0) {
        const auto [data, offset] = Map(size, alignment);
        ASSERT_MSG(data, "Failed to copy {:#x} bytes", size);
        std::memcpy(data, reinterpret_cast<const void*>(src), size);
        Commit();
        return offset;
    }

private:
    struct Watch {
        u64 tick{};
        u64 upper_bound{};
    };

    /// Aligns, wraps if needed and waits for the GPU. On success mapped_size is set and the
    /// region starts at offset.
    bool PrepareMap(u64 size, u64 alignment, bool allow_wait);

    /// Advances offset past the mapped region and records the current tick for it.
    void AdvanceAndWatch();

    /// Increases the amount of watches available.
    void ReserveWatches(std::vector<Watch>& watches, std::size_t grow_size);

    /// Waits pending watches until requested upper bound.
    bool WaitPendingOperations(u64 requested_upper_bound, bool allow_wait);

private:
    Backend::Device& ticks; ///< the device whose submissions use the regions
    vk::DeviceSize non_coherent_atom_size{};
    u64 offset{};
    u64 mapped_size{};
    u64 last_tick{};
    std::vector<Watch> current_watches;
    std::size_t current_watch_cursor{};
    std::optional<size_t> invalidation_mark;
    std::vector<Watch> previous_watches;
    std::size_t wait_cursor{};
    u64 wait_bound{};
};

} // namespace VideoCore
