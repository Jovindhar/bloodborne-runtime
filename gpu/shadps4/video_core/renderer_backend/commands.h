// The render adapter's draw and dispatch commands (docs/RENDER_ADAPTER.md, A4).
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>

#include "common/types.h"
#include "video_core/renderer_backend/binding.h"
#include "video_core/renderer_backend/format.h"

namespace VideoCore::Backend {

struct PipelineAllocation;

/// What a command was, for naming the command a GPU hang is stuck in (breadcrumbs).
enum class CrumbKind : u8 { Pass, Draw, DrawIndexed, DrawIndirect, Dispatch, DispatchIndirect };

struct Crumb {
    CrumbKind kind = CrumbKind::Pass;
    const char* name = nullptr; ///< our passes: a string literal
    u64 hash[2]{};              ///< guest shaders: vertex and pixel, or compute
    u64 program[2]{};           ///< their code in guest memory
    u64 address = 0;            ///< indirect arguments
    u32 count[3]{};             ///< indices or vertices and instances, or groups
    u32 args_slot = ~0u;        ///< indirect dispatches: where the GPU copies its arguments
};

/// Values as Vulkan's (checked in renderer_vulkan/vk_state.h).
enum class IndexType : s32 { UInt16 = 0, UInt32 = 1 };
enum class VertexInputRate : s32 { Vertex = 0, Instance = 1 };

/// A vertex buffer slot of the vertex input state: its stride, and whether it advances per vertex
/// or per instance (every `divisor` instances).
struct VertexBinding {
    u32 binding{};
    u32 stride{};
    VertexInputRate input_rate{VertexInputRate::Vertex};
    u32 divisor{1};
};

/// A vertex attribute: its shader location, the slot it reads and its format.
struct VertexAttribute {
    u32 location{};
    u32 binding{};
    Format format{Format::Undefined};
    u32 offset{};
};

/// A draw with its pipeline (the backend's object, NativeHandle). Direct when `args_buffer` is
/// zero; otherwise its arguments are read from `args_buffer`, and their number from
/// `count_buffer` (at most `max_draws`) when that is not zero.
struct DrawCommand {
    u64 pipeline{};
    bool indexed{};
    u32 count{};     ///< indices or vertices
    u32 instances{};
    u32 first{};     ///< first vertex (vertex offset when indexed)
    u32 first_instance{};
    u64 args_buffer{}, args_offset{};
    u64 count_buffer{}, count_offset{};
    u32 max_draws{}, stride{};
};

/// A dispatch with its pipeline. Indirect when `args_buffer` is not zero; `crumb_args_buffer`
/// and `crumb_args_offset` are the arguments as the game wrote them, copied for breadcrumbs.
struct DispatchCommand {
    u64 pipeline{};
    u32 groups[3]{};
    u64 args_buffer{}, args_offset{};
    u64 crumb_args_buffer{}, crumb_args_offset{};
};

/// Records commands into the backend's current submission, in order.
class CommandRecorder {
public:
    virtual ~CommandRecorder() = default;

    virtual void Draw(const DrawCommand& draw, const Crumb& crumb) = 0;
    virtual void Dispatch(const DispatchCommand& dispatch, const Crumb& crumb) = 0;
    virtual void BindIndexBuffer(u64 buffer, u64 offset, IndexType type) = 0;
    /// Binds `buffers` from slot 0. `sizes` and `strides` empty: the pipeline's strides.
    virtual void BindVertexBuffers(std::span<const u64> buffers, std::span<const u64> offsets,
                                   std::span<const u64> sizes, std::span<const u64> strides) = 0;

    /// The resources of the next draws or dispatches with `pipeline`: `writes` into its slots
    /// and `push_constants`. Writes may point into `images` and `buffers`, which are then copied
    /// whole instead of element by element.
    virtual void BindResources(const PipelineAllocation& pipeline, std::span<const SlotWrite> writes,
                               std::span<const u8> push_constants,
                               std::span<const ImageBinding> images = {},
                               std::span<const BufferBinding> buffers = {}) = 0;

    /// The vertex input state of the next draws (pipelines built with dynamic vertex input).
    virtual void SetVertexInput(std::span<const VertexBinding> bindings,
                                std::span<const VertexAttribute> attributes) = 0;

    /// Writes `value` (`num_bytes`: 4 or 8) to the buffer once every earlier command has
    /// finished; a 64-bit value as two 32-bit writes, the high half first.
    virtual void WriteLabel(u64 buffer, u64 offset, u64 value, u32 num_bytes) = 0;

    /// Debug labels for capture tools (`color`: 0xAARRGGBB, 0 for none).
    virtual void BeginLabel(const char* name) = 0;
    virtual void EndLabel() = 0;
    virtual void InsertLabel(const char* name, u32 color) = 0;
};

} // namespace VideoCore::Backend
