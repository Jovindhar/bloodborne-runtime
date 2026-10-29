// SPDX-License-Identifier: GPL-2.0-or-later

#include <boost/container/small_vector.hpp>

#include "video_core/renderer_backend/pipeline.h"
#include "video_core/renderer_vulkan/vk_backend_device.h"
#include "video_core/renderer_vulkan/vk_breadcrumbs.h"
#include "video_core/renderer_vulkan/vk_command_recorder.h"
#include "video_core/renderer_vulkan/vk_gpu_labels.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_state.h"

#include <cstring>
#include <memory>

namespace Vulkan {

namespace Backend = VideoCore::Backend;

static_assert(s32(vk::IndexType::eUint16) == s32(Backend::IndexType::UInt16) &&
              s32(vk::IndexType::eUint32) == s32(Backend::IndexType::UInt32));
static_assert(s32(vk::VertexInputRate::eVertex) == s32(Backend::VertexInputRate::Vertex) &&
              s32(vk::VertexInputRate::eInstance) == s32(Backend::VertexInputRate::Instance));

namespace {
vk::Buffer AsBuffer(u64 native) {
    return vk::Buffer{reinterpret_cast<VkBuffer>(native)};
}
vk::Pipeline AsPipeline(u64 native) {
    return vk::Pipeline{reinterpret_cast<VkPipeline>(native)};
}
} // namespace

// Each form captures only what it records: recorded commands live in the recording chunks, and
// larger captures fill them sooner (an 80-byte capture per draw raised recording from ~4.2 to
// ~5.8 us a draw).
namespace {
/// The stages and accesses of `uses`.
std::pair<vk::PipelineStageFlags2, vk::AccessFlags2> StagesAndAccess(Backend::MemoryUse uses) {
    using Use = Backend::MemoryUse;
    using Stage = vk::PipelineStageFlagBits2;
    using Access = vk::AccessFlagBits2;
    vk::PipelineStageFlags2 stages{};
    vk::AccessFlags2 access{};
    const auto add = [&](Use use, vk::PipelineStageFlags2 stage, vk::AccessFlags2 bits) {
        if (Backend::Has(uses, use)) {
            stages |= stage;
            access |= bits;
        }
    };
    add(Use::ComputeRead, Stage::eComputeShader, Access::eShaderRead);
    add(Use::ComputeWrite, Stage::eComputeShader, Access::eShaderWrite);
    add(Use::VertexRead, Stage::eVertexShader | Stage::eVertexInput,
        Access::eShaderRead | Access::eVertexAttributeRead | Access::eIndexRead);
    add(Use::VertexWrite, Stage::eVertexShader, Access::eShaderWrite);
    add(Use::FragmentRead, Stage::eFragmentShader, Access::eShaderRead);
    add(Use::FragmentWrite, Stage::eFragmentShader, Access::eShaderWrite);
    add(Use::IndirectRead, Stage::eDrawIndirect, Access::eIndirectCommandRead);
    add(Use::TransferRead, Stage::eAllTransfer, Access::eTransferRead);
    add(Use::TransferWrite, Stage::eAllTransfer, Access::eTransferWrite);
    add(Use::HostRead, Stage::eHost, Access::eHostRead);
    add(Use::HostWrite, Stage::eHost, Access::eHostWrite);
    add(Use::Any, Stage::eAllCommands, Access::eMemoryRead | Access::eMemoryWrite);
    return {stages ? stages : vk::PipelineStageFlags2{Stage::eNone}, access};
}
} // namespace

void CommandRecorder::TransitImage(const Backend::ImageAllocation& image, Backend::ImageAspect aspect,
                                   Backend::ImageAccess access, bool discard) {
    auto& state = static_cast<ImageState*>(image.backend_state)->state;
    const bool depth = Backend::Has(aspect, Backend::ImageAspect::Depth);
    const bool stencil = Backend::Has(aspect, Backend::ImageAspect::Stencil);
    // The renderer's own images are never read while they are targets: no feedback loops.
    const auto next = ImageAccessState(access, depth, stencil, false);
    const vk::ImageMemoryBarrier2 barrier{
        .srcStageMask = state.pl_stage,
        .srcAccessMask = state.access_mask,
        .dstStageMask = next.pl_stage,
        .dstAccessMask = next.access_mask,
        .oldLayout = discard ? vk::ImageLayout::eUndefined : state.layout,
        .newLayout = next.layout,
        .image = NativeImage(image.native),
        .subresourceRange = {ToVk(aspect), 0, 1, 0, 1},
    };
    state = next;
    scheduler.Record([barrier](vk::CommandBuffer cmdbuf) {
        cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});
    });
}

