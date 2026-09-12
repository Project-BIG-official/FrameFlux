// PB FrameFlux - LGPL-2.1
// layer/layer_entrypoints.cpp: Robust Wine/DXVK/Native Vulkan Interceptor

#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include "swapchain_interceptor.hpp"
#include "vulkan_extensions.hpp"
#include "vulkan_dispatch.hpp"

#include <unordered_map>
#include <mutex>
#include <vector>
#include <cstring>
#include <iostream>

#ifdef VK_LAYER_EXPORT
#undef VK_LAYER_EXPORT
#endif
#define VK_LAYER_EXPORT extern "C" __attribute__((visibility("default"), used))

namespace FrameFlux {

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

static VKAPI_ATTR VkResult VKAPI_CALL Hook_CreateSwapchainKHR(
    VkDevice device,
    const VkSwapchainCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSwapchainKHR* pSwapchain
) {
    if (!device || !pCreateInfo || !pSwapchain) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    PFN_vkCreateSwapchainKHR realFunc = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_dispatchLock);
        void* key = GetDispatchKey(device);
        auto it = g_deviceDispatches.find(key);
        realFunc = (it != g_deviceDispatches.end() && it->second.CreateSwapchainKHR) ? it->second.CreateSwapchainKHR : g_globalDeviceDispatch.CreateSwapchainKHR;
    }

    if (!realFunc) return VK_ERROR_INITIALIZATION_FAILED;
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
    if (!pCreateInfo || !pInstance) return VK_ERROR_INITIALIZATION_FAILED;

    VkLayerInstanceCreateInfo* chainInfo = (VkLayerInstanceCreateInfo*)pCreateInfo->pNext;
    while (chainInfo && (chainInfo->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO ||
                         chainInfo->function != VK_LAYER_LINK_INFO)) {
        chainInfo = (VkLayerInstanceCreateInfo*)chainInfo->pNext;
    }

    if (!chainInfo || !chainInfo->u.pLayerInfo) return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkGetInstanceProcAddr nextGIPA = chainInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    if (!nextGIPA) return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkCreateInstance realCreateInstance = (PFN_vkCreateInstance)nextGIPA(VK_NULL_HANDLE, "vkCreateInstance");
    if (!realCreateInstance) return VK_ERROR_INITIALIZATION_FAILED;

    chainInfo->u.pLayerInfo = chainInfo->u.pLayerInfo->pNext;

    VkResult res = realCreateInstance(pCreateInfo, pAllocator, pInstance);
    if (res != VK_SUCCESS) return res;

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
    if (!physicalDevice || !pCreateInfo || !pDevice) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::cout << "[PB FrameFlux Hook] vkCreateDevice intercepted!" << std::endl;

    if (pCreateInfo->queueCreateInfoCount > 0 && pCreateInfo->pQueueCreateInfos) {
        g_graphicsQueueFamilyIndex = pCreateInfo->pQueueCreateInfos[0].queueFamilyIndex;
    }

    VkLayerDeviceCreateInfo* chainInfo = (VkLayerDeviceCreateInfo*)pCreateInfo->pNext;
    while (chainInfo && (chainInfo->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO ||
                         chainInfo->function != VK_LAYER_LINK_INFO)) {
        chainInfo = (VkLayerDeviceCreateInfo*)chainInfo->pNext;
    }

    if (!chainInfo || !chainInfo->u.pLayerInfo) return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkGetInstanceProcAddr nextGIPA = chainInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr nextGDPA = chainInfo->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    
    if (!nextGIPA || !nextGDPA) return VK_ERROR_INITIALIZATION_FAILED;

    // ВАЖНО: Всегда передаем VK_NULL_HANDLE в nextGIPA для поиска функции создания устройства
    PFN_vkCreateDevice realCreateDevice = (PFN_vkCreateDevice)nextGIPA(VK_NULL_HANDLE, "vkCreateDevice");
    if (!realCreateDevice) {
        std::lock_guard<std::mutex> lock(g_dispatchLock);
        if (g_globalInstanceDispatch.GetInstanceProcAddr) {
            realCreateDevice = (PFN_vkCreateDevice)g_globalInstanceDispatch.GetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateDevice");
        }
    }
    if (!realCreateDevice) return VK_ERROR_INITIALIZATION_FAILED;

    // Продвигаем цепочку загрузчика
    chainInfo->u.pLayerInfo = chainInfo->u.pLayerInfo->pNext;

    // Опрашиваем физический GPU
    PFN_vkEnumerateDeviceExtensionProperties pfnEnum = 
        (PFN_vkEnumerateDeviceExtensionProperties)nextGIPA(VK_NULL_HANDLE, "vkEnumerateDeviceExtensionProperties");
    if (pfnEnum) {
        ExtensionManager::Get().CollectGpuExtensions(physicalDevice, pfnEnum);
    }

    // Собираем список расширений
    std::vector<const char*> enabledExts;
    if (pCreateInfo->ppEnabledExtensionNames) {
        for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; ++i) {
            enabledExts.push_back(pCreateInfo->ppEnabledExtensionNames[i]);
            ExtensionManager::Get().NotifyExtensionActivated(pCreateInfo->ppEnabledExtensionNames[i]);
        }
    }

    auto tryInjectExt = [&](const char* extName) -> bool {
        if (ExtensionManager::Get().IsDeviceExtensionSupportedByGPU(extName)) {
            for (const char* existing : enabledExts) {
                if (strcmp(existing, extName) == 0) return true;
            }
            enabledExts.push_back(extName);
            ExtensionManager::Get().NotifyExtensionActivated(extName);
            return true;
        }
        return false;
    };

    tryInjectExt("VK_KHR_calibrated_timestamps");
    tryInjectExt("VK_EXT_calibrated_timestamps");
    tryInjectExt("VK_EXT_present_timing");
    tryInjectExt("VK_GOOGLE_display_timing");
    tryInjectExt("VK_EXT_frame_boundary");
    tryInjectExt("VK_AMD_anti_lag");
    tryInjectExt("VK_NV_low_latency2");
    tryInjectExt("VK_KHR_present_wait");
    tryInjectExt("VK_KHR_present_wait2");
    tryInjectExt("VK_KHR_present_id");
    tryInjectExt("VK_KHR_swapchain_maintenance1");
    tryInjectExt("VK_EXT_swapchain_maintenance1");
    tryInjectExt("VK_EXT_subgroup_size_control");

    VkDeviceCreateInfo modifiedCreateInfo = *pCreateInfo;
    modifiedCreateInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExts.size());
    modifiedCreateInfo.ppEnabledExtensionNames = enabledExts.data();

    VkResult res = realCreateDevice(physicalDevice, &modifiedCreateInfo, pAllocator, pDevice);
    if (res != VK_SUCCESS) {
        return res;
    }

    // Регистрируем устройство в безопасной внутренней таблице диспетчеризации
    DispatchManager::Get().RegisterDevice(*pDevice, nextGDPA);
    ExtensionManager::Get().ResolveDeviceFunctions(*pDevice, nextGDPA);

    // Получаем свойства памяти и лимиты устройства
    PFN_vkGetPhysicalDeviceMemoryProperties pfnGetMemProps = 
        (PFN_vkGetPhysicalDeviceMemoryProperties)nextGIPA(VK_NULL_HANDLE, "vkGetPhysicalDeviceMemoryProperties");
    if (pfnGetMemProps) {
        pfnGetMemProps(physicalDevice, &g_deviceMemoryProperties);
    }

    VkPhysicalDeviceProperties devProps{};
    PFN_vkGetPhysicalDeviceProperties pfnGetProps = 
        (PFN_vkGetPhysicalDeviceProperties)nextGIPA(VK_NULL_HANDLE, "vkGetPhysicalDeviceProperties");
    if (pfnGetProps) {
        pfnGetProps(physicalDevice, &devProps);
    }

    float tsPeriod = (devProps.limits.timestampPeriod > 0.0f) ? devProps.limits.timestampPeriod : 1.0f;
    Interceptor::Get().SetDeviceInfo(g_deviceMemoryProperties, g_graphicsQueueFamilyIndex, tsPeriod);

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

    return VK_SUCCESS;
}

} // namespace FrameFlux

VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL FrameFlux_GetDeviceProcAddr(VkDevice device, const char* pName) {
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

VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL FrameFlux_GetInstanceProcAddr(VkInstance instance, const char* pName) {
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

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* pVersionStruct) {
    if (pVersionStruct->loaderLayerInterfaceVersion < 2) return VK_ERROR_INITIALIZATION_FAILED;
    pVersionStruct->loaderLayerInterfaceVersion = 2;
    pVersionStruct->pfnGetInstanceProcAddr = FrameFlux_GetInstanceProcAddr;
    pVersionStruct->pfnGetDeviceProcAddr = FrameFlux_GetDeviceProcAddr;
    return VK_SUCCESS;
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL FrameFlux_NegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* pVersionStruct) {
    return vkNegotiateLoaderLayerInterfaceVersion(pVersionStruct);
}