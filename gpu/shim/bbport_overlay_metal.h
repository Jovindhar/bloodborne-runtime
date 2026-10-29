// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the settings overlay drawn by Dear ImGui's Metal backend, for the Metal frame's
// presenter (docs/RENDER_ADAPTER.md, M2 step 4). Plain C++: Metal objects cross as void*.

#pragma once

#include <functional>

namespace BbOverlay::MetalBackend {

/// `device`: id<MTLDevice>.
bool Init(void* device);
/// Draws a frame into `target` (id<MTLTexture>, loaded, not cleared) on `command_buffer`
/// (id<MTLCommandBuffer>): the backend's new frame, then `build` (ImGui::NewFrame .. Render),
/// then its draw data, if any.
void Frame(void* command_buffer, void* target, const std::function<void()>& build);

} // namespace BbOverlay::MetalBackend
