// PB FrameFlux - LGPL-2.1
// layer/swapchain_interceptor.hpp: Clean Decoupled Swapchain Interceptor

#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <unordered_map>
#include <vector>
#include <memory>
#include <chrono>
#include <cstring>
#include <strings.h>
#include <cstdlib>

#include "compute_engine.hpp"

namespace FrameFlux {

enum class FrameFluxMode {
    Disabled,
    LegacyV1, // Fast Frame Blending (v1.0, 0.05ms overhead, no optical flow)
    TrueFGV2  // Motion-Compensated Optical Flow Warping (v2.0)
};

inline FrameFluxMode GetConfiguredMode() {
    const char* env = std::getenv("ENABLE_FRAMEFLUX");
    if (!env || std::strlen(env) == 0 || std::strcmp(env, "0") == 0) {
        return FrameFluxMode::Disabled;
    }
    if (strcasecmp(env, "legacy") == 0 || std::strcmp(env, "1.0") == 0 || strcasecmp(env, "blend") == 0) {
        return FrameFluxMode::LegacyV1;
    }
    return FrameFluxMode::TrueFGV2; // "1", "2.0", "true"
}

struct SwapchainData {
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    uint32_t queueFamilyIndex = 0;
    VkFormat imageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D extent = {0, 0};
    std::vector<VkImage> realImages;

    FrameFluxMode mode = FrameFluxMode::TrueFGV2;
    bool buffersAllocated = false;

    // Command resources
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer genCommandBuffer = VK_NULL_HANDLE;
    VkCommandBuffer realCommandBuffer = VK_NULL_HANDLE;

    // Synchronization
    VkSemaphore timelineSemaphore = VK_NULL_HANDLE;
    VkSemaphore internalAcquireSemaphore = VK_NULL_HANDLE;
    uint64_t currentTimelineValue = 0;

    // Intermediate frame textures
    VkImage frameAImage = VK_NULL_HANDLE;
    VkDeviceMemory frameAMemory = VK_NULL_HANDLE;
    VkImageView frameAView = VK_NULL_HANDLE;

    VkImage frameBImage = VK_NULL_HANDLE;
    VkDeviceMemory frameBMemory = VK_NULL_HANDLE;
    VkImageView frameBView = VK_NULL_HANDLE;

    VkImage lumaAImage = VK_NULL_HANDLE;
    VkDeviceMemory lumaAMemory = VK_NULL_HANDLE;
    VkImageView lumaAView = VK_NULL_HANDLE;

    VkImage lumaBImage = VK_NULL_HANDLE;
    VkDeviceMemory lumaBMemory = VK_NULL_HANDLE;
    VkImageView lumaBView = VK_NULL_HANDLE;

    VkImage dummyCoarseImage = VK_NULL_HANDLE;
    VkDeviceMemory dummyCoarseMemory = VK_NULL_HANDLE;
    VkImageView dummyCoarseView = VK_NULL_HANDLE;

    VkImage motionImage = VK_NULL_HANDLE;
    VkDeviceMemory motionMemory = VK_NULL_HANDLE;
    VkImageView motionView = VK_NULL_HANDLE;

    VkImage confidenceImage = VK_NULL_HANDLE;
    VkDeviceMemory confidenceMemory = VK_NULL_HANDLE;
    VkImageView confidenceView = VK_NULL_HANDLE;

    VkImage generatedImage = VK_NULL_HANDLE;
    VkDeviceMemory generatedMemory = VK_NULL_HANDLE;
    VkImageView generatedView = VK_NULL_HANDLE;

    VkSampler linearSampler = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

    VkDescriptorSet lumaDescSet = VK_NULL_HANDLE;
    VkDescriptorSet flowDescSet = VK_NULL_HANDLE;
    VkDescriptorSet warpDescSet = VK_NULL_HANDLE;

    std::chrono::high_resolution_clock::time_point lastPresentTime;
    float smoothedFrametimeMs = 16.6f;
    uint32_t frameCounter = 0;
};

class Interceptor {
public:
    static Interceptor& Get();

    void SetDeviceInfo(const VkPhysicalDeviceMemoryProperties& memProps, uint32_t queueFamily) {
        m_cachedMemProps = memProps;
        m_cachedQueueFamily = queueFamily;
    }

    VkResult OnCreateSwapchainKHR(
        VkDevice device,
        const VkSwapchainCreateInfoKHR* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkSwapchainKHR* pSwapchain,
        PFN_vkCreateSwapchainKHR realFunc
    );

    void OnDestroySwapchainKHR(
        VkDevice device,
        VkSwapchainKHR swapchain,
        const VkAllocationCallbacks* pAllocator,
        PFN_vkDestroySwapchainKHR realFunc
    );

    VkResult OnQueuePresentKHR(
        VkQueue queue,
        const VkPresentInfoKHR* pPresentInfo,
        PFN_vkQueuePresentKHR realFunc
    );

private:
    Interceptor() = default;
    ~Interceptor() = default;

    std::unordered_map<VkSwapchainKHR, std::unique_ptr<SwapchainData>> m_swapchains;
    ComputeEngine m_computeEngine;
    bool m_computeEngineInitialized = false;

    VkPhysicalDeviceMemoryProperties m_cachedMemProps{};
    uint32_t m_cachedQueueFamily = 0;

    bool AllocateFrameBuffers(SwapchainData& data);
    void CleanupSwapchainData(SwapchainData& data);
    void ComputeAdaptiveTiming(SwapchainData& data, float& outNormalizedT);
    void DispatchGenerationPass(SwapchainData& data, VkQueue queue, uint32_t imageIndex, float t);
    void PresentRealFrame(SwapchainData& data, VkQueue queue, uint32_t imageIndex);
};

} // namespace FrameFlux