// The render adapter's object table: generation-checked handles (docs/RENDER_ADAPTER.md).
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>
#include <utility>
#include <vector>

#include "video_core/renderer_backend/resource.h"

namespace VideoCore::Backend {

/// Objects of one kind behind Handle<Tag>. A slot is reused after Erase with a new generation,
/// so a handle kept past its object's release finds nothing instead of the slot's next object.
/// Not thread-safe: the owner locks.
template <typename Tag, typename T>
class HandleTable {
public:
    Handle<Tag> Insert(T value) {
        u32 slot;
        if (!free_slots.empty()) {
            slot = free_slots.back();
            free_slots.pop_back();
        } else {
            slot = static_cast<u32>(entries.size());
            entries.emplace_back();
        }
        Entry& entry = entries[slot];
        entry.value.emplace(std::move(value));
        ++live;
        return {.slot = slot, .generation = entry.generation};
    }

    /// The object, or null when the handle is empty or stale.
    T* Get(Handle<Tag> handle) {
        if (handle.slot >= entries.size()) {
            return nullptr;
        }
        Entry& entry = entries[handle.slot];
        return entry.value && entry.generation == handle.generation ? &*entry.value : nullptr;
    }

    /// Destroys the object; false when the handle is empty or stale.
    bool Erase(Handle<Tag> handle) {
        if (!Get(handle)) {
            return false;
        }
        Entry& entry = entries[handle.slot];
        entry.value.reset();
        // Zero is never a live generation.
        entry.generation = entry.generation == ~0u ? 1 : entry.generation + 1;
        free_slots.push_back(handle.slot);
        --live;
        return true;
    }

    [[nodiscard]] std::size_t Size() const noexcept {
        return live;
    }

private:
    struct Entry {
        std::optional<T> value;
        u32 generation{1};
    };
    std::vector<Entry> entries;
    std::vector<u32> free_slots;
    std::size_t live{};
};

} // namespace VideoCore::Backend
