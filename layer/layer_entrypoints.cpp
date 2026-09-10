// PB FrameFlux - LGPL-2.1
// layer/layer_entrypoints.cpp: Robust Wine/DXVK/Native Vulkan Interceptor

#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include "swapchain_interceptor.hpp"
#include "vulkan_extensions.hpp"

#include <unordered_map>
#include <mutex>
#include <cstring>
#include <iostream>

#define VK_LAYER_EXPORT extern "C" __attribute__((visibility("default")))

namespace FrameFlux {

static VkInstance g_instance = VK_NULL_HANDLE;
static VkPhysicalDeviceMemoryProperties g_deviceMemoryProperties{};
static uint32_t g_graphicsQueueFamilyIndex = 0;

struct InstanceDispatch {
    PFN_vkGetInstanceProcAddr GetInstanceProcAddr = nullptr;
    PFN_vkDestroyInstance DestroyInstance = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties GetPhysicalDeviceMemoryProperties = nullptr;
};

struct DeviceDispatch {
    PFN_vkGetDeviceProcAddr GetDeviceProcAddr = nullptr;
    PFN_vkDestroyDevice DestroyDevice = nullptr;
    PFN_vkCreateSwapchainKHR CreateSwapchainKHR = nullptr;
    PFN_vkDestroySwapchainKHR DestroySwapchainKHR = nullptr;
    PFN_vkQueuePresentKHR QueuePresentKHR = nullptr;
};

static std::unordered_map<void*, InstanceDispatch> g_instanceDispatches;
static std::unordered_map<void*, DeviceDispatch> g_deviceDispatches;
static InstanceDispatch g_globalInstanceDispatch{};
static DeviceDispatch g_globalDeviceDispatch{};
static std::mutex g_dispatchLock;

template <typename DispatchableType>
void* GetDispatchKey(DispatchableType inst) {
    if (!inst) return nullptr;
    return *(void**)inst;
}

// -----------------------------------------------------------------------------
// Intercepted Device Functions
// -----------------------------------------------------------------------------

static VKAPI_ATTR VkResult VKAPI_CALL Hook_CreateSwapchainKHR(
    VkDevice device,
    const VkSwapchainCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSwapchainKHR* pSwapchain
) {
    PFN_vkCreateSwapchainKHR realFunc = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_dispatchLock);
        void* key = GetDispatchKey(device);
        auto it = g_deviceDispatches.find(key);
        realFunc = (it != g_deviceDispatches.end() && it->second.CreateSwapchainKHR) ? it->second.CreateSwapchainKHR : g_globalDeviceDispatch.CreateSwapchainKHR;
    }

    if (!realFunc) return VK_ERROR_INITIALIZATION_FAILED;

    // Clean 5-argument call matching standard Vulkan signature
    return Interceptor::Get().OnCreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain, realFunc);
}

static VKAPI_ATTR void VKAPI_CALL Hook_DestroySwapchainKHR(
    VkDevice device,
    VkSwapchainKHR swapchain,
    const VkAllocationCallbacks* pAllocator
) {
    PFN_vkDestroySwapchainKHR realFunc = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_dispatchLock);
        void* key = GetDispatchKey(device);
        auto it = g_deviceDispatches.find(key);
        realFunc = (it != g_deviceDispatches.end()) ? it->second.DestroySwapchainKHR : g_globalDeviceDispatch.DestroySwapchainKHR;
    }

    if (realFunc) {
        Interceptor::Get().OnDestroySwapchainKHR(device, swapchain, pAllocator, realFunc);
    }
}

static VKAPI_ATTR VkResult VKAPI_CALL Hook_QueuePresentKHR(
    VkQueue queue,
    const VkPresentInfoKHR* pPresentInfo
) {
    PFN_vkQueuePresentKHR realFunc = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_dispatchLock);
        void* key = GetDispatchKey(queue);
        auto it = g_deviceDispatches.find(key);
        realFunc = (it != g_deviceDispatches.end()) ? it->second.QueuePresentKHR : g_globalDeviceDispatch.QueuePresentKHR;
    }

    if (!realFunc) return VK_ERROR_INITIALIZATION_FAILED;

    return Interceptor::Get().OnQueuePresentKHR(queue, pPresentInfo, realFunc);
}

// -----------------------------------------------------------------------------
// Intercepted Instance Functions
// -----------------------------------------------------------------------------

static VKAPI_ATTR void VKAPI_CALL Hook_GetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceMemoryProperties* pMemoryProperties
) {
    PFN_vkGetPhysicalDeviceMemoryProperties realFunc = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_dispatchLock);
        void* key = GetDispatchKey(physicalDevice);
        auto it = g_instanceDispatches.find(key);
        if (it != g_instanceDispatches.end() && it->second.GetPhysicalDeviceMemoryProperties) {
            realFunc = it->second.GetPhysicalDeviceMemoryProperties;
        } else {
            realFunc = g_globalInstanceDispatch.GetPhysicalDeviceMemoryProperties;
        }
    }

    if (realFunc) {
        realFunc(physicalDevice, pMemoryProperties);
        g_deviceMemoryProperties = *pMemoryProperties;
    }
}

