// SPDX-License-Identifier: GPL-2.0-or-later
// bbport (macOS): GPU memory kept resident in a Metal residency set attached to the graphics
// queue. Without one the driver makes the memory a command buffer uses resident (wires it) for
// that command buffer and lets it go after: the game's ~6 GiB imported memory pool was wired ~3
// times a second, 70-80 ms each, and ~200k smaller wires and unwires (texture and buffer memory)
// took ~1.4 s of every 6 s, with the GPU idle meanwhile (Metal System Trace, 2026-10-07).
// BB_RESIDENCY_SET=0 turns it off.
#pragma once

#include <cstdint>

#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {
class Instance;
}

namespace BbMetalResidency {

/// Once the device and its queues exist. No-op where unsupported (other platforms, macOS < 15).
void Init(const Vulkan::Instance& instance);
bool Active();

/// Device memory (its MTLBuffer), counted: resident from the first add to the last remove.
bool AddMemory(VkDeviceMemory memory);
void RemoveMemory(VkDeviceMemory memory);
/// An image's MTLTexture (MoltenVK gives each image its own), until removed before destruction.
void AddImage(VkImage image);
/// AddImage for an image whose texture (id<MTLTexture>) is known: an image only Metal has (M4).
void AddImageTexture(VkImage image, void* texture);
/// A buffer only Metal has (M4): `key` its placeholder VkBuffer, `buffer` its id<MTLBuffer>.
void AddMetalBuffer(VkBuffer key, void* buffer);
void RemoveMetalBuffer(VkBuffer key);
void RemoveImage(VkImage image);

/// The set is attached to the queue, so every command buffer committed before a removal keeps
/// the removed allocation in its residency until it completes. Removed objects are therefore
/// kept alive (retained) until the GPU has completed the submission open at the removal: freed
/// at once, Metal failed those command buffers ("destroyed while still required to be alive by
/// the command buffer": a device loss reported as VK_ERROR_OUT_OF_DEVICE_MEMORY). The scheduler
/// gives the ticks: `current` the open submission's, `completed` the GPU's last known one.
void SetTicks(std::uint64_t (*current)(void*), std::uint64_t (*completed)(void*), void* context);
/// Releases the removed objects whose submission has completed (each submission calls it).
void ReleaseRetired();

} // namespace BbMetalResidency
