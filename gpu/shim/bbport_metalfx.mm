// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: MetalFX temporal upscaler, Metal side (bbport_metalfx.h). Built with ARC on APPLE.
// MetalFX is opened at run time (dlopen) and its classes looked up by name: nothing links
// against it, so a build or a Rosetta process without an x86-64 MetalFX still runs (unsupported).

#include "bbport_metalfx.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>

#include <cstdio>
#include <dlfcn.h>
#include <mutex>

namespace BbMetalFx {

namespace {

Class DescriptorClass() {
    static Class cls = []() -> Class {
        // In the dyld shared cache; dlopen by path still works (macOS 11+).
        dlopen("/System/Library/Frameworks/MetalFX.framework/MetalFX", RTLD_LAZY | RTLD_LOCAL);
        return NSClassFromString(@"MTLFXTemporalScalerDescriptor");
    }();
    return cls;
}

std::string Describe(NSError* error) {
    const char* text = error ? error.localizedDescription.UTF8String : nullptr;
    return text ? text : "unknown error";
}

} // namespace

bool Supported(void* heap_handle, std::string& reason) {
    @autoreleasepool {
        Class cls = DescriptorClass();
        if (!cls) {
            reason = "MetalFX.framework did not load in this process (x86-64 under Rosetta?)";
            return false;
        }
        id<MTLHeap> heap = (__bridge id<MTLHeap>)heap_handle;
        id<MTLDevice> device = heap.device;
        if (!device) {
            reason = "the exported MTLHeap has no MTLDevice";
            return false;
        }
        if (![cls supportsDevice:device]) {
            reason = std::string{"MetalFX temporal scaling does not support "} +
                     device.name.UTF8String;
            return false;
        }
        if (heap.type != MTLHeapTypePlacement) {
            reason = "the exported MTLHeap is not a placement heap";
            return false;
        }
        return true;
    }
}

bool SupportedDevice(void* device_handle, std::string& reason) {
    @autoreleasepool {
        Class cls = DescriptorClass();
        if (!cls) {
            reason = "MetalFX.framework did not load in this process";
            return false;
        }
        id<MTLDevice> device = (__bridge id<MTLDevice>)device_handle;
        if (!device || ![cls supportsDevice:device]) {
            reason = "MetalFX temporal scaling does not support this Metal device";
            return false;
        }
        return true;
    }
}

struct Scaler::Impl {
    Config config{};
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    id<MTLFXTemporalScaler> scaler;
    // Linear views of the Vulkan buffers (offset 0 of each heap).
    id<MTLBuffer> color_buffer, depth_buffer, motion_buffer, output_buffer;
    // MetalFX's own textures: it requires a private output, and these have the usage it asks for.
    id<MTLTexture> color, depth, motion, output;
    id<MTLTexture> native_depth;
    id<MTLRenderPipelineState> depth_copy_pipeline;
    id<MTLDepthStencilState> depth_copy_state;
    id<MTLSamplerState> depth_copy_sampler;
    NSUInteger color_bytes = 8;
    // An asynchronous frame's failure, reported by the next Encode's caller (TakeError). Shared
    // with the completion handlers, which may run after the scaler is gone.
    struct Errors {
        std::mutex mutex;
        std::string text;
    };
    std::shared_ptr<Errors> errors = std::make_shared<Errors>();
    // The last command buffer: waited for before the scaler goes (its handler uses this state).
    id<MTLCommandBuffer> last;
};

Scaler::Scaler(std::unique_ptr<Impl> impl_) : impl{std::move(impl_)} {}

Scaler::~Scaler() {
    if (impl && impl->last) {
        [impl->last waitUntilCompleted];
    }
}

std::unique_ptr<Scaler> Scaler::Create(const Config& config, const Heaps& heaps,
                                       std::string& error) {
    @autoreleasepool {
        Class cls = DescriptorClass();
        if (!cls) {
            error = "MetalFX.framework did not load";
            return nullptr;
        }
        auto impl = std::make_unique<Impl>();
        impl->config = config;
        id<MTLHeap> color_heap = (__bridge id<MTLHeap>)heaps.color;
        impl->device = color_heap.device;
        id<MTLDevice> device = impl->device;
        const float scale = float(config.out_width) / float(config.in_width);
        // macOS 14+ reports the device's range; outside it the scaler would not be created.
        if ([cls respondsToSelector:@selector(supportedInputContentMinScaleForDevice:)]) {
            const float min_scale = [cls supportedInputContentMinScaleForDevice:device];
            const float max_scale = [cls supportedInputContentMaxScaleForDevice:device];
            if (scale < min_scale - 1e-3f || scale > max_scale + 1e-3f) {
                char text[160];
                std::snprintf(text, sizeof(text),
                              "scale %.2f outside the device's MetalFX range %.2f..%.2f", scale,
                              min_scale, max_scale);
                error = text;
                return nullptr;
            }
        }
        const MTLPixelFormat color_format = config.color == ColorFormat::Rgba16Float
                                                ? MTLPixelFormatRGBA16Float
                                                : MTLPixelFormatRGBA8Unorm;
        impl->color_bytes = config.color == ColorFormat::Rgba16Float ? 8 : 4;
        MTLFXTemporalScalerDescriptor* descriptor = [[cls alloc] init];
        descriptor.inputWidth = config.in_width;
        descriptor.inputHeight = config.in_height;
        descriptor.outputWidth = config.out_width;
        descriptor.outputHeight = config.out_height;
        descriptor.colorTextureFormat = color_format;
        descriptor.depthTextureFormat = MTLPixelFormatDepth32Float;
        descriptor.motionTextureFormat = MTLPixelFormatRG16Float;
        descriptor.outputTextureFormat = MTLPixelFormatRGBA16Float;
        descriptor.autoExposureEnabled = config.hdr;
        impl->scaler = [descriptor newTemporalScalerWithDevice:device];
        if (!impl->scaler) {
            error = "newTemporalScalerWithDevice failed";
            return nullptr;
        }
        const auto texture = [&](MTLPixelFormat format, uint32_t w, uint32_t h,
                                 MTLTextureUsage usage) -> id<MTLTexture> {
            MTLTextureDescriptor* td =
                [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                                   width:w
                                                                  height:h
                                                               mipmapped:NO];
            td.storageMode = MTLStorageModePrivate;
            td.usage = usage;
            return [device newTextureWithDescriptor:td];
        };
        const uint32_t w = config.in_width, h = config.in_height;
        impl->color = texture(color_format, w, h, impl->scaler.colorTextureUsage);
        impl->depth = texture(MTLPixelFormatDepth32Float, w, h, impl->scaler.depthTextureUsage);
        impl->motion = texture(MTLPixelFormatRG16Float, w, h, impl->scaler.motionTextureUsage);
        impl->output = texture(MTLPixelFormatRGBA16Float, config.out_width, config.out_height,
                               impl->scaler.outputTextureUsage);
        if (!impl->color || !impl->depth || !impl->motion || !impl->output) {
            error = "MetalFX texture creation failed";
            return nullptr;
        }
        const auto buffer = [&](void* handle, NSUInteger length) -> id<MTLBuffer> {
            id<MTLHeap> heap = (__bridge id<MTLHeap>)handle;
            if (heap.device != device || heap.type != MTLHeapTypePlacement || heap.size < length) {
                return nil;
            }
            // Same storage, cache and hazard tracking modes as the heap (required).
            return [heap newBufferWithLength:length options:heap.resourceOptions offset:0];
        };
        impl->color_buffer = buffer(heaps.color, NSUInteger(w) * h * impl->color_bytes);
        impl->depth_buffer = buffer(heaps.depth, NSUInteger(w) * h * 4);
        impl->motion_buffer = buffer(heaps.motion, NSUInteger(w) * h * 4);
        impl->output_buffer =
            buffer(heaps.output, NSUInteger(config.out_width) * config.out_height * 8);
        if (!impl->color_buffer || !impl->depth_buffer || !impl->motion_buffer ||
            !impl->output_buffer) {
            error = "cannot alias the exported MTLHeaps with MTLBuffers";
            return nullptr;
        }
        impl->queue = [device newCommandQueue];
        if (!impl->queue) {
            error = "newCommandQueue failed";
            return nullptr;
        }
        impl->queue.label = @"bbport MetalFX";
        impl->scaler.colorTexture = impl->color;
        impl->scaler.depthTexture = impl->depth;
        impl->scaler.motionTexture = impl->motion;
        impl->scaler.outputTexture = impl->output;
        return std::unique_ptr<Scaler>(new Scaler(std::move(impl)));
    }
}

std::unique_ptr<Scaler> Scaler::CreateNative(const Config& config, void* device_handle,
                                             std::string& error) {
    @autoreleasepool {
        if (!SupportedDevice(device_handle, error)) {
            return nullptr;
        }
        auto impl = std::make_unique<Impl>();
        impl->config = config;
        impl->device = (__bridge id<MTLDevice>)device_handle;
        id<MTLDevice> device = impl->device;
        Class cls = DescriptorClass();
        const float scale = float(config.out_width) / float(config.in_width);
        if ([cls respondsToSelector:@selector(supportedInputContentMinScaleForDevice:)]) {
            const float min_scale = [cls supportedInputContentMinScaleForDevice:device];
            const float max_scale = [cls supportedInputContentMaxScaleForDevice:device];
            if (scale < min_scale - 1e-3f || scale > max_scale + 1e-3f) {
                char text[160];
                std::snprintf(text, sizeof(text),
                              "scale %.2f outside the device's MetalFX range %.2f..%.2f", scale,
                              min_scale, max_scale);
                error = text;
                return nullptr;
            }
        }
        MTLFXTemporalScalerDescriptor* descriptor = [[cls alloc] init];
        descriptor.inputWidth = config.in_width;
        descriptor.inputHeight = config.in_height;
        descriptor.outputWidth = config.out_width;
        descriptor.outputHeight = config.out_height;
        descriptor.colorTextureFormat = config.color == ColorFormat::Rgba16Float
                                            ? MTLPixelFormatRGBA16Float : MTLPixelFormatRGBA8Unorm;
        descriptor.depthTextureFormat = MTLPixelFormatDepth32Float;
        descriptor.motionTextureFormat = MTLPixelFormatRG16Float;
        descriptor.outputTextureFormat = MTLPixelFormatRGBA16Float;
        descriptor.autoExposureEnabled = config.hdr;
        impl->scaler = [descriptor newTemporalScalerWithDevice:device];
        if (!impl->scaler) {
            error = "newTemporalScalerWithDevice failed";
            return nullptr;
        }
        // The game's depth source is often Depth32Float_Stencil8. Metal cannot make a
        // Depth32Float view of it, while MetalFX requires Depth32Float. Copy its depth aspect
        // through a depth-only render pass on the same command buffer.
        MTLTextureDescriptor* td =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                               width:config.in_width
                                                              height:config.in_height
                                                           mipmapped:NO];
        td.storageMode = MTLStorageModePrivate;
        td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        impl->native_depth = [device newTextureWithDescriptor:td];
        NSString* source =
            @"#include <metal_stdlib>\n"
             "using namespace metal;\n"
             "struct V { float4 pos [[position]]; float2 uv; };\n"
             "vertex V depth_copy_vs(uint id [[vertex_id]]) {\n"
             "  float2 p = float2((id << 1) & 2, id & 2);\n"
             "  V v; v.pos = float4(p * 2.0 - 1.0, 0.0, 1.0);\n"
             "  v.uv = float2(p.x, 1.0 - p.y); return v; }\n"
             "struct D { float depth [[depth(any)]]; };\n"
             "fragment D depth_copy_fs(V in [[stage_in]], depth2d<float> tex [[texture(0)]],\n"
             "                         sampler s [[sampler(0)]]) {\n"
             "  D d; d.depth = tex.sample(s, in.uv); return d; }\n";
        NSError* ns_error = nil;
        id<MTLLibrary> lib = [device newLibraryWithSource:source options:nil error:&ns_error];
        MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
        pd.vertexFunction = [lib newFunctionWithName:@"depth_copy_vs"];
        pd.fragmentFunction = [lib newFunctionWithName:@"depth_copy_fs"];
        pd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        impl->depth_copy_pipeline = [device newRenderPipelineStateWithDescriptor:pd error:&ns_error];
        MTLDepthStencilDescriptor* dd = [MTLDepthStencilDescriptor new];
        dd.depthCompareFunction = MTLCompareFunctionAlways;
        dd.depthWriteEnabled = YES;
        impl->depth_copy_state = [device newDepthStencilStateWithDescriptor:dd];
        MTLSamplerDescriptor* sd = [MTLSamplerDescriptor new];
        sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterNearest;
        sd.sAddressMode = sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
        impl->depth_copy_sampler = [device newSamplerStateWithDescriptor:sd];
        if (!impl->native_depth || !impl->depth_copy_pipeline || !impl->depth_copy_state ||
            !impl->depth_copy_sampler) {
            error = ns_error ? Describe(ns_error) : "MetalFX depth conversion setup failed";
            return nullptr;
        }
        return std::unique_ptr<Scaler>(new Scaler(std::move(impl)));
    }
}

