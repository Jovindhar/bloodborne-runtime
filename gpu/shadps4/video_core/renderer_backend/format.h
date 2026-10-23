// The render adapter's pixel formats (docs/RENDER_ADAPTER.md, step A1).
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"

namespace VideoCore::Backend {

// Every format the renderer names. The values are Vulkan's (VkFormat), so pipeline keys and the
// shader cache keep their layout; renderer_vulkan/vk_format.h checks each one at compile time.
#define BB_BACKEND_FORMATS(X) \
    X(Undefined, 0) \
    X(R4G4UnormPack8, 1) \
    X(R4G4B4A4UnormPack16, 2) \
    X(B4G4R4A4UnormPack16, 3) \
    X(R5G6B5UnormPack16, 4) \
    X(B5G6R5UnormPack16, 5) \
    X(R5G5B5A1UnormPack16, 6) \
    X(B5G5R5A1UnormPack16, 7) \
    X(A1R5G5B5UnormPack16, 8) \
    X(R8Unorm, 9) \
    X(R8Snorm, 10) \
    X(R8Uscaled, 11) \
    X(R8Sscaled, 12) \
    X(R8Uint, 13) \
    X(R8Sint, 14) \
    X(R8Srgb, 15) \
    X(R8G8Unorm, 16) \
    X(R8G8Snorm, 17) \
    X(R8G8Uscaled, 18) \
    X(R8G8Sscaled, 19) \
    X(R8G8Uint, 20) \
    X(R8G8Sint, 21) \
    X(R8G8Srgb, 22) \
    X(R8G8B8Unorm, 23) \
    X(R8G8B8Snorm, 24) \
    X(R8G8B8Uscaled, 25) \
    X(R8G8B8Sscaled, 26) \
    X(R8G8B8Uint, 27) \
    X(R8G8B8Sint, 28) \
    X(R8G8B8Srgb, 29) \
    X(B8G8R8Unorm, 30) \
    X(B8G8R8Snorm, 31) \
    X(B8G8R8Uscaled, 32) \
    X(B8G8R8Sscaled, 33) \
    X(B8G8R8Uint, 34) \
    X(B8G8R8Sint, 35) \
    X(B8G8R8Srgb, 36) \
    X(R8G8B8A8Unorm, 37) \
    X(R8G8B8A8Snorm, 38) \
    X(R8G8B8A8Uscaled, 39) \
    X(R8G8B8A8Sscaled, 40) \
    X(R8G8B8A8Uint, 41) \
    X(R8G8B8A8Sint, 42) \
    X(R8G8B8A8Srgb, 43) \
    X(B8G8R8A8Unorm, 44) \
    X(B8G8R8A8Snorm, 45) \
    X(B8G8R8A8Uscaled, 46) \
    X(B8G8R8A8Sscaled, 47) \
    X(B8G8R8A8Uint, 48) \
    X(B8G8R8A8Sint, 49) \
    X(B8G8R8A8Srgb, 50) \
    X(A8B8G8R8UnormPack32, 51) \
    X(A8B8G8R8SnormPack32, 52) \
    X(A8B8G8R8UscaledPack32, 53) \
    X(A8B8G8R8SscaledPack32, 54) \
    X(A8B8G8R8UintPack32, 55) \
    X(A8B8G8R8SintPack32, 56) \
    X(A8B8G8R8SrgbPack32, 57) \
    X(A2R10G10B10UnormPack32, 58) \
    X(A2R10G10B10SnormPack32, 59) \
    X(A2R10G10B10UscaledPack32, 60) \
    X(A2R10G10B10SscaledPack32, 61) \
    X(A2R10G10B10UintPack32, 62) \
    X(A2R10G10B10SintPack32, 63) \
    X(A2B10G10R10UnormPack32, 64) \
    X(A2B10G10R10SnormPack32, 65) \
    X(A2B10G10R10UscaledPack32, 66) \
    X(A2B10G10R10SscaledPack32, 67) \
    X(A2B10G10R10UintPack32, 68) \
    X(A2B10G10R10SintPack32, 69) \
    X(R16Unorm, 70) \
    X(R16Snorm, 71) \
    X(R16Uscaled, 72) \
    X(R16Sscaled, 73) \
    X(R16Uint, 74) \
    X(R16Sint, 75) \
    X(R16Sfloat, 76) \
    X(R16G16Unorm, 77) \
    X(R16G16Snorm, 78) \
    X(R16G16Uscaled, 79) \
    X(R16G16Sscaled, 80) \
    X(R16G16Uint, 81) \
    X(R16G16Sint, 82) \
    X(R16G16Sfloat, 83) \
    X(R16G16B16Unorm, 84) \
    X(R16G16B16Snorm, 85) \
    X(R16G16B16Uscaled, 86) \
    X(R16G16B16Sscaled, 87) \
    X(R16G16B16Uint, 88) \
    X(R16G16B16Sint, 89) \
    X(R16G16B16Sfloat, 90) \
    X(R16G16B16A16Unorm, 91) \
    X(R16G16B16A16Snorm, 92) \
    X(R16G16B16A16Uscaled, 93) \
    X(R16G16B16A16Sscaled, 94) \
    X(R16G16B16A16Uint, 95) \
    X(R16G16B16A16Sint, 96) \
    X(R16G16B16A16Sfloat, 97) \
    X(R32Uint, 98) \
    X(R32Sint, 99) \
    X(R32Sfloat, 100) \
    X(R32G32Uint, 101) \
    X(R32G32Sint, 102) \
    X(R32G32Sfloat, 103) \
    X(R32G32B32Uint, 104) \
    X(R32G32B32Sint, 105) \
    X(R32G32B32Sfloat, 106) \
    X(R32G32B32A32Uint, 107) \
    X(R32G32B32A32Sint, 108) \
    X(R32G32B32A32Sfloat, 109) \
    X(R64Uint, 110) \
    X(R64Sint, 111) \
    X(R64Sfloat, 112) \
    X(R64G64Uint, 113) \
    X(R64G64Sint, 114) \
    X(R64G64Sfloat, 115) \
    X(R64G64B64Uint, 116) \
    X(R64G64B64Sint, 117) \
    X(R64G64B64Sfloat, 118) \
    X(R64G64B64A64Uint, 119) \
    X(R64G64B64A64Sint, 120) \
    X(R64G64B64A64Sfloat, 121) \
    X(B10G11R11UfloatPack32, 122) \
    X(E5B9G9R9UfloatPack32, 123) \
    X(D16Unorm, 124) \
    X(X8D24UnormPack32, 125) \
    X(D32Sfloat, 126) \
    X(S8Uint, 127) \
    X(D16UnormS8Uint, 128) \
    X(D24UnormS8Uint, 129) \
    X(D32SfloatS8Uint, 130) \
    X(Bc1RgbUnormBlock, 131) \
    X(Bc1RgbSrgbBlock, 132) \
    X(Bc1RgbaUnormBlock, 133) \
    X(Bc1RgbaSrgbBlock, 134) \
    X(Bc2UnormBlock, 135) \
    X(Bc2SrgbBlock, 136) \
    X(Bc3UnormBlock, 137) \
    X(Bc3SrgbBlock, 138) \
    X(Bc4UnormBlock, 139) \
    X(Bc4SnormBlock, 140) \
    X(Bc5UnormBlock, 141) \
    X(Bc5SnormBlock, 142) \
    X(Bc6HUfloatBlock, 143) \
    X(Bc6HSfloatBlock, 144) \
    X(Bc7UnormBlock, 145) \
    X(Bc7SrgbBlock, 146) \
    X(R10X6UnormPack16, 1000156007) \
    X(R10X6G10X6Unorm2Pack16, 1000156008) \
    X(R12X4UnormPack16, 1000156017) \
    X(R12X4G12X4Unorm2Pack16, 1000156018) \
    X(A4R4G4B4UnormPack16, 1000340000) \
    X(A4B4G4R4UnormPack16, 1000340001)

enum class Format : s32 {
#define BB_BACKEND_FORMAT_ENUMERATOR(name, value) name = value,
    BB_BACKEND_FORMATS(BB_BACKEND_FORMAT_ENUMERATOR)
#undef BB_BACKEND_FORMAT_ENUMERATOR
};

/// The format's name for logs ("R8G8B8A8Unorm"), or "Unknown".
const char* FormatName(Format format);

} // namespace VideoCore::Backend
