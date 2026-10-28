// SPDX-License-Identifier: GPL-2.0-or-later
// bloodborne-runtime: BB_DRAW_CENSUS=1 counts the game's draws at the engine's render boundary
// (bloodborne-recomp's annotations/functions.tsv: CoreGraphics2's draw and dispatch methods and
// its render-target passes) against every draw that reaches libGnm, and prints both every 5 s
// with the busiest callers that reach libGnm another way. For the native renderer's N6.
#pragma once

#include <cstdint>

namespace BbDrawCensus {
/// Installs the counting hooks (BB_DRAW_CENSUS=1 only), from bbgpu_patch_image.
void PatchImage(unsigned char* image, std::uint64_t size);
} // namespace BbDrawCensus
