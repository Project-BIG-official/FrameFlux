// PB FrameFlux - LGPL-2.1
// layer/swapchain_interceptor.cpp: Complete Clean Architecture with True Fractional Target FPS

#include "swapchain_interceptor.hpp"
#include "settings.hpp"
#include "hotkey_manager.hpp"

#include <algorithm>
#include <iostream>
#include <thread>
#include <vector>
#include <cmath>

namespace FrameFlux {

static uint32_t GetConfiguredMultiplier() {
    const char* env = getenv("FRAMEFLUX_MULTIPLIER");
    if (env) {
        int m = atoi(env);
        if (m >= 2 && m <= 6) return static_cast<uint32_t>(m);
    }
    uint32_t cfgMult = SettingsManager::Get().GetSettings().multiplier;
    return std::clamp(cfgMult, 2u, 6u);
}

static bool IsDebugWatermarkEnabled() {
    const char* env = getenv("FRAMEFLUX_DEBUG");
    if (env) return (strcmp(env, "1") == 0 || strcasecmp(env, "true") == 0);
    return SettingsManager::Get().GetSettings().showWatermark;
}

static uint32_t FindMemoryType(const VkPhysicalDeviceMemoryProperties& memProperties, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    if (memProperties.memoryTypeCount > 0) {
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
    }
    for (uint32_t i = 0; i < 32; i++) {
        if (typeFilter & (1 << i)) return i;
    }
    return 0;
}

static void TransitionImage(
    VkCommandBuffer cmd,
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkPipelineStageFlags srcStage,
    VkPipelineStageFlags dstStage,
    VkAccessFlags srcAccess,
    VkAccessFlags dstAccess
) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

static void PreciseDelay(uint32_t microseconds) {
    auto start = std::chrono::high_resolution_clock::now();
    auto target = start + std::chrono::microseconds(microseconds);

    if (microseconds > 2000) {
        std::this_thread::sleep_for(std::chrono::microseconds(microseconds - 1500));
    }

    while (std::chrono::high_resolution_clock::now() < target) {
        #if defined(__x86_64__) || defined(_M_X64)
        __builtin_ia32_pause();
        #endif
    }
}

Interceptor& Interceptor::Get() {
    static Interceptor instance;
    return instance;
}

bool Interceptor::AllocateFrameBuffers(SwapchainData& data) {
    uint32_t w = data.extent.width;
    uint32_t h = data.extent.height;

    if (w == 0 || h == 0) return false;

    uint32_t packedW = (w + 3) / 4;
    uint32_t blockGridH = (h + 3) / 4;
    uint32_t halfPackedW = (packedW + 1) / 2;
    uint32_t halfH = (h + 1) / 2;
    uint32_t halfBlockGridH = (halfH + 3) / 4;

    size_t totalAllocatedBytes = 0;

    auto allocTex = [&](uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage,
                        VkImage& outImg, VkDeviceMemory& outMem, VkImageView& outView) -> bool {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {width, height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = usage;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(data.device, &imageInfo, nullptr, &outImg) != VK_SUCCESS) return false;

        VkMemoryRequirements memReqs{};
        vkGetImageMemoryRequirements(data.device, outImg, &memReqs);

        totalAllocatedBytes += memReqs.size;

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = FindMemoryType(data.memoryProperties, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(data.device, &allocInfo, nullptr, &outMem) != VK_SUCCESS) return false;
        if (vkBindImageMemory(data.device, outImg, outMem, 0) != VK_SUCCESS) return false;

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = outImg;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;

        return vkCreateImageView(data.device, &viewInfo, nullptr, &outView) == VK_SUCCESS;
    };

    VkFormat workingFormat = data.imageFormat;
    if (workingFormat == VK_FORMAT_B8G8R8A8_SRGB) workingFormat = VK_FORMAT_B8G8R8A8_UNORM;
    if (workingFormat == VK_FORMAT_R8G8B8A8_SRGB) workingFormat = VK_FORMAT_R8G8B8A8_UNORM;

    VkImageUsageFlags sampledUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (!allocTex(w, h, workingFormat, sampledUsage, data.frameAImage, data.frameAMemory, data.frameAView)) return false;
    if (!allocTex(w, h, workingFormat, sampledUsage, data.frameBImage, data.frameBMemory, data.frameBView)) return false;

    VkImageUsageFlags storageUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (!allocTex(w, h, workingFormat, storageUsage, data.generatedImage, data.generatedMemory, data.generatedView)) return false;

    if (!allocTex(packedW, h, VK_FORMAT_R32_UINT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, data.lumaAImage, data.lumaAMemory, data.lumaAView)) return false;
    if (!allocTex(packedW, h, VK_FORMAT_R32_UINT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, data.lumaBImage, data.lumaBMemory, data.lumaBView)) return false;

    if (!allocTex(halfPackedW, halfH, VK_FORMAT_R32_UINT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, data.lumaAHalfImage, data.lumaAHalfMemory, data.lumaAHalfView)) return false;
    if (!allocTex(halfPackedW, halfH, VK_FORMAT_R32_UINT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, data.lumaBHalfImage, data.lumaBHalfMemory, data.lumaBHalfView)) return false;

    if (!allocTex(1, 1, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, data.dummyCoarseImage, data.dummyCoarseMemory, data.dummyCoarseView)) return false;
    if (!allocTex(halfPackedW, halfBlockGridH, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, data.coarseMotionImage, data.coarseMotionMemory, data.coarseMotionView)) return false;
    if (!allocTex(packedW, blockGridH, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, data.motionImage, data.motionMemory, data.motionView)) return false;
    if (!allocTex(packedW, blockGridH, VK_FORMAT_R8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, data.confidenceImage, data.confidenceMemory, data.confidenceView)) return false;

    data.totalAllocatedVramMb = static_cast<uint32_t>(totalAllocatedBytes / (1024 * 1024));

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(data.device, &samplerInfo, nullptr, &data.linearSampler);

    std::vector<VkDescriptorPoolSize> poolSizes = {
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 24},
        {VK_DESCRIPTOR_TYPE_SAMPLER, 2},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 12}
    };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 8;
    poolInfo.poolSizeCount = (uint32_t)poolSizes.size();
    poolInfo.pPoolSizes = poolSizes.data();
    vkCreateDescriptorPool(data.device, &poolInfo, nullptr, &data.descriptorPool);

    auto allocSet = [&](VkDescriptorSetLayout layout, VkDescriptorSet& outSet) {
        VkDescriptorSetAllocateInfo allocSetInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, data.descriptorPool, 1, &layout};
        vkAllocateDescriptorSets(data.device, &allocSetInfo, &outSet);
    };

    allocSet(m_computeEngine.GetLumaDescLayout(), data.lumaDescSet);
    allocSet(m_computeEngine.GetDownsampleDescLayout(), data.downsampleDescSet);
    allocSet(m_computeEngine.GetFlowDescLayout(), data.coarseFlowDescSet);
    allocSet(m_computeEngine.GetRefineDescLayout(), data.refineDescSet);
    allocSet(m_computeEngine.GetFlowDescLayout(), data.directFlowDescSet);
    allocSet(m_computeEngine.GetWarpDescLayout(), data.warpDescSet);
    allocSet(m_computeEngine.GetOverlayDescLayout(), data.overlayDescSet);

    // Updates
    VkDescriptorImageInfo lumaImgs[2]{
        {VK_NULL_HANDLE, data.frameBView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {VK_NULL_HANDLE, data.lumaBView,  VK_IMAGE_LAYOUT_GENERAL}
    };
    VkWriteDescriptorSet lumaWrites[2]{};
    lumaWrites[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, data.lumaDescSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &lumaImgs[0], nullptr, nullptr};
    lumaWrites[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, data.lumaDescSet, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &lumaImgs[1], nullptr, nullptr};
    vkUpdateDescriptorSets(data.device, 2, lumaWrites, 0, nullptr);

    VkDescriptorImageInfo downImgs[2]{
        {VK_NULL_HANDLE, data.lumaBView,     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {VK_NULL_HANDLE, data.lumaBHalfView, VK_IMAGE_LAYOUT_GENERAL}
    };
    VkWriteDescriptorSet downWrites[2]{};
    downWrites[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, data.downsampleDescSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &downImgs[0], nullptr, nullptr};
    downWrites[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, data.downsampleDescSet, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &downImgs[1], nullptr, nullptr};
    vkUpdateDescriptorSets(data.device, 2, downWrites, 0, nullptr);

    VkDescriptorImageInfo coarseImgs[5]{
        {VK_NULL_HANDLE, data.lumaAHalfView,     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {VK_NULL_HANDLE, data.lumaBHalfView,     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {VK_NULL_HANDLE, data.dummyCoarseView,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {VK_NULL_HANDLE, data.coarseMotionView,  VK_IMAGE_LAYOUT_GENERAL},
        {VK_NULL_HANDLE, data.confidenceView,    VK_IMAGE_LAYOUT_GENERAL}
    };
    VkWriteDescriptorSet coarseWrites[5]{};
    for (uint32_t i = 0; i < 5; ++i) {
        coarseWrites[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, data.coarseFlowDescSet, i, 0, 1, (i >= 3) ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &coarseImgs[i], nullptr, nullptr};
    }
    vkUpdateDescriptorSets(data.device, 5, coarseWrites, 0, nullptr);

    VkDescriptorImageInfo refineImgs[5]{
        {VK_NULL_HANDLE, data.lumaAView,         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {VK_NULL_HANDLE, data.lumaBView,         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {VK_NULL_HANDLE, data.coarseMotionView,  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {VK_NULL_HANDLE, data.motionView,        VK_IMAGE_LAYOUT_GENERAL},
        {VK_NULL_HANDLE, data.confidenceView,    VK_IMAGE_LAYOUT_GENERAL}
    };
    VkWriteDescriptorSet refineWrites[5]{};
    for (uint32_t i = 0; i < 5; ++i) {
        refineWrites[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, data.refineDescSet, i, 0, 1, (i >= 3) ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &refineImgs[i], nullptr, nullptr};
    }
    vkUpdateDescriptorSets(data.device, 5, refineWrites, 0, nullptr);

    VkDescriptorImageInfo directImgs[5]{
        {VK_NULL_HANDLE, data.lumaAView,         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {VK_NULL_HANDLE, data.lumaBView,         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {VK_NULL_HANDLE, data.dummyCoarseView,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {VK_NULL_HANDLE, data.motionView,        VK_IMAGE_LAYOUT_GENERAL},
        {VK_NULL_HANDLE, data.confidenceView,    VK_IMAGE_LAYOUT_GENERAL}
    };
    VkWriteDescriptorSet directWrites[5]{};
    for (uint32_t i = 0; i < 5; ++i) {
        directWrites[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, data.directFlowDescSet, i, 0, 1, (i >= 3) ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &directImgs[i], nullptr, nullptr};
    }
    vkUpdateDescriptorSets(data.device, 5, directWrites, 0, nullptr);

    VkDescriptorImageInfo warpImgs[6]{
        {VK_NULL_HANDLE, data.frameAView,     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {VK_NULL_HANDLE, data.frameBView,     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {VK_NULL_HANDLE, data.motionView,     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {VK_NULL_HANDLE, data.confidenceView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {data.linearSampler, VK_NULL_HANDLE,  VK_IMAGE_LAYOUT_UNDEFINED},
        {VK_NULL_HANDLE, data.generatedView,  VK_IMAGE_LAYOUT_GENERAL}
    };
    VkWriteDescriptorSet warpWrites[6]{};
    for (uint32_t i = 0; i < 6; ++i) {
        warpWrites[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, data.warpDescSet, i, 0, 1, (i == 4) ? VK_DESCRIPTOR_TYPE_SAMPLER : ((i == 5) ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), &warpImgs[i], nullptr, nullptr};
    }
    vkUpdateDescriptorSets(data.device, 6, warpWrites, 0, nullptr);

    VkDescriptorImageInfo overlayImg{VK_NULL_HANDLE, data.generatedView, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet overlayWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, data.overlayDescSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &overlayImg, nullptr, nullptr};
    vkUpdateDescriptorSets(data.device, 1, &overlayWrite, 0, nullptr);

    VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    vkCreateSemaphore(data.device, &semInfo, nullptr, &data.internalAcquireSemaphore);

    data.buffersAllocated = true;
    return true;
}

void Interceptor::CleanupSwapchainData(SwapchainData& data) {
    if (data.device == VK_NULL_HANDLE) return;

    if (data.linearSampler != VK_NULL_HANDLE) vkDestroySampler(data.device, data.linearSampler, nullptr);
    if (data.descriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(data.device, data.descriptorPool, nullptr);

    auto destroyTex = [&](VkImage img, VkDeviceMemory mem, VkImageView view) {
        if (view != VK_NULL_HANDLE) vkDestroyImageView(data.device, view, nullptr);
        if (img != VK_NULL_HANDLE) vkDestroyImage(data.device, img, nullptr);
        if (mem != VK_NULL_HANDLE) vkFreeMemory(data.device, mem, nullptr);
    };

    destroyTex(data.frameAImage, data.frameAMemory, data.frameAView);
    destroyTex(data.frameBImage, data.frameBMemory, data.frameBView);
    destroyTex(data.lumaAImage, data.lumaAMemory, data.lumaAView);
    destroyTex(data.lumaBImage, data.lumaBMemory, data.lumaBView);
    destroyTex(data.lumaAHalfImage, data.lumaAHalfMemory, data.lumaAHalfView);
    destroyTex(data.lumaBHalfImage, data.lumaBHalfMemory, data.lumaBHalfView);
    destroyTex(data.dummyCoarseImage, data.dummyCoarseMemory, data.dummyCoarseView);
    destroyTex(data.coarseMotionImage, data.coarseMotionMemory, data.coarseMotionView);
    destroyTex(data.motionImage, data.motionMemory, data.motionView);
    destroyTex(data.confidenceImage, data.confidenceMemory, data.confidenceView);
    destroyTex(data.generatedImage, data.generatedMemory, data.generatedView);

    if (data.commandPool != VK_NULL_HANDLE) vkDestroyCommandPool(data.device, data.commandPool, nullptr);
    if (data.internalAcquireSemaphore != VK_NULL_HANDLE) vkDestroySemaphore(data.device, data.internalAcquireSemaphore, nullptr);
}

VkResult Interceptor::OnCreateSwapchainKHR(
    VkDevice device,
    const VkSwapchainCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSwapchainKHR* pSwapchain,
    PFN_vkCreateSwapchainKHR realFunc
) {
    SettingsManager::Get().LoadOrCreate();
    uint32_t multiplier = GetConfiguredMultiplier();

    VkSwapchainCreateInfoKHR modifiedCreateInfo = *pCreateInfo;
    modifiedCreateInfo.minImageCount = std::max(pCreateInfo->minImageCount + multiplier, 4u);

    VkResult result = realFunc(device, &modifiedCreateInfo, pAllocator, pSwapchain);
    if (result != VK_SUCCESS) {
        result = realFunc(device, pCreateInfo, pAllocator, pSwapchain);
        if (result != VK_SUCCESS) return result;
    }

    if (!m_computeEngineInitialized) {
        m_computeEngine.Initialize(device, VK_NULL_HANDLE);
        m_computeEngineInitialized = true;
    }

    auto data = std::make_unique<SwapchainData>();
    data->device = device;
    data->memoryProperties = m_cachedMemProps;
    data->queueFamilyIndex = m_cachedQueueFamily;
    data->swapchain = *pSwapchain;
    data->imageFormat = pCreateInfo->imageFormat;
    data->extent = pCreateInfo->imageExtent;
    data->lastPresentTime = std::chrono::high_resolution_clock::now();
    data->mode = (SettingsManager::Get().GetSettings().mode == "v1") ? FrameFluxMode::LegacyV1 : FrameFluxMode::TrueFGV2;

    uint32_t imageCount = 0;
    vkGetSwapchainImagesKHR(device, *pSwapchain, &imageCount, nullptr);
    data->realImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device, *pSwapchain, &imageCount, data->realImages.data());

    VkCommandPoolCreateInfo cmdPoolInfo{};
    cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cmdPoolInfo.queueFamilyIndex = m_cachedQueueFamily;
    vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &data->commandPool);

    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = data->commandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 2;

    VkCommandBuffer bufs[2];
    vkAllocateCommandBuffers(device, &cmdAllocInfo, bufs);
    data->genCommandBuffer = bufs[0];
    data->realCommandBuffer = bufs[1];

    AllocateFrameBuffers(*data);

    m_swapchains[*pSwapchain] = std::move(data);
    return VK_SUCCESS;
}

void Interceptor::OnDestroySwapchainKHR(
    VkDevice device,
    VkSwapchainKHR swapchain,
    const VkAllocationCallbacks* pAllocator,
    PFN_vkDestroySwapchainKHR realFunc
) {
    auto it = m_swapchains.find(swapchain);
    if (it != m_swapchains.end()) {
        CleanupSwapchainData(*(it->second));
        m_swapchains.erase(it);
    }
    realFunc(device, swapchain, pAllocator);
}

void Interceptor::ComputeAdaptiveTiming(SwapchainData& data, float& outNormalizedT) {
    auto now = std::chrono::high_resolution_clock::now();
    float deliveryDeltaMs = std::chrono::duration<float, std::milli>(now - data.lastPresentTime).count();
    deliveryDeltaMs = std::clamp(deliveryDeltaMs, 2.0f, 100.0f);

    constexpr float alpha = 0.15f;
    data.smoothedFrametimeMs = alpha * deliveryDeltaMs + (1.0f - alpha) * data.smoothedFrametimeMs;
    outNormalizedT = 0.5f;
}

static void IngestAndComputeFlow(
    SwapchainData& data,
    VkQueue queue,
    uint32_t imageIndex,
    ComputeEngine& computeEngine
) {
    VkCommandBuffer cmd = data.genCommandBuffer;
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr};
    vkBeginCommandBuffer(cmd, &beginInfo);

    uint32_t w = data.extent.width;
    uint32_t h = data.extent.height;
    uint32_t packedW = (w + 3) / 4;
    uint32_t halfPackedW = (packedW + 1) / 2;
    uint32_t halfH = (h + 1) / 2;

    VkImageCopy fullCopyRegion{};
    fullCopyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    fullCopyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    fullCopyRegion.extent = {w, h, 1};

    computeEngine.BeginTimestamp(cmd);

    // 1. Ingest pristine real game frame B
    TransitionImage(cmd, data.realImages[imageIndex], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    TransitionImage(cmd, data.frameBImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT);

    vkCmdCopyImage(cmd, data.realImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   data.frameBImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &fullCopyRegion);

    const auto& cfg = SettingsManager::Get().GetSettings();

    if (data.mode == FrameFluxMode::TrueFGV2) {
        // 2. Luma Pack
        TransitionImage(cmd, data.frameBImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
        TransitionImage(cmd, data.lumaBImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_ACCESS_SHADER_WRITE_BIT);
        computeEngine.RecordLumaPass(cmd, data.lumaDescSet, w, h);

        if (cfg.profile == "quality") {
            // Downsample
            TransitionImage(cmd, data.lumaBImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
            TransitionImage(cmd, data.lumaBHalfImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_ACCESS_SHADER_WRITE_BIT);
            computeEngine.RecordDownsamplePass(cmd, data.downsampleDescSet, packedW, h, halfPackedW, halfH);

            // Pass 1: Coarse Search
            TransitionImage(cmd, data.lumaBHalfImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
            TransitionImage(cmd, data.lumaAHalfImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
            TransitionImage(cmd, data.coarseMotionImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_ACCESS_SHADER_WRITE_BIT);
            computeEngine.RecordFlowPass(cmd, data.coarseFlowDescSet, (w + 1) / 2, (h + 1) / 2, 2);

            // Pass 2: Fine Refine
            TransitionImage(cmd, data.coarseMotionImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
            TransitionImage(cmd, data.lumaAImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
            TransitionImage(cmd, data.motionImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_ACCESS_SHADER_WRITE_BIT);
            TransitionImage(cmd, data.confidenceImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_ACCESS_SHADER_WRITE_BIT);
            computeEngine.RecordRefinePass(cmd, data.refineDescSet, w, h);
        } else {
            TransitionImage(cmd, data.lumaBImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
            TransitionImage(cmd, data.lumaAImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
            TransitionImage(cmd, data.motionImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_ACCESS_SHADER_WRITE_BIT);
            TransitionImage(cmd, data.confidenceImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_ACCESS_SHADER_WRITE_BIT);
            computeEngine.RecordFlowPass(cmd, data.directFlowDescSet, w, h, (cfg.searchMode == "high") ? 3 : 1);
        }

        TransitionImage(cmd, data.motionImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
        TransitionImage(cmd, data.confidenceImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    } else {
        TransitionImage(cmd, data.frameBImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    }

    TransitionImage(cmd, data.frameAImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

    computeEngine.EndTimestamp(cmd);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
}

static void RecordHudOverlay(SwapchainData& data, VkCommandBuffer cmd, ComputeEngine& computeEngine) {
    const auto& cfg = SettingsManager::Get().GetSettings();
    uint32_t w = data.extent.width;
    uint32_t h = data.extent.height;

    float frametimeMs = data.smoothedFrametimeMs;
    if (frametimeMs < 0.1f) frametimeMs = 16.66f;

    uint32_t activeMultiplier = GetConfiguredMultiplier();

    if (cfg.schedulerMode == "target_fps" && cfg.targetFps > 0) {
        float targetIntervalMs = 1000.0f / static_cast<float>(cfg.targetFps);
        activeMultiplier = std::clamp(static_cast<uint32_t>(std::round(frametimeMs / targetIntervalMs)), 1u, 6u);
    }

    uint32_t nativeFpsX10 = static_cast<uint32_t>((1000.0f / frametimeMs) * 10.0f);
    uint32_t outputFpsX10 = (cfg.schedulerMode == "target_fps" && cfg.targetFps > 0) 
        ? (cfg.targetFps * 10) 
        : (nativeFpsX10 * activeMultiplier);

    uint32_t gpuUs = static_cast<uint32_t>(computeEngine.QueryLastGpuTimeMs() * 1000.0f);
    uint32_t cpuUs = static_cast<uint32_t>(data.lastCpuTimeMs * 1000.0f);
    uint32_t latUs = static_cast<uint32_t>((frametimeMs * 1000.0f) / activeMultiplier);
    uint32_t modeCode = (cfg.mode == "off") ? 0 : ((cfg.mode == "v1") ? 1 : 2);
    uint32_t profCode = (cfg.profile == "quality") ? 1 : 0;
    uint32_t searchCode = (cfg.searchMode == "high") ? 1 : 0;
    uint32_t schedCode = (cfg.schedulerMode == "fixed") ? 1 : ((cfg.schedulerMode == "target_fps") ? 2 : 0);
    uint32_t fallbackCode = (cfg.fallbackAction == "repeat") ? 0 : ((cfg.fallbackAction == "drop") ? 2 : 1);
    uint32_t isMenu = HotkeyManager::Get().IsMenuOpen() ? 1 : 0;
    uint32_t isHud = cfg.showWatermark ? 1 : 0;
    uint32_t selectedItem = HotkeyManager::Get().GetSelectedItem();

    OverlayPushConstants opc{
        w,
        h,
        isMenu,
        isHud,
        selectedItem,
        modeCode,
        activeMultiplier,
        profCode,
        searchCode,
        schedCode,
        cfg.targetFps,
        fallbackCode,
        cfg.hudCorner,
        nativeFpsX10,
        outputFpsX10,
        gpuUs,
        cpuUs,
        latUs,
        980,
        0,
        data.totalAllocatedVramMb
    };
    computeEngine.RecordOverlayPass(cmd, data.overlayDescSet, opc, w, h);
}

static void WarpAndBlitIntermediate(
    SwapchainData& data,
    VkQueue queue,
    uint32_t targetImageIndex,
    float t,
    ComputeEngine& computeEngine
) {
    const auto& cfg = SettingsManager::Get().GetSettings();

    VkCommandBuffer cmd = data.genCommandBuffer;
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr};
    vkBeginCommandBuffer(cmd, &beginInfo);

    uint32_t w = data.extent.width;
    uint32_t h = data.extent.height;

    VkImageCopy fullCopyRegion{};
    fullCopyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    fullCopyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    fullCopyRegion.extent = {w, h, 1};

    TransitionImage(cmd, data.generatedImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_ACCESS_SHADER_WRITE_BIT);

    uint32_t fallbackAct = 1;
    if (cfg.fallbackAction == "repeat") fallbackAct = 0;
    else if (cfg.fallbackAction == "drop") fallbackAct = 2;

    WarpPushConstants pc{};
    pc.t = t;
    pc.confidenceThreshold = (data.mode == FrameFluxMode::LegacyV1) ? 2.0f : 0.65f;
    pc.fallbackAction = fallbackAct;
    pc.showDebugWatermark = 0;
    pc.resolutionX = w;
    pc.resolutionY = h;
    pc.invResolutionX = 1.0f / static_cast<float>(w);
    pc.invResolutionY = 1.0f / static_cast<float>(h);
    computeEngine.RecordWarpPass(cmd, data.warpDescSet, pc, w, h);

    if (cfg.showWatermark || HotkeyManager::Get().IsMenuOpen()) {
        RecordHudOverlay(data, cmd, computeEngine);
    }

    TransitionImage(cmd, data.generatedImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    TransitionImage(cmd, data.realImages[targetImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT);

    vkCmdCopyImage(cmd, data.generatedImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   data.realImages[targetImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &fullCopyRegion);

    TransitionImage(cmd, data.realImages[targetImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, 0);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
}

static void ExecuteRealAndHistoryPass(
    SwapchainData& data,
    VkQueue queue,
    uint32_t imageIndex,
    ComputeEngine& computeEngine
) {
    const auto& cfg = SettingsManager::Get().GetSettings();

    VkCommandBuffer cmd = data.realCommandBuffer;
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr};
    vkBeginCommandBuffer(cmd, &beginInfo);

    uint32_t w = data.extent.width;
    uint32_t h = data.extent.height;
    uint32_t packedW = (w + 3) / 4;
    uint32_t halfPackedW = (packedW + 1) / 2;
    uint32_t halfH = (h + 1) / 2;

    VkImageCopy fullCopyRegion{};
    fullCopyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    fullCopyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    fullCopyRegion.extent = {w, h, 1};

    if (cfg.showWatermark || HotkeyManager::Get().IsMenuOpen()) {
        TransitionImage(cmd, data.generatedImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
        TransitionImage(cmd, data.frameBImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        
        vkCmdCopyImage(cmd, data.frameBImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       data.generatedImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &fullCopyRegion);

        TransitionImage(cmd, data.generatedImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

        RecordHudOverlay(data, cmd, computeEngine);

        TransitionImage(cmd, data.generatedImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        TransitionImage(cmd, data.realImages[imageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT);

        vkCmdCopyImage(cmd, data.generatedImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       data.realImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &fullCopyRegion);
    } else {
        TransitionImage(cmd, data.realImages[imageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
        TransitionImage(cmd, data.frameBImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT);

        vkCmdCopyImage(cmd, data.frameBImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       data.realImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &fullCopyRegion);
    }

    TransitionImage(cmd, data.realImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, 0);

    TransitionImage(cmd, data.frameBImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    TransitionImage(cmd, data.frameAImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    vkCmdCopyImage(cmd, data.frameBImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   data.frameAImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &fullCopyRegion);

    if (data.mode == FrameFluxMode::TrueFGV2) {
        VkImageCopy lumaCopyRegion{};
        lumaCopyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        lumaCopyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        lumaCopyRegion.extent = {packedW, h, 1};

        TransitionImage(cmd, data.lumaBImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        TransitionImage(cmd, data.lumaAImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        vkCmdCopyImage(cmd, data.lumaBImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       data.lumaAImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &lumaCopyRegion);

        VkImageCopy halfLumaCopyRegion{};
        halfLumaCopyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        halfLumaCopyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        halfLumaCopyRegion.extent = {halfPackedW, halfH, 1};

        TransitionImage(cmd, data.lumaBHalfImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        TransitionImage(cmd, data.lumaAHalfImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        vkCmdCopyImage(cmd, data.lumaBHalfImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       data.lumaAHalfImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &halfLumaCopyRegion);
    }

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
}

VkResult Interceptor::OnQueuePresentKHR(
    VkQueue queue,
    const VkPresentInfoKHR* pPresentInfo,
    PFN_vkQueuePresentKHR realFunc
) {
    HotkeyManager::Get().PollHotkeys();
    SettingsManager::Get().CheckHotReload();

    for (uint32_t i = 0; i < pPresentInfo->swapchainCount; ++i) {
        VkSwapchainKHR swapchain = pPresentInfo->pSwapchains[i];
        auto it = m_swapchains.find(swapchain);

        if (it != m_swapchains.end()) {
            SwapchainData& data = *(it->second);

            if (!data.buffersAllocated) {
                return realFunc(queue, pPresentInfo);
            }

            const auto& cfg = SettingsManager::Get().GetSettings();

            if (cfg.mode == "off") {
                if (HotkeyManager::Get().IsMenuOpen() || cfg.showWatermark) {
                    ExecuteRealAndHistoryPass(data, queue, pPresentInfo->pImageIndices[i], m_computeEngine);
                }
                return realFunc(queue, pPresentInfo);
            }

            data.frameCounter++;

            if (data.frameCounter <= 2) {
                VkCommandBuffer cmd = data.genCommandBuffer;
                vkResetCommandBuffer(cmd, 0);
                VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                vkBeginCommandBuffer(cmd, &beginInfo);

                TransitionImage(cmd, data.frameAImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
                TransitionImage(cmd, data.lumaAImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
                TransitionImage(cmd, data.lumaAHalfImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT);

                vkEndCommandBuffer(cmd);
                VkSubmitInfo sub{VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr, 0, nullptr, nullptr, 1, &cmd, 0, nullptr};
                vkQueueSubmit(queue, 1, &sub, VK_NULL_HANDLE);
                vkQueueWaitIdle(queue);

                data.lastPresentTime = std::chrono::high_resolution_clock::now();
                return realFunc(queue, pPresentInfo);
            }

            auto startCpuTime = std::chrono::high_resolution_clock::now();

            float t = 0.5f;
            ComputeAdaptiveTiming(data, t);
            data.lastPresentTime = std::chrono::high_resolution_clock::now();

            uint32_t activeMultiplier = GetConfiguredMultiplier();
            float baseGameMs = data.smoothedFrametimeMs;
            if (baseGameMs < 1.0f) baseGameMs = 16.66f;

            if (cfg.schedulerMode == "target_fps" && cfg.targetFps > 0) {
                float targetIntervalMs = 1000.0f / static_cast<float>(cfg.targetFps);
                activeMultiplier = std::clamp(static_cast<uint32_t>(std::round(baseGameMs / targetIntervalMs)), 1u, 6u);
            }

            uint32_t intervalUs = 0;
            if (cfg.schedulerMode == "target_fps" && cfg.targetFps > 0) {
                intervalUs = 1000000u / cfg.targetFps;
            } else {
                intervalUs = static_cast<uint32_t>((baseGameMs * 1000.0f) / activeMultiplier);
            }
            intervalUs = std::clamp(intervalUs, 1500u, 35000u);

            // Phase 1: Ingest B and run 2-Pass Pyramid Flow ONCE
            IngestAndComputeFlow(data, queue, pPresentInfo->pImageIndices[i], m_computeEngine);

            float stepT = 1.0f / static_cast<float>(activeMultiplier);

            // Phase 2.1: Generate G1 into first buffer
            WarpAndBlitIntermediate(data, queue, pPresentInfo->pImageIndices[i], stepT, m_computeEngine);

            VkPresentInfoKHR presentG = *pPresentInfo;
            presentG.swapchainCount = 1;
            presentG.pSwapchains = &data.swapchain;
            presentG.pImageIndices = &pPresentInfo->pImageIndices[i];
            realFunc(queue, &presentG);

            PreciseDelay(intervalUs);

            // Phase 2.2: MULTIPLIER LOOP for 3x, 4x, 5x, 6x!
            for (uint32_t m = 2; m < activeMultiplier; ++m) {
                float intermediateT = stepT * static_cast<float>(m);
                uint32_t midImageIndex = 0;
                VkResult midRes = vkAcquireNextImageKHR(
                    data.device, data.swapchain, 0ULL,
                    data.internalAcquireSemaphore, VK_NULL_HANDLE, &midImageIndex
                );

                if ((midRes == VK_SUCCESS || midRes == VK_SUBOPTIMAL_KHR) && midImageIndex < data.realImages.size()) {
                    WarpAndBlitIntermediate(data, queue, midImageIndex, intermediateT, m_computeEngine);

                    VkPresentInfoKHR presentMid{};
                    presentMid.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
                    presentMid.swapchainCount = 1;
                    presentMid.pSwapchains = &data.swapchain;
                    presentMid.pImageIndices = &midImageIndex;
                    realFunc(queue, &presentMid);

                    PreciseDelay(intervalUs);
                } else {
                    break;
                }
            }

            // Phase 3: Present Real Frame B
            uint32_t secondImageIndex = 0;
            VkResult acqRes = vkAcquireNextImageKHR(
                data.device, data.swapchain, 5000000ULL,
                data.internalAcquireSemaphore, VK_NULL_HANDLE, &secondImageIndex
            );

            if ((acqRes == VK_SUCCESS || acqRes == VK_SUBOPTIMAL_KHR) && secondImageIndex < data.realImages.size()) {
                ExecuteRealAndHistoryPass(data, queue, secondImageIndex, m_computeEngine);

                VkPresentInfoKHR presentB{};
                presentB.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
                presentB.swapchainCount = 1;
                presentB.pSwapchains = &data.swapchain;
                presentB.pImageIndices = &secondImageIndex;
                realFunc(queue, &presentB);
            }

            auto endCpuTime = std::chrono::high_resolution_clock::now();
            data.lastCpuTimeMs = std::chrono::duration<float, std::milli>(endCpuTime - startCpuTime).count();

            return VK_SUCCESS;
        }
    }

    return realFunc(queue, pPresentInfo);
}

} // namespace FrameFlux