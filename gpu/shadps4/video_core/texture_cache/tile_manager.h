// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <unordered_map>
#include <utility>
#include <boost/container_hash/hash.hpp>

#include "video_core/renderer_backend/transfers.h"
#include "common/types.h"
#include "video_core/amdgpu/tiling.h"
#include "video_core/buffer_cache/buffer.h"
#include "video_core/renderer_backend/host_pipeline.h"

namespace VideoCore {

struct ImageInfo;
struct Image;

class TileManager {
    struct TilingKey {
        AmdGpu::TileMode tile_mode;
        u32 num_bits;
        u32 num_samples;
        bool is_tiler;

        bool operator==(const TilingKey&) const = default;

        struct Hash {
            size_t operator()(const TilingKey& key) const {
                size_t hash = 0;
                boost::hash_combine(hash, key.tile_mode);
                boost::hash_combine(hash, key.num_bits);
                boost::hash_combine(hash, key.num_samples);
                boost::hash_combine(hash, key.is_tiler);
                return hash;
            }
        };
    };

public:
    explicit TileManager(Backend::Device& device, VideoCore::Backend::Transfers& runtime,
                         StreamBuffer& stream_buffer);
    ~TileManager();

    void TileImage(Image& in_image, std::span<Backend::BufferImageCopy> buffer_copies,
                   const VideoCore::Buffer* out_buffer, u64 out_offset);

    std::pair<const Buffer*, u64> DetileImage(const VideoCore::Buffer* in_buffer, u64 in_offset,
                                              const ImageInfo& info);

private:
    void RecordTilingDispatch(const Backend::OwnedPipeline& pipeline,
                              const Backend::BufferBinding& tiled,
                              const Backend::BufferBinding& linear,
                              const Backend::BufferBinding& params, u32 dim_x);

    const Backend::OwnedPipeline& GetTilingPipeline(const ImageInfo& info, bool is_tiler);

private:
    Backend::Device& device;
    VideoCore::Backend::Transfers& runtime;
    StreamBuffer& stream_buffer;
    std::unordered_map<TilingKey, Backend::OwnedPipeline, TilingKey::Hash> tiling_pipelines;
};

} // namespace VideoCore
