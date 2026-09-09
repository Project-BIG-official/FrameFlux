// PB FrameFlux - LGPL-2.1
// layer/swapchain_interceptor.hpp: Complete Unified Swapchain Header

#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <unordered_map>
#include <vector>
#include <memory>
#include <chrono>

#include "compute_engine.hpp"
#include "settings.hpp"
#include "frametime_smoother.hpp"
#include "vulkan_extensions.hpp"

namespace FrameFlux {

enum class FrameFluxMode {
    Disabled,
    LegacyV1, // v1.0 Fast Blend (0.05ms)
    TrueFGV2  // v2.0 True Optical Flow FG
};

struct SwapchainData {
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    uint32_t queueFamilyIndex = 0;
    VkFormat imageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D extent = {0, 0};
    std::vector<VkImage> realImages;

    bool buffersAllocated = false;
    FrameFluxMode mode = FrameFluxMode::TrueFGV2;
    FrametimeSmoother smoother;

    // Command resources
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer genCommandBuffer = VK_NULL_HANDLE;
    VkCommandBuffer realCommandBuffer = VK_NULL_HANDLE;

    // Synchronization Semaphores
    VkSemaphore timelineSemaphore = VK_NULL_HANDLE;
    VkSemaphore internalAcquireSemaphore = VK_NULL_HANDLE;
    VkSemaphore realDoneSemaphore = VK_NULL_HANDLE;
    std::vector<VkSemaphore> acquireSemaphores;
    std::vector<VkSemaphore> genDoneSemaphores;

    // Full-Res RGBA Frame history
    VkImage frameAImage = VK_NULL_HANDLE;
    VkDeviceMemory frameAMemory = VK_NULL_HANDLE;
    VkImageView frameAView = VK_NULL_HANDLE;

    VkImage frameBImage = VK_NULL_HANDLE;
    VkDeviceMemory frameBMemory = VK_NULL_HANDLE;
    VkImageView frameBView = VK_NULL_HANDLE;

    // Full-Res Luma textures
    VkImage lumaAImage = VK_NULL_HANDLE;
    VkDeviceMemory lumaAMemory = VK_NULL_HANDLE;
    VkImageView lumaAView = VK_NULL_HANDLE;

    VkImage lumaBImage = VK_NULL_HANDLE;
    VkDeviceMemory lumaBMemory = VK_NULL_HANDLE;
    VkImageView lumaBView = VK_NULL_HANDLE;

    // Half-Res Pyramid Luma textures (for Quality 2-Pass Refinement)
    VkImage lumaAHalfImage = VK_NULL_HANDLE;
    VkDeviceMemory lumaAHalfMemory = VK_NULL_HANDLE;
    VkImageView lumaAHalfView = VK_NULL_HANDLE;

    VkImage lumaBHalfImage = VK_NULL_HANDLE;
    VkDeviceMemory lumaBHalfMemory = VK_NULL_HANDLE;
    VkImageView lumaBHalfView = VK_NULL_HANDLE;

    // Motion & Confidence Textures
    VkImage dummyCoarseImage = VK_NULL_HANDLE;
    VkDeviceMemory dummyCoarseMemory = VK_NULL_HANDLE;
    VkImageView dummyCoarseView = VK_NULL_HANDLE;

    VkImage coarseMotionImage = VK_NULL_HANDLE;
    VkDeviceMemory coarseMotionMemory = VK_NULL_HANDLE;
    VkImageView coarseMotionView = VK_NULL_HANDLE;

    VkImage motionImage = VK_NULL_HANDLE;
    VkDeviceMemory motionMemory = VK_NULL_HANDLE;
    VkImageView motionView = VK_NULL_HANDLE;

    VkImage confidenceImage = VK_NULL_HANDLE;
    VkDeviceMemory confidenceMemory = VK_NULL_HANDLE;
    VkImageView confidenceView = VK_NULL_HANDLE;

    // Output Generated Frame
    VkImage generatedImage = VK_NULL_HANDLE;
    VkDeviceMemory generatedMemory = VK_NULL_HANDLE;
    VkImageView generatedView = VK_NULL_HANDLE;

    VkDescriptorSet overlayDescSet = VK_NULL_HANDLE;

    // Sampler & Descriptor Pools
    VkSampler linearSampler = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

    VkDescriptorSet lumaDescSet = VK_NULL_HANDLE;
    VkDescriptorSet downsampleDescSet = VK_NULL_HANDLE;
    VkDescriptorSet coarseFlowDescSet = VK_NULL_HANDLE;
    VkDescriptorSet refineDescSet = VK_NULL_HANDLE;
    VkDescriptorSet directFlowDescSet = VK_NULL_HANDLE;
    VkDescriptorSet warpDescSet = VK_NULL_HANDLE;

    // Timing metrics
    std::chrono::high_resolution_clock::time_point lastPresentTime;
    float smoothedFrametimeMs = 16.6f;
    uint32_t frameCounter = 0;

    uint32_t totalAllocatedVramMb = 0;
    float lastCpuTimeMs = 0.03f;
    std::chrono::high_resolution_clock::time_point presentStartTime;
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
};

} // namespace FrameFlux