void CommandRecorder::ClearImage(const Backend::ImageAllocation& image,
                                 const Backend::ClearColor& value) {
    const auto layout = static_cast<ImageState*>(image.backend_state)->state.layout;
    vk::ClearColorValue clear{};
    std::memcpy(clear.uint32.data(), value.data(), sizeof(value));
    scheduler.Record([image = NativeImage(image.native), layout, clear](vk::CommandBuffer cmdbuf) {
        cmdbuf.clearColorImage(image, layout, clear,
                               vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
    });
}

void CommandRecorder::BlitImage(u64 src, u64 dst, const Backend::ImageBlit& blit) {
    const vk::ImageBlit region{
        .srcSubresource = {ToVk(blit.aspect), blit.src_level, 0, 1},
        .srcOffsets = std::array{vk::Offset3D{},
                                 vk::Offset3D{s32(blit.src_width), s32(blit.src_height), 1}},
        .dstSubresource = {ToVk(blit.aspect), blit.dst_level, 0, 1},
        .dstOffsets = std::array{vk::Offset3D{},
                                 vk::Offset3D{s32(blit.dst_width), s32(blit.dst_height), 1}},
    };
    scheduler.Record([src = NativeImage(src), dst = NativeImage(dst), region](vk::CommandBuffer cmd) {
        cmd.blitImage(src, vk::ImageLayout::eTransferSrcOptimal, dst,
                      vk::ImageLayout::eTransferDstOptimal, region, vk::Filter::eNearest);
    });
}

u32 CommandRecorder::StateOf(const Backend::ImageAllocation& image) const {
    return static_cast<u32>(static_cast<const ImageState*>(image.backend_state)->state.layout);
}

namespace {
vk::QueryPool NativeQueryPool(u64 pool) {
    return vk::QueryPool{reinterpret_cast<VkQueryPool>(pool)};
}
vk::Buffer NativeBuffer(u64 buffer) {
    return vk::Buffer{reinterpret_cast<VkBuffer>(buffer)};
}
} // namespace

void CommandRecorder::BeginQuery(u64 pool, u32 slot, bool precise) {
    scheduler.Record([pool = NativeQueryPool(pool), slot, precise](vk::CommandBuffer cmdbuf) {
        cmdbuf.beginQuery(pool, slot,
                          precise ? vk::QueryControlFlagBits::ePrecise : vk::QueryControlFlags{});
    });
}

void CommandRecorder::EndQuery(u64 pool, u32 slot) {
    scheduler.Record([pool = NativeQueryPool(pool), slot](vk::CommandBuffer cmdbuf) {
        cmdbuf.endQuery(pool, slot);
    });
}

void CommandRecorder::ResetQueries(u64 pool, u32 first, u32 count) {
    scheduler.Record([pool = NativeQueryPool(pool), first, count](vk::CommandBuffer cmdbuf) {
        cmdbuf.resetQueryPool(pool, first, count);
    });
}

void CommandRecorder::WriteTimestamp(u64 pool, u32 slot, bool end_of_pipe) {
    scheduler.Record([pool = NativeQueryPool(pool), slot, end_of_pipe](vk::CommandBuffer cmdbuf) {
        cmdbuf.writeTimestamp2(end_of_pipe ? vk::PipelineStageFlagBits2::eAllCommands
                                           : vk::PipelineStageFlagBits2::eNone,
                               pool, slot);
    });
}

void CommandRecorder::CopyQueryResults(u64 pool, u32 first, u32 count, u64 buffer, u64 offset) {
    scheduler.Record([pool = NativeQueryPool(pool), first, count, buffer = NativeBuffer(buffer),
                      offset](vk::CommandBuffer cmdbuf) {
        cmdbuf.copyQueryPoolResults(pool, first, count, buffer, offset, sizeof(u64),
                                    vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait);
    });
}

void CommandRecorder::FillBuffer(u64 buffer, u64 offset, u64 size, u32 value) {
    scheduler.Record([buffer = NativeBuffer(buffer), offset, size, value](vk::CommandBuffer cmdbuf) {
        cmdbuf.fillBuffer(buffer, offset, size, value);
    });
}

void CommandRecorder::Barrier(Backend::MemoryUse before, Backend::MemoryUse after) {
    const auto [src_stage, src_access] = StagesAndAccess(before);
    const auto [dst_stage, dst_access] = StagesAndAccess(after);
    scheduler.Record([barrier = vk::MemoryBarrier2{.srcStageMask = src_stage,
                                                   .srcAccessMask = src_access,
                                                   .dstStageMask = dst_stage,
                                                   .dstAccessMask = dst_access}](
                         vk::CommandBuffer cmdbuf) {
        cmdbuf.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &barrier});
    });
}

