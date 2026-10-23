// SPDX-License-Identifier: GPL-2.0-or-later
// The in-game trophy notices' queue: capacity, expiry, display times and the gap between them.
#include <cassert>
#include <cstdio>
#include "bbport_trophy_queue.h"

int main() {
    using namespace BbTrophyQueue;
    const Clock::time_point t0{};
    Queue queue;
    assert(!queue.Pending() && !queue.Update(t0));

    // Sixteen wait; the seventeenth is refused.
    for (int id = 1; id <= int(Capacity); ++id) assert(queue.Push({.id = id, .at = t0}));
    assert(!queue.Push({.id = 30, .at = t0}) && queue.Pending());

    // One at a time: five seconds, a one-second gap, then the next.
    const Notice* shown = queue.Update(t0);
    assert(shown && shown->id == 1 && queue.Until() == t0 + Shown);
    assert(queue.Update(t0 + Shown - 1ms)->id == 1);
    assert(!queue.Update(t0 + Shown) && queue.Pending());
    assert(!queue.Update(t0 + Shown + Gap - 1ms));
    assert(queue.Update(t0 + Shown + Gap)->id == 2);

    // Unlocks waiting longer than 30 s are dropped: the queue empties.
    const auto late = t0 + Expiry + 1min;
    assert(!queue.Update(late) && queue.Pending()); // the gap after notice 2
    assert(!queue.Update(late + Gap) && !queue.Pending());

    // Platinum stays seven seconds; a preview keeps its flag.
    const auto t1 = late + 1min;
    assert(queue.Push({.id = 0, .at = t1, .preview = true, .platinum = true}));
    shown = queue.Update(t1);
    assert(shown && shown->id == 0 && shown->preview && queue.Until() == t1 + ShownPlatinum);
    assert(queue.Update(t1 + ShownPlatinum - 1ms));
    assert(!queue.Update(t1 + ShownPlatinum));
    assert(!queue.Update(t1 + ShownPlatinum + Gap) && !queue.Pending());

    std::puts("PASS: trophy notice queue: capacity, display times, gap, expiry, platinum");
    return 0;
}
