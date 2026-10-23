// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <utility>

#include "video_core/amdgpu/regs_texture.h"
#include "video_core/amdgpu/resource.h"
#include "video_core/renderer_backend/device.h"

namespace VideoCore {

/// A sampler created through the render adapter (docs/RENDER_ADAPTER.md, A2); its destruction
/// is deferred by the device.
class Sampler {
public:
    explicit Sampler(Backend::Device& device, const AmdGpu::Sampler& sampler,
                     const AmdGpu::BorderColorBuffer border_color_base, bool is_depth,
                     float extra_lod_bias = 0.0f);
    ~Sampler();

    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;

    Sampler(Sampler&& other) noexcept
        : device{other.device}, handle{std::exchange(other.handle, {})}, native{other.native},
          lru_id{other.lru_id} {}
    Sampler& operator=(Sampler&& other) noexcept {
        std::swap(device, other.device);
        std::swap(handle, other.handle);
        std::swap(native, other.native);
        lru_id = other.lru_id;
        return *this;
    }

    [[nodiscard]] Backend::SamplerHandle Handle() const noexcept {
        return handle;
    }
    /// The backend's object (Backend::Device::NativeHandle), kept for binding without a lookup.
    [[nodiscard]] u64 Native() const noexcept {
        return native;
    }

private:
    Backend::Device* device;
    Backend::SamplerHandle handle;
    u64 native{};

public:
    size_t lru_id{};
};

} // namespace VideoCore