static VKAPI_ATTR VkResult VKAPI_CALL Hook_CreateInstance(
    const VkInstanceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkInstance* pInstance
) {
    VkLayerInstanceCreateInfo* chainInfo = (VkLayerInstanceCreateInfo*)pCreateInfo->pNext;
    while (chainInfo && (chainInfo->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO ||
                         chainInfo->function != VK_LAYER_LINK_INFO)) {
        chainInfo = (VkLayerInstanceCreateInfo*)chainInfo->pNext;
    }

    if (!chainInfo) return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkGetInstanceProcAddr nextGIPA = chainInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkCreateInstance realCreateInstance = (PFN_vkCreateInstance)nextGIPA(VK_NULL_HANDLE, "vkCreateInstance");

    chainInfo->u.pLayerInfo = chainInfo->u.pLayerInfo->pNext;

    VkResult res = realCreateInstance(pCreateInfo, pAllocator, pInstance);
    if (res != VK_SUCCESS) return res;

    g_instance = *pInstance;

    InstanceDispatch dispatch{};
    dispatch.GetInstanceProcAddr = nextGIPA;
    dispatch.DestroyInstance = (PFN_vkDestroyInstance)nextGIPA(*pInstance, "vkDestroyInstance");
    dispatch.GetPhysicalDeviceMemoryProperties = (PFN_vkGetPhysicalDeviceMemoryProperties)nextGIPA(*pInstance, "vkGetPhysicalDeviceMemoryProperties");

    {
        std::lock_guard<std::mutex> lock(g_dispatchLock);
        g_instanceDispatches[GetDispatchKey(*pInstance)] = dispatch;
        g_globalInstanceDispatch = dispatch;
    }

    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL Hook_CreateDevice(
    VkPhysicalDevice physicalDevice,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice* pDevice
) {
    std::cout << "[PB FrameFlux Hook] vkCreateDevice intercepted!" << std::endl;

    // Detect graphics queue family from DXVK's device request
    if (pCreateInfo->queueCreateInfoCount > 0 && pCreateInfo->pQueueCreateInfos) {
        g_graphicsQueueFamilyIndex = pCreateInfo->pQueueCreateInfos[0].queueFamilyIndex;
    }

    VkLayerDeviceCreateInfo* chainInfo = (VkLayerDeviceCreateInfo*)pCreateInfo->pNext;
    while (chainInfo && (chainInfo->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO ||
                         chainInfo->function != VK_LAYER_LINK_INFO)) {
        chainInfo = (VkLayerDeviceCreateInfo*)chainInfo->pNext;
    }

    if (!chainInfo) return VK_ERROR_INITIALIZATION_FAILED;

    // 1. Declare downstream pointers
    PFN_vkGetInstanceProcAddr nextGIPA = chainInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr nextGDPA = chainInfo->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    
    PFN_vkCreateDevice realCreateDevice = (PFN_vkCreateDevice)nextGIPA(g_instance, "vkCreateDevice");
    if (!realCreateDevice) {
        realCreateDevice = (PFN_vkCreateDevice)nextGIPA(VK_NULL_HANDLE, "vkCreateDevice");
    }

    // 2. Safely query downstream extension prober function pointer
    PFN_vkEnumerateDeviceExtensionProperties pfnEnum = 
        (PFN_vkEnumerateDeviceExtensionProperties)nextGIPA(g_instance, "vkEnumerateDeviceExtensionProperties");

    chainInfo->u.pLayerInfo = chainInfo->u.pLayerInfo->pNext;

VkResult res = realCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
    if (res != VK_SUCCESS) return res;

    // Query memory properties directly for THIS specific device and physical device
    PFN_vkGetPhysicalDeviceMemoryProperties pfnGetMemProps = 
        (PFN_vkGetPhysicalDeviceMemoryProperties)nextGIPA(g_instance, "vkGetPhysicalDeviceMemoryProperties");
    if (!pfnGetMemProps) {
        pfnGetMemProps = (PFN_vkGetPhysicalDeviceMemoryProperties)nextGIPA(VK_NULL_HANDLE, "vkGetPhysicalDeviceMemoryProperties");
    }

    if (pfnGetMemProps) {
        pfnGetMemProps(physicalDevice, &g_deviceMemoryProperties);
    }

    // 3. Probe extensions safely without calling loader trampolines
    ExtensionManager::Get().ProbeDeviceExtensions(physicalDevice, pCreateInfo, pfnEnum);

    DeviceDispatch dispatch{};
    dispatch.GetDeviceProcAddr = nextGDPA;
    dispatch.DestroyDevice = (PFN_vkDestroyDevice)nextGDPA(*pDevice, "vkDestroyDevice");
    dispatch.CreateSwapchainKHR = (PFN_vkCreateSwapchainKHR)nextGDPA(*pDevice, "vkCreateSwapchainKHR");
    dispatch.DestroySwapchainKHR = (PFN_vkDestroySwapchainKHR)nextGDPA(*pDevice, "vkDestroySwapchainKHR");
    dispatch.QueuePresentKHR = (PFN_vkQueuePresentKHR)nextGDPA(*pDevice, "vkQueuePresentKHR");

    {
        std::lock_guard<std::mutex> lock(g_dispatchLock);
        g_deviceDispatches[GetDispatchKey(*pDevice)] = dispatch;
        g_globalDeviceDispatch = dispatch;
    }

    Interceptor::Get().SetDeviceInfo(g_deviceMemoryProperties, g_graphicsQueueFamilyIndex);

    return VK_SUCCESS;
}

} // namespace FrameFlux

