// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/renderer_backend/device.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace VideoCore::Backend {

Api SelectedApi() {
    static const Api api = [] {
        const char* value = std::getenv("BB_GFX_BACKEND");
        if (value && std::strcmp(value, "metal") == 0) {
#ifdef __APPLE__
            // M1: resources are made in Metal; MoltenVK still records.
            return Api::Metal;
#else
            std::printf("Render adapter: the Metal backend needs macOS; using Vulkan\n");
#endif
        } else if (value && *value && std::strcmp(value, "vulkan") != 0) {
            std::printf("Render adapter: unknown BB_GFX_BACKEND=%s; using Vulkan\n", value);
        }
        return Api::Vulkan;
    }();
    return api;
}

} // namespace VideoCore::Backend