void CommandRecorder::BufferBarrier(u64 buffer, u64 offset, u64 size, Backend::MemoryUse before,
                                    Backend::MemoryUse after) {
    const auto [src_stage, src_access] = StagesAndAccess(before);
    const auto [dst_stage, dst_access] = StagesAndAccess(after);
    scheduler.Record([barrier = vk::BufferMemoryBarrier2{
                          .srcStageMask = src_stage,
                          .srcAccessMask = src_access,
                          .dstStageMask = dst_stage,
                          .dstAccessMask = dst_access,
                          .buffer = vk::Buffer{reinterpret_cast<VkBuffer>(buffer)},
                          .offset = offset,
                          .size = size,
                      }](vk::CommandBuffer cmdbuf) {
        cmdbuf.pipelineBarrier2({.dependencyFlags = vk::DependencyFlagBits::eByRegion,
                                 .bufferMemoryBarrierCount = 1,
                                 .pBufferMemoryBarriers = &barrier});
    });
}

Backend::DrawState& CommandRecorder::GetDrawState() const {
    return scheduler.GetDynamicState();
}

void CommandRecorder::CommitDrawState() const {
    const auto& instance = scheduler.GetInstance();
    scheduler.GetDynamicState().CommitWith(
        instance.IsDepthBoundsSupported(), instance.IsDynamicColorWriteMaskSupported(),
        instance.IsAttachmentFeedbackLoopLayoutSupported(),
        [this](auto&& command) { scheduler.Record(std::move(command)); });
}

void CommandRecorder::Submit() {
    SubmitInfo info{};
    scheduler.Flush(info);
}

void CommandRecorder::Finish() {
    scheduler.Finish();
}

void CommandRecorder::Kick(bool force) {
    scheduler.KickRecording(force);
}

void CommandRecorder::CollectCompleted() {
    scheduler.PopPendingOperations();
}

void CommandRecorder::RecordHostCopy(Common::UniqueFunction<void>&& copy) {
    scheduler.RecordHostCopy([copy = std::move(copy)] { copy(); });
}

void CommandRecorder::NoteHostCopySource(u64 address, u64 size) {
    scheduler.NoteHostCopySource(address, size);
}

