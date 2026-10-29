// The Vulkan backend's view of the render adapter's pipeline state (docs/RENDER_ADAPTER.md, A1).
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <bit>
#include <iterator>
#include <span>
#include <type_traits>
#include <cstddef>

#include "video_core/renderer_backend/binding.h"
#include "video_core/renderer_backend/pass.h"
#include "video_core/renderer_backend/pipeline.h"
#include "video_core/renderer_backend/resource.h"
#include "video_core/renderer_backend/state.h"
#include "video_core/renderer_vulkan/vk_format.h"

namespace Vulkan {

#define BB_BACKEND_STATE_CHECK(vk_type, list)                                                      \
    list(BB_BACKEND_STATE_CHECK_ONE_##vk_type)
#define BB_BACKEND_STATE_CHECK_ONE(vk_type, name, value)                                           \
    static_assert(static_cast<s64>(vk::vk_type::e##name) == value, #vk_type " value of " #name);
#define BB_BACKEND_STATE_CHECK_ONE_BlendFactor(name, value) BB_BACKEND_STATE_CHECK_ONE(BlendFactor, name, value)
#define BB_BACKEND_STATE_CHECK_ONE_BlendOp(name, value) BB_BACKEND_STATE_CHECK_ONE(BlendOp, name, value)
#define BB_BACKEND_STATE_CHECK_ONE_CompareOp(name, value) BB_BACKEND_STATE_CHECK_ONE(CompareOp, name, value)
#define BB_BACKEND_STATE_CHECK_ONE_StencilOp(name, value) BB_BACKEND_STATE_CHECK_ONE(StencilOp, name, value)
#define BB_BACKEND_STATE_CHECK_ONE_CullModeFlagBits(name, value) BB_BACKEND_STATE_CHECK_ONE(CullModeFlagBits, name, value)
#define BB_BACKEND_STATE_CHECK_ONE_FrontFace(name, value) BB_BACKEND_STATE_CHECK_ONE(FrontFace, name, value)
#define BB_BACKEND_STATE_CHECK_ONE_PrimitiveTopology(name, value) BB_BACKEND_STATE_CHECK_ONE(PrimitiveTopology, name, value)
#define BB_BACKEND_STATE_CHECK_ONE_PolygonMode(name, value) BB_BACKEND_STATE_CHECK_ONE(PolygonMode, name, value)
#define BB_BACKEND_STATE_CHECK_ONE_LogicOp(name, value) BB_BACKEND_STATE_CHECK_ONE(LogicOp, name, value)
#define BB_BACKEND_STATE_CHECK_ONE_Filter(name, value) BB_BACKEND_STATE_CHECK_ONE(Filter, name, value)
#define BB_BACKEND_STATE_CHECK_ONE_SamplerMipmapMode(name, value) BB_BACKEND_STATE_CHECK_ONE(SamplerMipmapMode, name, value)
#define BB_BACKEND_STATE_CHECK_ONE_SamplerAddressMode(name, value) BB_BACKEND_STATE_CHECK_ONE(SamplerAddressMode, name, value)
#define BB_BACKEND_STATE_CHECK_ONE_BorderColor(name, value) BB_BACKEND_STATE_CHECK_ONE(BorderColor, name, value)
BB_BACKEND_STATE_CHECK(BlendFactor, BB_BACKEND_BLEND_FACTORS)
BB_BACKEND_STATE_CHECK(BlendOp, BB_BACKEND_BLEND_OPS)
BB_BACKEND_STATE_CHECK(CompareOp, BB_BACKEND_COMPARE_OPS)
BB_BACKEND_STATE_CHECK(StencilOp, BB_BACKEND_STENCIL_OPS)
BB_BACKEND_STATE_CHECK(CullModeFlagBits, BB_BACKEND_CULL_MODES)
BB_BACKEND_STATE_CHECK(FrontFace, BB_BACKEND_FRONT_FACES)
BB_BACKEND_STATE_CHECK(PrimitiveTopology, BB_BACKEND_PRIMITIVE_TOPOLOGIES)
BB_BACKEND_STATE_CHECK(PolygonMode, BB_BACKEND_POLYGON_MODES)
BB_BACKEND_STATE_CHECK(LogicOp, BB_BACKEND_LOGIC_OPS)
BB_BACKEND_STATE_CHECK(Filter, BB_BACKEND_FILTERS)
BB_BACKEND_STATE_CHECK(SamplerMipmapMode, BB_BACKEND_MIPMAP_MODES)
BB_BACKEND_STATE_CHECK(SamplerAddressMode, BB_BACKEND_ADDRESS_MODES)
BB_BACKEND_STATE_CHECK(BorderColor, BB_BACKEND_BORDER_COLORS)
static_assert(static_cast<s32>(vk::BorderColor::eFloatCustomEXT) ==
              static_cast<s32>(Backend::BorderColor::FloatCustom));
#undef BB_BACKEND_STATE_CHECK_ONE_BlendFactor
#undef BB_BACKEND_STATE_CHECK_ONE_BlendOp
#undef BB_BACKEND_STATE_CHECK_ONE_CompareOp
#undef BB_BACKEND_STATE_CHECK_ONE_StencilOp
#undef BB_BACKEND_STATE_CHECK_ONE_CullModeFlagBits
#undef BB_BACKEND_STATE_CHECK_ONE_FrontFace
#undef BB_BACKEND_STATE_CHECK_ONE_PrimitiveTopology
#undef BB_BACKEND_STATE_CHECK_ONE_PolygonMode
#undef BB_BACKEND_STATE_CHECK_ONE_LogicOp
#undef BB_BACKEND_STATE_CHECK_ONE_Filter
#undef BB_BACKEND_STATE_CHECK_ONE_SamplerMipmapMode
#undef BB_BACKEND_STATE_CHECK_ONE_SamplerAddressMode
#undef BB_BACKEND_STATE_CHECK_ONE_BorderColor
#undef BB_BACKEND_STATE_CHECK_ONE
#undef BB_BACKEND_STATE_CHECK
static_assert(static_cast<u32>(vk::ColorComponentFlagBits::eR) == u32(Backend::ColorWriteMask::R) &&
              static_cast<u32>(vk::ColorComponentFlagBits::eG) == u32(Backend::ColorWriteMask::G) &&
              static_cast<u32>(vk::ColorComponentFlagBits::eB) == u32(Backend::ColorWriteMask::B) &&
              static_cast<u32>(vk::ColorComponentFlagBits::eA) == u32(Backend::ColorWriteMask::A));
static_assert(static_cast<s32>(vk::ImageType::e1D) == s32(Backend::ImageType::Image1D) &&
              static_cast<s32>(vk::ImageType::e2D) == s32(Backend::ImageType::Image2D) &&
              static_cast<s32>(vk::ImageType::e3D) == s32(Backend::ImageType::Image3D));
static_assert(u32(vk::ImageUsageFlagBits::eTransferSrc) == u32(Backend::ImageUse::TransferSource) &&
              u32(vk::ImageUsageFlagBits::eTransferDst) == u32(Backend::ImageUse::TransferDestination) &&
              u32(vk::ImageUsageFlagBits::eSampled) == u32(Backend::ImageUse::Sampled) &&
              u32(vk::ImageUsageFlagBits::eStorage) == u32(Backend::ImageUse::Storage) &&
              u32(vk::ImageUsageFlagBits::eColorAttachment) == u32(Backend::ImageUse::ColorAttachment) &&
              u32(vk::ImageUsageFlagBits::eDepthStencilAttachment) ==
                  u32(Backend::ImageUse::DepthStencilAttachment) &&
              u32(vk::ImageUsageFlagBits::eAttachmentFeedbackLoopEXT) ==
                  u32(Backend::ImageUse::AttachmentFeedbackLoop));
static_assert(s32(vk::ImageViewType::e1D) == s32(Backend::ImageViewType::View1D) &&
              s32(vk::ImageViewType::e2D) == s32(Backend::ImageViewType::View2D) &&
              s32(vk::ImageViewType::e3D) == s32(Backend::ImageViewType::View3D) &&
              s32(vk::ImageViewType::eCube) == s32(Backend::ImageViewType::Cube) &&
              s32(vk::ImageViewType::e1DArray) == s32(Backend::ImageViewType::View1DArray) &&
              s32(vk::ImageViewType::e2DArray) == s32(Backend::ImageViewType::View2DArray) &&
              s32(vk::ImageViewType::eCubeArray) == s32(Backend::ImageViewType::CubeArray));
static_assert(u32(vk::ImageAspectFlagBits::eColor) == u32(Backend::ImageAspect::Color) &&
              u32(vk::ImageAspectFlagBits::eDepth) == u32(Backend::ImageAspect::Depth) &&
              u32(vk::ImageAspectFlagBits::eStencil) == u32(Backend::ImageAspect::Stencil));
static_assert(s32(vk::ComponentSwizzle::eIdentity) == s32(Backend::ComponentSwizzle::Identity) &&
              s32(vk::ComponentSwizzle::eZero) == s32(Backend::ComponentSwizzle::Zero) &&
              s32(vk::ComponentSwizzle::eOne) == s32(Backend::ComponentSwizzle::One) &&
              s32(vk::ComponentSwizzle::eR) == s32(Backend::ComponentSwizzle::R) &&
              s32(vk::ComponentSwizzle::eG) == s32(Backend::ComponentSwizzle::G) &&
              s32(vk::ComponentSwizzle::eB) == s32(Backend::ComponentSwizzle::B) &&
              s32(vk::ComponentSwizzle::eA) == s32(Backend::ComponentSwizzle::A));
static_assert(s32(vk::DescriptorType::eSampler) == s32(Backend::BindingKind::Sampler) &&
              s32(vk::DescriptorType::eCombinedImageSampler) ==
                  s32(Backend::BindingKind::CombinedImageSampler) &&
              s32(vk::DescriptorType::eSampledImage) == s32(Backend::BindingKind::SampledImage) &&
              s32(vk::DescriptorType::eStorageImage) == s32(Backend::BindingKind::StorageImage) &&
              s32(vk::DescriptorType::eUniformBuffer) == s32(Backend::BindingKind::UniformBuffer) &&
              s32(vk::DescriptorType::eStorageBuffer) == s32(Backend::BindingKind::StorageBuffer));
static_assert(Backend::WholeRange == VK_WHOLE_SIZE &&
              Backend::GeneralLayout == u32(vk::ImageLayout::eGeneral));
// Bindings are read in place as Vulkan's descriptor infos.
static_assert(sizeof(Backend::BufferBinding) == sizeof(VkDescriptorBufferInfo) &&
              offsetof(Backend::BufferBinding, buffer) == offsetof(VkDescriptorBufferInfo, buffer) &&
              offsetof(Backend::BufferBinding, offset) == offsetof(VkDescriptorBufferInfo, offset) &&
              offsetof(Backend::BufferBinding, range) == offsetof(VkDescriptorBufferInfo, range));
static_assert(sizeof(Backend::ImageBinding) == sizeof(VkDescriptorImageInfo) &&
              offsetof(Backend::ImageBinding, sampler) == offsetof(VkDescriptorImageInfo, sampler) &&
              offsetof(Backend::ImageBinding, view) == offsetof(VkDescriptorImageInfo, imageView) &&
              offsetof(Backend::ImageBinding, layout) == offsetof(VkDescriptorImageInfo, imageLayout));
static_assert(sizeof(Backend::Viewport) == sizeof(VkViewport) &&
              offsetof(Backend::Viewport, x) == offsetof(VkViewport, x) &&
              offsetof(Backend::Viewport, max_depth) == offsetof(VkViewport, maxDepth));
static_assert(sizeof(Backend::Rect2D) == sizeof(VkRect2D) &&
              offsetof(Backend::Rect2D, x) == offsetof(VkRect2D, offset) &&
              offsetof(Backend::Rect2D, width) == offsetof(VkRect2D, extent));
static_assert(u32(vk::ShaderStageFlagBits::eVertex) == u32(Backend::ShaderStage::Vertex) &&
              u32(vk::ShaderStageFlagBits::eTessellationControl) ==
                  u32(Backend::ShaderStage::TessellationControl) &&
              u32(vk::ShaderStageFlagBits::eTessellationEvaluation) ==
                  u32(Backend::ShaderStage::TessellationEvaluation) &&
              u32(vk::ShaderStageFlagBits::eGeometry) == u32(Backend::ShaderStage::Geometry) &&
              u32(vk::ShaderStageFlagBits::eFragment) == u32(Backend::ShaderStage::Fragment) &&
              u32(vk::ShaderStageFlagBits::eCompute) == u32(Backend::ShaderStage::Compute));
// Pipeline keys hold write masks; the serialized shader cache depends on their size.
static_assert(sizeof(Backend::ColorWriteMask) == sizeof(vk::ColorComponentFlags));

// The values are Vulkan's, so the conversions are casts.
[[nodiscard]] constexpr vk::BlendFactor ToVk(Backend::BlendFactor v) noexcept {
    return static_cast<vk::BlendFactor>(v);
}
[[nodiscard]] constexpr vk::BlendOp ToVk(Backend::BlendOp v) noexcept {
    return static_cast<vk::BlendOp>(v);
}
[[nodiscard]] constexpr vk::CompareOp ToVk(Backend::CompareOp v) noexcept {
    return static_cast<vk::CompareOp>(v);
}
[[nodiscard]] constexpr vk::StencilOp ToVk(Backend::StencilOp v) noexcept {
    return static_cast<vk::StencilOp>(v);
}
[[nodiscard]] constexpr vk::CullModeFlags ToVk(Backend::CullMode v) noexcept {
    return vk::CullModeFlags(static_cast<VkCullModeFlags>(v));
}
[[nodiscard]] constexpr vk::FrontFace ToVk(Backend::FrontFace v) noexcept {
    return static_cast<vk::FrontFace>(v);
}
[[nodiscard]] constexpr vk::PrimitiveTopology ToVk(Backend::PrimitiveTopology v) noexcept {
    return static_cast<vk::PrimitiveTopology>(v);
}
[[nodiscard]] constexpr vk::PolygonMode ToVk(Backend::PolygonMode v) noexcept {
    return static_cast<vk::PolygonMode>(v);
}
[[nodiscard]] constexpr vk::LogicOp ToVk(Backend::LogicOp v) noexcept {
    return static_cast<vk::LogicOp>(v);
}
[[nodiscard]] constexpr vk::Filter ToVk(Backend::Filter v) noexcept {
    return static_cast<vk::Filter>(v);
}
[[nodiscard]] constexpr vk::SamplerMipmapMode ToVk(Backend::MipmapMode v) noexcept {
    return static_cast<vk::SamplerMipmapMode>(v);
}
[[nodiscard]] constexpr vk::SamplerAddressMode ToVk(Backend::AddressMode v) noexcept {
    return static_cast<vk::SamplerAddressMode>(v);
}
[[nodiscard]] constexpr vk::BorderColor ToVk(Backend::BorderColor v) noexcept {
    return static_cast<vk::BorderColor>(v);
}
[[nodiscard]] constexpr vk::ImageType ToVk(Backend::ImageType v) noexcept {
    return static_cast<vk::ImageType>(v);
}
[[nodiscard]] constexpr vk::ImageUsageFlags ToVk(Backend::ImageUse v) noexcept {
    return vk::ImageUsageFlags(static_cast<VkImageUsageFlags>(v));
}
[[nodiscard]] constexpr Backend::ImageUse FromVk(vk::ImageUsageFlags v) noexcept {
    return static_cast<Backend::ImageUse>(static_cast<VkImageUsageFlags>(v));
}
[[nodiscard]] constexpr vk::ImageViewType ToVk(Backend::ImageViewType v) noexcept {
    return static_cast<vk::ImageViewType>(v);
}
[[nodiscard]] constexpr vk::ImageAspectFlags ToVk(Backend::ImageAspect v) noexcept {
    return vk::ImageAspectFlags(static_cast<VkImageAspectFlags>(v));
}
[[nodiscard]] constexpr Backend::ImageAspect FromVk(vk::ImageAspectFlags v) noexcept {
    return static_cast<Backend::ImageAspect>(static_cast<VkImageAspectFlags>(v));
}
[[nodiscard]] constexpr vk::ComponentSwizzle ToVk(Backend::ComponentSwizzle v) noexcept {
    return static_cast<vk::ComponentSwizzle>(v);
}
[[nodiscard]] constexpr vk::ComponentMapping ToVk(const Backend::ComponentMapping& m) noexcept {
    return {ToVk(m.r), ToVk(m.g), ToVk(m.b), ToVk(m.a)};
}
[[nodiscard]] constexpr vk::BufferCopy ToVk(const Backend::BufferCopy& c) noexcept {
    return {c.src_offset, c.dst_offset, c.size};
}
[[nodiscard]] constexpr vk::BufferImageCopy ToVk(const Backend::BufferImageCopy& c) noexcept {
    return vk::BufferImageCopy{
        .bufferOffset = c.buffer_offset,
        .bufferRowLength = c.buffer_row_length,
        .bufferImageHeight = c.buffer_image_height,
        .imageSubresource{
            .aspectMask = ToVk(c.aspect),
            .mipLevel = c.level,
            .baseArrayLayer = c.base_layer,
            .layerCount = c.layers,
        },
        .imageOffset = {c.x, c.y, c.z},
        .imageExtent = {c.width, c.height, c.depth},
    };
}
[[nodiscard]] inline Backend::BufferBinding MakeBufferBinding(vk::Buffer buffer, u64 offset,
                                                              u64 range) noexcept {
    return {reinterpret_cast<u64>(static_cast<VkBuffer>(buffer)), offset, range};
}
[[nodiscard]] inline Backend::ImageBinding MakeImageBinding(vk::Sampler sampler, vk::ImageView view,
                                                            vk::ImageLayout layout) noexcept {
    return {reinterpret_cast<u64>(static_cast<VkSampler>(sampler)),
            reinterpret_cast<u64>(static_cast<VkImageView>(view)), static_cast<u32>(layout)};
}
/// The adapter's slot write as Vulkan's (its infos are read in place).
[[nodiscard]] inline vk::WriteDescriptorSet ToVk(const Backend::SlotWrite& w) noexcept {
    return vk::WriteDescriptorSet{
        .dstBinding = w.slot,
        .dstArrayElement = 0,
        .descriptorCount = w.count,
        .descriptorType = static_cast<vk::DescriptorType>(w.kind),
        .pImageInfo = reinterpret_cast<const vk::DescriptorImageInfo*>(w.images),
        .pBufferInfo = reinterpret_cast<const vk::DescriptorBufferInfo*>(w.buffers),
    };
}
/// Viewports and scissors are laid out like Vulkan's (checked below): read in place.
template <typename Range>
[[nodiscard]] inline auto ToVk(const Range& range) noexcept
    requires std::is_same_v<std::remove_cvref_t<decltype(*std::data(range))>, Backend::Viewport>
{
    return std::span<const vk::Viewport>{reinterpret_cast<const vk::Viewport*>(std::data(range)),
                                         std::size(range)};
}
template <typename Range>
[[nodiscard]] inline auto ToVk(const Range& range) noexcept
    requires std::is_same_v<std::remove_cvref_t<decltype(*std::data(range))>, Backend::Rect2D>
{
    return std::span<const vk::Rect2D>{reinterpret_cast<const vk::Rect2D*>(std::data(range)),
                                       std::size(range)};
}
/// A render target's view and layout as RenderAttachment holds them, and back.
[[nodiscard]] inline u64 ToNative(vk::ImageView view) noexcept {
    return reinterpret_cast<u64>(static_cast<VkImageView>(view));
}
[[nodiscard]] inline u64 ToNative(vk::Buffer buffer) noexcept {
    return reinterpret_cast<u64>(static_cast<VkBuffer>(buffer));
}
[[nodiscard]] inline u64 ToNative(vk::Pipeline pipeline) noexcept {
    return reinterpret_cast<u64>(static_cast<VkPipeline>(pipeline));
}
[[nodiscard]] constexpr u32 ToNative(vk::ImageLayout layout) noexcept {
    return static_cast<u32>(layout);
}
[[nodiscard]] inline vk::ImageView ToVkImageView(u64 view) noexcept {
    return vk::ImageView{reinterpret_cast<VkImageView>(view)};
}
[[nodiscard]] constexpr vk::ImageLayout ToVkImageLayout(u32 layout) noexcept {
    return static_cast<vk::ImageLayout>(layout);
}
[[nodiscard]] constexpr vk::ShaderStageFlags ToVk(Backend::ShaderStage v) noexcept {
    return vk::ShaderStageFlags{static_cast<u32>(v)};
}
[[nodiscard]] constexpr vk::ColorComponentFlags ToVk(Backend::ColorWriteMask v) noexcept {
    return vk::ColorComponentFlags(static_cast<VkColorComponentFlags>(v));
}
template <std::size_t N>
[[nodiscard]] inline std::array<vk::ColorComponentFlags, N> ToVk(
    const std::array<Backend::ColorWriteMask, N>& masks) noexcept {
    return std::bit_cast<std::array<vk::ColorComponentFlags, N>>(masks);
}

} // namespace Vulkan
