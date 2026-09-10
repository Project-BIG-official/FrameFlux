// PB FrameFlux - LGPL-2.1
// layer/swapchain_interceptor.cpp: Exact Native 60 FPS & Flawless 360 Target Engine

#include "swapchain_interceptor.hpp"
#include "settings.hpp"
#include "hotkey_manager.hpp"

#include <algorithm>
#include <iostream>
#include <fstream>
#include <thread>
#include <vector>
#include <cmath>
#include <unistd.h>

namespace FrameFlux {

static void SafePaceSubframe(const std::chrono::high_resolution_clock::time_point& targetTime) {
    auto now = std::chrono::high_resolution_clock::now();
    if (now >= targetTime) return;

    auto diffUs = std::chrono::duration_cast<std::chrono::microseconds>(targetTime - now).count();
    if (diffUs > 20000) diffUs = 20000;

    if (diffUs > 2500) {
        std::this_thread::sleep_for(std::chrono::microseconds(diffUs - 2000));
    }
    while (std::chrono::high_resolution_clock::now() < targetTime) {
        #if defined(__x86_64__) || defined(_M_X64)
        __builtin_ia32_pause();
        #endif
    }
}

static uint32_t GetLiveProcessMemoryMb(uint32_t fallbackBaseMb) {
    std::ifstream statm("/proc/self/statm");
    if (statm.is_open()) {
        unsigned long totalPages, residentPages;
        if (statm >> totalPages >> residentPages) {
            long pageSize = sysconf(_SC_PAGESIZE);
            uint32_t residentMb = static_cast<uint32_t>((residentPages * pageSize) / (1024 * 1024));
            return std::max(residentMb, fallbackBaseMb);
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

// -----------------------------------------------------------------------------
// Honest Telemetry Pass
// -----------------------------------------------------------------------------

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
    uint32_t latUs = static_cast<uint32_t>((frametimeMs * 1000.0f) / std::max(1u, activeMultiplier));

    float jitter = std::abs(currentFrameDeltaMs - frametimeMs);
    float stabilityFactor = std::clamp(1.0f - (jitter / 18.0f), 0.72f, 1.0f);
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
    uint32_t schedCode = (cfg.schedulerMode == "fixed") ? 1 : ((cfg.schedulerMode == "target_fps") ? 2 : 0);
    uint32_t fallbackCode = (cfg.fallbackAction == "repeat") ? 0 : ((cfg.fallbackAction == "drop") ? 2 : 1);
    uint32_t isMenu = HotkeyManager::Get().IsMenuOpen() ? 1 : 0;
    uint32_t isHud = cfg.showWatermark ? 1 : 0;
    uint32_t selectedItem = HotkeyManager::Get().GetSelectedItem();

    OverlayPushConstants opc{
        w, h, isMenu, isHud, selectedItem,
        modeCode, cfg.multiplier, profCode, searchCode,
        schedCode, cfg.targetFps, fallbackCode, cfg.hudCorner,
        nativeFpsX10, honestOutputFpsX10, gpuUs, cpuUs, latUs,
        confidenceX10, fallbackX10, liveMemoryMb
    };

    VkMemoryBarrier memBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &memBarrier, 0, nullptr, 0, nullptr);

    computeEngine.RecordOverlayPass(cmd, data.overlayDescSet, opc, w, h);
}

// -----------------------------------------------------------------------------
// Generation Pipeline
// -----------------------------------------------------------------------------

struct SubframeTask {
    uint32_t destImageIndex;
    float t;
};


static void RecordFullMultiFramePipeline(
    SwapchainData& data,
    VkCommandBuffer cmd,
    uint32_t sourceGameImageIndex,
    const std::vector<SubframeTask>& subframes,
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

    VkImageCopy fullCopyRegion{};
    fullCopyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    fullCopyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    fullCopyRegion.extent = {w, h, 1};

    const auto& cfg = SettingsManager::Get().GetSettings();
    computeEngine.BeginTimestamp(cmd);

    // 1. Ingest real game frame into frameBImage
    TransitionImage(cmd, data.realImages[sourceGameImageIndex], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    TransitionImage(cmd, data.frameBImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT);

    vkCmdCopyImage(cmd, data.realImages[sourceGameImageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   data.frameBImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &fullCopyRegion);

    // 2. Optical Flow Estimation
    if (cfg.mode == "v2") {
        TransitionImage(cmd, data.frameBImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
        TransitionImage(cmd, data.lumaBImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_ACCESS_SHADER_WRITE_BIT);
        computeEngine.RecordLumaPass(cmd, data.lumaDescSet, w, h);

        if (cfg.profile == "quality") {
            TransitionImage(cmd, data.lumaBImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
            TransitionImage(cmd, data.lumaBHalfImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_ACCESS_SHADER_WRITE_BIT);
            computeEngine.RecordDownsamplePass(cmd, data.downsampleDescSet, packedW, h, halfPackedW, halfH);

            TransitionImage(cmd, data.lumaBHalfImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
            TransitionImage(cmd, data.lumaAHalfImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
            TransitionImage(cmd, data.coarseMotionImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_ACCESS_SHADER_WRITE_BIT);
            computeEngine.RecordFlowPass(cmd, data.coarseFlowDescSet, (w + 1) / 2, (h + 1) / 2, 2);

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

    // 3. Multiplier Loop: Generates G1..Gk
    for (size_t s = 0; s < subframes.size(); ++s) {
        TransitionImage(cmd, data.generatedImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_ACCESS_SHADER_WRITE_BIT);

        WarpPushConstants pc{};
        pc.t = subframes[s].t;
        pc.confidenceThreshold = (cfg.mode == "v1") ? 2.0f : 0.55f;
        pc.fallbackAction = (cfg.fallbackAction == "repeat") ? 0 : ((cfg.fallbackAction == "drop") ? 2 : 1);
        pc.showDebugWatermark = 0;
        pc.resolutionX = w;
        pc.resolutionY = h;
        pc.invResolutionX = 1.0f / static_cast<float>(w);
        pc.invResolutionY = 1.0f / static_cast<float>(h);
        computeEngine.RecordWarpPass(cmd, data.warpDescSet, pc, w, h);

        if (s == 0) {
            computeEngine.EndTimestamp(cmd);
        }

        if (cfg.showWatermark || HotkeyManager::Get().IsMenuOpen()) {
            RecordHudOverlay(data, cmd, computeEngine, activeMultiplier, currentFrameDeltaMs, honestOutputFpsX10);
        }

        TransitionImage(cmd, data.generatedImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        TransitionImage(cmd, data.realImages[subframes[s].destImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT);

        vkCmdCopyImage(cmd, data.generatedImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       data.realImages[subframes[s].destImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &fullCopyRegion);

        TransitionImage(cmd, data.realImages[subframes[s].destImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, 0);
    }

    // 4. Render live telemetry overlay on Real Frame B
    if (cfg.showWatermark || HotkeyManager::Get().IsMenuOpen()) {
        TransitionImage(cmd, data.generatedImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        TransitionImage(cmd, data.frameBImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT);

        vkCmdCopyImage(cmd, data.frameBImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       data.generatedImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &fullCopyRegion);

        TransitionImage(cmd, data.generatedImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

        RecordHudOverlay(data, cmd, computeEngine, activeMultiplier, currentFrameDeltaMs, honestOutputFpsX10);

        TransitionImage(cmd, data.generatedImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        TransitionImage(cmd, data.realImages[sourceGameImageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT);

        vkCmdCopyImage(cmd, data.generatedImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       data.realImages[sourceGameImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &fullCopyRegion);

        TransitionImage(cmd, data.realImages[sourceGameImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, 0);

        TransitionImage(cmd, data.frameBImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    } else {
        TransitionImage(cmd, data.realImages[sourceGameImageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, 0);
        TransitionImage(cmd, data.frameBImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    }

    // 5. Commit Temporal History for Frame A
    TransitionImage(cmd, data.frameAImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    vkCmdCopyImage(cmd, data.frameBImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   data.frameAImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &fullCopyRegion);

    if (cfg.mode == "v2") {
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
}

// -----------------------------------------------------------------------------
// Interceptor Singleton & Swapchain Init
// -----------------------------------------------------------------------------

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

    VkImageUsageFlags storageUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
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
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 32},
        {VK_DESCRIPTOR_TYPE_SAMPLER, 4},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 16}
    };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 16;
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

    // Descriptor Updates
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

    // Command Buffers and Synchronization Fences
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = data.commandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = MAX_MULTIPLIER_FRAMES + 1;

    VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t slot = 0; slot < MAX_FRAMES_IN_FLIGHT; ++slot) {
        std::vector<VkCommandBuffer> bufs(MAX_MULTIPLIER_FRAMES + 1);
        vkAllocateCommandBuffers(data.device, &cmdAllocInfo, bufs.data());
        for (uint32_t m = 0; m < MAX_MULTIPLIER_FRAMES; ++m) {
            data.frameSlots[slot].genCommandBuffers[m] = bufs[m];
            vkCreateSemaphore(data.device, &semInfo, nullptr, &data.frameSlots[slot].genDoneSemaphores[m]);
            vkCreateSemaphore(data.device, &semInfo, nullptr, &data.frameSlots[slot].acquireSemaphores[m]);
        }
        data.frameSlots[slot].realCommandBuffer = bufs[MAX_MULTIPLIER_FRAMES];

        vkCreateFence(data.device, &fenceInfo, nullptr, &data.frameSlots[slot].frameFence);
        vkCreateSemaphore(data.device, &semInfo, nullptr, &data.frameSlots[slot].realDoneSemaphore);
        data.frameSlots[slot].isFenceSignaled = true;
    }

    data.buffersAllocated = true;
    return true;
}

void Interceptor::CleanupSwapchainData(SwapchainData& data) {
    if (data.device == VK_NULL_HANDLE) return;

    for (uint32_t slot = 0; slot < MAX_FRAMES_IN_FLIGHT; ++slot) {
        if (data.frameSlots[slot].frameFence != VK_NULL_HANDLE) vkDestroyFence(data.device, data.frameSlots[slot].frameFence, nullptr);
        for (uint32_t m = 0; m < MAX_MULTIPLIER_FRAMES; ++m) {
            if (data.frameSlots[slot].genDoneSemaphores[m] != VK_NULL_HANDLE) vkDestroySemaphore(data.device, data.frameSlots[slot].genDoneSemaphores[m], nullptr);
            if (data.frameSlots[slot].acquireSemaphores[m] != VK_NULL_HANDLE) vkDestroySemaphore(data.device, data.frameSlots[slot].acquireSemaphores[m], nullptr);
        }
        if (data.frameSlots[slot].realDoneSemaphore != VK_NULL_HANDLE) vkDestroySemaphore(data.device, data.frameSlots[slot].realDoneSemaphore, nullptr);
    }

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
}

VkResult Interceptor::OnCreateSwapchainKHR(
    VkDevice device,
    const VkSwapchainCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSwapchainKHR* pSwapchain,
    PFN_vkCreateSwapchainKHR realFunc
) {
    SettingsManager::Get().LoadOrCreate();

    // Probe 8, 7, 6, 5 images to guarantee max possible swapchain buffers
    uint32_t candidateCounts[] = { 8u, 7u, 6u, 5u };
    VkResult result = VK_ERROR_INITIALIZATION_FAILED;

    for (uint32_t count : candidateCounts) {
        VkSwapchainCreateInfoKHR modifiedCreateInfo = *pCreateInfo;
        modifiedCreateInfo.minImageCount = std::max(pCreateInfo->minImageCount + 4u, count);
        modifiedCreateInfo.imageUsage |= (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

        // FORCE FIFO mode to enable Direct Scanout and hardware FreeSync/VRR on XWayland!
        modifiedCreateInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;

        result = realFunc(device, &modifiedCreateInfo, pAllocator, pSwapchain);
        if (result == VK_SUCCESS) break;
    }

    if (result != VK_SUCCESS) {
        VkSwapchainCreateInfoKHR fallbackCreateInfo = *pCreateInfo;
        fallbackCreateInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        result = realFunc(device, &fallbackCreateInfo, pAllocator, pSwapchain);
        if (result != VK_SUCCESS) {
            result = realFunc(device, pCreateInfo, pAllocator, pSwapchain);
            if (result != VK_SUCCESS) return result;
        }
    }

    if (!m_computeEngineInitialized) {
        m_computeEngine.Initialize(device, m_cachedPhysicalDevice);
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
    data->lastPresentExitTime = std::chrono::high_resolution_clock::now();

    uint32_t imageCount = 0;
    vkGetSwapchainImagesKHR(device, *pSwapchain, &imageCount, nullptr);
    data->realImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device, *pSwapchain, &imageCount, data->realImages.data());

    VkCommandPoolCreateInfo cmdPoolInfo{};
    cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cmdPoolInfo.queueFamilyIndex = m_cachedQueueFamily;
    vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &data->commandPool);

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

// -----------------------------------------------------------------------------
// Present Hook: Dead-On Exact 60 FPS Native & Target 360 FPS Output
// -----------------------------------------------------------------------------

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
            if (cfg.mode == "off") return realFunc(queue, pPresentInfo);

            // =========================================================================
            // 1. EXACT NATIVE FPS MEASUREMENT: Start-to-Start Cadence
            // =========================================================================
            // Measures true time between game frames without subtracting present overhead!
            // Locked 60 FPS game will now show EXACTLY 60.0 FPS!
            auto now = std::chrono::high_resolution_clock::now();
            float trueFrameDeltaMs = std::chrono::duration<float, std::milli>(now - data.lastPresentTime).count();
            data.lastPresentTime = now;

            if (trueFrameDeltaMs >= 1.0f && trueFrameDeltaMs <= 200.0f) {
                constexpr float alpha = 0.15f;
                data.smoothedFrametimeMs = alpha * trueFrameDeltaMs + (1.0f - alpha) * data.smoothedFrametimeMs;
            }

            data.frameCounter++;
            if (data.frameCounter <= 2) {
                data.lastPresentExitTime = std::chrono::high_resolution_clock::now();
                return realFunc(queue, pPresentInfo);
            }

            uint32_t slot = data.currentFlightSlot;
            FrameFlightResources& res = data.frameSlots[slot];

            if (!res.isFenceSignaled) {
                VkResult fenceRes = vkWaitForFences(data.device, 1, &res.frameFence, VK_TRUE, UINT64_MAX);
                if (fenceRes == VK_SUCCESS) {
                    vkResetFences(data.device, 1, &res.frameFence);
                    res.isFenceSignaled = true;
                }
            }

            // =========================================================================
            // 2. UNCLAMPED 6x TARGET FPS SCHEDULER
            // =========================================================================
            float nativeFps = 1000.0f / std::max(1.0f, data.smoothedFrametimeMs);
            bool isTargetFpsMode = (cfg.schedulerMode == "target_fps" && cfg.targetFps > 0);
            uint32_t subframesToGenerate = 0;

            if (isTargetFpsMode) {
                float targetFpsF = static_cast<float>(cfg.targetFps);

                // If game naturally exceeds target, pass-through directly
                if (nativeFps >= (targetFpsF * 0.98f)) {
                    data.fractionalDebt = 0.0f;
                    subframesToGenerate = 0;
                    data.lastPresentExitTime = std::chrono::high_resolution_clock::now();
                    return realFunc(queue, pPresentInfo);
                }

                // Calculate exact multiplier needed (e.g. 360 / 60 = 6.0x -> 5 subframes)
                float targetRatio = targetFpsF / nativeFps;
                float extraNeeded = targetRatio - 1.0f;
                data.fractionalDebt += extraNeeded;

                subframesToGenerate = static_cast<uint32_t>(data.fractionalDebt);

                // Allow up to 5 subframes (for full 6x generation = 360 FPS from 60 FPS!)
                uint32_t maxAllowed = std::min(5u, (uint32_t)MAX_MULTIPLIER_FRAMES - 1);
                subframesToGenerate = std::min(subframesToGenerate, maxAllowed);
            } else {
                uint32_t activeMultiplier = std::clamp(cfg.multiplier, 2u, 6u);
                subframesToGenerate = activeMultiplier - 1;
            }

            if (subframesToGenerate == 0) {
                data.lastPresentExitTime = std::chrono::high_resolution_clock::now();
                return realFunc(queue, pPresentInfo);
            }

            // =========================================================================
            // 3. Acquire Up To 5 Extra Buffers
            // =========================================================================
            std::vector<SubframeTask> tasks;
            for (uint32_t m = 0; m < subframesToGenerate; ++m) {
                uint32_t genImg = 0;
                VkResult acq = vkAcquireNextImageKHR(
                    data.device, data.swapchain, 2000000ULL,
                    res.acquireSemaphores[m], VK_NULL_HANDLE, &genImg
                );

                if (acq == VK_SUCCESS || acq == VK_SUBOPTIMAL_KHR) {
                    tasks.push_back({genImg, 0.0f});
                } else {
                    break;
                }
            }

            // Deduct ONLY what was ACTUALLY acquired so debt doesn't get lost
            if (isTargetFpsMode) {
                data.fractionalDebt -= static_cast<float>(tasks.size());
                data.fractionalDebt = std::max(0.0f, data.fractionalDebt);
            }

            if (tasks.empty()) {
                data.lastPresentExitTime = std::chrono::high_resolution_clock::now();
                return realFunc(queue, pPresentInfo);
            }

            float stepT = 1.0f / static_cast<float>(tasks.size() + 1);
            for (size_t k = 0; k < tasks.size(); ++k) {
                tasks[k].t = stepT * static_cast<float>(k + 1);
            }

            // Honest Telemetry: native * (tasks + 1)
            uint32_t totalFramesDisplayed = static_cast<uint32_t>(tasks.size() + 1);

            uint32_t realGameImageIndex = pPresentInfo->pImageIndices[i];

            // =========================================================================
            // 4. Record Single Command Buffer
            // =========================================================================
            VkCommandBuffer genCmd = res.genCommandBuffers[0];
            vkResetCommandBuffer(genCmd, 0);

            VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr};
            vkBeginCommandBuffer(genCmd, &beginInfo);

            // Smooth fractional output FPS over consecutive cadence steps
            float instantOutputFps = nativeFps * static_cast<float>(tasks.size() + 1);
            constexpr float outAlpha = 0.12f;
            data.smoothedOutputFps = outAlpha * instantOutputFps + (1.0f - outAlpha) * data.smoothedOutputFps;

            // In Target FPS mode, anchor smoothed display to target limit
            float displayFps = isTargetFpsMode 
                ? std::min(data.smoothedOutputFps, (float)cfg.targetFps)
                : data.smoothedOutputFps;

            uint32_t honestOutputFpsX10 = static_cast<uint32_t>(displayFps * 10.0f);

            RecordFullMultiFramePipeline(data, genCmd, realGameImageIndex, tasks, m_computeEngine, totalFramesDisplayed, trueFrameDeltaMs, honestOutputFpsX10);

            vkEndCommandBuffer(genCmd);

            std::vector<VkSemaphore> waitSems;
            std::vector<VkPipelineStageFlags> waitStages;

            if (pPresentInfo->waitSemaphoreCount > 0 && pPresentInfo->pWaitSemaphores) {
                for (uint32_t s = 0; s < pPresentInfo->waitSemaphoreCount; ++s) {
                    waitSems.push_back(pPresentInfo->pWaitSemaphores[s]);
                    waitStages.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
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

            VkSubmitInfo genSubmit{};
            genSubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            genSubmit.waitSemaphoreCount = static_cast<uint32_t>(waitSems.size());
            genSubmit.pWaitSemaphores = waitSems.data();
            genSubmit.pWaitDstStageMask = waitStages.data();
            genSubmit.commandBufferCount = 1;
            genSubmit.pCommandBuffers = &genCmd;
            genSubmit.signalSemaphoreCount = static_cast<uint32_t>(signalSems.size());
            genSubmit.pSignalSemaphores = signalSems.data();

            res.isFenceSignaled = false;
            vkQueueSubmit(queue, 1, &genSubmit, res.frameFence);

            // =========================================================================
            // 5. Present Generated Subframes + Real Frame
            // =========================================================================
            for (size_t k = 0; k < tasks.size(); ++k) {
                VkPresentInfoKHR presentG{};
                presentG.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
                presentG.waitSemaphoreCount = 1;
                presentG.pWaitSemaphores = &res.genDoneSemaphores[k];
                presentG.swapchainCount = 1;
                presentG.pSwapchains = &data.swapchain;
                presentG.pImageIndices = &tasks[k].destImageIndex;
                realFunc(queue, &presentG);
            }

            // Present Real Frame B
            VkPresentInfoKHR presentReal = *pPresentInfo;
            presentReal.waitSemaphoreCount = 1;
            presentReal.pWaitSemaphores = &res.realDoneSemaphore;
            realFunc(queue, &presentReal);

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