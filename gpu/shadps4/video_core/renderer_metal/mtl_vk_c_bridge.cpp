// SPDX-License-Identifier: GPL-2.0-or-later
// Vulkan C entry points retained by optional Vulkan upscalers in the macOS build.
// Resolve them through the native Metal adapter or the optional fallback loader.
// Neither path links the loader, and native Metal never opens it.
#include <vulkan/vulkan_core.h>
#include <dlfcn.h>
#include <cstdlib>
#include <type_traits>
#include "video_core/renderer_metal/mtl_vk_layer.h"

namespace {
template <typename R> R Unavailable() {
    if constexpr (std::is_same_v<R, VkResult>) return VK_ERROR_FEATURE_NOT_PRESENT;
    else return R{};
}
template <typename F> F Resolve(const char* name) {
    if (MetalLayer::NativeBoot()) {
        const auto get_proc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(MetalLayer::NativeInstanceProcAddr());
        return reinterpret_cast<F>(get_proc(VK_NULL_HANDLE, name));
    }
    static void* loader = [] {
        const char* path = std::getenv("BB_VULKAN_LOADER");
        return path ? dlopen(path, RTLD_NOW | RTLD_LOCAL) : nullptr;
    }();
    return loader ? reinterpret_cast<F>(dlsym(loader, name)) : nullptr;
}
} // namespace

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkAllocateDescriptorSets(VkDevice                                    device,
    const VkDescriptorSetAllocateInfo*          pAllocateInfo,
    VkDescriptorSet*                            pDescriptorSets) {
    const auto proc = Resolve<PFN_vkAllocateDescriptorSets>("vkAllocateDescriptorSets");
    if (!proc) {
        return Unavailable<VkResult>();
    }
    return proc(device, pAllocateInfo, pDescriptorSets);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(VkDevice                                    device,
    const VkMemoryAllocateInfo*                 pAllocateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDeviceMemory*                             pMemory) {
    const auto proc = Resolve<PFN_vkAllocateMemory>("vkAllocateMemory");
    if (!proc) {
        return Unavailable<VkResult>();
    }
    return proc(device, pAllocateInfo, pAllocator, pMemory);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(VkDevice                                    device,
    VkBuffer                                    buffer,
    VkDeviceMemory                              memory,
    VkDeviceSize                                memoryOffset) {
    const auto proc = Resolve<PFN_vkBindBufferMemory>("vkBindBufferMemory");
    if (!proc) {
        return Unavailable<VkResult>();
    }
    return proc(device, buffer, memory, memoryOffset);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(VkDevice                                    device,
    VkImage                                     image,
    VkDeviceMemory                              memory,
    VkDeviceSize                                memoryOffset) {
    const auto proc = Resolve<PFN_vkBindImageMemory>("vkBindImageMemory");
    if (!proc) {
        return Unavailable<VkResult>();
    }
    return proc(device, image, memory, memoryOffset);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets(VkCommandBuffer                             commandBuffer,
    VkPipelineBindPoint                         pipelineBindPoint,
    VkPipelineLayout                            layout,
    uint32_t                                    firstSet,
    uint32_t                                    descriptorSetCount,
    const VkDescriptorSet*                      pDescriptorSets,
    uint32_t                                    dynamicOffsetCount,
    const uint32_t*                             pDynamicOffsets) {
    const auto proc = Resolve<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets");
    if (!proc) {
        return;
    }
    proc(commandBuffer, pipelineBindPoint, layout, firstSet, descriptorSetCount, pDescriptorSets, dynamicOffsetCount, pDynamicOffsets);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkCmdBindPipeline(VkCommandBuffer                             commandBuffer,
    VkPipelineBindPoint                         pipelineBindPoint,
    VkPipeline                                  pipeline) {
    const auto proc = Resolve<PFN_vkCmdBindPipeline>("vkCmdBindPipeline");
    if (!proc) {
        return;
    }
    proc(commandBuffer, pipelineBindPoint, pipeline);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkCmdClearColorImage(VkCommandBuffer                             commandBuffer,
    VkImage                                     image,
    VkImageLayout                               imageLayout,
    const VkClearColorValue*                    pColor,
    uint32_t                                    rangeCount,
    const VkImageSubresourceRange*              pRanges) {
    const auto proc = Resolve<PFN_vkCmdClearColorImage>("vkCmdClearColorImage");
    if (!proc) {
        return;
    }
    proc(commandBuffer, image, imageLayout, pColor, rangeCount, pRanges);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    srcBuffer,
    VkBuffer                                    dstBuffer,
    uint32_t                                    regionCount,
    const VkBufferCopy*                         pRegions) {
    const auto proc = Resolve<PFN_vkCmdCopyBuffer>("vkCmdCopyBuffer");
    if (!proc) {
        return;
    }
    proc(commandBuffer, srcBuffer, dstBuffer, regionCount, pRegions);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkCmdDispatch(VkCommandBuffer                             commandBuffer,
    uint32_t                                    groupCountX,
    uint32_t                                    groupCountY,
    uint32_t                                    groupCountZ) {
    const auto proc = Resolve<PFN_vkCmdDispatch>("vkCmdDispatch");
    if (!proc) {
        return;
    }
    proc(commandBuffer, groupCountX, groupCountY, groupCountZ);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkCmdFillBuffer(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    dstBuffer,
    VkDeviceSize                                dstOffset,
    VkDeviceSize                                size,
    uint32_t                                    data) {
    const auto proc = Resolve<PFN_vkCmdFillBuffer>("vkCmdFillBuffer");
    if (!proc) {
        return;
    }
    proc(commandBuffer, dstBuffer, dstOffset, size, data);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer                             commandBuffer,
    VkPipelineStageFlags                        srcStageMask,
    VkPipelineStageFlags                        dstStageMask,
    VkDependencyFlags                           dependencyFlags,
    uint32_t                                    memoryBarrierCount,
    const VkMemoryBarrier*                      pMemoryBarriers,
    uint32_t                                    bufferMemoryBarrierCount,
    const VkBufferMemoryBarrier*                pBufferMemoryBarriers,
    uint32_t                                    imageMemoryBarrierCount,
    const VkImageMemoryBarrier*                 pImageMemoryBarriers) {
    const auto proc = Resolve<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier");
    if (!proc) {
        return;
    }
    proc(commandBuffer, srcStageMask, dstStageMask, dependencyFlags, memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount, pBufferMemoryBarriers, imageMemoryBarrierCount, pImageMemoryBarriers);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkCmdResetQueryPool(VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery,
    uint32_t                                    queryCount) {
    const auto proc = Resolve<PFN_vkCmdResetQueryPool>("vkCmdResetQueryPool");
    if (!proc) {
        return;
    }
    proc(commandBuffer, queryPool, firstQuery, queryCount);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkCmdWriteTimestamp(VkCommandBuffer                             commandBuffer,
    VkPipelineStageFlagBits                     pipelineStage,
    VkQueryPool                                 queryPool,
    uint32_t                                    query) {
    const auto proc = Resolve<PFN_vkCmdWriteTimestamp>("vkCmdWriteTimestamp");
    if (!proc) {
        return;
    }
    proc(commandBuffer, pipelineStage, queryPool, query);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(VkDevice                                    device,
    const VkBufferCreateInfo*                   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkBuffer*                                   pBuffer) {
    const auto proc = Resolve<PFN_vkCreateBuffer>("vkCreateBuffer");
    if (!proc) {
        return Unavailable<VkResult>();
    }
    return proc(device, pCreateInfo, pAllocator, pBuffer);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateComputePipelines(VkDevice                                    device,
    VkPipelineCache                             pipelineCache,
    uint32_t                                    createInfoCount,
    const VkComputePipelineCreateInfo*          pCreateInfos,
    const VkAllocationCallbacks*                pAllocator,
    VkPipeline*                                 pPipelines) {
    const auto proc = Resolve<PFN_vkCreateComputePipelines>("vkCreateComputePipelines");
    if (!proc) {
        return Unavailable<VkResult>();
    }
    return proc(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorPool(VkDevice                                    device,
    const VkDescriptorPoolCreateInfo*           pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDescriptorPool*                           pDescriptorPool) {
    const auto proc = Resolve<PFN_vkCreateDescriptorPool>("vkCreateDescriptorPool");
    if (!proc) {
        return Unavailable<VkResult>();
    }
    return proc(device, pCreateInfo, pAllocator, pDescriptorPool);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorSetLayout(VkDevice                                    device,
    const VkDescriptorSetLayoutCreateInfo*      pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDescriptorSetLayout*                      pSetLayout) {
    const auto proc = Resolve<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout");
    if (!proc) {
        return Unavailable<VkResult>();
    }
    return proc(device, pCreateInfo, pAllocator, pSetLayout);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(VkDevice                                    device,
    const VkImageCreateInfo*                    pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkImage*                                    pImage) {
    const auto proc = Resolve<PFN_vkCreateImage>("vkCreateImage");
    if (!proc) {
        return Unavailable<VkResult>();
    }
    return proc(device, pCreateInfo, pAllocator, pImage);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(VkDevice                                    device,
    const VkImageViewCreateInfo*                pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkImageView*                                pView) {
    const auto proc = Resolve<PFN_vkCreateImageView>("vkCreateImageView");
    if (!proc) {
        return Unavailable<VkResult>();
    }
    return proc(device, pCreateInfo, pAllocator, pView);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineLayout(VkDevice                                    device,
    const VkPipelineLayoutCreateInfo*           pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkPipelineLayout*                           pPipelineLayout) {
    const auto proc = Resolve<PFN_vkCreatePipelineLayout>("vkCreatePipelineLayout");
    if (!proc) {
        return Unavailable<VkResult>();
    }
    return proc(device, pCreateInfo, pAllocator, pPipelineLayout);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateQueryPool(VkDevice                                    device,
    const VkQueryPoolCreateInfo*                pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkQueryPool*                                pQueryPool) {
    const auto proc = Resolve<PFN_vkCreateQueryPool>("vkCreateQueryPool");
    if (!proc) {
        return Unavailable<VkResult>();
    }
    return proc(device, pCreateInfo, pAllocator, pQueryPool);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateSampler(VkDevice                                    device,
    const VkSamplerCreateInfo*                  pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSampler*                                  pSampler) {
    const auto proc = Resolve<PFN_vkCreateSampler>("vkCreateSampler");
    if (!proc) {
        return Unavailable<VkResult>();
    }
    return proc(device, pCreateInfo, pAllocator, pSampler);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderModule(VkDevice                                    device,
    const VkShaderModuleCreateInfo*             pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkShaderModule*                             pShaderModule) {
    const auto proc = Resolve<PFN_vkCreateShaderModule>("vkCreateShaderModule");
    if (!proc) {
        return Unavailable<VkResult>();
    }
    return proc(device, pCreateInfo, pAllocator, pShaderModule);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(VkDevice                                    device,
    VkBuffer                                    buffer,
    const VkAllocationCallbacks*                pAllocator) {
    const auto proc = Resolve<PFN_vkDestroyBuffer>("vkDestroyBuffer");
    if (!proc) {
        return;
    }
    proc(device, buffer, pAllocator);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorPool(VkDevice                                    device,
    VkDescriptorPool                            descriptorPool,
    const VkAllocationCallbacks*                pAllocator) {
    const auto proc = Resolve<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool");
    if (!proc) {
        return;
    }
    proc(device, descriptorPool, pAllocator);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorSetLayout(VkDevice                                    device,
    VkDescriptorSetLayout                       descriptorSetLayout,
    const VkAllocationCallbacks*                pAllocator) {
    const auto proc = Resolve<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout");
    if (!proc) {
        return;
    }
    proc(device, descriptorSetLayout, pAllocator);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkDestroyImage(VkDevice                                    device,
    VkImage                                     image,
    const VkAllocationCallbacks*                pAllocator) {
    const auto proc = Resolve<PFN_vkDestroyImage>("vkDestroyImage");
    if (!proc) {
        return;
    }
    proc(device, image, pAllocator);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(VkDevice                                    device,
    VkImageView                                 imageView,
    const VkAllocationCallbacks*                pAllocator) {
    const auto proc = Resolve<PFN_vkDestroyImageView>("vkDestroyImageView");
    if (!proc) {
        return;
    }
    proc(device, imageView, pAllocator);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkDestroyPipeline(VkDevice                                    device,
    VkPipeline                                  pipeline,
    const VkAllocationCallbacks*                pAllocator) {
    const auto proc = Resolve<PFN_vkDestroyPipeline>("vkDestroyPipeline");
    if (!proc) {
        return;
    }
    proc(device, pipeline, pAllocator);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineLayout(VkDevice                                    device,
    VkPipelineLayout                            pipelineLayout,
    const VkAllocationCallbacks*                pAllocator) {
    const auto proc = Resolve<PFN_vkDestroyPipelineLayout>("vkDestroyPipelineLayout");
    if (!proc) {
        return;
    }
    proc(device, pipelineLayout, pAllocator);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkDestroyQueryPool(VkDevice                                    device,
    VkQueryPool                                 queryPool,
    const VkAllocationCallbacks*                pAllocator) {
    const auto proc = Resolve<PFN_vkDestroyQueryPool>("vkDestroyQueryPool");
    if (!proc) {
        return;
    }
    proc(device, queryPool, pAllocator);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkDestroySampler(VkDevice                                    device,
    VkSampler                                   sampler,
    const VkAllocationCallbacks*                pAllocator) {
    const auto proc = Resolve<PFN_vkDestroySampler>("vkDestroySampler");
    if (!proc) {
        return;
    }
    proc(device, sampler, pAllocator);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkDestroyShaderModule(VkDevice                                    device,
    VkShaderModule                              shaderModule,
    const VkAllocationCallbacks*                pAllocator) {
    const auto proc = Resolve<PFN_vkDestroyShaderModule>("vkDestroyShaderModule");
    if (!proc) {
        return;
    }
    proc(device, shaderModule, pAllocator);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice                                    device) {
    const auto proc = Resolve<PFN_vkDeviceWaitIdle>("vkDeviceWaitIdle");
    if (!proc) {
        return Unavailable<VkResult>();
    }
    return proc(device);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(VkPhysicalDevice                            physicalDevice,
    const char*                                 pLayerName,
    uint32_t*                                   pPropertyCount,
    VkExtensionProperties*                      pProperties) {
    const auto proc = Resolve<PFN_vkEnumerateDeviceExtensionProperties>("vkEnumerateDeviceExtensionProperties");
    if (!proc) {
        return Unavailable<VkResult>();
    }
    return proc(physicalDevice, pLayerName, pPropertyCount, pProperties);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkFreeMemory(VkDevice                                    device,
    VkDeviceMemory                              memory,
    const VkAllocationCallbacks*                pAllocator) {
    const auto proc = Resolve<PFN_vkFreeMemory>("vkFreeMemory");
    if (!proc) {
        return;
    }
    proc(device, memory, pAllocator);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(VkDevice                                    device,
    VkBuffer                                    buffer,
    VkMemoryRequirements*                       pMemoryRequirements) {
    const auto proc = Resolve<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements");
    if (!proc) {
        return;
    }
    proc(device, buffer, pMemoryRequirements);
}

extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice                                    device,
    const char*                                 pName) {
    const auto proc = Resolve<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr");
    if (!proc) {
        return Unavailable<PFN_vkVoidFunction>();
    }
    return proc(device, pName);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(VkDevice                                    device,
    VkImage                                     image,
    VkMemoryRequirements*                       pMemoryRequirements) {
    const auto proc = Resolve<PFN_vkGetImageMemoryRequirements>("vkGetImageMemoryRequirements");
    if (!proc) {
        return;
    }
    proc(device, image, pMemoryRequirements);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures(VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceFeatures*                   pFeatures) {
    const auto proc = Resolve<PFN_vkGetPhysicalDeviceFeatures>("vkGetPhysicalDeviceFeatures");
    if (!proc) {
        return;
    }
    proc(physicalDevice, pFeatures);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2(VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceFeatures2*                  pFeatures) {
    const auto proc = Resolve<PFN_vkGetPhysicalDeviceFeatures2>("vkGetPhysicalDeviceFeatures2");
    if (!proc) {
        return;
    }
    proc(physicalDevice, pFeatures);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice                            physicalDevice,
    VkFormat                                    format,
    VkFormatProperties*                         pFormatProperties) {
    const auto proc = Resolve<PFN_vkGetPhysicalDeviceFormatProperties>("vkGetPhysicalDeviceFormatProperties");
    if (!proc) {
        return;
    }
    proc(physicalDevice, format, pFormatProperties);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceMemoryProperties*           pMemoryProperties) {
    const auto proc = Resolve<PFN_vkGetPhysicalDeviceMemoryProperties>("vkGetPhysicalDeviceMemoryProperties");
    if (!proc) {
        return;
    }
    proc(physicalDevice, pMemoryProperties);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceProperties*                 pProperties) {
    const auto proc = Resolve<PFN_vkGetPhysicalDeviceProperties>("vkGetPhysicalDeviceProperties");
    if (!proc) {
        return;
    }
    proc(physicalDevice, pProperties);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties2(VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceProperties2*                pProperties) {
    const auto proc = Resolve<PFN_vkGetPhysicalDeviceProperties2>("vkGetPhysicalDeviceProperties2");
    if (!proc) {
        return;
    }
    proc(physicalDevice, pProperties);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pQueueFamilyPropertyCount,
    VkQueueFamilyProperties*                    pQueueFamilyProperties) {
    const auto proc = Resolve<PFN_vkGetPhysicalDeviceQueueFamilyProperties>("vkGetPhysicalDeviceQueueFamilyProperties");
    if (!proc) {
        return;
    }
    proc(physicalDevice, pQueueFamilyPropertyCount, pQueueFamilyProperties);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkGetQueryPoolResults(VkDevice                                    device,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery,
    uint32_t                                    queryCount,
    size_t                                      dataSize,
    void*                                       pData,
    VkDeviceSize                                stride,
    VkQueryResultFlags                          flags) {
    const auto proc = Resolve<PFN_vkGetQueryPoolResults>("vkGetQueryPoolResults");
    if (!proc) {
        return Unavailable<VkResult>();
    }
    return proc(device, queryPool, firstQuery, queryCount, dataSize, pData, stride, flags);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(VkDevice                                    device,
    VkDeviceMemory                              memory,
    VkDeviceSize                                offset,
    VkDeviceSize                                size,
    VkMemoryMapFlags                            flags,
    void**                                      ppData) {
    const auto proc = Resolve<PFN_vkMapMemory>("vkMapMemory");
    if (!proc) {
        return Unavailable<VkResult>();
    }
    return proc(device, memory, offset, size, flags, ppData);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkResetDescriptorPool(VkDevice                                    device,
    VkDescriptorPool                            descriptorPool,
    VkDescriptorPoolResetFlags                  flags) {
    const auto proc = Resolve<PFN_vkResetDescriptorPool>("vkResetDescriptorPool");
    if (!proc) {
        return Unavailable<VkResult>();
    }
    return proc(device, descriptorPool, flags);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(VkDevice                                    device,
    VkDeviceMemory                              memory) {
    const auto proc = Resolve<PFN_vkUnmapMemory>("vkUnmapMemory");
    if (!proc) {
        return;
    }
    proc(device, memory);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(VkDevice                                    device,
    uint32_t                                    descriptorWriteCount,
    const VkWriteDescriptorSet*                 pDescriptorWrites,
    uint32_t                                    descriptorCopyCount,
    const VkCopyDescriptorSet*                  pDescriptorCopies) {
    const auto proc = Resolve<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets");
    if (!proc) {
        return;
    }
    proc(device, descriptorWriteCount, pDescriptorWrites, descriptorCopyCount, pDescriptorCopies);
}
