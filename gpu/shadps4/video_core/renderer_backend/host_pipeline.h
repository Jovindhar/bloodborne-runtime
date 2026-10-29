// The render adapter's pipelines of the renderer's own passes (docs/RENDER_ADAPTER.md, M2): a
// compute pipeline made from the renderer's SPIR-V by any backend, destroyed with its owner.
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>
#include <string_view>
#include <utility>

#include "common/assert.h"
#include "video_core/renderer_backend/device.h"
#include "video_core/renderer_backend/pipeline.h"

namespace VideoCore::Backend {

/// A pipeline the device made, destroyed with this object.
class OwnedPipeline {
public:
    OwnedPipeline() = default;
    OwnedPipeline(Device& device, const PipelineAllocation& allocation)
        : device{&device}, allocation{allocation} {}
    ~OwnedPipeline() {
        if (device) {
            device->DestroyPipeline(allocation.handle);
        }
    }

    OwnedPipeline(OwnedPipeline&& other) noexcept
        : device{std::exchange(other.device, nullptr)},
          allocation{std::exchange(other.allocation, {})} {}
    OwnedPipeline& operator=(OwnedPipeline&& other) noexcept {
        if (this != &other) {
            if (device) {
                device->DestroyPipeline(allocation.handle);
            }
            device = std::exchange(other.device, nullptr);
            allocation = std::exchange(other.allocation, {});
        }
        return *this;
    }
    OwnedPipeline(const OwnedPipeline&) = delete;
    OwnedPipeline& operator=(const OwnedPipeline&) = delete;

    explicit operator bool() const noexcept {
        return bool(allocation.handle);
    }
    [[nodiscard]] const PipelineAllocation& Allocation() const noexcept {
        return allocation;
    }

private:
    Device* device{};
    PipelineAllocation allocation;
};

/// A compute pipeline of a host pass from its SPIR-V; fatal when the driver rejects it.
[[nodiscard]] inline PipelineAllocation MakeComputeAllocation(
    Device& device, std::span<const u32> spirv, std::span<const BindingSlot> slots,
    u32 push_constant_size, std::string_view name, std::span<const u32> specialization = {}) {
    const auto shader = device.CreateShader(spirv, name);
    ASSERT_MSG(shader.handle, "Failed to compile the {} shader", name);
    const auto allocation = device.CreateComputePipeline(
        {.shader = shader.handle,
         .layout = {.slots = slots, .push_constant_size = push_constant_size},
         .host_pass = true,
         .specialization = specialization},
        name);
    device.DestroyShader(shader.handle); // the pipeline holds it
    ASSERT_MSG(allocation.handle, "Failed to create the {} pipeline", name);
    return allocation;
}

[[nodiscard]] inline OwnedPipeline MakeCompute(Device& device, std::span<const u32> spirv,
                                               std::span<const BindingSlot> slots,
                                               u32 push_constant_size, std::string_view name,
                                               std::span<const u32> specialization = {}) {
    return {device, MakeComputeAllocation(device, spirv, slots, push_constant_size, name,
                                          specialization)};
}

} // namespace VideoCore::Backend