bool CommandRecorder::HostCopiesPendingFor(u64 address, u64 size) {
    return scheduler.HostCopiesPendingFor(address, size);
}

void CommandRecorder::WaitHostCopies() {
    scheduler.WaitHostCopies();
}

void CommandRecorder::WaitHostCopiesFor(u64 address, u64 size) {
    scheduler.WaitHostCopiesFor(address, size);
}

void CommandRecorder::SignalAfterHostCopies(std::function<void()> signal) {
    scheduler.SignalAfterHostCopies(std::move(signal));
}

void CommandRecorder::WaitDeferredSignals() {
    scheduler.WaitDeferredSignals();
}

void CommandRecorder::BeginPass(const Backend::RenderState& state) {
    scheduler.BeginRendering(state);
}

void CommandRecorder::EndPass() {
    scheduler.EndRendering();
}

bool CommandRecorder::InPass() const {
    return scheduler.IsRendering();
}

bool CommandRecorder::DefersRecording() const {
    return scheduler.IsRecordingDeferred();
}

const Backend::RenderState& CommandRecorder::CurrentPass() const {
    return scheduler.GetRenderState();
}

void CommandRecorder::SetCarriedScope(Backend::CarriedScope* scope) {
    scheduler.SetCarriedScope(scope);
}

bool CommandRecorder::InPassWith(const Backend::RenderState& state) const {
    return scheduler.IsRenderingWith(state);
}

bool CommandRecorder::WillBeginPass(const Backend::RenderState& state) const {
    return scheduler.WillBeginRendering(state);
}

void CommandRecorder::ForceNewPass() {
    scheduler.ForceNewPass();
}

void CommandRecorder::SetPassLabel(std::string label) {
    scheduler.SetPassLabel(std::move(label));
}

void CommandRecorder::Draw(const Backend::DrawCommand& d, const Backend::Crumb& crumb) {
    const vk::Pipeline pipeline = AsPipeline(d.pipeline);
    if (!d.args_buffer) {
        scheduler.RecordCrumb(crumb, [pipeline, indexed = d.indexed, count = d.count,
                                      instances = d.instances, first = d.first,
                                      first_instance = d.first_instance](vk::CommandBuffer cmdbuf) {
            cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
            if (indexed) {
                cmdbuf.drawIndexed(count, instances, 0, s32(first), first_instance);
            } else {
                cmdbuf.draw(count, instances, first, first_instance);
            }
        });
        return;
    }
    scheduler.RecordCrumb(crumb, [pipeline, indexed = d.indexed, args = AsBuffer(d.args_buffer),
                                  args_offset = d.args_offset, counts = AsBuffer(d.count_buffer),
                                  counts_offset = d.count_offset, max_draws = d.max_draws,
                                  stride = d.stride](vk::CommandBuffer cmdbuf) {
        cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
        if (counts) {
            if (indexed) {
                cmdbuf.drawIndexedIndirectCount(args, args_offset, counts, counts_offset, max_draws,
                                                stride);
            } else {
                cmdbuf.drawIndirectCount(args, args_offset, counts, counts_offset, max_draws, stride);
            }
        } else if (indexed) {
            cmdbuf.drawIndexedIndirect(args, args_offset, max_draws, stride);
        } else {
            cmdbuf.drawIndirect(args, args_offset, max_draws, stride);
        }
    });
}

void CommandRecorder::Dispatch(const Backend::DispatchCommand& d, const Backend::Crumb& crumb) {
    const vk::Pipeline pipeline = AsPipeline(d.pipeline);
    if (!d.args_buffer) {
        scheduler.RecordCrumb(crumb, [pipeline, x = d.groups[0], y = d.groups[1],
                                      z = d.groups[2]](vk::CommandBuffer cmdbuf) {
            cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline);
            cmdbuf.dispatch(x, y, z);
        });
        return;
    }
    scheduler.RecordCrumb(crumb, [pipeline, args = AsBuffer(d.args_buffer),
                                  args_offset = d.args_offset,
                                  raw = AsBuffer(d.crumb_args_buffer),
                                  raw_offset = d.crumb_args_offset,
                                  slot = crumb.args_slot](vk::CommandBuffer cmdbuf) {
        Breadcrumbs::CopyArgs(cmdbuf, raw, raw_offset, slot); // what the game wrote
        cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline);
        cmdbuf.dispatchIndirect(args, args_offset);
    });
}

