// The render adapter's pipeline-state vocabulary (docs/RENDER_ADAPTER.md, step A1).
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"

namespace VideoCore::Backend {

// The values are Vulkan's, so pipeline keys keep their layout and hash;
// renderer_vulkan/vk_state.h checks each one at compile time. Each list is X(name, value).

#define BB_BACKEND_BLEND_FACTORS(X)                                                                \
    X(Zero, 0)                                                                                     \
    X(One, 1)                                                                                      \
    X(SrcColor, 2)                                                                                 \
    X(OneMinusSrcColor, 3)                                                                         \
    X(DstColor, 4)                                                                                 \
    X(OneMinusDstColor, 5)                                                                         \
    X(SrcAlpha, 6)                                                                                 \
    X(OneMinusSrcAlpha, 7)                                                                         \
    X(DstAlpha, 8)                                                                                 \
    X(OneMinusDstAlpha, 9)                                                                         \
    X(ConstantColor, 10)                                                                           \
    X(OneMinusConstantColor, 11)                                                                   \
    X(ConstantAlpha, 12)                                                                           \
    X(OneMinusConstantAlpha, 13)                                                                   \
    X(SrcAlphaSaturate, 14)                                                                        \
    X(Src1Color, 15)                                                                               \
    X(OneMinusSrc1Color, 16)                                                                       \
    X(Src1Alpha, 17)                                                                               \
    X(OneMinusSrc1Alpha, 18)

#define BB_BACKEND_BLEND_OPS(X)                                                                    \
    X(Add, 0)                                                                                      \
    X(Subtract, 1)                                                                                 \
    X(ReverseSubtract, 2)                                                                          \
    X(Min, 3)                                                                                      \
    X(Max, 4)

#define BB_BACKEND_COMPARE_OPS(X)                                                                  \
    X(Never, 0)                                                                                    \
    X(Less, 1)                                                                                     \
    X(Equal, 2)                                                                                    \
    X(LessOrEqual, 3)                                                                              \
    X(Greater, 4)                                                                                  \
    X(NotEqual, 5)                                                                                 \
    X(GreaterOrEqual, 6)                                                                           \
    X(Always, 7)

#define BB_BACKEND_STENCIL_OPS(X)                                                                  \
    X(Keep, 0)                                                                                     \
    X(Zero, 1)                                                                                     \
    X(Replace, 2)                                                                                  \
    X(IncrementAndClamp, 3)                                                                        \
    X(DecrementAndClamp, 4)                                                                        \
    X(Invert, 5)                                                                                   \
    X(IncrementAndWrap, 6)                                                                         \
    X(DecrementAndWrap, 7)

#define BB_BACKEND_CULL_MODES(X)                                                                   \
    X(None, 0)                                                                                     \
    X(Front, 1)                                                                                    \
    X(Back, 2)                                                                                     \
    X(FrontAndBack, 3)

#define BB_BACKEND_FRONT_FACES(X)                                                                  \
    X(CounterClockwise, 0)                                                                         \
    X(Clockwise, 1)

#define BB_BACKEND_PRIMITIVE_TOPOLOGIES(X)                                                         \
    X(PointList, 0)                                                                                \
    X(LineList, 1)                                                                                 \
    X(LineStrip, 2)                                                                                \
    X(TriangleList, 3)                                                                             \
    X(TriangleStrip, 4)                                                                            \
    X(TriangleFan, 5)                                                                              \
    X(LineListWithAdjacency, 6)                                                                    \
    X(LineStripWithAdjacency, 7)                                                                   \
    X(TriangleListWithAdjacency, 8)                                                                \
    X(TriangleStripWithAdjacency, 9)                                                               \
    X(PatchList, 10)

#define BB_BACKEND_POLYGON_MODES(X)                                                                \
    X(Fill, 0)                                                                                     \
    X(Line, 1)                                                                                     \
    X(Point, 2)

#define BB_BACKEND_LOGIC_OPS(X)                                                                    \
    X(Clear, 0)                                                                                    \
    X(And, 1)                                                                                      \
    X(AndReverse, 2)                                                                               \
    X(Copy, 3)                                                                                     \
    X(AndInverted, 4)                                                                              \
    X(NoOp, 5)                                                                                     \
    X(Xor, 6)                                                                                      \
    X(Or, 7)                                                                                       \
    X(Nor, 8)                                                                                      \
    X(Equivalent, 9)                                                                               \
    X(Invert, 10)                                                                                  \
    X(OrReverse, 11)                                                                               \
    X(CopyInverted, 12)                                                                            \
    X(OrInverted, 13)                                                                              \
    X(Nand, 14)                                                                                    \
    X(Set, 15)

#define BB_BACKEND_FILTERS(X) \
    X(Nearest, 0) \
    X(Linear, 1)

#define BB_BACKEND_MIPMAP_MODES(X) \
    X(Nearest, 0) \
    X(Linear, 1)

#define BB_BACKEND_ADDRESS_MODES(X) \
    X(Repeat, 0) \
    X(MirroredRepeat, 1) \
    X(ClampToEdge, 2) \
    X(ClampToBorder, 3) \
    X(MirrorClampToEdge, 4)

#define BB_BACKEND_BORDER_COLORS(X) \
    X(FloatTransparentBlack, 0) \
    X(IntTransparentBlack, 1) \
    X(FloatOpaqueBlack, 2) \
    X(IntOpaqueBlack, 3) \
    X(FloatOpaqueWhite, 4) \
    X(IntOpaqueWhite, 5)

#define BB_BACKEND_ENUMERATOR(name, value) name = value,
enum class BlendFactor : s32 { BB_BACKEND_BLEND_FACTORS(BB_BACKEND_ENUMERATOR) };
enum class BlendOp : s32 { BB_BACKEND_BLEND_OPS(BB_BACKEND_ENUMERATOR) };
enum class CompareOp : s32 { BB_BACKEND_COMPARE_OPS(BB_BACKEND_ENUMERATOR) };
enum class StencilOp : s32 { BB_BACKEND_STENCIL_OPS(BB_BACKEND_ENUMERATOR) };
enum class CullMode : u32 { BB_BACKEND_CULL_MODES(BB_BACKEND_ENUMERATOR) };
enum class FrontFace : s32 { BB_BACKEND_FRONT_FACES(BB_BACKEND_ENUMERATOR) };
enum class PrimitiveTopology : s32 { BB_BACKEND_PRIMITIVE_TOPOLOGIES(BB_BACKEND_ENUMERATOR) };
enum class PolygonMode : s32 { BB_BACKEND_POLYGON_MODES(BB_BACKEND_ENUMERATOR) };
enum class LogicOp : s32 { BB_BACKEND_LOGIC_OPS(BB_BACKEND_ENUMERATOR) };
enum class Filter : s32 { BB_BACKEND_FILTERS(BB_BACKEND_ENUMERATOR) };
enum class MipmapMode : s32 { BB_BACKEND_MIPMAP_MODES(BB_BACKEND_ENUMERATOR) };
enum class AddressMode : s32 { BB_BACKEND_ADDRESS_MODES(BB_BACKEND_ENUMERATOR) };
/// FloatCustom takes SamplerDesc::border_color_value (Vulkan's VK_EXT_custom_border_color).
enum class BorderColor : s32 { BB_BACKEND_BORDER_COLORS(BB_BACKEND_ENUMERATOR) FloatCustom = 1000287003 };
#undef BB_BACKEND_ENUMERATOR

/// Which color components an attachment writes (bits as Vulkan's).
enum class ColorWriteMask : u32 {
    None = 0,
    R = 1u << 0,
    G = 1u << 1,
    B = 1u << 2,
    A = 1u << 3,
    All = R | G | B | A,
};
constexpr ColorWriteMask operator|(ColorWriteMask a, ColorWriteMask b) noexcept {
    return static_cast<ColorWriteMask>(static_cast<u32>(a) | static_cast<u32>(b));
}
constexpr ColorWriteMask operator&(ColorWriteMask a, ColorWriteMask b) noexcept {
    return static_cast<ColorWriteMask>(static_cast<u32>(a) & static_cast<u32>(b));
}

} // namespace VideoCore::Backend
