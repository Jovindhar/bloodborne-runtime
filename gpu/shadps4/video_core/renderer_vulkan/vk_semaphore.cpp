// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <limits>
#include "video_core/renderer_vulkan/vk_breadcrumbs.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_semaphore.h"

#include "common/assert.h"
#if BB_METAL_BACKEND
#include "video_core/renderer_metal/mtl_vk_layer.h"
#endif

namespace Vulkan {

constexpr u64 WAIT_TIMEOUT = std::numeric_limits<u64>::max();

Semaphore::Semaphore(const Instance& instance_) : instance{instance_} {
    vk::StructureChain semaphore_chain = {
        vk::SemaphoreCreateInfo{},
        vk::SemaphoreTypeCreateInfo{
            .semaphoreType = vk::SemaphoreType::eTimeline,
            .initialValue = 0,
        },
#if BB_METAL_BACKEND && defined(VK_USE_PLATFORM_METAL_EXT)
        // The Metal frame signals the tick on its MTLSharedEvent itself (Metal submissions).
        vk::ExportMetalObjectCreateInfoEXT{
            .exportObjectType = vk::ExportMetalObjectTypeFlagBitsEXT::eMetalSharedEvent,
        },
#endif
    };
#if BB_METAL_BACKEND && defined(VK_USE_PLATFORM_METAL_EXT)
    if (!MetalLayer::FrameEnabled()) {
        semaphore_chain.unlink<vk::ExportMetalObjectCreateInfoEXT>();
    }
#endif
    auto [semaphore_result, sem] =
        instance.GetDevice().createSemaphoreUnique(semaphore_chain.get());
    ASSERT_MSG(semaphore_result == vk::Result::eSuccess, "Failed to create master semaphore: {}",
               vk::to_string(semaphore_result));
    semaphore = std::move(sem);
#if BB_METAL_BACKEND
    // With the Metal frame the ticks are read and waited for on the event, not through MoltenVK.
    event = MetalLayer::SemaphoreEvent(static_cast<VkSemaphore>(*semaphore));
#endif
}

Semaphore::~Semaphore() = default;

void Semaphore::Refresh() {
    u64 this_tick{};
    u64 counter{};
    do {
        this_tick = gpu_tick.load(std::memory_order_acquire);
#if BB_METAL_BACKEND
        if (event) {
            counter = MetalLayer::EventValue(event);
            if (counter < this_tick) {
                return;
            }
            continue;
        }
#endif
        auto [counter_result, cntr] = instance.GetDevice().getSemaphoreCounterValue(*semaphore);
        if (counter_result == vk::Result::eErrorDeviceLost) {
            Breadcrumbs::ReportDeviceLost("reading the GPU progress");
        }
        ASSERT_MSG(counter_result == vk::Result::eSuccess,
                   "Failed to get master semaphore value: {}", vk::to_string(counter_result));
        counter = cntr;
        if (counter < this_tick) {
            return;
        }
    } while (!gpu_tick.compare_exchange_weak(this_tick, counter, std::memory_order_release,
                                             std::memory_order_relaxed));
}

void Semaphore::Wait(u64 tick) {
    // No need to wait if the GPU is ahead of the tick
    if (IsFree(tick)) {
        return;
    }
    // Update the GPU tick and try again
    Refresh();
    if (IsFree(tick)) {
        return;
    }

#if BB_METAL_BACKEND
    if (event) {
        MetalLayer::WaitEvent(event, tick);
        Refresh();
        return;
    }
#endif
    // If none of the above is hit, fallback to a regular wait
    const vk::SemaphoreWaitInfo wait_info = {
        .semaphoreCount = 1,
        .pSemaphores = &semaphore.get(),
        .pValues = &tick,
    };

    // bbport: a lost device ends the wait with a report instead of a silent endless loop.
    while (true) {
        const vk::Result result = instance.GetDevice().waitSemaphores(&wait_info, WAIT_TIMEOUT);
        if (result == vk::Result::eSuccess) {
            break;
        }
        if (result == vk::Result::eErrorDeviceLost) {
            Breadcrumbs::ReportDeviceLost("waiting for a submission");
            ASSERT_MSG(false, "Device lost while waiting for a submission");
        }
    }
    Refresh();
}

} // namespace Vulkan
