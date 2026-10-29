// SPDX-License-Identifier: GPL-2.0-or-later
// The render adapter's Metal presents (docs/RENDER_ADAPTER.md, M2 step 4): with the Metal frame
// (BB_METAL_FRAME=1) the window's CAMetalLayer is presented through Metal, not a MoltenVK
// swapchain. Each present draws the game frame letterboxed into the layer's next drawable, the
// overlay over it, on MoltenVK's queue (after the frame's own work, so in order), and presents
// it. Plain C++: Metal objects cross as void*.

#pragma once

#include <functional>
#include <string>
#include <utility>

#include "common/types.h"

namespace MetalPresent {

struct LayerConfig {
    bool hdr{};           ///< HDR10 (RGB10A2, BT.2100 PQ); else BGRA8 in sRGB
    bool display_sync{true};
    u32 max_drawables{3};
};

/// Sets up the window's CAMetalLayer (void* = CAMetalLayer*) for presents on MoltenVK's device.
void Configure(void* layer, const LayerConfig& config);
/// The layer's size in pixels (its bounds times its scale); sets it as the drawable size.
[[nodiscard]] std::pair<u32, u32> FitDrawableSize(void* layer);
/// Whether the display can show HDR (extended dynamic range) content.
[[nodiscard]] bool DisplaySupportsHdr(void* layer);

struct Rect {
    s32 x{}, y{};
    u32 width{}, height{};
};

struct PresentInfo {
    void* frame{};             ///< the game frame (a VkImage), sampled
    Rect fit;                  ///< where it goes in the drawable (letterboxed); the rest is black
    /// Draws over the frame: (id<MTLCommandBuffer>, id<MTLTexture> drawable, width, height).
    std::function<void(void*, void*, u32, u32)> overlay;
    std::function<void()> done; ///< once the GPU has read the frame (also when nothing is shown)
    /// Not empty: the drawable as presented is also written there (raw, 4 bytes a pixel in the
    /// layer's order; tools/grab.sh --screen), the game's own picture.
    std::string dump_path;
};

/// Draws `info.frame` into the layer's next drawable and presents it, on MoltenVK's queue.
/// False when there was no drawable (the frame is not shown).
bool Present(void* layer, PresentInfo info);

} // namespace MetalPresent
