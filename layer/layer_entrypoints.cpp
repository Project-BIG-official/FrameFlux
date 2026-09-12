// PB FrameFlux - LGPL-2.1
// layer/layer_entrypoints.cpp: Robust Wine/DXVK/Native Vulkan Interceptor with Extension Injection

#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include "swapchain_interceptor.hpp"
#include "vulkan_extensions.hpp"

#include <unordered_map>
#include <mutex>
#include <vector>
#include <cstring>
#include <iostream>

#ifdef VK_LAYER_EXPORT
#undef VK_LAYER_EXPORT
#endif
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

    if (pCreateInfo->queueCreateInfoCount > 0 && pCreateInfo->pQueueCreateInfos) {
        g_graphicsQueueFamilyIndex = pCreateInfo->pQueueCreateInfos[0].queueFamilyIndex;
    }

    VkLayerDeviceCreateInfo* chainInfo = (VkLayerDeviceCreateInfo*)pCreateInfo->pNext;
    while (chainInfo && (chainInfo->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO ||
                         chainInfo->function != VK_LAYER_LINK_INFO)) {
        chainInfo = (VkLayerDeviceCreateInfo*)chainInfo->pNext;
    }

    if (!chainInfo) return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkGetInstanceProcAddr nextGIPA = chainInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr nextGDPA = chainInfo->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    
    PFN_vkCreateDevice realCreateDevice = (PFN_vkCreateDevice)nextGIPA(g_instance, "vkCreateDevice");
    if (!realCreateDevice) {
        realCreateDevice = (PFN_vkCreateDevice)nextGIPA(VK_NULL_HANDLE, "vkCreateDevice");
    }

    PFN_vkEnumerateDeviceExtensionProperties pfnEnum = 
        (PFN_vkEnumerateDeviceExtensionProperties)nextGIPA(g_instance, "vkEnumerateDeviceExtensionProperties");

    // 1. Опрашиваем физический GPU
    ExtensionManager::Get().CollectGpuExtensions(physicalDevice, pfnEnum);

    // 2. Проверяем реально доступные расширения на GPU
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

    // Активируем безопасные расширения без обязательных Features в pNext
    tryInjectExt("VK_KHR_calibrated_timestamps");
    tryInjectExt("VK_EXT_calibrated_timestamps");
    tryInjectExt("VK_EXT_present_timing");
    tryInjectExt("VK_GOOGLE_display_timing");
    tryInjectExt("VK_EXT_frame_boundary");
    tryInjectExt("VK_AMD_anti_lag");
    tryInjectExt("VK_NV_low_latency2");

    // 3. Безопасный запрос Features через vkGetPhysicalDeviceFeatures2
    PFN_vkGetPhysicalDeviceFeatures2KHR pfnGetFeat2 = 
        (PFN_vkGetPhysicalDeviceFeatures2KHR)nextGIPA(g_instance, "vkGetPhysicalDeviceFeatures2KHR");
    if (!pfnGetFeat2) {
        pfnGetFeat2 = (PFN_vkGetPhysicalDeviceFeatures2KHR)nextGIPA(g_instance, "vkGetPhysicalDeviceFeatures2");
    }

    void* currentPNext = (void*)pCreateInfo->pNext;

    VkPhysicalDevicePresentWaitFeaturesKHR waitFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR, nullptr, VK_FALSE};
    VkPhysicalDevicePresentIdFeaturesKHR idFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR, nullptr, VK_FALSE};
    VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT maintFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT, nullptr, VK_FALSE};
    VkPhysicalDeviceSubgroupSizeControlFeaturesEXT subgroupFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES_EXT, nullptr, VK_FALSE, VK_FALSE};

    if (pfnGetFeat2) {
        // Связываем структуру запроса
        void* queryChain = nullptr;
        auto appendQuery = [&](void* s) {
            VkBaseOutStructure* st = (VkBaseOutStructure*)s;
            st->pNext = (VkBaseOutStructure*)queryChain;
            queryChain = s;
        };

        bool suppWait = tryInjectExt("VK_KHR_present_wait");
        tryInjectExt("VK_KHR_present_wait2");
        bool suppId = tryInjectExt("VK_KHR_present_id");
        bool suppMaint = tryInjectExt("VK_KHR_swapchain_maintenance1") || tryInjectExt("VK_EXT_swapchain_maintenance1");
        bool suppSubgroup = tryInjectExt("VK_EXT_subgroup_size_control");

        if (suppWait) appendQuery(&waitFeatures);
        if (suppId) appendQuery(&idFeatures);
        if (suppMaint) appendQuery(&maintFeatures);
        if (suppSubgroup) appendQuery(&subgroupFeatures);

        VkPhysicalDeviceFeatures2 feat2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, queryChain, {}};
        pfnGetFeat2(physicalDevice, &feat2);

        // Инжектируем в VkDeviceCreateInfo ТОЛЬКО ТО, ЧТО ПОДДЕРЖИВАЕТСЯ ЖЕЛЕЗОМ!
        auto chainStruct = [&](void* structPtr) {
            VkBaseOutStructure* s = (VkBaseOutStructure*)structPtr;
            s->pNext = (VkBaseOutStructure*)currentPNext;
            currentPNext = structPtr;
        };

        if (suppWait && waitFeatures.presentWait) chainStruct(&waitFeatures);
        if (suppId && idFeatures.presentId) chainStruct(&idFeatures);
        if (suppMaint && maintFeatures.swapchainMaintenance1) chainStruct(&maintFeatures);
        if (suppSubgroup && subgroupFeatures.subgroupSizeControl) chainStruct(&subgroupFeatures);
    }

    VkDeviceCreateInfo modifiedCreateInfo = *pCreateInfo;
    modifiedCreateInfo.pNext = currentPNext;
    modifiedCreateInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExts.size());
    modifiedCreateInfo.ppEnabledExtensionNames = enabledExts.data();

    chainInfo->u.pLayerInfo = chainInfo->u.pLayerInfo->pNext;

    VkResult res = realCreateDevice(physicalDevice, &modifiedCreateInfo, pAllocator, pDevice);
    if (res != VK_SUCCESS) {
        // Fallback на случай строгого окружения
        res = realCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
        if (res != VK_SUCCESS) return res;
    }

    PFN_vkGetPhysicalDeviceMemoryProperties pfnGetMemProps = 
        (PFN_vkGetPhysicalDeviceMemoryProperties)nextGIPA(g_instance, "vkGetPhysicalDeviceMemoryProperties");
    if (pfnGetMemProps) {
        pfnGetMemProps(physicalDevice, &g_deviceMemoryProperties);
    }

    PFN_vkGetPhysicalDeviceProperties pfnGetProps = 
        (PFN_vkGetPhysicalDeviceProperties)nextGIPA(g_instance, "vkGetPhysicalDeviceProperties");
    VkPhysicalDeviceProperties props{};
    if (pfnGetProps) {
        pfnGetProps(physicalDevice, &props);
    }

    ExtensionManager::Get().ResolveDeviceFunctions(*pDevice, nextGDPA);

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

    float tsPeriod = (props.limits.timestampPeriod > 0.0f) ? props.limits.timestampPeriod : 1.0f;
    Interceptor::Get().SetDeviceInfo(g_deviceMemoryProperties, g_graphicsQueueFamilyIndex, tsPeriod);

    return VK_SUCCESS;
}

}

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