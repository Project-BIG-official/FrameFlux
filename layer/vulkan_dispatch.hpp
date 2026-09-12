// PB FrameFlux - LGPL-2.1
// layer/vulkan_dispatch.hpp: Safe Layer-Internal Vulkan Dispatch Table

#pragma once

#include <vulkan/vulkan.h>
#include <unordered_map>
#include <mutex>
#include <cstring>

namespace FrameFlux {

struct DeviceDispatchTable {
    PFN_vkGetDeviceProcAddr GetDeviceProcAddr = nullptr;
    PFN_vkDestroyDevice DestroyDevice = nullptr;

    PFN_vkCreateSwapchainKHR CreateSwapchainKHR = nullptr;
    PFN_vkDestroySwapchainKHR DestroySwapchainKHR = nullptr;
    PFN_vkGetSwapchainImagesKHR GetSwapchainImagesKHR = nullptr;
    PFN_vkAcquireNextImageKHR AcquireNextImageKHR = nullptr;
    PFN_vkQueuePresentKHR QueuePresentKHR = nullptr;

    PFN_vkCreateImage CreateImage = nullptr;
    PFN_vkDestroyImage DestroyImage = nullptr;
    PFN_vkGetImageMemoryRequirements GetImageMemoryRequirements = nullptr;
    PFN_vkAllocateMemory AllocateMemory = nullptr;
    PFN_vkFreeMemory FreeMemory = nullptr;
    PFN_vkBindImageMemory BindImageMemory = nullptr;
    PFN_vkCreateImageView CreateImageView = nullptr;
    PFN_vkDestroyImageView DestroyImageView = nullptr;
    PFN_vkCreateSampler CreateSampler = nullptr;
    PFN_vkDestroySampler DestroySampler = nullptr;

    PFN_vkCreateDescriptorSetLayout CreateDescriptorSetLayout = nullptr;
    PFN_vkDestroyDescriptorSetLayout DestroyDescriptorSetLayout = nullptr;
    PFN_vkCreateDescriptorPool CreateDescriptorPool = nullptr;
    PFN_vkDestroyDescriptorPool DestroyDescriptorPool = nullptr;
    PFN_vkAllocateDescriptorSets AllocateDescriptorSets = nullptr;
    PFN_vkUpdateDescriptorSets UpdateDescriptorSets = nullptr;

    PFN_vkCreateShaderModule CreateShaderModule = nullptr;
    PFN_vkDestroyShaderModule DestroyShaderModule = nullptr;
    PFN_vkCreatePipelineLayout CreatePipelineLayout = nullptr;
    PFN_vkDestroyPipelineLayout DestroyPipelineLayout = nullptr;
    PFN_vkCreateComputePipelines CreateComputePipelines = nullptr;
    PFN_vkDestroyPipeline DestroyPipeline = nullptr;

    PFN_vkCreateCommandPool CreateCommandPool = nullptr;
    PFN_vkDestroyCommandPool DestroyCommandPool = nullptr;
    PFN_vkAllocateCommandBuffers AllocateCommandBuffers = nullptr;
    PFN_vkResetCommandBuffer ResetCommandBuffer = nullptr;
    PFN_vkBeginCommandBuffer BeginCommandBuffer = nullptr;
    PFN_vkEndCommandBuffer EndCommandBuffer = nullptr;
    PFN_vkCmdPipelineBarrier CmdPipelineBarrier = nullptr;
    PFN_vkCmdCopyImage CmdCopyImage = nullptr;
    PFN_vkCmdBindPipeline CmdBindPipeline = nullptr;
    PFN_vkCmdBindDescriptorSets CmdBindDescriptorSets = nullptr;
    PFN_vkCmdPushConstants CmdPushConstants = nullptr;
    PFN_vkCmdDispatch CmdDispatch = nullptr;

    PFN_vkCreateFence CreateFence = nullptr;
    PFN_vkDestroyFence DestroyFence = nullptr;
    PFN_vkWaitForFences WaitForFences = nullptr;
    PFN_vkResetFences ResetFences = nullptr;
    PFN_vkCreateSemaphore CreateSemaphore = nullptr;
    PFN_vkDestroySemaphore DestroySemaphore = nullptr;
    PFN_vkQueueSubmit QueueSubmit = nullptr;

    PFN_vkCreateQueryPool CreateQueryPool = nullptr;
    PFN_vkDestroyQueryPool DestroyQueryPool = nullptr;
    PFN_vkCmdResetQueryPool CmdResetQueryPool = nullptr;
    PFN_vkCmdWriteTimestamp CmdWriteTimestamp = nullptr;
    PFN_vkGetQueryPoolResults GetQueryPoolResults = nullptr;

