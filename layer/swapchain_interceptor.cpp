// PB FrameFlux - LGPL-2.1
// layer/swapchain_interceptor.cpp: Full Optical Flow, Warping & Double Present

#include "swapchain_interceptor.hpp"
#include <algorithm>
#include <iostream>

namespace FrameFlux {

static uint32_t FindMemoryType(const VkPhysicalDeviceMemoryProperties& memProperties, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
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
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

// -----------------------------------------------------------------------------
// Singleton Getter (Fixed Missing Symbol)
// -----------------------------------------------------------------------------
Interceptor& Interceptor::Get() {
    static Interceptor instance;
    return instance;
}

bool Interceptor::CreateTexture(
    VkDevice device,
    VkPhysicalDevice physicalDevice,
    uint32_t width,
    uint32_t height,
    VkFormat format,
    VkImageUsageFlags usage,
    VkImage& outImage,
    VkDeviceMemory& outMemory,
    VkImageView& outView
) {
    return false; // Deprecated by inline allocTex lambda
}

void Interceptor::AllocateFrameBuffers(SwapchainData& data) {
    uint32_t w = data.extent.width;
    uint32_t h = data.extent.height;
    uint32_t packedW = (w + 3) / 4;

    auto allocTex = [&](uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage,
                        VkImage& outImg, VkDeviceMemory& outMem, VkImageView& outView) {
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

        if (vkCreateImage(data.device, &imageInfo, nullptr, &outImg) != VK_SUCCESS) {
            std::cerr << "[PB FrameFlux] Failed to create image for format " << format << std::endl;
            return;
        }

        VkMemoryRequirements memReqs{};
        vkGetImageMemoryRequirements(data.device, outImg, &memReqs);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = FindMemoryType(data.memoryProperties, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(data.device, &allocInfo, nullptr, &outMem) != VK_SUCCESS) {
            std::cerr << "[PB FrameFlux] Failed to allocate device memory!" << std::endl;
            return;
        }
        vkBindImageMemory(data.device, outImg, outMem, 0);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = outImg;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;

        vkCreateImageView(data.device, &viewInfo, nullptr, &outView);
    };

    // 1. Frame history (sampled + transfer only, perfectly safe for SRGB on AMD)
    VkImageUsageFlags sampledUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    allocTex(w, h, data.imageFormat, sampledUsage, data.frameAImage, data.frameAMemory, data.frameAView);
    allocTex(w, h, data.imageFormat, sampledUsage, data.frameBImage, data.frameBMemory, data.frameBView);

    // 2. Output generated image (storage + transfer, using standard R8G8B8A8_UNORM)
    VkFormat genFormat = (data.imageFormat == VK_FORMAT_B8G8R8A8_SRGB) ? VK_FORMAT_B8G8R8A8_UNORM : data.imageFormat;
    VkImageUsageFlags storageUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    allocTex(w, h, genFormat, storageUsage, data.generatedImage, data.generatedMemory, data.generatedView);

    // 3. Packed Luminance textures
    allocTex(packedW, h, VK_FORMAT_R32_UINT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, data.lumaAImage, data.lumaAMemory, data.lumaAView);
    allocTex(packedW, h, VK_FORMAT_R32_UINT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, data.lumaBImage, data.lumaBMemory, data.lumaBView);

    // 4. Dummy coarse motion field
    allocTex(1, 1, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, data.dummyCoarseImage, data.dummyCoarseMemory, data.dummyCoarseView);

    // 5. Motion Vectors & Confidence
    allocTex(w, h, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, data.motionImage, data.motionMemory, data.motionView);
    allocTex(w, h, VK_FORMAT_R8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, data.confidenceImage, data.confidenceMemory, data.confidenceView);

    // 6. Bilinear Sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(data.device, &samplerInfo, nullptr, &data.linearSampler);

    // 7. Descriptor Pool
    std::vector<VkDescriptorPoolSize> poolSizes = {
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 10},
        {VK_DESCRIPTOR_TYPE_SAMPLER, 2},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 6}
    };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 4;
    poolInfo.poolSizeCount = (uint32_t)poolSizes.size();
    poolInfo.pPoolSizes = poolSizes.data();
    vkCreateDescriptorPool(data.device, &poolInfo, nullptr, &data.descriptorPool);

    // 8. Descriptor Sets
    VkDescriptorSetAllocateInfo allocSetInfo{};
    allocSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocSetInfo.descriptorPool = data.descriptorPool;
    allocSetInfo.descriptorSetCount = 1;

