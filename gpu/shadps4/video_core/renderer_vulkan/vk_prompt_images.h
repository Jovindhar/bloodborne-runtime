// SPDX-License-Identifier: GPL-2.0-or-later
// bbport 0.6's active-device key guides, uploaded through the shared render adapter.
#pragma once

#include <memory>
#include <array>
#include <unordered_map>
#include <vector>

#include "common/slot_vector.h"
#include "video_core/renderer_backend/binding.h"
#include "video_core/texture_cache/image.h"

namespace VideoCore {
class BufferCache;
namespace Backend { class Device; class Transfers; }
}

namespace Vulkan {

class PromptImages {
public:
    PromptImages(VideoCore::Backend::Device& device, VideoCore::Backend::Transfers& runtime,
                 VideoCore::BufferCache& buffer_cache);
    void Update();
    bool Active() const { return active; }
    VideoCore::Backend::ImageBinding View(u32 icon, const VideoCore::ImageViewInfo& info);
    VideoCore::Backend::ImageBinding AtlasView(VideoCore::Image& source,
                                                const VideoCore::ImageViewInfo& info);

private:
    std::unique_ptr<VideoCore::Image> MakeImage(u32 width, u32 height, bool array);
    struct Atlas {
        std::unique_ptr<VideoCore::Image> image;
        u32 upload{}, generation{};
    };
    VideoCore::Backend::Device& device;
    VideoCore::Backend::Transfers& runtime;
    VideoCore::BufferCache& buffer_cache;
    // Views must die before their images (members are destroyed in reverse order).
    std::vector<std::array<std::unique_ptr<VideoCore::Image>, 2>> icons;
    std::unordered_map<u64, Atlas> atlases;
    Common::SlotVector<VideoCore::ImageView> views;
    std::vector<bool> drawn;
    std::vector<u32> small;
    u32 generation{};
    bool active{};
};

} // namespace Vulkan
