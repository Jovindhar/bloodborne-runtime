// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <memory>
#include <optional>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "video_core/renderer_backend/device.h"
#include "video_core/renderer_backend/transfers.h"
#include "video_core/renderer_vulkan/scene_resolution.h"
#include "video_core/texture_cache/image.h"

namespace VideoCore { class TextureCache; }
namespace Vulkan {

// Reduced raster targets, with native-size images retained for unmodified guest compute,
// integer texture loads, copies and CPU readbacks. All native access passes Transfers::Transit
// (its native access hook). Made and recorded through the render adapter's device.
class SceneTargets {
public:
    SceneTargets(VideoCore::Backend::Device&, VideoCore::Backend::Transfers&,
                 VideoCore::TextureCache&);
    using Lookup = std::function<VideoCore::Image*(VideoCore::ImageId, u64)>;
    SceneTargets(VideoCore::Backend::Device&, VideoCore::Backend::Transfers&, Lookup);
    ~SceneTargets();
    bool SetSize(SceneResolution::Size);
    SceneResolution::Size Size() const { return size; }
    bool Reduced() const { return size != SceneResolution::Size{}; }
    /// A reduced proxy can stand in for the image: a 1920x1080 scene target or, scaled by
    /// the same factor, a half-resolution (960x540) one (toggle SceneHalfRes).
    bool Eligible(const VideoCore::Image&) const;
    /// Eligible at the full scene size (1920x1080): the upscaler's inputs, the scene start.
    bool EligibleScene(const VideoCore::Image&) const;
    /// The proxy size of an eligible image: the scene size divided like the native size.
    SceneResolution::Size ProxySize(const VideoCore::Image&, u32 level = 0) const;
    /// A proxy's image and view (NativeHandles) and the state value it is bound with.
    struct Target {
        u64 image{};
        u64 view{};
        u32 layout{};

        [[nodiscard]] u64 NativeView() const noexcept {
            return view;
        }
        [[nodiscard]] u32 NativeLayout() const noexcept {
            return layout;
        }
    };
    Target Attachment(VideoCore::ImageId, const VideoCore::ImageViewInfo&);
    /// The scene-size proxy of the image (the upscaler's inputs), in `access` use.
    Target Read(VideoCore::ImageId, const VideoCore::ImageViewInfo&,
                VideoCore::Backend::ImageAccess access = VideoCore::Backend::ImageAccess::ComputeRead);
    /// A use of the native image (Transfers' native access hook): a proxy written since is
    /// copied back first; `writes`: the proxy is stale after it.
    void NativeAccess(VideoCore::Image&, bool writes);
    /// The proxy of `image` for sampling when it holds the current content (else nullopt: the
    /// native image is current). Transitions only after other use (attachment, copy).
    /// Mipmapped targets have one proxy per level: views of one level only.
    std::optional<Target> SampleProxy(const VideoCore::Image& image,
                                      const VideoCore::ImageViewInfo& info);
    /// Whether SampleProxy would return the proxy of `image` for these levels.
    [[nodiscard]] bool ProxyCurrent(const VideoCore::Image& image, u32 level = 0,
                                    u32 levels = 1) const;
    /// SampleProxy for a view obtained before (valid while Generation() is unchanged): the
    /// state value to sample it with.
    u32 PrepareSampleLayout(const VideoCore::Image& image, u32 level = 0);
    /// Changes when proxies and their views are destroyed (SetSize).
    [[nodiscard]] u64 Generation() const noexcept {
        return generation;
    }
    /// Whether NativeAccess has work for this image (a reduced-size proxy exists).
    [[nodiscard]] bool Tracks(u64 image_uid) const {
        return !copying && tracked.contains(image_uid);
    }
    void ResolveAll();
    bool debug = false; ///< BB_SCENE_DEBUG frame: print resolves and fills
private:
    struct Entry {
        VideoCore::ImageId source{};
        u64 uid = 0;
        u32 level = 0; ///< mip level of the native image this proxy stands for
        VideoCore::Backend::ImageAllocation image;
        VideoCore::Backend::ImageDesc desc;
        std::vector<std::pair<VideoCore::ImageViewInfo, VideoCore::Backend::ImageViewAllocation>>
            views;
        std::optional<VideoCore::Backend::ImageAccess> access; ///< since the last Transition
        SceneResolution::Coherence state;
    };
    static constexpr u64 Key(u64 uid, u32 level) {
        return uid << 4 | level;
    }
    Entry& Get(VideoCore::ImageId, u32 level = 0);
    u64 View(Entry&, const VideoCore::Image&, const VideoCore::ImageViewInfo&);
    void Copy(Entry&, VideoCore::Image&, bool to_native);
    // Depth/stencil formats without blit support (D32S8 on RADV) are resampled by a
    // fullscreen draw (Transfers::ResampleDepth).
    bool Blittable(VideoCore::Backend::Format) const;
    bool ShaderResampled(const VideoCore::Image&) const;
    void Transition(Entry&, VideoCore::Backend::ImageAspect, VideoCore::Backend::ImageAccess);
    void Destroy(Entry&);
    static void OnNativeAccess(void* self, VideoCore::Image& image, bool writes) {
        static_cast<SceneTargets*>(self)->NativeAccess(image, writes);
    }
    VideoCore::Backend::Device& device;
    VideoCore::Backend::Transfers& runtime;
    Lookup lookup;
    SceneResolution::Size size;
    std::unordered_map<u64, std::unique_ptr<Entry>> entries; ///< by Key(uid, level)
    std::unordered_set<u64> tracked; ///< uids with proxies
    bool copying = false;
    bool force_stencil_bits = false; ///< test the portable resampler on any driver
    u64 generation = 0;
    std::array<std::pair<u64, Entry*>, 8> recent{}; ///< last entries by Key
    u32 recent_next = 0;
};
} // namespace Vulkan