    VkDescriptorSetLayout lumaLayout = m_computeEngine.GetLumaDescLayout();
    allocSetInfo.pSetLayouts = &lumaLayout;
    vkAllocateDescriptorSets(data.device, &allocSetInfo, &data.lumaDescSet);

    VkDescriptorSetLayout flowLayout = m_computeEngine.GetFlowDescLayout();
    allocSetInfo.pSetLayouts = &flowLayout;
    vkAllocateDescriptorSets(data.device, &allocSetInfo, &data.flowDescSet);

    VkDescriptorSetLayout warpLayout = m_computeEngine.GetWarpDescLayout();
    allocSetInfo.pSetLayouts = &warpLayout;
    vkAllocateDescriptorSets(data.device, &allocSetInfo, &data.warpDescSet);

    // 9. Updates for Luma
    VkDescriptorImageInfo lumaImgs[2]{
        {VK_NULL_HANDLE, data.frameBView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {VK_NULL_HANDLE, data.lumaBView,  VK_IMAGE_LAYOUT_GENERAL}
    };
    VkWriteDescriptorSet lumaWrites[2]{};
    lumaWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    lumaWrites[0].dstSet = data.lumaDescSet;
    lumaWrites[0].dstBinding = 0;
    lumaWrites[0].descriptorCount = 1;
    lumaWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    lumaWrites[0].pImageInfo = &lumaImgs[0];

    lumaWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    lumaWrites[1].dstSet = data.lumaDescSet;
    lumaWrites[1].dstBinding = 1;
    lumaWrites[1].descriptorCount = 1;
    lumaWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    lumaWrites[1].pImageInfo = &lumaImgs[1];
    vkUpdateDescriptorSets(data.device, 2, lumaWrites, 0, nullptr);

    // 10. Updates for Flow
    VkDescriptorImageInfo flowImgs[5]{
        {VK_NULL_HANDLE, data.lumaAView,        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {VK_NULL_HANDLE, data.lumaBView,        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {VK_NULL_HANDLE, data.dummyCoarseView,  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {VK_NULL_HANDLE, data.motionView,       VK_IMAGE_LAYOUT_GENERAL},
        {VK_NULL_HANDLE, data.confidenceView,   VK_IMAGE_LAYOUT_GENERAL}
    };
    VkWriteDescriptorSet flowWrites[5]{};
    for (uint32_t i = 0; i < 5; ++i) {
        flowWrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        flowWrites[i].dstSet = data.flowDescSet;
        flowWrites[i].dstBinding = i;
        flowWrites[i].descriptorCount = 1;
        flowWrites[i].descriptorType = (i >= 3) ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        flowWrites[i].pImageInfo = &flowImgs[i];
    }
    vkUpdateDescriptorSets(data.device, 5, flowWrites, 0, nullptr);

    // 11. Updates for Warp
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
        warpWrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        warpWrites[i].dstSet = data.warpDescSet;
        warpWrites[i].dstBinding = i;
        warpWrites[i].descriptorCount = 1;
        warpWrites[i].descriptorType = (i == 4) ? VK_DESCRIPTOR_TYPE_SAMPLER : (i == 5 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
        warpWrites[i].pImageInfo = &warpImgs[i];
    }
    vkUpdateDescriptorSets(data.device, 6, warpWrites, 0, nullptr);
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
    destroyTex(data.dummyCoarseImage, data.dummyCoarseMemory, data.dummyCoarseView);
    destroyTex(data.motionImage, data.motionMemory, data.motionView);
    destroyTex(data.confidenceImage, data.confidenceMemory, data.confidenceView);
    destroyTex(data.generatedImage, data.generatedMemory, data.generatedView);

    if (data.commandPool != VK_NULL_HANDLE) vkDestroyCommandPool(data.device, data.commandPool, nullptr);
    if (data.timelineSemaphore != VK_NULL_HANDLE) vkDestroySemaphore(data.device, data.timelineSemaphore, nullptr);
    if (data.internalAcquireSemaphore != VK_NULL_HANDLE) vkDestroySemaphore(data.device, data.internalAcquireSemaphore, nullptr);
}

VkResult Interceptor::OnCreateSwapchainKHR(
    VkDevice device,
    const VkPhysicalDeviceMemoryProperties& memProperties,
    const VkSwapchainCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSwapchainKHR* pSwapchain,
    PFN_vkCreateSwapchainKHR realFunc
) {
    // 1. Expand buffer count cleanly
    VkSwapchainCreateInfoKHR modifiedCreateInfo = *pCreateInfo;
    modifiedCreateInfo.minImageCount = std::max(pCreateInfo->minImageCount + 2, 4u);

    VkResult result = realFunc(device, &modifiedCreateInfo, pAllocator, pSwapchain);
    if (result != VK_SUCCESS) {
        result = realFunc(device, pCreateInfo, pAllocator, pSwapchain);
        if (result != VK_SUCCESS) return result;
    }

    m_computeEngine.Initialize(device, VK_NULL_HANDLE);

    auto data = std::make_unique<SwapchainData>();
    data->device = device;
    data->memoryProperties = memProperties;
    data->swapchain = *pSwapchain;
    data->imageFormat = pCreateInfo->imageFormat;
    data->extent = pCreateInfo->imageExtent;
    data->lastPresentTime = std::chrono::high_resolution_clock::now();

    uint32_t imageCount = 0;
    vkGetSwapchainImagesKHR(device, *pSwapchain, &imageCount, nullptr);
    data->realImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device, *pSwapchain, &imageCount, data->realImages.data());

    VkCommandPoolCreateInfo cmdPoolInfo{};
    cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cmdPoolInfo.queueFamilyIndex = 0;
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

    VkSemaphoreTypeCreateInfo timelineCreateInfo{};
    timelineCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    timelineCreateInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timelineCreateInfo.initialValue = 0;

    VkSemaphoreCreateInfo semCreateInfo{};
    semCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semCreateInfo.pNext = &timelineCreateInfo;
    vkCreateSemaphore(device, &semCreateInfo, nullptr, &data->timelineSemaphore);

    VkSemaphoreCreateInfo acqSemInfo{};
    acqSemInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    vkCreateSemaphore(device, &acqSemInfo, nullptr, &data->internalAcquireSemaphore);

    AllocateFrameBuffers(*data);

    std::cout << "[PB FrameFlux] Swapchain created with " << modifiedCreateInfo.minImageCount << " image buffers." << std::endl;
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
    float rawDeltaMs = std::chrono::duration<float, std::milli>(now - data.lastPresentTime).count();
    data.lastPresentTime = now;

    constexpr float alpha = 0.15f;
    data.smoothedFrametimeMs = alpha * rawDeltaMs + (1.0f - alpha) * data.smoothedFrametimeMs;
    outNormalizedT = 0.5f;
}

void Interceptor::DispatchGenerationPass(SwapchainData& data, VkQueue queue, uint32_t imageIndex, float t) {
    VkCommandBuffer cmd = data.genCommandBuffer;
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    uint32_t w = data.extent.width;
    uint32_t h = data.extent.height;

    VkImageCopy fullCopyRegion{};
    fullCopyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    fullCopyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    fullCopyRegion.extent = {w, h, 1};

    // 1. Capture incoming game frame -> frameBImage
    TransitionImage(cmd, data.realImages[imageIndex], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    TransitionImage(cmd, data.frameBImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT);

    vkCmdCopyImage(cmd, data.realImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   data.frameBImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &fullCopyRegion);

    // 2. Luma Pack
    TransitionImage(cmd, data.frameBImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    TransitionImage(cmd, data.lumaBImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_ACCESS_SHADER_WRITE_BIT);
    m_computeEngine.RecordLumaPass(cmd, data.lumaDescSet, w, h);

    // 3. Optical Flow Search
    TransitionImage(cmd, data.lumaBImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    TransitionImage(cmd, data.lumaAImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_ACCESS_SHADER_READ_BIT);
    TransitionImage(cmd, data.dummyCoarseImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_ACCESS_SHADER_READ_BIT);
    TransitionImage(cmd, data.motionImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_ACCESS_SHADER_WRITE_BIT);
    TransitionImage(cmd, data.confidenceImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_ACCESS_SHADER_WRITE_BIT);
    m_computeEngine.RecordFlowPass(cmd, data.flowDescSet, w, h);

    // 4. Warp Interpolation
    TransitionImage(cmd, data.motionImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    TransitionImage(cmd, data.confidenceImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    TransitionImage(cmd, data.frameAImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_ACCESS_SHADER_READ_BIT);
    TransitionImage(cmd, data.generatedImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_ACCESS_SHADER_WRITE_BIT);

    WarpPushConstants pc{};
    pc.t = t;
    pc.confidenceThreshold = 2.0f;
    pc.fallbackAction = 1;
    pc.searchMode = 1;
    pc.resolutionX = w;
    pc.resolutionY = h;
    pc.invResolutionX = 1.0f / static_cast<float>(w);
    pc.invResolutionY = 1.0f / static_cast<float>(h);
    m_computeEngine.RecordWarpPass(cmd, data.warpDescSet, pc, w, h);

    // 5. Overwrite realImages[imageIndex] with Generated Frame G
    TransitionImage(cmd, data.generatedImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    TransitionImage(cmd, data.realImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);

    vkCmdCopyImage(cmd, data.generatedImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   data.realImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &fullCopyRegion);

    TransitionImage(cmd, data.realImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, 0);

    // 6. History advance: frameB -> frameA, lumaB -> lumaA
    TransitionImage(cmd, data.frameBImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    TransitionImage(cmd, data.frameAImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    vkCmdCopyImage(cmd, data.frameBImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   data.frameAImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &fullCopyRegion);

    VkImageCopy lumaCopyRegion{};
    lumaCopyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    lumaCopyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    lumaCopyRegion.extent = {(w + 3) / 4, h, 1};

    TransitionImage(cmd, data.lumaBImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    TransitionImage(cmd, data.lumaAImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    vkCmdCopyImage(cmd, data.lumaBImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   data.lumaAImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &lumaCopyRegion);

    vkEndCommandBuffer(cmd);

// 11. Submit work to GPU queue and WAIT for completion before presenting
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);

    vkQueueWaitIdle(queue);
}

void Interceptor::PresentRealFrame(SwapchainData& data, VkQueue queue, uint32_t imageIndex) {
    VkCommandBuffer cmd = data.realCommandBuffer;
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    uint32_t w = data.extent.width;
    uint32_t h = data.extent.height;

    VkImageCopy copyRegion{};
    copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copyRegion.extent = {w, h, 1};

    TransitionImage(cmd, data.realImages[imageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
    TransitionImage(cmd, data.frameBImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT);

    vkCmdCopyImage(cmd, data.frameBImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   data.realImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

    TransitionImage(cmd, data.realImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, 0);

    vkEndCommandBuffer(cmd);

VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &data.internalAcquireSemaphore;
    submitInfo.pWaitDstStageMask = &waitStage;
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
    for (uint32_t i = 0; i < pPresentInfo->swapchainCount; ++i) {
        VkSwapchainKHR swapchain = pPresentInfo->pSwapchains[i];
        auto it = m_swapchains.find(swapchain);

        if (it != m_swapchains.end()) {
            SwapchainData& data = *(it->second);
            data.frameCounter++;

            if (data.frameCounter > 2) {
                float t = 0.5f;
                ComputeAdaptiveTiming(data, t);

                // 1. Generate frame G into realImages[imageIndex]
                DispatchGenerationPass(data, queue, pPresentInfo->pImageIndices[i], t);

                // 2. Present #1: Generated Frame G
                VkPresentInfoKHR presentG = *pPresentInfo;
                presentG.swapchainCount = 1;
                presentG.pSwapchains = &data.swapchain;
                presentG.pImageIndices = &pPresentInfo->pImageIndices[i];
                realFunc(queue, &presentG);

                // 3. Acquire next available buffer for Real Frame B
                uint32_t secondImageIndex = 0;
                VkResult acqRes = vkAcquireNextImageKHR(
                    data.device, data.swapchain, 50000000ULL,
                    data.internalAcquireSemaphore, VK_NULL_HANDLE, &secondImageIndex
                );

                if (acqRes == VK_SUCCESS || acqRes == VK_SUBOPTIMAL_KHR) {
                    // 4. Blit real frame B into the second buffer
                    PresentRealFrame(data, queue, secondImageIndex);

                    // 5. Present #2: Real Frame B
                    VkPresentInfoKHR presentB{};
                    presentB.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
                    presentB.swapchainCount = 1;
                    presentB.pSwapchains = &data.swapchain;
                    presentB.pImageIndices = &secondImageIndex;
                    return realFunc(queue, &presentB);
                }

                return VK_SUCCESS;
            }
        }
    }

    return realFunc(queue, pPresentInfo);
}

} // namespace FrameFlux