void CommandRecorder::BindIndexBuffer(u64 buffer, u64 offset, Backend::IndexType type) {
    scheduler.Record([handle = AsBuffer(buffer), offset, type](vk::CommandBuffer cmdbuf) {
        cmdbuf.bindIndexBuffer(handle, offset, static_cast<vk::IndexType>(type));
    });
}

void CommandRecorder::BindVertexBuffers(std::span<const u64> buffers, std::span<const u64> offsets,
                                        std::span<const u64> sizes, std::span<const u64> strides) {
    const u32 count = static_cast<u32>(buffers.size());
    // Handles and sizes are 64-bit like u64: read in place.
    const auto vk_buffers = scheduler.RecordData(
        std::span<const vk::Buffer>{reinterpret_cast<const vk::Buffer*>(buffers.data()), count});
    const auto vk_offsets = scheduler.RecordData(std::span<const vk::DeviceSize>{offsets});
    if (sizes.empty()) {
        scheduler.Record([count, vk_buffers, vk_offsets](vk::CommandBuffer cmdbuf) {
            cmdbuf.bindVertexBuffers(0, count, vk_buffers.data(), vk_offsets.data());
        });
        return;
    }
    const auto vk_sizes = scheduler.RecordData(std::span<const vk::DeviceSize>{sizes});
    const auto vk_strides = scheduler.RecordData(std::span<const vk::DeviceSize>{strides});
    scheduler.Record([count, vk_buffers, vk_offsets, vk_sizes, vk_strides](vk::CommandBuffer cmdbuf) {
        cmdbuf.bindVertexBuffers2(0, count, vk_buffers.data(), vk_offsets.data(), vk_sizes.data(),
                                  vk_strides.data());
    });
}

void CommandRecorder::SetVertexInput(std::span<const Backend::VertexBinding> bindings,
                                     std::span<const Backend::VertexAttribute> attributes) {
    // Converted straight into the recording chunk (as the descriptor writes).
    const size_t num_bindings = bindings.size();
    const size_t num_attributes = attributes.size();
    scheduler.ReserveRecordData(num_bindings * sizeof(vk::VertexInputBindingDescription2EXT) +
                                num_attributes * sizeof(vk::VertexInputAttributeDescription2EXT) +
                                64);
    auto* vk_bindings =
        scheduler.AllocateRecordData<vk::VertexInputBindingDescription2EXT>(num_bindings);
    for (size_t i = 0; i < num_bindings; ++i) {
        const auto& b = bindings[i];
        std::construct_at(&vk_bindings[i], vk::VertexInputBindingDescription2EXT{
                                               .binding = b.binding,
                                               .stride = b.stride,
                                               .inputRate = static_cast<vk::VertexInputRate>(b.input_rate),
                                               .divisor = b.divisor,
                                           });
    }
    auto* vk_attributes =
        scheduler.AllocateRecordData<vk::VertexInputAttributeDescription2EXT>(num_attributes);
    for (size_t i = 0; i < num_attributes; ++i) {
        const auto& a = attributes[i];
        std::construct_at(&vk_attributes[i], vk::VertexInputAttributeDescription2EXT{
                                                 .location = a.location,
                                                 .binding = a.binding,
                                                 .format = ToVk(a.format),
                                                 .offset = a.offset,
                                             });
    }
    scheduler.Record([b = std::span{vk_bindings, num_bindings},
                      a = std::span{vk_attributes, num_attributes}](vk::CommandBuffer cmdbuf) {
        cmdbuf.setVertexInputEXT(b, a);
    });
}

