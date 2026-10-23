// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/renderer_backend/format.h"

namespace VideoCore::Backend {

const char* FormatName(Format format) {
    switch (format) {
#define BB_BACKEND_FORMAT_NAME(name, value)                                                        \
    case Format::name:                                                                             \
        return #name;
        BB_BACKEND_FORMATS(BB_BACKEND_FORMAT_NAME)
#undef BB_BACKEND_FORMAT_NAME
    }
    return "Unknown";
}

} // namespace VideoCore::Backend
