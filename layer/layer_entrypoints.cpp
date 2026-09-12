// PB FrameFlux - LGPL-2.1
// layer/layer_entrypoints.cpp: Robust Multi-Device & Extension Discovery Interceptor

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

static VkInstance g_instance = VK_NULL_HANDLE;
static PFN_vkGetInstanceProcAddr g_instanceGIPA = nullptr;
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
    PFN_vkGetDeviceQueue GetDeviceQueue = nullptr;
    PFN_vkGetDeviceQueue2 GetDeviceQueue2 = nullptr;
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

static VKAPI_ATTR void VKAPI_CALL Hook_GetDeviceQueue(
    VkDevice device,
    uint32_t queueFamilyIndex,
    uint32_t queueIndex,
    VkQueue* pQueue
) {
    PFN_vkGetDeviceQueue realFunc = nullptr;
    DeviceDispatch dispatch{};
    {
        std::lock_guard<std::mutex> lock(g_dispatchLock);
        void* key = GetDispatchKey(device);
        auto it = g_deviceDispatches.find(key);
        if (it != g_deviceDispatches.end()) {
            dispatch = it->second;
            realFunc = dispatch.GetDeviceQueue;
        } else {
            realFunc = g_globalDeviceDispatch.GetDeviceQueue;
            dispatch = g_globalDeviceDispatch;
        }
    }

    if (realFunc) {
        realFunc(device, queueFamilyIndex, queueIndex, pQueue);
        if (pQueue && *pQueue) {
            std::lock_guard<std::mutex> lock(g_dispatchLock);
            g_deviceDispatches[GetDispatchKey(*pQueue)] = dispatch;
        }
    }
}