void CommandRecorder::WriteLabel(u64 buffer, u64 offset, u64 value, u32 num_bytes) {
    scheduler.Record([buffer = AsBuffer(buffer), offset, value, num_bytes,
                      marker = buffer_markers](vk::CommandBuffer cmdbuf) {
        if (!marker) {
            RecordPortableGpuLabel(cmdbuf, buffer, offset, value, num_bytes);
            return;
        }
        // High half first: whoever sees the new low half (the guest polls that) sees the new
        // high half too.
        if (num_bytes == 8) {
            cmdbuf.writeBufferMarkerAMD(vk::PipelineStageFlagBits::eBottomOfPipe, buffer,
                                        offset + 4, static_cast<u32>(value >> 32));
        }
        cmdbuf.writeBufferMarkerAMD(vk::PipelineStageFlagBits::eBottomOfPipe, buffer, offset,
                                    static_cast<u32>(value));
    });
}

void CommandRecorder::BeginLabel(const char* name) {
    scheduler.CommandBuffer().beginDebugUtilsLabelEXT(vk::DebugUtilsLabelEXT{.pLabelName = name});
}

void CommandRecorder::EndLabel() {
    scheduler.CommandBuffer().endDebugUtilsLabelEXT();
}

void CommandRecorder::InsertLabel(const char* name, u32 color) {
    vk::DebugUtilsLabelEXT label{.pLabelName = name};
    if (color != 0) {
        label.color = std::array<f32, 4>({f32((color >> 16) & 0xff) / 255.0f,
                                          f32((color >> 8) & 0xff) / 255.0f,
                                          f32(color & 0xff) / 255.0f,
                                          f32((color >> 24) & 0xff) / 255.0f});
    }
    scheduler.CommandBuffer().insertDebugUtilsLabelEXT(label);
}

void CommandRecorder::RecordPushConstants(vk::PipelineLayout layout, vk::ShaderStageFlags stages,
                                          std::span<const u8> data) {
    scheduler.Record([layout, stages, data](vk::CommandBuffer cmdbuf) {
        cmdbuf.pushConstants(layout, stages, 0u, u32(data.size()), data.data());
    });
}

void CommandRecorder::RecordPushDescriptors(vk::PipelineBindPoint bind_point,
                                            vk::PipelineLayout layout,
                                            std::span<const vk::WriteDescriptorSet> writes) {
    scheduler.Record([bind_point, layout, writes](vk::CommandBuffer cmdbuf) {
        cmdbuf.pushDescriptorSetKHR(bind_point, layout, 0, writes);
    });
}