bool Scaler::EncodeNative(const Frame& frame, void* command_buffer,
                          const Textures& textures, std::string& error) {
    @autoreleasepool {
        id<MTLCommandBuffer> cmd = (__bridge id<MTLCommandBuffer>)command_buffer;
        id<MTLTexture> color = (__bridge id<MTLTexture>)textures.color;
        id<MTLTexture> depth = (__bridge id<MTLTexture>)textures.depth;
        id<MTLTexture> motion = (__bridge id<MTLTexture>)textures.motion;
        id<MTLTexture> output = (__bridge id<MTLTexture>)textures.output;
        if (!cmd || !color || !depth || !motion || !output) {
            error = "a Metal frame texture or command buffer is missing";
            return false;
        }
        if (depth.pixelFormat == MTLPixelFormatDepth32Float_Stencil8) {
            MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
            rp.depthAttachment.texture = impl->native_depth;
            rp.depthAttachment.loadAction = MTLLoadActionDontCare;
            rp.depthAttachment.storeAction = MTLStoreActionStore;
            id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rp];
            [enc setRenderPipelineState:impl->depth_copy_pipeline];
            [enc setDepthStencilState:impl->depth_copy_state];
            [enc setFragmentTexture:depth atIndex:0];
            [enc setFragmentSamplerState:impl->depth_copy_sampler atIndex:0];
            [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            [enc endEncoding];
            depth = impl->native_depth;
        }
        if (depth.pixelFormat != MTLPixelFormatDepth32Float) {
            error = "the depth texture has no MetalFX-compatible depth view";
            return false;
        }
        id<MTLFXTemporalScaler> scaler = impl->scaler;
        scaler.colorTexture = color;
        scaler.depthTexture = depth;
        scaler.motionTexture = motion;
        scaler.outputTexture = output;
        scaler.inputContentWidth = impl->config.in_width;
        scaler.inputContentHeight = impl->config.in_height;
        scaler.jitterOffsetX = frame.jitter_x;
        scaler.jitterOffsetY = frame.jitter_y;
        scaler.motionVectorScaleX = frame.motion_scale_x;
        scaler.motionVectorScaleY = frame.motion_scale_y;
        scaler.preExposure = 1.0f;
        scaler.reset = frame.reset;
        scaler.depthReversed = frame.depth_reversed;
        [scaler encodeToCommandBuffer:cmd];
        return true;
    }
}

