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
            std::printf("Render adapter: the Metal backend is not available yet; using Vulkan\n");
        } else if (value && *value && std::strcmp(value, "vulkan") != 0) {
            std::printf("Render adapter: unknown BB_GFX_BACKEND=%s; using Vulkan\n", value);
        }
        return Api::Vulkan;
    }();
    return api;
}

} // namespace VideoCore::Backend
