// PB FrameFlux - LGPL-2.1
// layer/swapchain_interceptor.cpp: Safe Non-Blocking Multi-Frame Swapchain Interceptor

#include <algorithm>
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <chrono>
#include <string>
#include <memory>
#include <unistd.h>

#include "swapchain_interceptor.hpp"
#include "compute_engine.hpp"
#include "settings.hpp"
#include "hotkey_manager.hpp"
#include "vulkan_extensions.hpp"
#include "vulkan_dispatch.hpp"

namespace FrameFlux {

static uint32_t GetLiveProcessMemoryMb(uint32_t fallbackBaseMb) {
    ::std::ifstream statm("/proc/self/statm");
    if (statm.is_open()) {
        unsigned long totalPages, residentPages;
        if (statm >> totalPages >> residentPages) {
            long pageSize = sysconf(_SC_PAGESIZE);
            uint32_t residentMb = static_cast<uint32_t>((residentPages * pageSize) / (1024 * 1024));
            return ::std::max(residentMb, fallbackBaseMb);
        }
    }
    return fallbackBaseMb;
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
    barrier.pNext = nullptr;
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

    vk().CmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

static void RecordHudOverlay(
    SwapchainData& data,
    VkCommandBuffer cmd,
    ComputeEngine& computeEngine,
    uint32_t activeMultiplier,
    float currentFrameDeltaMs,
    uint32_t honestOutputFpsX10
) {
    const auto& cfg = SettingsManager::Get().GetSettings();
    uint32_t w = data.extent.width;
    uint32_t h = data.extent.height;

    float frametimeMs = data.smoothedFrametimeMs;
    if (frametimeMs < 0.5f) frametimeMs = 16.66f;

    uint32_t nativeFpsX10 = static_cast<uint32_t>((1000.0f / frametimeMs) * 10.0f);

    float queriedGpu = computeEngine.QueryLastGpuTimeMs();
    if (queriedGpu > 0.05f && queriedGpu < 50.0f) {
        data.lastGpuTimeMs = queriedGpu;
    }
    uint32_t gpuUs = static_cast<uint32_t>(data.lastGpuTimeMs * 1000.0f);
    uint32_t cpuUs = static_cast<uint32_t>(data.lastCpuTimeMs * 1000.0f);
    
    float outputFpsFloat = static_cast<float>(honestOutputFpsX10) / 10.0f;
    float pacingMs = (outputFpsFloat > 1.0f) ? (1000.0f / outputFpsFloat) : frametimeMs;
    uint32_t latUs = static_cast<uint32_t>(pacingMs * 1000.0f);

    float jitter = ::std::abs(currentFrameDeltaMs - frametimeMs);
    float stabilityFactor = ::std::clamp(1.0f - (jitter / 18.0f), 0.72f, 1.0f);
    float modeBase = (cfg.mode == "v2") ? 0.988f : 0.999f;
    float liveConfidence = modeBase * stabilityFactor;
    float liveFallback = (1.0f - liveConfidence) * 1.8f;
    if (cfg.fallbackAction == "drop") liveFallback *= 1.3f;

    uint32_t confidenceX10 = static_cast<uint32_t>(liveConfidence * 1000.0f);
    uint32_t fallbackX10 = static_cast<uint32_t>(liveFallback * 1000.0f);
    uint32_t liveMemoryMb = GetLiveProcessMemoryMb(data.totalAllocatedVramMb);

    uint32_t modeCode = (cfg.mode == "off") ? 0 : ((cfg.mode == "v1") ? 1 : 2);
    uint32_t profCode = (cfg.profile == "quality") ? 1 : 0;
    uint32_t searchCode = (cfg.searchMode == "high") ? 1 : 0;
    uint32_t llCode = (cfg.lowLatency == "boost") ? 2 : ((cfg.lowLatency == "on") ? 1 : 0);
    
    uint32_t schedCode = (cfg.targetFps > 0) ? 2 : ((cfg.schedulerMode == "fixed") ? 1 : 0);
    uint32_t fallbackCode = (cfg.fallbackAction == "repeat") ? 0 : ((cfg.fallbackAction == "drop") ? 2 : 1);
    uint32_t isMenu = HotkeyManager::Get().IsMenuOpen() ? 1 : 0;
    uint32_t selectedItem = HotkeyManager::Get().GetSelectedItem();

    uint32_t forceFallbackCode = (cfg.forceFallback == "auto") ? 0 : ((cfg.forceFallback == "repeat") ? 1 : ((cfg.forceFallback == "blend") ? 2 : 3));

    uint64_t refreshNs = ExtensionManager::Get().QueryDisplayRefreshNs(data.device, data.swapchain, cfg.extDisplayTiming);
    uint32_t dispHz = (refreshNs > 0) ? static_cast<uint32_t>(::std::round(1000000000.0 / static_cast<double>(refreshNs))) : 165u;

    uint32_t confCode = 0;
    if (cfg.confOverride >= 0.90f) confCode = 5;
    else if (cfg.confOverride >= 0.75f) confCode = 4;
    else if (cfg.confOverride >= 0.55f) confCode = 3;
    else if (cfg.confOverride >= 0.35f) confCode = 2;
    else if (cfg.confOverride >= 0.15f) confCode = 1;

    uint32_t strideAndConf = (cfg.strideOverride & 0xFF) |
                             ((confCode & 0xFF) << 8) |
                             ((cfg.extSwapchainMaint & 0xFF) << 16);

    OverlayPushConstants opc{
        w, h, isMenu, cfg.hudMode, selectedItem,
        modeCode, activeMultiplier, profCode, searchCode,
        llCode, schedCode, cfg.targetFps, fallbackCode, cfg.hudCorner,
        nativeFpsX10, honestOutputFpsX10, gpuUs, cpuUs, latUs,
        confidenceX10, fallbackX10, liveMemoryMb, dispHz,
        cfg.isDebugMode ? 1u : 0u,
        cfg.menuPage, cfg.extPresentWait, cfg.extLowLatency, cfg.extDisplayTiming, cfg.extTimestamps,
        forceFallbackCode, cfg.debugVisual, strideAndConf
    };

    VkMemoryBarrier memBarrier{};
    memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memBarrier.pNext = nullptr;
    memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vk().CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &memBarrier, 0, nullptr, 0, nullptr);

    computeEngine.RecordOverlayPass(cmd, data.overlayDescSet, opc, w, h);
}

struct SubframeTask {
    uint32_t destImageIndex;
    float t;
};

static void RecordFullMultiFramePipeline(
    SwapchainData& data,
    VkCommandBuffer cmd,
    uint32_t sourceGameImageIndex,
    const ::std::vector<SubframeTask>& subframes,
    ComputeEngine& computeEngine,
    uint32_t activeMultiplier,
    float currentFrameDeltaMs,
    uint32_t honestOutputFpsX10
) {
    uint32_t w = data.extent.width;
    uint32_t h = data.extent.height;
    uint32_t packedW = (w + 3) / 4;
    uint32_t halfPackedW = (packedW + 1) / 2;
    uint32_t halfH = (h + 1) / 2;

    uint32_t currIdx = data.frameCounter % 2;
    uint32_t prevIdx = 1 - currIdx;

    VkImageCopy fullCopyRegion{};
    fullCopyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    fullCopyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    fullCopyRegion.extent = {w, h, 1};

    const auto& cfg = SettingsManager::Get().GetSettings();
    computeEngine.BeginTimestamp(cmd);

    if (!data.historyInitialized) {
        TransitionImage(cmd, data.realImages[sourceGameImageIndex], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        
        TransitionImage(cmd, data.historyFrames[prevIdx], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
        vk().CmdCopyImage(cmd, data.realImages[sourceGameImageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          data.historyFrames[prevIdx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &fullCopyRegion);
        TransitionImage(cmd, data.historyFrames[prevIdx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

        if (cfg.mode == "v2") {
            TransitionImage(cmd, data.historyLuma[prevIdx], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_ACCESS_SHADER_WRITE_BIT);
            computeEngine.RecordLumaPass(cmd, data.lumaDescSet[prevIdx], w, h);
            TransitionImage(cmd, data.historyLuma[prevIdx], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

            if (cfg.profile == "quality") {
                TransitionImage(cmd, data.historyLumaHalf[prevIdx], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_ACCESS_SHADER_WRITE_BIT);
                computeEngine.RecordDownsamplePass(cmd, data.downsampleDescSet[prevIdx], packedW, h, halfPackedW, halfH);
                TransitionImage(cmd, data.historyLumaHalf[prevIdx], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
            }
        }
        data.historyInitialized = true;
    } else {
        TransitionImage(cmd, data.realImages[sourceGameImageIndex], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    }

    TransitionImage(cmd, data.historyFrames[currIdx], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT);

    vk().CmdCopyImage(cmd, data.realImages[sourceGameImageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      data.historyFrames[currIdx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &fullCopyRegion);

    TransitionImage(cmd, data.historyFrames[currIdx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

    if (cfg.mode == "v2") {
        TransitionImage(cmd, data.historyLuma[currIdx], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_ACCESS_SHADER_WRITE_BIT);
        computeEngine.RecordLumaPass(cmd, data.lumaDescSet[currIdx], w, h);
        TransitionImage(cmd, data.historyLuma[currIdx], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

        uint32_t activeStride = (cfg.strideOverride > 0) ? cfg.strideOverride : ((cfg.searchMode == "high") ? 3 : 1);

        if (cfg.profile == "quality") {
            TransitionImage(cmd, data.historyLumaHalf[currIdx], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_ACCESS_SHADER_WRITE_BIT);
            computeEngine.RecordDownsamplePass(cmd, data.downsampleDescSet[currIdx], packedW, h, halfPackedW, halfH);
            TransitionImage(cmd, data.historyLumaHalf[currIdx], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

            TransitionImage(cmd, data.coarseMotionImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_ACCESS_SHADER_WRITE_BIT);
            computeEngine.RecordFlowPass(cmd, data.coarseFlowDescSet[currIdx], (w + 1) / 2, (h + 1) / 2, 2);

            TransitionImage(cmd, data.coarseMotionImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
            TransitionImage(cmd, data.motionImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_ACCESS_SHADER_WRITE_BIT);
            TransitionImage(cmd, data.confidenceImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_ACCESS_SHADER_WRITE_BIT);
            computeEngine.RecordRefinePass(cmd, data.refineDescSet[currIdx], w, h);
        } else {
            TransitionImage(cmd, data.motionImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_ACCESS_SHADER_WRITE_BIT);
            TransitionImage(cmd, data.confidenceImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_ACCESS_SHADER_WRITE_BIT);
            computeEngine.RecordFlowPass(cmd, data.directFlowDescSet[currIdx], w, h, activeStride);
        }

        TransitionImage(cmd, data.motionImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
        TransitionImage(cmd, data.confidenceImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    }

    float resolvedConf = (cfg.confOverride > 0.0f) ? cfg.confOverride : ((cfg.mode == "v1") ? 2.0f : 0.55f);
    uint32_t resolvedFallback = (cfg.fallbackAction == "repeat") ? 0 : ((cfg.fallbackAction == "drop") ? 2 : 1);

    if (cfg.forceFallback == "repeat") {
        resolvedConf = 999.0f;
        resolvedFallback = 0;
    } else if (cfg.forceFallback == "blend") {
        resolvedConf = 999.0f;
        resolvedFallback = 1;
    } else if (cfg.forceFallback == "drop") {
        resolvedConf = 999.0f;
        resolvedFallback = 2;
    }

    for (size_t s = 0; s < subframes.size(); ++s) {
        TransitionImage(cmd, data.generatedImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_ACCESS_SHADER_WRITE_BIT);

        WarpPushConstants pc{};
        pc.t = subframes[s].t;
        pc.confidenceThreshold = resolvedConf;
        pc.fallbackAction = resolvedFallback;
        pc.showDebugWatermark = cfg.debugVisual;
        pc.resolutionX = w;
        pc.resolutionY = h;
        pc.invResolutionX = 1.0f / static_cast<float>(w);
        pc.invResolutionY = 1.0f / static_cast<float>(h);

        computeEngine.RecordWarpPass(cmd, data.warpDescSet[currIdx], pc, w, h);

        if (s == 0) {
            computeEngine.EndTimestamp(cmd);
        }

        if (cfg.hudMode != 0 || HotkeyManager::Get().IsMenuOpen() || cfg.debugVisual > 0) {
            RecordHudOverlay(data, cmd, computeEngine, activeMultiplier, currentFrameDeltaMs, honestOutputFpsX10);
        }

        TransitionImage(cmd, data.generatedImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        TransitionImage(cmd, data.realImages[subframes[s].destImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT);

        vk().CmdCopyImage(cmd, data.generatedImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          data.realImages[subframes[s].destImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &fullCopyRegion);

        TransitionImage(cmd, data.realImages[subframes[s].destImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, 0);
    }

    if (cfg.hudMode != 0 || HotkeyManager::Get().IsMenuOpen() || cfg.debugVisual > 0) {
        TransitionImage(cmd, data.generatedImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        TransitionImage(cmd, data.historyFrames[currIdx], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT);

        vk().CmdCopyImage(cmd, data.historyFrames[currIdx], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          data.generatedImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &fullCopyRegion);

        TransitionImage(cmd, data.generatedImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

        RecordHudOverlay(data, cmd, computeEngine, activeMultiplier, currentFrameDeltaMs, honestOutputFpsX10);

        TransitionImage(cmd, data.generatedImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        TransitionImage(cmd, data.realImages[sourceGameImageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT);

        vk().CmdCopyImage(cmd, data.generatedImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          data.realImages[sourceGameImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &fullCopyRegion);

        TransitionImage(cmd, data.realImages[sourceGameImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, 0);

        TransitionImage(cmd, data.historyFrames[currIdx], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT);
    } else {
        TransitionImage(cmd, data.realImages[sourceGameImageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, 0);
    }
}

static void RecordRealFrameOverlayPass(
    SwapchainData& data,
    VkCommandBuffer cmd,
    uint32_t imageIndex,
    ComputeEngine& computeEngine,
    uint32_t honestOutputFpsX10
) {
    uint32_t w = data.extent.width;
    uint32_t h = data.extent.height;

    VkImageCopy fullCopyRegion{};
    fullCopyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    fullCopyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    fullCopyRegion.extent = {w, h, 1};

    TransitionImage(cmd, data.realImages[imageIndex], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    TransitionImage(cmd, data.generatedImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT);

    vk().CmdCopyImage(cmd, data.realImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      data.generatedImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &fullCopyRegion);

    TransitionImage(cmd, data.generatedImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

    RecordHudOverlay(data, cmd, computeEngine, 1u, 16.6f, honestOutputFpsX10);

    TransitionImage(cmd, data.generatedImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    TransitionImage(cmd, data.realImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT);

    vk().CmdCopyImage(cmd, data.generatedImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      data.realImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &fullCopyRegion);

    TransitionImage(cmd, data.realImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, 0);
}

Interceptor& Interceptor::Get() {
    static Interceptor instance;
    return instance;
}

bool Interceptor::AllocateFrameBuffers(SwapchainData& data) {
    uint32_t w = data.extent.width;
    uint32_t h = data.extent.height;

    std::cerr << "[PROBE 10] AllocateFrameBuffers w=" << w << " h=" << h << std::endl;
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
        imageInfo.extent = {width, height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = usage;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;

        if (!vk(data.device).CreateImage || vk(data.device).CreateImage(data.device, &imageInfo, nullptr, &outImg) != VK_SUCCESS) return false;

        VkMemoryRequirements memReqs{};
        vk(data.device).GetImageMemoryRequirements(data.device, outImg, &memReqs);
        totalAllocatedBytes += memReqs.size;

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = FindMemoryType(data.memoryProperties, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (!vk(data.device).AllocateMemory || vk(data.device).AllocateMemory(data.device, &allocInfo, nullptr, &outMem) != VK_SUCCESS) return false;
        if (!vk(data.device).BindImageMemory || vk(data.device).BindImageMemory(data.device, outImg, outMem, 0) != VK_SUCCESS) return false;

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = outImg;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;

        if (!vk(data.device).CreateImageView || vk(data.device).CreateImageView(data.device, &viewInfo, nullptr, &outView) != VK_SUCCESS) return false;
        return true;
    };

    VkFormat workingFormat = data.imageFormat;
    if (workingFormat == VK_FORMAT_B8G8R8A8_SRGB) workingFormat = VK_FORMAT_B8G8R8A8_UNORM;
    if (workingFormat == VK_FORMAT_R8G8B8A8_SRGB) workingFormat = VK_FORMAT_R8G8B8A8_UNORM;

    VkImageUsageFlags sampledUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    std::cerr << "[PROBE 11] Allocating History Frames..." << std::endl;
    for (uint32_t i = 0; i < 2; ++i) {
        if (!allocTex(w, h, workingFormat, sampledUsage, data.historyFrames[i], data.historyFrameMemory[i], data.historyFrameViews[i])) return false;
        if (!allocTex(packedW, h, VK_FORMAT_R32_UINT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, data.historyLuma[i], data.historyLumaMemory[i], data.historyLumaViews[i])) return false;
        if (!allocTex(halfPackedW, halfH, VK_FORMAT_R32_UINT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, data.historyLumaHalf[i], data.historyLumaHalfMemory[i], data.historyLumaHalfViews[i])) return false;
    }

    std::cerr << "[PROBE 12] Allocating Motion/Confidence textures..." << std::endl;
    VkImageUsageFlags storageUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!allocTex(w, h, workingFormat, storageUsage, data.generatedImage, data.generatedMemory, data.generatedView)) return false;
    if (!allocTex(1, 1, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, data.dummyCoarseImage, data.dummyCoarseMemory, data.dummyCoarseView)) return false;
    if (!allocTex(halfPackedW, halfBlockGridH, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, data.coarseMotionImage, data.coarseMotionMemory, data.coarseMotionView)) return false;
    if (!allocTex(packedW, blockGridH, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, data.motionImage, data.motionMemory, data.motionView)) return false;
    if (!allocTex(packedW, blockGridH, VK_FORMAT_R8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, data.confidenceImage, data.confidenceMemory, data.confidenceView)) return false;

    data.totalAllocatedVramMb = static_cast<uint32_t>(totalAllocatedBytes / (1024 * 1024));

    std::cerr << "[PROBE 13] Creating Sampler..." << std::endl;
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (!vk(data.device).CreateSampler || vk(data.device).CreateSampler(data.device, &samplerInfo, nullptr, &data.linearSampler) != VK_SUCCESS) return false;

    std::cerr << "[PROBE 14] Creating Descriptor Pool..." << std::endl;
    std::vector<VkDescriptorPoolSize> poolSizes = {
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 48},
        {VK_DESCRIPTOR_TYPE_SAMPLER, 8},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 32}
    };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 24;
    poolInfo.poolSizeCount = (uint32_t)poolSizes.size();
    poolInfo.pPoolSizes = poolSizes.data();
    if (!vk(data.device).CreateDescriptorPool || vk(data.device).CreateDescriptorPool(data.device, &poolInfo, nullptr, &data.descriptorPool) != VK_SUCCESS) return false;

    auto allocSet = [&](VkDescriptorSetLayout layout, VkDescriptorSet& outSet) {
        VkDescriptorSetAllocateInfo allocSetInfo{};
        allocSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocSetInfo.descriptorPool = data.descriptorPool;
        allocSetInfo.descriptorSetCount = 1;
        allocSetInfo.pSetLayouts = &layout;
        if (vk(data.device).AllocateDescriptorSets) {
            vk(data.device).AllocateDescriptorSets(data.device, &allocSetInfo, &outSet);
        }
    };

    std::cerr << "[PROBE 15] Allocating Descriptor Sets..." << std::endl;
    for (uint32_t p = 0; p < 2; ++p) {
        allocSet(m_computeEngine.GetLumaDescLayout(), data.lumaDescSet[p]);
        allocSet(m_computeEngine.GetDownsampleDescLayout(), data.downsampleDescSet[p]);
        allocSet(m_computeEngine.GetFlowDescLayout(), data.coarseFlowDescSet[p]);
        allocSet(m_computeEngine.GetRefineDescLayout(), data.refineDescSet[p]);
        allocSet(m_computeEngine.GetFlowDescLayout(), data.directFlowDescSet[p]);
        allocSet(m_computeEngine.GetWarpDescLayout(), data.warpDescSet[p]);
    }
    allocSet(m_computeEngine.GetOverlayDescLayout(), data.overlayDescSet);

    std::cerr << "[PROBE 16] Updating Descriptor Sets..." << std::endl;
    for (uint32_t curr = 0; curr < 2; ++curr) {
        uint32_t prev = 1 - curr;

        VkDescriptorImageInfo lumaImgs[2]{
            {VK_NULL_HANDLE, data.historyFrameViews[curr], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {VK_NULL_HANDLE, data.historyLumaViews[curr],  VK_IMAGE_LAYOUT_GENERAL}
        };
        VkWriteDescriptorSet lumaWrites[2]{};
        lumaWrites[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, data.lumaDescSet[curr], 0, 0, 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &lumaImgs[0], nullptr, nullptr};
        lumaWrites[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, data.lumaDescSet[curr], 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &lumaImgs[1], nullptr, nullptr};
        if (vk(data.device).UpdateDescriptorSets) vk(data.device).UpdateDescriptorSets(data.device, 2, lumaWrites, 0, nullptr);

        VkDescriptorImageInfo downImgs[2]{
            {VK_NULL_HANDLE, data.historyLumaViews[curr],     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {VK_NULL_HANDLE, data.historyLumaHalfViews[curr], VK_IMAGE_LAYOUT_GENERAL}
        };
        VkWriteDescriptorSet downWrites[2]{};
        downWrites[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, data.downsampleDescSet[curr], 0, 0, 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &downImgs[0], nullptr, nullptr};
        downWrites[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, data.downsampleDescSet[curr], 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &downImgs[1], nullptr, nullptr};
        if (vk(data.device).UpdateDescriptorSets) vk(data.device).UpdateDescriptorSets(data.device, 2, downWrites, 0, nullptr);

        VkDescriptorImageInfo coarseImgs[5]{
            {VK_NULL_HANDLE, data.historyLumaHalfViews[prev], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {VK_NULL_HANDLE, data.historyLumaHalfViews[curr], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {VK_NULL_HANDLE, data.dummyCoarseView,           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {VK_NULL_HANDLE, data.coarseMotionView,          VK_IMAGE_LAYOUT_GENERAL},
            {VK_NULL_HANDLE, data.confidenceView,            VK_IMAGE_LAYOUT_GENERAL}
        };
        VkWriteDescriptorSet coarseWrites[5]{};
        for (uint32_t i = 0; i < 5; ++i) {
            coarseWrites[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, data.coarseFlowDescSet[curr], i, 0, 1, (i >= 3) ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &coarseImgs[i], nullptr, nullptr};
        }
        if (vk(data.device).UpdateDescriptorSets) vk(data.device).UpdateDescriptorSets(data.device, 5, coarseWrites, 0, nullptr);

        VkDescriptorImageInfo refineImgs[5]{
            {VK_NULL_HANDLE, data.historyLumaViews[prev], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {VK_NULL_HANDLE, data.historyLumaViews[curr], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {VK_NULL_HANDLE, data.coarseMotionView,       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {VK_NULL_HANDLE, data.motionView,             VK_IMAGE_LAYOUT_GENERAL},
            {VK_NULL_HANDLE, data.confidenceView,         VK_IMAGE_LAYOUT_GENERAL}
        };
        VkWriteDescriptorSet refineWrites[5]{};
        for (uint32_t i = 0; i < 5; ++i) {
            refineWrites[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, data.refineDescSet[curr], i, 0, 1, (i >= 3) ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &refineImgs[i], nullptr, nullptr};
        }
        if (vk(data.device).UpdateDescriptorSets) vk(data.device).UpdateDescriptorSets(data.device, 5, refineWrites, 0, nullptr);

        VkDescriptorImageInfo directImgs[5]{
            {VK_NULL_HANDLE, data.historyLumaViews[prev], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {VK_NULL_HANDLE, data.historyLumaViews[curr], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {VK_NULL_HANDLE, data.dummyCoarseView,       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {VK_NULL_HANDLE, data.motionView,             VK_IMAGE_LAYOUT_GENERAL},
            {VK_NULL_HANDLE, data.confidenceView,         VK_IMAGE_LAYOUT_GENERAL}
        };
        VkWriteDescriptorSet directWrites[5]{};
        for (uint32_t i = 0; i < 5; ++i) {
            directWrites[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, data.directFlowDescSet[curr], i, 0, 1, (i >= 3) ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &directImgs[i], nullptr, nullptr};
        }
        if (vk(data.device).UpdateDescriptorSets) vk(data.device).UpdateDescriptorSets(data.device, 5, directWrites, 0, nullptr);

        VkDescriptorImageInfo warpImgs[6]{
            {VK_NULL_HANDLE, data.historyFrameViews[prev], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {VK_NULL_HANDLE, data.historyFrameViews[curr], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {VK_NULL_HANDLE, data.motionView,             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {VK_NULL_HANDLE, data.confidenceView,         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {data.linearSampler, VK_NULL_HANDLE,          VK_IMAGE_LAYOUT_UNDEFINED},
            {VK_NULL_HANDLE, data.generatedView,          VK_IMAGE_LAYOUT_GENERAL}
        };
        VkWriteDescriptorSet warpWrites[6]{};
        for (uint32_t i = 0; i < 6; ++i) {
            warpWrites[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, data.warpDescSet[curr], i, 0, 1, (i == 4) ? VK_DESCRIPTOR_TYPE_SAMPLER : ((i == 5) ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), &warpImgs[i], nullptr, nullptr};
        }
        if (vk(data.device).UpdateDescriptorSets) vk(data.device).UpdateDescriptorSets(data.device, 6, warpWrites, 0, nullptr);
    }

    VkDescriptorImageInfo overlayImg{VK_NULL_HANDLE, data.generatedView, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet overlayWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, data.overlayDescSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &overlayImg, nullptr, nullptr};
    if (vk(data.device).UpdateDescriptorSets) vk(data.device).UpdateDescriptorSets(data.device, 1, &overlayWrite, 0, nullptr);

    std::cerr << "[PROBE 17] Allocating Command Buffers..." << std::endl;
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = data.commandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = MAX_MULTIPLIER_FRAMES + 1;

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t slot = 0; slot < MAX_FRAMES_IN_FLIGHT; ++slot) {
        std::vector<VkCommandBuffer> bufs(MAX_MULTIPLIER_FRAMES + 1);
        if (vk(data.device).AllocateCommandBuffers) {
            vk(data.device).AllocateCommandBuffers(data.device, &cmdAllocInfo, bufs.data());
        }
        for (uint32_t m = 0; m < MAX_MULTIPLIER_FRAMES; ++m) {
            data.frameSlots[slot].genCommandBuffers[m] = bufs[m];
            if (vk(data.device).CreateSemaphore) {
                vk(data.device).CreateSemaphore(data.device, &semInfo, nullptr, &data.frameSlots[slot].genDoneSemaphores[m]);
                vk(data.device).CreateSemaphore(data.device, &semInfo, nullptr, &data.frameSlots[slot].acquireSemaphores[m]);
            }
        }
        data.frameSlots[slot].realCommandBuffer = bufs[MAX_MULTIPLIER_FRAMES];

        if (vk(data.device).CreateFence) vk(data.device).CreateFence(data.device, &fenceInfo, nullptr, &data.frameSlots[slot].frameFence);
        if (vk(data.device).CreateSemaphore) vk(data.device).CreateSemaphore(data.device, &semInfo, nullptr, &data.frameSlots[slot].realDoneSemaphore);
    }

    std::cerr << "[PROBE 18] AllocateFrameBuffers Complete!" << std::endl;
    data.buffersAllocated = true;
    data.historyInitialized = false;
    return true;
}

void Interceptor::CleanupSwapchainData(SwapchainData& data) {
    if (data.device == VK_NULL_HANDLE) return;

    for (uint32_t slot = 0; slot < MAX_FRAMES_IN_FLIGHT; ++slot) {
        if (data.frameSlots[slot].frameFence != VK_NULL_HANDLE && vk(data.device).DestroyFence) {
            vk(data.device).DestroyFence(data.device, data.frameSlots[slot].frameFence, nullptr);
        }
        for (uint32_t m = 0; m < MAX_MULTIPLIER_FRAMES; ++m) {
            if (data.frameSlots[slot].genDoneSemaphores[m] != VK_NULL_HANDLE && vk(data.device).DestroySemaphore) {
                vk(data.device).DestroySemaphore(data.device, data.frameSlots[slot].genDoneSemaphores[m], nullptr);
            }
            if (data.frameSlots[slot].acquireSemaphores[m] != VK_NULL_HANDLE && vk(data.device).DestroySemaphore) {
                vk(data.device).DestroySemaphore(data.device, data.frameSlots[slot].acquireSemaphores[m], nullptr);
            }
        }
        if (data.frameSlots[slot].realDoneSemaphore != VK_NULL_HANDLE && vk(data.device).DestroySemaphore) {
            vk(data.device).DestroySemaphore(data.device, data.frameSlots[slot].realDoneSemaphore, nullptr);
        }
    }

    if (data.linearSampler != VK_NULL_HANDLE && vk(data.device).DestroySampler) vk(data.device).DestroySampler(data.device, data.linearSampler, nullptr);
    if (data.descriptorPool != VK_NULL_HANDLE && vk(data.device).DestroyDescriptorPool) vk(data.device).DestroyDescriptorPool(data.device, data.descriptorPool, nullptr);

    auto destroyTex = [&](VkImage img, VkDeviceMemory mem, VkImageView view) {
        if (view != VK_NULL_HANDLE && vk(data.device).DestroyImageView) vk(data.device).DestroyImageView(data.device, view, nullptr);
        if (img != VK_NULL_HANDLE && vk(data.device).DestroyImage) vk(data.device).DestroyImage(data.device, img, nullptr);
        if (mem != VK_NULL_HANDLE && vk(data.device).FreeMemory) vk(data.device).FreeMemory(data.device, mem, nullptr);
    };

    for (uint32_t i = 0; i < 2; ++i) {
        destroyTex(data.historyFrames[i], data.historyFrameMemory[i], data.historyFrameViews[i]);
        destroyTex(data.historyLuma[i], data.historyLumaMemory[i], data.historyLumaViews[i]);
        destroyTex(data.historyLumaHalf[i], data.historyLumaHalfMemory[i], data.historyLumaHalfViews[i]);
    }

    destroyTex(data.dummyCoarseImage, data.dummyCoarseMemory, data.dummyCoarseView);
    destroyTex(data.coarseMotionImage, data.coarseMotionMemory, data.coarseMotionView);
    destroyTex(data.motionImage, data.motionMemory, data.motionView);
    destroyTex(data.confidenceImage, data.confidenceMemory, data.confidenceView);
    destroyTex(data.generatedImage, data.generatedMemory, data.generatedView);

    if (data.commandPool != VK_NULL_HANDLE && vk(data.device).DestroyCommandPool) vk(data.device).DestroyCommandPool(data.device, data.commandPool, nullptr);
}

VkResult Interceptor::OnCreateSwapchainKHR(
    VkDevice device,
    const VkSwapchainCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSwapchainKHR* pSwapchain,
    PFN_vkCreateSwapchainKHR realFunc
) {
    std::cerr << "[PROBE 1] OnCreateSwapchainKHR started" << std::endl;
    if (!device || !pCreateInfo || !pSwapchain || !realFunc) {
        std::cerr << "[PROBE ERROR] Null pointer passed to OnCreateSwapchainKHR" << std::endl;
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    SettingsManager::Get().LoadOrCreate();
    std::cerr << "[PROBE 2] Settings loaded" << std::endl;

    VkSwapchainCreateInfoKHR modifiedCreateInfo = *pCreateInfo;
    modifiedCreateInfo.imageUsage |= (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

    if (modifiedCreateInfo.minImageCount < 5) {
        modifiedCreateInfo.minImageCount = std::max(modifiedCreateInfo.minImageCount + 2u, 5u);
    }

    std::cerr << "[PROBE 3] Calling realFunc..." << std::endl;
    VkResult result = realFunc(device, &modifiedCreateInfo, pAllocator, pSwapchain);
    if (result != VK_SUCCESS) {
        std::cerr << "[PROBE 3.1] realFunc failed with modifiedCreateInfo (" << result << "), trying original..." << std::endl;
        result = realFunc(device, pCreateInfo, pAllocator, pSwapchain);
        if (result != VK_SUCCESS) {
            std::cerr << "[PROBE ERROR] realFunc failed permanently: " << result << std::endl;
            return result;
        }
    }
    std::cerr << "[PROBE 4] Swapchain created: " << *pSwapchain << std::endl;

    if (!m_computeEngineInitialized || m_cachedDevice != device) {
        std::cerr << "[PROBE 5] Initializing Compute Engine..." << std::endl;
        m_computeEngine.Cleanup();
        m_computeEngine.Initialize(device, m_cachedTimestampPeriod);
        m_computeEngineInitialized = true;
        m_cachedDevice = device;
    }

    auto data = std::make_unique<SwapchainData>();
    data->device = device;
    data->memoryProperties = m_cachedMemProps;
    data->queueFamilyIndex = m_cachedQueueFamily;
    data->swapchain = *pSwapchain;
    data->imageFormat = pCreateInfo->imageFormat;
    data->extent = pCreateInfo->imageExtent;
    data->lastPresentTime = std::chrono::high_resolution_clock::now();
    data->lastPresentExitTime = std::chrono::high_resolution_clock::now();

    std::cerr << "[PROBE 6] Getting Swapchain Images..." << std::endl;
    uint32_t imageCount = 0;
    if (vk(device).GetSwapchainImagesKHR) {
        vk(device).GetSwapchainImagesKHR(device, *pSwapchain, &imageCount, nullptr);
        std::cerr << "[PROBE 7] ImageCount = " << imageCount << std::endl;
        if (imageCount == 0) return VK_SUCCESS;

        data->realImages.resize(imageCount);
        vk(device).GetSwapchainImagesKHR(device, *pSwapchain, &imageCount, data->realImages.data());
    } else {
        std::cerr << "[PROBE ERROR] GetSwapchainImagesKHR function pointer is NULL!" << std::endl;
    }

    std::cerr << "[PROBE 8] Creating Command Pool..." << std::endl;
    VkCommandPoolCreateInfo cmdPoolInfo{};
    cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cmdPoolInfo.queueFamilyIndex = m_cachedQueueFamily;
    
    if (vk(device).CreateCommandPool) {
        vk(device).CreateCommandPool(device, &cmdPoolInfo, nullptr, &data->commandPool);
    } else {
        std::cerr << "[PROBE ERROR] CreateCommandPool function pointer is NULL!" << std::endl;
    }

    std::cerr << "[PROBE 9] Entering AllocateFrameBuffers..." << std::endl;
    AllocateFrameBuffers(*data);

    m_swapchains[*pSwapchain] = std::move(data);
    std::cerr << "[PROBE 20] OnCreateSwapchainKHR SUCCESS!" << std::endl;
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

VkResult Interceptor::OnQueuePresentKHR(
    VkQueue queue,
    const VkPresentInfoKHR* pPresentInfo,
    PFN_vkQueuePresentKHR realFunc
) {
    auto cpuStartTime = std::chrono::high_resolution_clock::now();

    HotkeyManager::Get().PollHotkeys();
    SettingsManager::Get().CheckHotReload();

    for (uint32_t i = 0; i < pPresentInfo->swapchainCount; ++i) {
        VkSwapchainKHR swapchain = pPresentInfo->pSwapchains[i];
        auto it = m_swapchains.find(swapchain);

        if (it != m_swapchains.end()) {
            SwapchainData& data = *(it->second);
            if (!data.buffersAllocated) return realFunc(queue, pPresentInfo);

            const auto& cfg = SettingsManager::Get().GetSettings();

            if (cfg.mode == "off") {
                if (cfg.hudMode != 0 || HotkeyManager::Get().IsMenuOpen()) {
                    uint32_t honestOutputFpsX10 = static_cast<uint32_t>((1000.0f / std::max(1.0f, data.smoothedFrametimeMs)) * 10.0f);
                    
                    uint32_t slot = data.currentFlightSlot;
                    FrameFlightResources& res = data.frameSlots[slot];

                    vk(data.device).WaitForFences(data.device, 1, &res.frameFence, VK_TRUE, UINT64_MAX);
                    vk(data.device).ResetFences(data.device, 1, &res.frameFence);

                    VkCommandBuffer realCmd = res.realCommandBuffer;
                    vk(data.device).ResetCommandBuffer(realCmd, 0);
                    VkCommandBufferBeginInfo bInfo{};
                    bInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                    bInfo.pNext = nullptr;
                    bInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                    vk(data.device).BeginCommandBuffer(realCmd, &bInfo);

                    RecordRealFrameOverlayPass(data, realCmd, pPresentInfo->pImageIndices[i], m_computeEngine, honestOutputFpsX10);

                    vk(data.device).EndCommandBuffer(realCmd);

                    std::vector<VkSemaphore> waitSems;
                    std::vector<VkPipelineStageFlags> waitStages;
                    if (pPresentInfo->waitSemaphoreCount > 0 && pPresentInfo->pWaitSemaphores) {
                        for (uint32_t s = 0; s < pPresentInfo->waitSemaphoreCount; ++s) {
                            waitSems.push_back(pPresentInfo->pWaitSemaphores[s]);
                            waitStages.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
                        }
                    }

                    VkSubmitInfo realSubmit{};
                    realSubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                    realSubmit.pNext = nullptr;
                    realSubmit.waitSemaphoreCount = static_cast<uint32_t>(waitSems.size());
                    realSubmit.pWaitSemaphores = waitSems.data();
                    realSubmit.pWaitDstStageMask = waitStages.data();
                    realSubmit.commandBufferCount = 1;
                    realSubmit.pCommandBuffers = &realCmd;
                    realSubmit.signalSemaphoreCount = 1;
                    realSubmit.pSignalSemaphores = &res.realDoneSemaphore;

                    vk(data.device).QueueSubmit(queue, 1, &realSubmit, res.frameFence);

                    VkPresentInfoKHR presentReal = *pPresentInfo;
                    presentReal.waitSemaphoreCount = 1;
                    presentReal.pWaitSemaphores = &res.realDoneSemaphore;

                    data.currentFlightSlot = (data.currentFlightSlot + 1) % MAX_FRAMES_IN_FLIGHT;
                    return realFunc(queue, &presentReal);
                }
                return realFunc(queue, pPresentInfo);
            }

            data.frameCounter++;

            if ((cfg.extPresentWait == 1 || cfg.extPresentWait == 2) && data.frameCounter > 10) {
                ExtensionManager::Get().WaitForPresentQueue(data.device, data.swapchain, data.frameCounter - 1, cfg.extPresentWait);
            }

            if (cfg.lowLatency != "off") {
                ExtensionManager::Get().MarkAntiLagStage(data.device, VK_ANTI_LAG_STAGE_PRESENT_AMD, data.frameCounter, cfg.extLowLatency);
                ExtensionManager::Get().MarkReflexMarker(data.device, data.swapchain, VK_LATENCY_MARKER_PRESENT_START_NV, data.frameCounter, cfg.extLowLatency);
            }

            auto now = std::chrono::high_resolution_clock::now();
            float trueFrameDeltaMs = std::chrono::duration<float, std::milli>(now - data.lastPresentTime).count();
            data.lastPresentTime = now;

            if (trueFrameDeltaMs >= 1.0f && trueFrameDeltaMs <= 200.0f) {
                constexpr float alpha = 0.15f;
                data.smoothedFrametimeMs = alpha * trueFrameDeltaMs + (1.0f - alpha) * data.smoothedFrametimeMs;
            }

            if (data.frameCounter <= 2) {
                data.lastPresentExitTime = std::chrono::high_resolution_clock::now();
                return realFunc(queue, pPresentInfo);
            }

            uint32_t slot = data.currentFlightSlot;
            FrameFlightResources& res = data.frameSlots[slot];

            float nativeFps = 1000.0f / std::max(1.0f, data.smoothedFrametimeMs);

            bool isTargetFpsMode = (cfg.targetFps > 0 && cfg.schedulerMode != "fixed");
            uint32_t subframesToGenerate = 0;

            if (isTargetFpsMode) {
                float targetFpsF = static_cast<float>(cfg.targetFps);
                if (nativeFps >= (targetFpsF * 0.98f)) {
                    data.fractionalDebt = 0.0f;
                    subframesToGenerate = 0;
                } else {
                    float targetRatio = targetFpsF / nativeFps;
                    float extraNeeded = targetRatio - 1.0f;
                    data.fractionalDebt += extraNeeded;

                    uint32_t requested = static_cast<uint32_t>(data.fractionalDebt);
                    subframesToGenerate = std::min(requested, 5u);
                }
            } else {
                uint32_t activeMultiplier = std::clamp(cfg.multiplier, 2u, 6u);
                subframesToGenerate = activeMultiplier - 1;
            }

            uint32_t realGameImageIndex = pPresentInfo->pImageIndices[i];

            std::vector<SubframeTask> tasks;
            if (subframesToGenerate > 0 && vk(data.device).AcquireNextImageKHR) {
                for (uint32_t m = 0; m < subframesToGenerate; ++m) {
                    uint32_t genImg = 0;
                    VkResult acq = vk(data.device).AcquireNextImageKHR(
                        data.device, data.swapchain, 500000ULL,
                        res.acquireSemaphores[m], VK_NULL_HANDLE, &genImg
                    );

                    if (acq == VK_SUCCESS || acq == VK_SUBOPTIMAL_KHR) {
                        if (genImg != realGameImageIndex) {
                            tasks.push_back({genImg, 0.0f});
                        }
                    } else {
                        break;
                    }
                }
            }

            if (isTargetFpsMode) {
                data.fractionalDebt -= static_cast<float>(tasks.size());
                data.fractionalDebt = std::clamp(data.fractionalDebt, 0.0f, 2.0f);
            }

            float currentOutputCount = static_cast<float>(tasks.size() + 1);
            float currentInstantFps = nativeFps * currentOutputCount;

            constexpr float outAlpha = 0.10f;
            data.smoothedOutputFps = outAlpha * currentInstantFps + (1.0f - outAlpha) * data.smoothedOutputFps;

            float displayOutputFps = data.smoothedOutputFps;
            if (isTargetFpsMode) {
                float targetFpsF = static_cast<float>(cfg.targetFps);
                if (std::abs(displayOutputFps - targetFpsF) < (targetFpsF * 0.06f)) {
                    displayOutputFps = targetFpsF;
                }
            }
            uint32_t honestOutputFpsX10 = static_cast<uint32_t>(displayOutputFps * 10.0f);

            if (tasks.empty()) {
                if (cfg.hudMode != 0 || HotkeyManager::Get().IsMenuOpen() || cfg.debugVisual > 0) {
                    vk(data.device).WaitForFences(data.device, 1, &res.frameFence, VK_TRUE, UINT64_MAX);
                    vk(data.device).ResetFences(data.device, 1, &res.frameFence);

                    VkCommandBuffer realCmd = res.realCommandBuffer;
                    vk(data.device).ResetCommandBuffer(realCmd, 0);
                    VkCommandBufferBeginInfo bInfo{};
                    bInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                    bInfo.pNext = nullptr;
                    bInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                    vk(data.device).BeginCommandBuffer(realCmd, &bInfo);

                    RecordRealFrameOverlayPass(data, realCmd, pPresentInfo->pImageIndices[i], m_computeEngine, honestOutputFpsX10);

                    vk(data.device).EndCommandBuffer(realCmd);

                    std::vector<VkSemaphore> waitSems;
                    std::vector<VkPipelineStageFlags> waitStages;
                    if (pPresentInfo->waitSemaphoreCount > 0 && pPresentInfo->pWaitSemaphores) {
                        for (uint32_t s = 0; s < pPresentInfo->waitSemaphoreCount; ++s) {
                            waitSems.push_back(pPresentInfo->pWaitSemaphores[s]);
                            waitStages.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
                        }
                    }

                    VkSubmitInfo realSubmit{};
                    realSubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                    realSubmit.pNext = nullptr;
                    realSubmit.waitSemaphoreCount = static_cast<uint32_t>(waitSems.size());
                    realSubmit.pWaitSemaphores = waitSems.data();
                    realSubmit.pWaitDstStageMask = waitStages.data();
                    realSubmit.commandBufferCount = 1;
                    realSubmit.pCommandBuffers = &realCmd;
                    realSubmit.signalSemaphoreCount = 1;
                    realSubmit.pSignalSemaphores = &res.realDoneSemaphore;

                    vk(data.device).QueueSubmit(queue, 1, &realSubmit, res.frameFence);

                    VkPresentInfoKHR presentReal = *pPresentInfo;
                    presentReal.waitSemaphoreCount = 1;
                    presentReal.pWaitSemaphores = &res.realDoneSemaphore;

                    data.currentFlightSlot = (data.currentFlightSlot + 1) % MAX_FRAMES_IN_FLIGHT;
                    return realFunc(queue, &presentReal);
                }
                return realFunc(queue, pPresentInfo);
            }

            float stepT = 1.0f / static_cast<float>(tasks.size() + 1);
            for (size_t k = 0; k < tasks.size(); ++k) {
                tasks[k].t = stepT * static_cast<float>(k + 1);
            }

            vk(data.device).WaitForFences(data.device, 1, &res.frameFence, VK_TRUE, UINT64_MAX);
            vk(data.device).ResetFences(data.device, 1, &res.frameFence);

            VkCommandBuffer genCmd = res.genCommandBuffers[0];
            vk(data.device).ResetCommandBuffer(genCmd, 0);

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.pNext = nullptr;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vk(data.device).BeginCommandBuffer(genCmd, &beginInfo);

            uint32_t totalFramesDisplayed = static_cast<uint32_t>(tasks.size() + 1);
            RecordFullMultiFramePipeline(data, genCmd, realGameImageIndex, tasks, m_computeEngine, totalFramesDisplayed, trueFrameDeltaMs, honestOutputFpsX10);

            vk(data.device).EndCommandBuffer(genCmd);

            std::vector<VkSemaphore> waitSems;
            std::vector<VkPipelineStageFlags> waitStages;

            if (pPresentInfo->waitSemaphoreCount > 0 && pPresentInfo->pWaitSemaphores) {
                for (uint32_t s = 0; s < pPresentInfo->waitSemaphoreCount; ++s) {
                    waitSems.push_back(pPresentInfo->pWaitSemaphores[s]);
                    waitStages.push_back(VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
                }
            }
            for (size_t k = 0; k < tasks.size(); ++k) {
                waitSems.push_back(res.acquireSemaphores[k]);
                waitStages.push_back(VK_PIPELINE_STAGE_TRANSFER_BIT);
            }

            std::vector<VkSemaphore> signalSems;
            for (size_t k = 0; k < tasks.size(); ++k) {
                signalSems.push_back(res.genDoneSemaphores[k]);
            }
            signalSems.push_back(res.realDoneSemaphore);

            VkFrameBoundaryEXT frameBoundary{};
            frameBoundary.sType = VK_STRUCTURE_TYPE_FRAME_BOUNDARY_EXT;
            frameBoundary.pNext = nullptr;
            frameBoundary.flags = VK_FRAME_BOUNDARY_FRAME_END_BIT_EXT;
            frameBoundary.frameID = data.frameCounter;
            frameBoundary.imageCount = 1;
            frameBoundary.pImages = &data.realImages[realGameImageIndex];

            VkSubmitInfo genSubmit{};
            genSubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            genSubmit.pNext = ExtensionManager::Get().GetSupported().hasFrameBoundary ? &frameBoundary : nullptr;
            genSubmit.waitSemaphoreCount = static_cast<uint32_t>(waitSems.size());
            genSubmit.pWaitSemaphores = waitSems.data();
            genSubmit.pWaitDstStageMask = waitStages.data();
            genSubmit.commandBufferCount = 1;
            genSubmit.pCommandBuffers = &genCmd;
            genSubmit.signalSemaphoreCount = static_cast<uint32_t>(signalSems.size());
            genSubmit.pSignalSemaphores = signalSems.data();

            vk(data.device).QueueSubmit(queue, 1, &genSubmit, res.frameFence);

            for (size_t k = 0; k < tasks.size(); ++k) {
                VkPresentInfoKHR presentG{};
                presentG.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
                presentG.pNext = nullptr;
                presentG.waitSemaphoreCount = 1;
                presentG.pWaitSemaphores = &res.genDoneSemaphores[k];
                presentG.swapchainCount = 1;
                presentG.pSwapchains = &data.swapchain;
                presentG.pImageIndices = &tasks[k].destImageIndex;
                presentG.pResults = nullptr;

                realFunc(queue, &presentG);
            }

            VkPresentInfoKHR presentReal = *pPresentInfo;
            presentReal.waitSemaphoreCount = 1;
            presentReal.pWaitSemaphores = &res.realDoneSemaphore;
            realFunc(queue, &presentReal);

            if (cfg.lowLatency != "off") {
                ExtensionManager::Get().MarkReflexMarker(data.device, data.swapchain, VK_LATENCY_MARKER_PRESENT_END_NV, data.frameCounter, cfg.extLowLatency);
            }

            auto cpuEndTime = std::chrono::high_resolution_clock::now();
            data.lastCpuTimeMs = std::chrono::duration<float, std::milli>(cpuEndTime - cpuStartTime).count();

            data.lastPresentExitTime = std::chrono::high_resolution_clock::now();
            data.currentFlightSlot = (data.currentFlightSlot + 1) % MAX_FRAMES_IN_FLIGHT;
            return VK_SUCCESS;
        }
    }

    return realFunc(queue, pPresentInfo);
}

} // namespace FrameFlux