bool Scaler::Encode(const Frame& frame, std::string& error) {
    @autoreleasepool {
        Impl& m = *impl;
        const NSUInteger w = m.config.in_width, h = m.config.in_height;
        const NSUInteger ow = m.config.out_width, oh = m.config.out_height;
        id<MTLCommandBuffer> cmd = [m.queue commandBuffer];
        if (!cmd) {
            error = "commandBuffer failed";
            return false;
        }
        const bool async = frame.wait_event && frame.signal_event;
        id<MTLSharedEvent> wait_event = (__bridge id<MTLSharedEvent>)frame.wait_event;
        id<MTLSharedEvent> signal_event = (__bridge id<MTLSharedEvent>)frame.signal_event;
        if (async) {
            // The Vulkan copies of this frame's inputs (their submission's timeline value).
            [cmd encodeWaitForEvent:wait_event value:frame.wait_value];
        }
        const MTLOrigin zero = MTLOriginMake(0, 0, 0);
        id<MTLBlitCommandEncoder> uploads = [cmd blitCommandEncoder];
        const auto upload = [&](id<MTLBuffer> source, id<MTLTexture> target, NSUInteger bytes) {
            [uploads copyFromBuffer:source
                       sourceOffset:0
                  sourceBytesPerRow:w * bytes
                sourceBytesPerImage:w * h * bytes
                         sourceSize:MTLSizeMake(w, h, 1)
                          toTexture:target
                   destinationSlice:0
                   destinationLevel:0
                  destinationOrigin:zero];
        };
        upload(m.color_buffer, m.color, m.color_bytes);
        upload(m.depth_buffer, m.depth, 4);
        upload(m.motion_buffer, m.motion, 4);
        [uploads endEncoding];

        id<MTLFXTemporalScaler> scaler = m.scaler;
        scaler.inputContentWidth = w;
        scaler.inputContentHeight = h;
        scaler.jitterOffsetX = frame.jitter_x;
        scaler.jitterOffsetY = frame.jitter_y;
        scaler.motionVectorScaleX = frame.motion_scale_x;
        scaler.motionVectorScaleY = frame.motion_scale_y;
        scaler.preExposure = 1.0f;
        scaler.reset = frame.reset;
        scaler.depthReversed = frame.depth_reversed;
        [scaler encodeToCommandBuffer:cmd];

        id<MTLBlitCommandEncoder> downloads = [cmd blitCommandEncoder];
        [downloads copyFromTexture:m.output
                         sourceSlice:0
                         sourceLevel:0
                        sourceOrigin:zero
                          sourceSize:MTLSizeMake(ow, oh, 1)
                            toBuffer:m.output_buffer
                   destinationOffset:0
              destinationBytesPerRow:ow * 8
            destinationBytesPerImage:ow * oh * 8];
        [downloads endEncoding];
        if (async) {
            // Vulkan's next submission waits for this value on the GPU. A failed command buffer
            // may not reach the signal: the handler sets it, so Vulkan never waits forever.
            const uint64_t value = frame.signal_value;
            [cmd encodeSignalEvent:signal_event value:value];
            std::shared_ptr<Impl::Errors> errors = m.errors;
            [cmd addCompletedHandler:^(id<MTLCommandBuffer> done) {
              if (done.status != MTLCommandBufferStatusCompleted) {
                  if (signal_event.signaledValue < value) {
                      signal_event.signaledValue = value;
                  }
                  std::scoped_lock lock{errors->mutex};
                  errors->text = "command buffer failed: " + Describe(done.error);
              }
            }];
            m.last = cmd;
            [cmd commit];
            return true;
        }
        [cmd commit];
        [cmd waitUntilCompleted];
        if (cmd.status != MTLCommandBufferStatusCompleted) {
            error = "command buffer failed: " + Describe(cmd.error);
            return false;
        }
        return true;
    }
}

bool Scaler::TakeError(std::string& error) {
    std::scoped_lock lock{impl->errors->mutex};
    if (impl->errors->text.empty()) {
        return false;
    }
    error = std::move(impl->errors->text);
    impl->errors->text.clear();
    return true;
}

} // namespace BbMetalFx