static VKAPI_ATTR void VKAPI_CALL Hook_GetDeviceQueue2(
    VkDevice device,
    const VkDeviceQueueInfo2* pQueueInfo,
    VkQueue* pQueue
) {
    PFN_vkGetDeviceQueue2 realFunc = nullptr;
    DeviceDispatch dispatch{};
    {
        std::lock_guard<std::mutex> lock(g_dispatchLock);
        void* key = GetDispatchKey(device);
        auto it = g_deviceDispatches.find(key);
        if (it != g_deviceDispatches.end()) {
            dispatch = it->second;
            realFunc = dispatch.GetDeviceQueue2;
        } else {
            realFunc = g_globalDeviceDispatch.GetDeviceQueue2;
            dispatch = g_globalDeviceDispatch;
        }
    }

    if (realFunc) {
        realFunc(device, pQueueInfo, pQueue);
        if (pQueue && *pQueue) {
            std::lock_guard<std::mutex> lock(g_dispatchLock);
            g_deviceDispatches[GetDispatchKey(*pQueue)] = dispatch;
        }
    }
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

    // ТЕПЕРЬ low_latency_layer гарантированно завершил создание VkDevice
    // и успешно вернет валидный указатель на vkAntiLagUpdateAMD!
    ExtensionManager::Get().ResolveDeviceFunctions(device, nullptr);

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

// Добавьте реализацию хука перечисления расширений:
static VKAPI_ATTR VkResult VKAPI_CALL Hook_EnumerateDeviceExtensionProperties(
    VkPhysicalDevice physicalDevice,
    const char* pLayerName,
    uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties
) {
    PFN_vkEnumerateDeviceExtensionProperties realFunc = nullptr;
    if (g_instance && g_instanceGIPA) {
        realFunc = (PFN_vkEnumerateDeviceExtensionProperties)g_instanceGIPA(g_instance, "vkEnumerateDeviceExtensionProperties");
    }
    if (!realFunc) {
        std::lock_guard<std::mutex> lock(g_dispatchLock);
        if (g_globalInstanceDispatch.GetInstanceProcAddr) {
            realFunc = (PFN_vkEnumerateDeviceExtensionProperties)g_globalInstanceDispatch.GetInstanceProcAddr(g_instance, "vkEnumerateDeviceExtensionProperties");
        }
    }

    if (!realFunc) return VK_ERROR_INITIALIZATION_FAILED;

    // Сначала получаем расширения от нижестоящих слоев и драйвера
    uint32_t rawCount = 0;
    VkResult res = realFunc(physicalDevice, pLayerName, &rawCount, nullptr);
    if (res != VK_SUCCESS) return res;

    std::vector<VkExtensionProperties> exts(rawCount);
    res = realFunc(physicalDevice, pLayerName, &rawCount, exts.data());
    if (res != VK_SUCCESS) return res;

    // Внедряем расширения low-latency, чтобы игра (CS2/DXVK) увидела их в своем меню!
    auto addExtIfMissing = [&](const char* name, uint32_t specVersion) {
        for (const auto& e : exts) {
            if (strcmp(e.extensionName, name) == 0) return;
        }
        VkExtensionProperties prop{};
        strncpy(prop.extensionName, name, VK_MAX_EXTENSION_NAME_SIZE - 1);
        prop.specVersion = specVersion;
        exts.push_back(prop);
    };

    addExtIfMissing("VK_AMD_anti_lag", 1);
    addExtIfMissing("VK_NV_low_latency2", 2);

    if (!pProperties) {
        *pPropertyCount = static_cast<uint32_t>(exts.size());
        return VK_SUCCESS;
    }

    uint32_t toCopy = std::min(*pPropertyCount, static_cast<uint32_t>(exts.size()));
    std::memcpy(pProperties, exts.data(), toCopy * sizeof(VkExtensionProperties));
    *pPropertyCount = toCopy;

    return (toCopy < exts.size()) ? VK_INCOMPLETE : VK_SUCCESS;
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
    if (!queue || !pPresentInfo) return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkQueuePresentKHR realFunc = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_dispatchLock);
        void* key = GetDispatchKey(queue);
        auto it = g_deviceDispatches.find(key);
        if (it != g_deviceDispatches.end() && it->second.QueuePresentKHR) {
            realFunc = it->second.QueuePresentKHR;
        } else {
            realFunc = g_globalDeviceDispatch.QueuePresentKHR;
        }
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

    g_instance = *pInstance;
    g_instanceGIPA = nextGIPA;

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

    PFN_vkCreateDevice realCreateDevice = (PFN_vkCreateDevice)nextGIPA(VK_NULL_HANDLE, "vkCreateDevice");
    if (!realCreateDevice && g_instance) {
        realCreateDevice = (PFN_vkCreateDevice)nextGIPA(g_instance, "vkCreateDevice");
    }
    if (!realCreateDevice) {
        std::lock_guard<std::mutex> lock(g_dispatchLock);
        if (g_globalInstanceDispatch.GetInstanceProcAddr) {
            realCreateDevice = (PFN_vkCreateDevice)g_globalInstanceDispatch.GetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateDevice");
        }
    }
    if (!realCreateDevice) return VK_ERROR_INITIALIZATION_FAILED;

    chainInfo->u.pLayerInfo = chainInfo->u.pLayerInfo->pNext;

    // Enumerate physical GPU extensions via g_instance
    PFN_vkEnumerateDeviceExtensionProperties pfnEnum = nullptr;
    if (g_instance) {
        if (g_instanceGIPA) {
            pfnEnum = (PFN_vkEnumerateDeviceExtensionProperties)g_instanceGIPA(g_instance, "vkEnumerateDeviceExtensionProperties");
        }
        if (!pfnEnum) {
            pfnEnum = (PFN_vkEnumerateDeviceExtensionProperties)nextGIPA(g_instance, "vkEnumerateDeviceExtensionProperties");
        }
    }
    if (pfnEnum) {
        ExtensionManager::Get().CollectGpuExtensions(physicalDevice, pfnEnum);
    }

    std::vector<const char*> enabledExts;
    if (pCreateInfo->ppEnabledExtensionNames) {
        for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; ++i) {
            enabledExts.push_back(pCreateInfo->ppEnabledExtensionNames[i]);
            ExtensionManager::Get().NotifyExtensionActivated(pCreateInfo->ppEnabledExtensionNames[i]);

            // Check if game natively requested Anti-Lag (e.g. Linux native CS2)
            if (strcmp(pCreateInfo->ppEnabledExtensionNames[i], "VK_AMD_anti_lag") == 0) {
                ExtensionManager::Get().SetGameRequestedAntiLag(true);
            }
        }
    }

    bool hasLowLatencyEnv = (getenv("LOW_LATENCY_LAYER") != nullptr && strcmp(getenv("LOW_LATENCY_LAYER"), "0") != 0);

    auto tryInjectExt = [&](const char* extName, bool force = false) -> bool {
        if (force || ExtensionManager::Get().IsDeviceExtensionSupportedByGPU(extName)) {
            for (const char* existing : enabledExts) {
                if (strcmp(existing, extName) == 0) return true;
            }
            enabledExts.push_back(extName);
            ExtensionManager::Get().NotifyExtensionActivated(extName);
            return true;
        }
        return false;
    };

    // Core timing and synchronization extensions
    tryInjectExt("VK_KHR_calibrated_timestamps");
    tryInjectExt("VK_EXT_calibrated_timestamps");
    tryInjectExt("VK_EXT_present_timing");
    tryInjectExt("VK_GOOGLE_display_timing");
    tryInjectExt("VK_EXT_frame_boundary");
    tryInjectExt("VK_EXT_subgroup_size_control");

    // Present wait & queue drain extensions
    tryInjectExt("VK_KHR_present_id");
    tryInjectExt("VK_KHR_present_id2");
    tryInjectExt("VK_KHR_present_wait");
    tryInjectExt("VK_KHR_present_wait2");
    tryInjectExt("VK_KHR_swapchain_maintenance1");
    tryInjectExt("VK_EXT_swapchain_maintenance1");

    // Low latency extensions
    tryInjectExt("VK_AMD_anti_lag", hasLowLatencyEnv);
    tryInjectExt("VK_NV_low_latency2", hasLowLatencyEnv || getenv("LOW_LATENCY_LAYER_REFLEX") != nullptr);

    VkDeviceCreateInfo modifiedCreateInfo = *pCreateInfo;
    modifiedCreateInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExts.size());
    modifiedCreateInfo.ppEnabledExtensionNames = enabledExts.data();

    VkResult res = realCreateDevice(physicalDevice, &modifiedCreateInfo, pAllocator, pDevice);
    if (res != VK_SUCCESS) {
        return res;
    }

    DispatchManager::Get().RegisterDevice(*pDevice, nextGDPA);
    ExtensionManager::Get().ResolveDeviceFunctions(*pDevice, nextGDPA);

    PFN_vkGetPhysicalDeviceMemoryProperties pfnGetMemProps = nullptr;
    if (g_instance && g_instanceGIPA) {
        pfnGetMemProps = (PFN_vkGetPhysicalDeviceMemoryProperties)g_instanceGIPA(g_instance, "vkGetPhysicalDeviceMemoryProperties");
    }
    if (!pfnGetMemProps && g_instance) {
        pfnGetMemProps = (PFN_vkGetPhysicalDeviceMemoryProperties)nextGIPA(g_instance, "vkGetPhysicalDeviceMemoryProperties");
    }
    if (pfnGetMemProps) {
        pfnGetMemProps(physicalDevice, &g_deviceMemoryProperties);
    }

    VkPhysicalDeviceProperties devProps{};
    PFN_vkGetPhysicalDeviceProperties pfnGetProps = nullptr;
    if (g_instance && g_instanceGIPA) {
        pfnGetProps = (PFN_vkGetPhysicalDeviceProperties)g_instanceGIPA(g_instance, "vkGetPhysicalDeviceProperties");
    }
    if (!pfnGetProps && g_instance) {
        pfnGetProps = (PFN_vkGetPhysicalDeviceProperties)nextGIPA(g_instance, "vkGetPhysicalDeviceProperties");
    }
    if (pfnGetProps) {
        pfnGetProps(physicalDevice, &devProps);
    }

    float tsPeriod = (devProps.limits.timestampPeriod > 0.0f) ? devProps.limits.timestampPeriod : 1.0f;
    Interceptor::Get().SetDeviceInfo(g_deviceMemoryProperties, g_graphicsQueueFamilyIndex, tsPeriod);

    DeviceDispatch dispatch{};
    dispatch.GetDeviceProcAddr = nextGDPA;
    dispatch.DestroyDevice = (PFN_vkDestroyDevice)nextGDPA(*pDevice, "vkDestroyDevice");
    dispatch.GetDeviceQueue = (PFN_vkGetDeviceQueue)nextGDPA(*pDevice, "vkGetDeviceQueue");
    dispatch.GetDeviceQueue2 = (PFN_vkGetDeviceQueue2)nextGDPA(*pDevice, "vkGetDeviceQueue2");
    dispatch.CreateSwapchainKHR = (PFN_vkCreateSwapchainKHR)nextGDPA(*pDevice, "vkCreateSwapchainKHR");
    dispatch.DestroySwapchainKHR = (PFN_vkDestroySwapchainKHR)nextGDPA(*pDevice, "vkDestroySwapchainKHR");
    dispatch.QueuePresentKHR = (PFN_vkQueuePresentKHR)nextGDPA(*pDevice, "vkQueuePresentKHR");

    {
        std::lock_guard<std::mutex> lock(g_dispatchLock);
        g_deviceDispatches[GetDispatchKey(*pDevice)] = dispatch;
        g_globalDeviceDispatch = dispatch;
    }

    if (dispatch.GetDeviceQueue) {
        for (uint32_t q = 0; q < pCreateInfo->queueCreateInfoCount; ++q) {
            uint32_t qFam = pCreateInfo->pQueueCreateInfos[q].queueFamilyIndex;
            for (uint32_t qc = 0; qc < pCreateInfo->pQueueCreateInfos[q].queueCount; ++qc) {
                VkQueue qHandle = VK_NULL_HANDLE;
                dispatch.GetDeviceQueue(*pDevice, qFam, qc, &qHandle);
                if (qHandle != VK_NULL_HANDLE) {
                    std::lock_guard<std::mutex> lock(g_dispatchLock);
                    g_deviceDispatches[GetDispatchKey(qHandle)] = dispatch;
                }
            }
        }
    }

    return VK_SUCCESS;
}

} // namespace FrameFlux

VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL FrameFlux_GetDeviceProcAddr(VkDevice device, const char* pName) {
    if (!pName) return nullptr;
    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) return (PFN_vkVoidFunction)FrameFlux_GetDeviceProcAddr;
    if (strcmp(pName, "vkGetDeviceQueue") == 0) return (PFN_vkVoidFunction)FrameFlux::Hook_GetDeviceQueue;
    if (strcmp(pName, "vkGetDeviceQueue2") == 0) return (PFN_vkVoidFunction)FrameFlux::Hook_GetDeviceQueue2;
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
    if (strcmp(pName, "vkEnumerateDeviceExtensionProperties") == 0) return (PFN_vkVoidFunction)FrameFlux::Hook_EnumerateDeviceExtensionProperties; // <-- ДОБАВИТЬ
    if (strcmp(pName, "vkGetDeviceQueue") == 0) return (PFN_vkVoidFunction)FrameFlux::Hook_GetDeviceQueue;
    if (strcmp(pName, "vkGetDeviceQueue2") == 0) return (PFN_vkVoidFunction)FrameFlux::Hook_GetDeviceQueue2;
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