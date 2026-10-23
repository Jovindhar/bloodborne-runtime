// SPDX-License-Identifier: GPL-2.0-or-later
// The in-game trophy notices' queue (bbport_overlay.cpp draws them): guest threads push newly
// recorded unlocks, the present thread shows one at a time. Separate from the overlay for tests.
#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>

namespace BbTrophyQueue {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

constexpr std::size_t Capacity = 16;
constexpr auto Expiry = 30s; ///< an unlock waiting longer is dropped
constexpr auto Gap = 1s;     ///< between two notices
constexpr auto Shown = 5s, ShownPlatinum = 7s;

struct Notice {
    int id = -1;
    Clock::time_point at{};
    bool preview = false;
    bool platinum = false;
};

class Queue {
public:
    /// Any thread. False when the queue is full.
    bool Push(const Notice& notice) {
        std::scoped_lock lock{mutex};
        if (waiting.size() >= Capacity) return false;
        waiting.push_back(notice);
        pending = true;
        return true;
    }

    /// Present thread: the notice to draw at `now`, or null.
    const Notice* Update(Clock::time_point now) {
        if (active.id >= 0 && now >= until) {
            active.id = -1;
            next = now + Gap;
        }
        if (active.id < 0 && now >= next) {
            std::scoped_lock lock{mutex};
            while (!waiting.empty() && now - waiting.front().at > Expiry) waiting.pop_front();
            if (!waiting.empty()) {
                active = waiting.front();
                waiting.pop_front();
                started = now;
                until = now + (active.platinum ? ShownPlatinum : Shown);
            }
            pending = active.id >= 0 || !waiting.empty();
        }
        return active.id >= 0 ? &active : nullptr;
    }

    /// Whether a notice is showing or waiting (the overlay draws while it is).
    bool Pending() const { return pending; }

    /// Present thread: since the active notice appeared, and until it goes.
    Clock::time_point Started() const { return started; }
    Clock::time_point Until() const { return until; }

private:
    std::mutex mutex;
    std::deque<Notice> waiting;
    std::atomic<bool> pending{false}; // written under the mutex
    // Present thread only.
    Notice active{};
    Clock::time_point started{}, until{}, next{};
};

} // namespace BbTrophyQueue