    void Init(VkDevice device, PFN_vkGetDeviceProcAddr gdpa) {
        if (!device || !gdpa) return;

        #define LOAD_DEV_FN(name) name = (PFN_vk##name)gdpa(device, "vk" #name)
        LOAD_DEV_FN(GetDeviceProcAddr);
        LOAD_DEV_FN(DestroyDevice);
        LOAD_DEV_FN(CreateSwapchainKHR);
        LOAD_DEV_FN(DestroySwapchainKHR);
        LOAD_DEV_FN(GetSwapchainImagesKHR);
        LOAD_DEV_FN(AcquireNextImageKHR);
        LOAD_DEV_FN(QueuePresentKHR);

        LOAD_DEV_FN(CreateImage);
        LOAD_DEV_FN(DestroyImage);
        LOAD_DEV_FN(GetImageMemoryRequirements);
        LOAD_DEV_FN(AllocateMemory);
        LOAD_DEV_FN(FreeMemory);
        LOAD_DEV_FN(BindImageMemory);
        LOAD_DEV_FN(CreateImageView);
        LOAD_DEV_FN(DestroyImageView);
        LOAD_DEV_FN(CreateSampler);
        LOAD_DEV_FN(DestroySampler);

        LOAD_DEV_FN(CreateDescriptorSetLayout);
        LOAD_DEV_FN(DestroyDescriptorSetLayout);
        LOAD_DEV_FN(CreateDescriptorPool);
        LOAD_DEV_FN(DestroyDescriptorPool);
        LOAD_DEV_FN(AllocateDescriptorSets);
        LOAD_DEV_FN(UpdateDescriptorSets);

        LOAD_DEV_FN(CreateShaderModule);
        LOAD_DEV_FN(DestroyShaderModule);
        LOAD_DEV_FN(CreatePipelineLayout);
        LOAD_DEV_FN(DestroyPipelineLayout);
        LOAD_DEV_FN(CreateComputePipelines);
        LOAD_DEV_FN(DestroyPipeline);

        LOAD_DEV_FN(CreateCommandPool);
        LOAD_DEV_FN(DestroyCommandPool);
        LOAD_DEV_FN(AllocateCommandBuffers);
        LOAD_DEV_FN(ResetCommandBuffer);
        LOAD_DEV_FN(BeginCommandBuffer);
        LOAD_DEV_FN(EndCommandBuffer);
        LOAD_DEV_FN(CmdPipelineBarrier);
        LOAD_DEV_FN(CmdCopyImage);
        LOAD_DEV_FN(CmdBindPipeline);
        LOAD_DEV_FN(CmdBindDescriptorSets);
        LOAD_DEV_FN(CmdPushConstants);
        LOAD_DEV_FN(CmdDispatch);

        LOAD_DEV_FN(CreateFence);
        LOAD_DEV_FN(DestroyFence);
        LOAD_DEV_FN(WaitForFences);
        LOAD_DEV_FN(ResetFences);
        LOAD_DEV_FN(CreateSemaphore);
        LOAD_DEV_FN(DestroySemaphore);
        LOAD_DEV_FN(QueueSubmit);

        LOAD_DEV_FN(CreateQueryPool);
        LOAD_DEV_FN(DestroyQueryPool);
        LOAD_DEV_FN(CmdResetQueryPool);
        LOAD_DEV_FN(CmdWriteTimestamp);
        LOAD_DEV_FN(GetQueryPoolResults);
        #undef LOAD_DEV_FN
    }
};

class DispatchManager {
public:
    static DispatchManager& Get() {
        static DispatchManager instance;
        return instance;
    }

    void RegisterDevice(VkDevice device, PFN_vkGetDeviceProcAddr gdpa) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_deviceTables[device].Init(device, gdpa);
        m_activeTable = m_deviceTables[device];
    }

    void UnregisterDevice(VkDevice device) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_deviceTables.erase(device);
    }

    const DeviceDispatchTable& GetTable(VkDevice device) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_deviceTables.find(device);
        if (it != m_deviceTables.end()) return it->second;
        return m_activeTable;
    }

    const DeviceDispatchTable& GetActiveTable() const {
        return m_activeTable;
    }

private:
    DispatchManager() = default;
    std::unordered_map<VkDevice, DeviceDispatchTable> m_deviceTables;
    DeviceDispatchTable m_activeTable{};
    std::mutex m_mutex;
};

inline const DeviceDispatchTable& vk(VkDevice device) {
    return DispatchManager::Get().GetTable(device);
}

inline const DeviceDispatchTable& vk() {
    return DispatchManager::Get().GetActiveTable();
}

} // namespace FrameFlux