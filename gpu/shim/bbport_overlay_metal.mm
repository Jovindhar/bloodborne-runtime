// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the settings overlay's Metal side (bbport_overlay_metal.h). ARC.

#import <Metal/Metal.h>

#include "bbport_overlay_metal.h"
#include "imgui.h"
#include "imgui_impl_metal.h"

namespace BbOverlay::MetalBackend {

bool Init(void* device) {
    return ImGui_ImplMetal_Init((__bridge id<MTLDevice>)device);
}

void Frame(void* command_buffer, void* target, const std::function<void()>& build) {
    id<MTLCommandBuffer> cmd = (__bridge id<MTLCommandBuffer>)command_buffer;
    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = (__bridge id<MTLTexture>)target;
    pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    // The backend picks its pipeline by the pass's formats: the descriptor comes first.
    ImGui_ImplMetal_NewFrame(pass);
    build();
    ImDrawData* draw_data = ImGui::GetDrawData();
    if (!draw_data || draw_data->CmdLists.Size == 0) {
        return; // nothing drawn: no pass
    }
    id<MTLRenderCommandEncoder> encoder = [cmd renderCommandEncoderWithDescriptor:pass];
    encoder.label = @"bbport overlay";
    ImGui_ImplMetal_RenderDrawData(draw_data, cmd, encoder);
    [encoder endEncoding];
}

} // namespace BbOverlay::MetalBackend
