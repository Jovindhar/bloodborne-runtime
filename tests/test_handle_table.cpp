// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include <memory>

#include "video_core/renderer_backend/handle_table.h"

// The render adapter's handle table (docs/RENDER_ADAPTER.md): a released object's handle must
// never reach the object that reuses its slot.
using namespace VideoCore::Backend;

int main() {
    HandleTable<BufferTag, std::unique_ptr<int>> table;
    assert(!BufferHandle{});
    assert(!table.Get(BufferHandle{}));

    const BufferHandle a = table.Insert(std::make_unique<int>(1));
    const BufferHandle b = table.Insert(std::make_unique<int>(2));
    assert(a && b && !(a == b));
    assert(**table.Get(a) == 1 && **table.Get(b) == 2);
    assert(table.Size() == 2);

    // Released: the handle is stale, and erasing it again does nothing.
    assert(table.Erase(a));
    assert(!table.Get(a));
    assert(!table.Erase(a));
    assert(table.Size() == 1);

    // The slot is reused under a new generation; the old handle still finds nothing.
    const BufferHandle c = table.Insert(std::make_unique<int>(3));
    assert(c.slot == a.slot && c.generation != a.generation);
    assert(!table.Get(a));
    assert(**table.Get(c) == 3);

    // A handle from beyond the table.
    assert(!table.Get(BufferHandle{.slot = 99, .generation = 1}));

    std::printf("handle table: ok\n");
    return 0;
}
