// PB FrameFlux - LGPL-2.1
// layer/swapchain_interceptor.hpp: Swapchain State & Full Pipeline Orchestration

#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <unordered_map>
#include <vector>
#include <memory>
#include <chrono>

#include "compute_engine.hpp"

namespace FrameFlux {

struct SwapchainData {
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memoryProperties{}; // Safe cached memory properties
    VkFormat imageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D extent = {0, 0};
    std::vector<VkImage> realImages;

    // Command resources
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer genCommandBuffer = VK_NULL_HANDLE;
    VkCommandBuffer realCommandBuffer = VK_NULL_HANDLE;

    // Synchronization
    VkSemaphore timelineSemaphore = VK_NULL_HANDLE;
    VkSemaphore internalAcquireSemaphore = VK_NULL_HANDLE;
    uint64_t currentTimelineValue = 0;

    // Full Color Frames (A = previous, B = current)
    VkImage frameAImage = VK_NULL_HANDLE;
    VkDeviceMemory frameAMemory = VK_NULL_HANDLE;
    VkImageView frameAView = VK_NULL_HANDLE;

    VkImage frameBImage = VK_NULL_HANDLE;
    VkDeviceMemory frameBMemory = VK_NULL_HANDLE;
    VkImageView frameBView = VK_NULL_HANDLE;

    // Packed Luminance (4 pixels per uint32, width / 4 x height)
    VkImage lumaAImage = VK_NULL_HANDLE;
    VkDeviceMemory lumaAMemory = VK_NULL_HANDLE;
    VkImageView lumaAView = VK_NULL_HANDLE;

    VkImage lumaBImage = VK_NULL_HANDLE;
    VkDeviceMemory lumaBMemory = VK_NULL_HANDLE;
    VkImageView lumaBView = VK_NULL_HANDLE;

    // Dummy coarse motion field (zeros)
    VkImage dummyCoarseImage = VK_NULL_HANDLE;
    VkDeviceMemory dummyCoarseMemory = VK_NULL_HANDLE;
    VkImageView dummyCoarseView = VK_NULL_HANDLE;

    // Motion Vectors & Confidence Map
    VkImage motionImage = VK_NULL_HANDLE;
    VkDeviceMemory motionMemory = VK_NULL_HANDLE;
    VkImageView motionView = VK_NULL_HANDLE;

    VkImage confidenceImage = VK_NULL_HANDLE;
    VkDeviceMemory confidenceMemory = VK_NULL_HANDLE;
    VkImageView confidenceView = VK_NULL_HANDLE;

    // Generated intermediate frame
    VkImage generatedImage = VK_NULL_HANDLE;
    VkDeviceMemory generatedMemory = VK_NULL_HANDLE;
    VkImageView generatedView = VK_NULL_HANDLE;

    // Sampler & Descriptor Resources
    VkSampler linearSampler = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

    VkDescriptorSet lumaDescSet = VK_NULL_HANDLE;
    VkDescriptorSet flowDescSet = VK_NULL_HANDLE;
    VkDescriptorSet warpDescSet = VK_NULL_HANDLE;

    // Timing metrics
    std::chrono::high_resolution_clock::time_point lastPresentTime;
    float smoothedFrametimeMs = 16.6f;
    uint32_t frameCounter = 0;
};

class Interceptor {
public:
    static Interceptor& Get();

    VkResult OnCreateSwapchainKHR(
        VkDevice device,
        const VkPhysicalDeviceMemoryProperties& memProperties,
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

    bool CreateTexture(
        VkDevice device,
        VkPhysicalDevice physicalDevice,
        uint32_t width,
        uint32_t height,
        VkFormat format,
        VkImageUsageFlags usage,
        VkImage& outImage,
        VkDeviceMemory& outMemory,
        VkImageView& outView
    );

    void AllocateFrameBuffers(SwapchainData& data);
    void CleanupSwapchainData(SwapchainData& data);
    void ComputeAdaptiveTiming(SwapchainData& data, float& outNormalizedT);
    void DispatchGenerationPass(SwapchainData& data, VkQueue queue, uint32_t imageIndex, float t);
    void PresentRealFrame(SwapchainData& data, VkQueue queue, uint32_t imageIndex);
};

} // namespace FrameFlux