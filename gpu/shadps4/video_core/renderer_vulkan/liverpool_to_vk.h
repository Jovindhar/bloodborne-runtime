// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>
#include "common/assert.h"
#include "video_core/amdgpu/pixel_format.h"
#include "video_core/amdgpu/regs_color.h"
#include "video_core/amdgpu/regs_depth.h"
#include "video_core/amdgpu/regs_primitive.h"
#include "video_core/amdgpu/regs_vertex.h"
#include "video_core/amdgpu/resource.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/renderer_vulkan/vk_format.h"
#include "video_core/renderer_vulkan/vk_state.h"

namespace Vulkan::LiverpoolToVK {

Backend::StencilOp StencilOp(AmdGpu::StencilFunc op);

Backend::CompareOp CompareOp(AmdGpu::CompareFunc func);

bool IsPrimitiveCulled(AmdGpu::PrimitiveType type);

Backend::PrimitiveTopology PrimitiveType(AmdGpu::PrimitiveType type);

Backend::PolygonMode PolygonMode(AmdGpu::PolygonMode mode);

Backend::CullMode CullMode(AmdGpu::CullMode mode);

Backend::FrontFace FrontFace(AmdGpu::FrontFace mode);

Backend::BlendFactor BlendFactor(AmdGpu::BlendControl::BlendFactor factor);

bool IsDualSourceBlendFactor(AmdGpu::BlendControl::BlendFactor factor);

Backend::BlendOp BlendOp(AmdGpu::BlendControl::BlendFunc func);

Backend::LogicOp LogicOp(AmdGpu::ColorControl::LogicOp logic_op);

Backend::AddressMode ClampMode(AmdGpu::ClampMode mode);

Backend::CompareOp DepthCompare(AmdGpu::DepthCompare comp);

Backend::Filter Filter(AmdGpu::Filter filter);

vk::SamplerReductionMode FilterMode(AmdGpu::FilterMode mode);

Backend::MipmapMode MipFilter(AmdGpu::MipFilter filter);

Backend::BorderColor BorderColor(AmdGpu::BorderColor color);

Backend::ComponentSwizzle ComponentSwizzle(AmdGpu::CompSwizzle comp_swizzle);

Backend::ComponentMapping ComponentMapping(AmdGpu::CompMapping comp_mapping);

struct SurfaceFormatInfo {
    AmdGpu::DataFormat data_format;
    AmdGpu::NumberFormat number_format;
    Backend::Format format;
    vk::FormatFeatureFlags2 flags;
};
std::span<const SurfaceFormatInfo> SurfaceFormats();

Backend::Format SurfaceFormat(AmdGpu::DataFormat data_format, AmdGpu::NumberFormat num_format);

/// bbport: SurfaceFormat without the assertion (eUndefined for unknown pairs), for speculative
/// readers such as the draw-preparation workers.
Backend::Format TrySurfaceFormat(AmdGpu::DataFormat data_format, AmdGpu::NumberFormat num_format);

struct DepthFormatInfo {
    AmdGpu::DepthBuffer::ZFormat z_format;
    AmdGpu::DepthBuffer::StencilFormat stencil_format;
    Backend::Format format;
    vk::FormatFeatureFlags2 flags;
};
std::span<const DepthFormatInfo> DepthFormats();

Backend::Format DepthFormat(AmdGpu::DepthBuffer::ZFormat z_format,
                       AmdGpu::DepthBuffer::StencilFormat stencil_format);

vk::ClearValue ColorBufferClearValue(const AmdGpu::ColorBuffer& color_buffer);

vk::SampleCountFlagBits NumSamples(u32 num_samples, vk::SampleCountFlags supported_flags);

static inline bool IsFormatDepthCompatible(Backend::Format fmt) {
    switch (fmt) {
    // 32-bit float compatible
    case Backend::Format::D32Sfloat:
    case Backend::Format::R32Sfloat:
    case Backend::Format::R32Uint:
    // 16-bit unorm compatible
    case Backend::Format::D16Unorm:
    case Backend::Format::R16Unorm:
        return true;
    default:
        return false;
    }
}

static inline bool IsFormatStencilCompatible(Backend::Format fmt) {
    switch (fmt) {
    // 8-bit uint compatible
    case Backend::Format::S8Uint:
    case Backend::Format::R8Uint:
    case Backend::Format::R8Unorm:
        return true;
    default:
        return false;
    }
}

static inline Backend::Format PromoteFormatToDepth(Backend::Format fmt) {
    if (fmt == Backend::Format::R32Sfloat || fmt == Backend::Format::R32Uint) {
        return Backend::Format::D32Sfloat;
    } else if (fmt == Backend::Format::R16Unorm) {
        return Backend::Format::D16Unorm;
    }
    UNREACHABLE_MSG("Unexpected depth format {}", Backend::FormatName(fmt));
}

} // namespace Vulkan::LiverpoolToVK