// -----------------------------------------------------------------------------
// Exported Layer ProcAddr Entry Points
// -----------------------------------------------------------------------------

VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL FrameFlux_GetDeviceProcAddr(
    VkDevice device,
    const char* pName
) {
    if (!pName) return nullptr;

    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) return (PFN_vkVoidFunction)FrameFlux_GetDeviceProcAddr;
    if (strcmp(pName, "vkCreateSwapchainKHR") == 0) return (PFN_vkVoidFunction)FrameFlux::Hook_CreateSwapchainKHR;
    if (strcmp(pName, "vkDestroySwapchainKHR") == 0) return (PFN_vkVoidFunction)FrameFlux::Hook_DestroySwapchainKHR;
    if (strcmp(pName, "vkQueuePresentKHR") == 0) return (PFN_vkVoidFunction)FrameFlux::Hook_QueuePresentKHR;

    std::lock_guard<std::mutex> lock(FrameFlux::g_dispatchLock);
    if (device != VK_NULL_HANDLE) {
        void* key = FrameFlux::GetDispatchKey(device);
        auto it = FrameFlux::g_deviceDispatches.find(key);
        if (it != FrameFlux::g_deviceDispatches.end() && it->second.GetDeviceProcAddr) {
            return it->second.GetDeviceProcAddr(device, pName);
        }
    }
    if (FrameFlux::g_globalDeviceDispatch.GetDeviceProcAddr) {
        return FrameFlux::g_globalDeviceDispatch.GetDeviceProcAddr(device, pName);
    }

    return nullptr;
}

VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL FrameFlux_GetInstanceProcAddr(
    VkInstance instance,
    const char* pName
) {
    if (!pName) return nullptr;

    if (strcmp(pName, "vkGetInstanceProcAddr") == 0) return (PFN_vkVoidFunction)FrameFlux_GetInstanceProcAddr;
    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) return (PFN_vkVoidFunction)FrameFlux_GetDeviceProcAddr;
    if (strcmp(pName, "vkCreateInstance") == 0) return (PFN_vkVoidFunction)FrameFlux::Hook_CreateInstance;
    if (strcmp(pName, "vkCreateDevice") == 0) return (PFN_vkVoidFunction)FrameFlux::Hook_CreateDevice;
    if (strcmp(pName, "vkGetPhysicalDeviceMemoryProperties") == 0) return (PFN_vkVoidFunction)FrameFlux::Hook_GetPhysicalDeviceMemoryProperties;

    if (strcmp(pName, "vkCreateSwapchainKHR") == 0) return (PFN_vkVoidFunction)FrameFlux::Hook_CreateSwapchainKHR;
    if (strcmp(pName, "vkDestroySwapchainKHR") == 0) return (PFN_vkVoidFunction)FrameFlux::Hook_DestroySwapchainKHR;
    if (strcmp(pName, "vkQueuePresentKHR") == 0) return (PFN_vkVoidFunction)FrameFlux::Hook_QueuePresentKHR;

    std::lock_guard<std::mutex> lock(FrameFlux::g_dispatchLock);
    if (instance != VK_NULL_HANDLE) {
        void* key = FrameFlux::GetDispatchKey(instance);
        auto it = FrameFlux::g_instanceDispatches.find(key);
        if (it != FrameFlux::g_instanceDispatches.end() && it->second.GetInstanceProcAddr) {
            return it->second.GetInstanceProcAddr(instance, pName);
        }
    }
    if (FrameFlux::g_globalInstanceDispatch.GetInstanceProcAddr) {
        return FrameFlux::g_globalInstanceDispatch.GetInstanceProcAddr(instance, pName);
    }

    return nullptr;
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(
    VkNegotiateLayerInterface* pVersionStruct
) {
    if (pVersionStruct->loaderLayerInterfaceVersion < 2) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    pVersionStruct->loaderLayerInterfaceVersion = 2;
    pVersionStruct->pfnGetInstanceProcAddr = FrameFlux_GetInstanceProcAddr;
    pVersionStruct->pfnGetDeviceProcAddr = FrameFlux_GetDeviceProcAddr;

    return VK_SUCCESS;
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL FrameFlux_NegotiateLoaderLayerInterfaceVersion(
    VkNegotiateLayerInterface* pVersionStruct
) {
    return vkNegotiateLoaderLayerInterfaceVersion(pVersionStruct);
}