// PB FrameFlux - LGPL-2.1
// layer/swapchain_interceptor.hpp: Multi-Frame Scalable Ring Synchronization with Zero-Copy Ping-Pong

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
    LegacyV1,
    TrueFGV2
};

constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
constexpr uint32_t MAX_MULTIPLIER_FRAMES = 6;

struct FrameFlightResources {
    VkCommandBuffer genCommandBuffers[MAX_MULTIPLIER_FRAMES]{};
    VkCommandBuffer realCommandBuffer = VK_NULL_HANDLE;
    VkFence frameFence = VK_NULL_HANDLE;
    VkSemaphore genDoneSemaphores[MAX_MULTIPLIER_FRAMES]{};
    VkSemaphore realDoneSemaphore = VK_NULL_HANDLE;
    VkSemaphore acquireSemaphores[MAX_MULTIPLIER_FRAMES]{};
    bool isFenceSignaled = true;
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
    bool historyInitialized = false;
    FrameFluxMode mode = FrameFluxMode::TrueFGV2;
    FrametimeSmoother smoother;

    VkCommandPool commandPool = VK_NULL_HANDLE;

    FrameFlightResources frameSlots[MAX_FRAMES_IN_FLIGHT];
    uint32_t currentFlightSlot = 0;

    // -------------------------------------------------------------------------
    // Zero-Copy Ping-Pong Ring Buffers: Index 0 and Index 1
    // -------------------------------------------------------------------------
    VkImage historyFrames[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkDeviceMemory historyFrameMemory[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkImageView historyFrameViews[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};

    VkImage historyLuma[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkDeviceMemory historyLumaMemory[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkImageView historyLumaViews[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};

    VkImage historyLumaHalf[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkDeviceMemory historyLumaHalfMemory[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkImageView historyLumaHalfViews[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};

    // Shared intermediate motion & generation textures
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

    VkImage generatedImage = VK_NULL_HANDLE;
    VkDeviceMemory generatedMemory = VK_NULL_HANDLE;
    VkImageView generatedView = VK_NULL_HANDLE;

    VkSampler linearSampler = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

    // Dual Ping-Pong pre-baked Descriptor Sets (Parity 0 and Parity 1)
    VkDescriptorSet lumaDescSet[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkDescriptorSet downsampleDescSet[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkDescriptorSet coarseFlowDescSet[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkDescriptorSet refineDescSet[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkDescriptorSet directFlowDescSet[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkDescriptorSet warpDescSet[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkDescriptorSet overlayDescSet = VK_NULL_HANDLE;

    std::chrono::high_resolution_clock::time_point lastPresentTime;
    std::chrono::high_resolution_clock::time_point lastPresentExitTime;
    float smoothedFrametimeMs = 16.6f;
    float smoothedOutputFps = 60.0f;
    uint32_t frameCounter = 0;
    uint32_t totalAllocatedVramMb = 0;
    float lastCpuTimeMs = 0.05f;
    float lastGpuTimeMs = 1.2f;

    float fractionalDebt = 0.0f;
};

class Interceptor {
public:
    static Interceptor& Get();

    void SetDeviceInfo(const VkPhysicalDeviceMemoryProperties& memProps, uint32_t queueFamily, float timestampPeriod) {
        m_cachedMemProps = memProps;
        m_cachedQueueFamily = queueFamily;
        m_cachedTimestampPeriod = (timestampPeriod > 0.0f) ? timestampPeriod : 1.0f;
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
    std::unordered_map<VkSwapchainKHR, std::unique_ptr<SwapchainData>> m_swapchains;
    ComputeEngine m_computeEngine;
    bool m_computeEngineInitialized = false;

    VkDevice m_cachedDevice = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties m_cachedMemProps{};
    uint32_t m_cachedQueueFamily = 0;
    float m_cachedTimestampPeriod = 1.0f;

    bool AllocateFrameBuffers(SwapchainData& data);
    void CleanupSwapchainData(SwapchainData& data);
};

} // namespace FrameFlux