void CommandRecorder::BindResources(const Backend::PipelineAllocation& pipeline,
                                    std::span<const Backend::SlotWrite> set_writes,
                                    std::span<const u8> push_constants,
                                    std::span<const Backend::ImageBinding> image_infos,
                                    std::span<const Backend::BufferBinding> buffer_infos) {
    const auto& record = *static_cast<const BackendDevice::PipelineRecord*>(pipeline.binding);
    const auto bind_point = record.bind_point;
    const vk::PipelineLayout layout = *record.layout;
    if (!push_constants.empty()) {
        RecordPushConstants(layout, record.push_stages, scheduler.RecordData(push_constants));
    }

    // Bind descriptor set.
    if (set_writes.empty()) {
        return;
    }
    // The adapter's bindings are laid out like Vulkan's descriptor infos (vk_state.h).
    const std::span<const vk::DescriptorImageInfo> vk_images{
        reinterpret_cast<const vk::DescriptorImageInfo*>(image_infos.data()), image_infos.size()};
    const std::span<const vk::DescriptorBufferInfo> vk_buffers{
        reinterpret_cast<const vk::DescriptorBufferInfo*>(buffer_infos.data()),
        buffer_infos.size()};

    if (record.push_descriptors && scheduler.IsRecordingDeferred()) {
        // Writes and the infos they point to are laid out in the recording chunk, with the
        // pointers already aimed at those copies: the command only carries a span. The writes
        // are converted straight into the chunk.
        // bbport: when every write points into the caller's info arrays (draws and
        // dispatches), each array is copied once and the pointers are rebased: a copy per
        // descriptor (two per write) cost the draw recording thread ~3% of its time.
        const auto within = [](const auto* p, u32 count, const auto& array) {
            return p >= array.data() && p + count <= array.data() + array.size();
        };
        bool bulk = !image_infos.empty() || !buffer_infos.empty();
        for (const auto& write : set_writes) {
            if (!bulk) {
                break;
            }
            bulk = (!write.images || within(write.images, write.count, image_infos)) &&
                   (!write.buffers || within(write.buffers, write.count, buffer_infos));
        }
        const size_t count = set_writes.size();
        if (bulk) {
            scheduler.ReserveRecordData(count * sizeof(vk::WriteDescriptorSet) +
                                        vk_images.size_bytes() + vk_buffers.size_bytes() + 256);
            const auto images = scheduler.RecordData(vk_images);
            const auto buffers = scheduler.RecordData(vk_buffers);
            auto* writes = scheduler.AllocateRecordData<vk::WriteDescriptorSet>(count);
            for (size_t i = 0; i < count; ++i) {
                const auto& write = set_writes[i];
                std::construct_at(&writes[i], ToVk(write));
                if (write.images) {
                    writes[i].pImageInfo = images.data() + (write.images - image_infos.data());
                }
                if (write.buffers) {
                    writes[i].pBufferInfo = buffers.data() + (write.buffers - buffer_infos.data());
                }
            }
            RecordPushDescriptors(bind_point, layout, std::span{writes, count});
            return;
        }
        size_t bytes = count * sizeof(vk::WriteDescriptorSet) + 64;
        for (const auto& write : set_writes) {
            bytes += write.count * (sizeof(vk::DescriptorBufferInfo) + sizeof(vk::DescriptorImageInfo)) +
                     32;
        }
        scheduler.ReserveRecordData(bytes);
        auto* writes = scheduler.AllocateRecordData<vk::WriteDescriptorSet>(count);
        for (size_t i = 0; i < count; ++i) {
            std::construct_at(&writes[i], ToVk(set_writes[i]));
            auto& write = writes[i];
            if (write.pBufferInfo) {
                write.pBufferInfo =
                    scheduler.RecordData(std::span{write.pBufferInfo, write.descriptorCount}).data();
            }
            if (write.pImageInfo) {
                write.pImageInfo =
                    scheduler.RecordData(std::span{write.pImageInfo, write.descriptorCount}).data();
            }
        }
        RecordPushDescriptors(bind_point, layout, std::span{writes, count});
        return;
    }

    boost::container::small_vector<vk::WriteDescriptorSet, 64> vk_writes;
    vk_writes.reserve(set_writes.size());
    for (const auto& write : set_writes) {
        vk_writes.push_back(ToVk(write));
    }
    if (record.push_descriptors) {
        // Recorded at once (not deferred): the writes may stay where they are.
        RecordPushDescriptors(bind_point, layout, vk_writes);
        return;
    }

    // Descriptor set updates are device calls: they stay on this thread.
    const auto desc_set = descriptor_heap.Commit(*record.set_layout);
    for (auto& set_write : vk_writes) {
        set_write.dstSet = desc_set;
    }
    device.updateDescriptorSets(vk_writes, {});
    scheduler.Record([bind_point, layout, desc_set](vk::CommandBuffer cmdbuf) {
        cmdbuf.bindDescriptorSets(bind_point, layout, 0, desc_set, {});
    });
}

} // namespace Vulkan
