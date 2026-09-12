// PB FrameFlux - LGPL-2.1
// runtime/vulkan_extensions.hpp: Full Multi-Level Fallback Extensions System with Universal CI Compatibility

#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <cstring>
#include <iostream>
#include <cstdint>
#include <cmath>
#include <chrono>

#ifdef __linux__
#include <time.h>
#endif

// -----------------------------------------------------------------------------
// 1. AMD Anti-Lag
// -----------------------------------------------------------------------------
#ifndef VK_AMD_anti_lag
#define VK_AMD_anti_lag 1
#define VK_STRUCTURE_TYPE_ANTI_LAG_DATA_AMD ((VkStructureType)1000476000)
#define VK_STRUCTURE_TYPE_ANTI_LAG_PRESENTATION_INFO_AMD ((VkStructureType)1000476001)

enum VkAntiLagModeAMD {
    VK_ANTI_LAG_MODE_DRIVER_CONTROL_AMD = 0,
    VK_ANTI_LAG_MODE_ON_AMD = 1,
    VK_ANTI_LAG_MODE_OFF_AMD = 2
};
enum VkAntiLagStageAMD {
    VK_ANTI_LAG_STAGE_INPUT_AMD = 0,
    VK_ANTI_LAG_STAGE_PRESENT_AMD = 1
};
struct VkAntiLagPresentationInfoAMD {
    VkStructureType sType;
    void* pNext;
    VkAntiLagStageAMD stage;
    uint64_t frameIndex;
};
struct VkAntiLagDataAMD {
    VkStructureType sType;
    const void* pNext;
    VkAntiLagModeAMD mode;
    uint32_t maxFPS;
    const VkAntiLagPresentationInfoAMD* pPresentationInfo;
};
typedef void (VKAPI_PTR *PFN_vkAntiLagUpdateAMD)(VkDevice device, const VkAntiLagDataAMD* pData);
#endif

// -----------------------------------------------------------------------------
// 2. NVIDIA Low Latency 2 (Reflex)
// -----------------------------------------------------------------------------
#ifndef VK_NV_low_latency2
#define VK_NV_low_latency2 1
#define VK_STRUCTURE_TYPE_SET_LATENCY_MARKER_INFO_NV ((VkStructureType)1000505002)
#define VK_STRUCTURE_TYPE_LATENCY_SLEEP_INFO_NV ((VkStructureType)1000505001)

enum VkLatencyMarkerNV {
    VK_LATENCY_MARKER_SIMULATION_START_NV = 0,
    VK_LATENCY_MARKER_SIMULATION_END_NV = 1,
    VK_LATENCY_MARKER_RENDERSUBMIT_START_NV = 2,
    VK_LATENCY_MARKER_RENDERSUBMIT_END_NV = 3,
    VK_LATENCY_MARKER_PRESENT_START_NV = 4,
    VK_LATENCY_MARKER_PRESENT_END_NV = 5,
    VK_LATENCY_MARKER_INPUT_SAMPLE_NV = 6,
    VK_LATENCY_MARKER_TRIGGER_FLASH_NV = 7
};
struct VkSetLatencyMarkerInfoNV {
    VkStructureType sType;
    const void* pNext;
    uint64_t presentID;
    VkLatencyMarkerNV marker;
};
struct VkLatencySleepInfoNV {
    VkStructureType sType;
    const void* pNext;
    VkSemaphore signalSemaphore;
    uint64_t value;
};
typedef VkResult (VKAPI_PTR *PFN_vkSetLatencyMarkerNV)(VkDevice device, VkSwapchainKHR swapchain, const VkSetLatencyMarkerInfoNV* pLatencyMarkerInfo);
typedef VkResult (VKAPI_PTR *PFN_vkLatencySleepNV)(VkDevice device, VkSwapchainKHR swapchain, const VkLatencySleepInfoNV* pSleepInfo);
#endif

// -----------------------------------------------------------------------------
// 3. Present ID & Present Wait
// -----------------------------------------------------------------------------
#ifndef VK_KHR_present_id
#define VK_KHR_present_id 1
#define VK_STRUCTURE_TYPE_PRESENT_ID_KHR ((VkStructureType)1000268000)
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR ((VkStructureType)1000268001)
struct VkPresentIdKHR {
    VkStructureType sType;
    const void* pNext;
    uint32_t swapchainCount;
    const uint64_t* pPresentIds;
};
struct VkPhysicalDevicePresentIdFeaturesKHR {
    VkStructureType sType;
    void* pNext;
    VkBool32 presentId;
};
#endif

#ifndef VK_KHR_present_wait
#define VK_KHR_present_wait 1
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR ((VkStructureType)1000248000)
struct VkPhysicalDevicePresentWaitFeaturesKHR {
    VkStructureType sType;
    void* pNext;
    VkBool32 presentWait;
};
typedef VkResult (VKAPI_PTR *PFN_vkWaitForPresentKHR)(VkDevice device, VkSwapchainKHR swapchain, uint64_t presentId, uint64_t timeout);
#endif

#ifndef VK_KHR_present_wait2
#define VK_KHR_present_wait2 1
#define VK_STRUCTURE_TYPE_PRESENT_WAIT_2_INFO_KHR ((VkStructureType)1000481000)
struct VkPresentWait2InfoKHR {
    VkStructureType sType;
    const void* pNext;
    uint64_t presentId;
    uint64_t timeout;
};
typedef VkResult (VKAPI_PTR *PFN_vkWaitForPresent2KHR)(VkDevice device, VkSwapchainKHR swapchain, const VkPresentWait2InfoKHR* pPresentWait2Info);
#endif

// -----------------------------------------------------------------------------
// 4. Swapchain Maintenance 1
// -----------------------------------------------------------------------------
#ifndef VK_EXT_swapchain_maintenance1
#define VK_EXT_swapchain_maintenance1 1
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT ((VkStructureType)1000275000)
#define VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT ((VkStructureType)1000275001)
struct VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT {
    VkStructureType sType;
    void* pNext;
    VkBool32 swapchainMaintenance1;
};
struct VkSwapchainPresentFenceInfoEXT {
    VkStructureType sType;
    const void* pNext;
    uint32_t swapchainCount;
    const VkFence* pFences;
};
#endif

#ifndef VK_KHR_swapchain_maintenance1
#define VK_KHR_swapchain_maintenance1 1
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT
#define VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_KHR VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT
typedef VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR;
typedef VkSwapchainPresentFenceInfoEXT VkSwapchainPresentFenceInfoKHR;
#endif

// -----------------------------------------------------------------------------
// 5. Frame Boundary
// -----------------------------------------------------------------------------
#ifndef VK_EXT_frame_boundary
#define VK_EXT_frame_boundary 1
#define VK_STRUCTURE_TYPE_FRAME_BOUNDARY_EXT ((VkStructureType)1000375000)
#define VK_FRAME_BOUNDARY_FRAME_END_BIT_EXT 0x00000001
typedef VkFlags VkFrameBoundaryFlagsEXT;
struct VkFrameBoundaryEXT {
    VkStructureType sType;
    const void* pNext;
    VkFrameBoundaryFlagsEXT flags;
    uint64_t frameID;
    uint32_t imageCount;
    const VkImage* pImages;
    uint32_t bufferCount;
    const VkBuffer* pBuffers;
    uint64_t tagName;
    size_t tagSize;
    const void* pTag;
};
#endif

// -----------------------------------------------------------------------------
// 6. Display Timing
// -----------------------------------------------------------------------------
#ifndef VK_EXT_present_timing
#define VK_EXT_present_timing 1
#define VK_STRUCTURE_TYPE_SWAPCHAIN_TIMING_PROPERTIES_EXT ((VkStructureType)1000209000)
struct VkSwapchainTimingPropertiesEXT {
    VkStructureType sType;
    void* pNext;
    uint64_t refreshDuration;
    uint64_t refreshInterval;
};
typedef VkResult (VKAPI_PTR *PFN_vkGetSwapchainTimingPropertiesEXT)(VkDevice device, VkSwapchainKHR swapchain, VkSwapchainTimingPropertiesEXT* pSwapchainTimingProperties, uint64_t* pSwapchainTimingPropertiesCounter);
#endif

#ifndef VK_GOOGLE_display_timing
#define VK_GOOGLE_display_timing 1
struct VkRefreshCycleDurationGOOGLE {
    uint64_t refreshDuration;
};
typedef VkResult (VKAPI_PTR *PFN_vkGetRefreshCycleDurationGOOGLE)(VkDevice device, VkSwapchainKHR swapchain, VkRefreshCycleDurationGOOGLE* pDisplayTimingProperties);
#endif

// -----------------------------------------------------------------------------
// 7. Calibrated Timestamps (Безопасный маппинг KHR <-> EXT для любых версий SDK)
// -----------------------------------------------------------------------------
#ifndef VK_EXT_calibrated_timestamps
#define VK_EXT_calibrated_timestamps 1
#define VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT ((VkStructureType)1000184000)
enum VkTimeDomainEXT {
    VK_TIME_DOMAIN_DEVICE_EXT = 0,
    VK_TIME_DOMAIN_CLOCK_MONOTONIC_EXT = 1,
    VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_EXT = 2,
    VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_EXT = 3
};
struct VkCalibratedTimestampInfoEXT {
    VkStructureType sType;
    const void* pNext;
    VkTimeDomainEXT timeDomain;
};
typedef VkResult (VKAPI_PTR *PFN_vkGetCalibratedTimestampsEXT)(VkDevice device, uint32_t timestampCount, const VkCalibratedTimestampInfoEXT* pTimestampInfos, uint64_t* pTimestamps, uint64_t* pMaxDeviation);
#endif

#ifndef VK_KHR_calibrated_timestamps
#define VK_KHR_calibrated_timestamps 1
#define VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT
#define VK_TIME_DOMAIN_DEVICE_KHR VK_TIME_DOMAIN_DEVICE_EXT
#define VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR VK_TIME_DOMAIN_CLOCK_MONOTONIC_EXT
#define VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_KHR VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_EXT
#define VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_KHR VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_EXT
typedef VkTimeDomainEXT VkTimeDomainKHR;
typedef VkCalibratedTimestampInfoEXT VkCalibratedTimestampInfoKHR;
typedef PFN_vkGetCalibratedTimestampsEXT PFN_vkGetCalibratedTimestampsKHR;
#endif

// -----------------------------------------------------------------------------
// 8. Subgroup Size Control
// -----------------------------------------------------------------------------
#ifndef VK_EXT_subgroup_size_control
#define VK_EXT_subgroup_size_control 1
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES_EXT ((VkStructureType)1000225000)
#define VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO_EXT ((VkStructureType)1000225001)
#define VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT 0x00000002
struct VkPhysicalDeviceSubgroupSizeControlFeaturesEXT {
    VkStructureType sType;
    void* pNext;
    VkBool32 subgroupSizeControl;
    VkBool32 computeFullSubgroups;
};
struct VkPipelineShaderStageRequiredSubgroupSizeCreateInfoEXT {
    VkStructureType sType;
    void* pNext;
    uint32_t requiredSubgroupSize;
};
#endif

namespace FrameFlux {

struct SupportedExtensions {
    bool hasSwapchainMaintenance1 = false;
    bool hasPresentWait2 = false;
    bool hasPresentWait = false;
    bool hasPresentId = false;
    bool hasExtPresentTiming = false;
    bool hasGoogleDisplayTiming = false;
    bool hasCalibratedTimestampsKHR = false;
    bool hasCalibratedTimestampsEXT = false;
    bool hasFrameBoundary = false;
    bool hasSubgroupSizeControl = false;
    bool hasNvidiaOFA = false;
    bool hasLowLatencyNV = false;
    bool hasAntiLagAMD = false;
};

class ExtensionManager {
public:
    static ExtensionManager& Get() {
        static ExtensionManager instance;
        return instance;
    }

    const SupportedExtensions& GetSupported() const { return m_ext; }

    bool IsDeviceExtensionSupportedByGPU(const char* name) const {
        for (const auto& s : m_availableGpuExtensions) {
            if (s == name) return true;
        }
        return false;
    }

    void CollectGpuExtensions(VkPhysicalDevice physicalDevice, PFN_vkEnumerateDeviceExtensionProperties pfnEnumDeviceExt) {
        if (!pfnEnumDeviceExt || physicalDevice == VK_NULL_HANDLE) return;
        uint32_t count = 0;
        if (pfnEnumDeviceExt(physicalDevice, nullptr, &count, nullptr) == VK_SUCCESS && count > 0) {
            std::vector<VkExtensionProperties> extensions(count);
            if (pfnEnumDeviceExt(physicalDevice, nullptr, &count, extensions.data()) == VK_SUCCESS) {
                m_availableGpuExtensions.clear();
                for (const auto& ext : extensions) {
                    m_availableGpuExtensions.push_back(ext.extensionName);
                    if (strcmp(ext.extensionName, "VK_EXT_subgroup_size_control") == 0) m_ext.hasSubgroupSizeControl = true;
                    if (strcmp(ext.extensionName, "VK_NV_optical_flow") == 0) m_ext.hasNvidiaOFA = true;
                }
            }
        }
    }

    void NotifyExtensionActivated(const char* name) {
        if (!name) return;
        if (strcmp(name, "VK_EXT_swapchain_maintenance1") == 0 || strcmp(name, "VK_KHR_swapchain_maintenance1") == 0) m_ext.hasSwapchainMaintenance1 = true;
        else if (strcmp(name, "VK_KHR_present_wait2") == 0) m_ext.hasPresentWait2 = true;
        else if (strcmp(name, "VK_KHR_present_wait") == 0) m_ext.hasPresentWait = true;
        else if (strcmp(name, "VK_KHR_present_id") == 0) m_ext.hasPresentId = true;
        else if (strcmp(name, "VK_EXT_present_timing") == 0) m_ext.hasExtPresentTiming = true;
        else if (strcmp(name, "VK_GOOGLE_display_timing") == 0) m_ext.hasGoogleDisplayTiming = true;
        else if (strcmp(name, "VK_KHR_calibrated_timestamps") == 0) m_ext.hasCalibratedTimestampsKHR = true;
        else if (strcmp(name, "VK_EXT_calibrated_timestamps") == 0) m_ext.hasCalibratedTimestampsEXT = true;
        else if (strcmp(name, "VK_EXT_frame_boundary") == 0) m_ext.hasFrameBoundary = true;
        else if (strcmp(name, "VK_NV_low_latency2") == 0) m_ext.hasLowLatencyNV = true;
        else if (strcmp(name, "VK_AMD_anti_lag") == 0) m_ext.hasAntiLagAMD = true;
        else if (strcmp(name, "VK_EXT_subgroup_size_control") == 0) m_ext.hasSubgroupSizeControl = true;
    }

    void ResolveDeviceFunctions(VkDevice device, PFN_vkGetDeviceProcAddr gdpa) {
        if (!device || !gdpa) return;

        if (m_ext.hasPresentWait2) pfnWaitForPresent2KHR = (PFN_vkWaitForPresent2KHR)gdpa(device, "vkWaitForPresent2KHR");
        if (m_ext.hasPresentWait) pfnWaitForPresentKHR = (PFN_vkWaitForPresentKHR)gdpa(device, "vkWaitForPresentKHR");

        if (m_ext.hasAntiLagAMD) pfnAntiLagUpdateAMD = (PFN_vkAntiLagUpdateAMD)gdpa(device, "vkAntiLagUpdateAMD");
        if (m_ext.hasLowLatencyNV) {
            pfnSetLatencyMarkerNV = (PFN_vkSetLatencyMarkerNV)gdpa(device, "vkSetLatencyMarkerNV");
            pfnLatencySleepNV = (PFN_vkLatencySleepNV)gdpa(device, "vkLatencySleepNV");
        }

        if (m_ext.hasExtPresentTiming) pfnGetSwapchainTimingPropertiesEXT = (PFN_vkGetSwapchainTimingPropertiesEXT)gdpa(device, "vkGetSwapchainTimingPropertiesEXT");
        if (m_ext.hasGoogleDisplayTiming) pfnGetRefreshCycleDurationGOOGLE = (PFN_vkGetRefreshCycleDurationGOOGLE)gdpa(device, "vkGetRefreshCycleDurationGOOGLE");

        if (m_ext.hasCalibratedTimestampsKHR) pfnGetCalibratedTimestampsKHR = (PFN_vkGetCalibratedTimestampsKHR)gdpa(device, "vkGetCalibratedTimestampsKHR");
        if (m_ext.hasCalibratedTimestampsEXT) pfnGetCalibratedTimestampsEXT = (PFN_vkGetCalibratedTimestampsEXT)gdpa(device, "vkGetCalibratedTimestampsEXT");
    }

    VkResult WaitForPresentQueue(VkDevice device, VkSwapchainKHR swapchain, uint64_t presentId, uint32_t mode = 0) {
        if (mode == 3 || device == VK_NULL_HANDLE || swapchain == VK_NULL_HANDLE || presentId == 0) {
            return VK_SUCCESS;
        }

        constexpr uint64_t timeoutNs = 20000000ULL;

        if ((mode == 0 || mode == 1) && pfnWaitForPresent2KHR) {
            VkPresentWait2InfoKHR waitInfo{VK_STRUCTURE_TYPE_PRESENT_WAIT_2_INFO_KHR, nullptr, presentId, timeoutNs};
            return pfnWaitForPresent2KHR(device, swapchain, &waitInfo);
        }

        if ((mode == 0 || mode == 2) && pfnWaitForPresentKHR) {
            return pfnWaitForPresentKHR(device, swapchain, presentId, timeoutNs);
        }

        return VK_SUCCESS;
    }

    void MarkAntiLagStage(VkDevice device, VkAntiLagStageAMD stage, uint64_t frameIndex, uint32_t debugOverride = 0) {
        if (debugOverride == 2 || debugOverride == 3 || debugOverride == 4) return;

        if (pfnAntiLagUpdateAMD && device != VK_NULL_HANDLE) {
            VkAntiLagPresentationInfoAMD presInfo{VK_STRUCTURE_TYPE_ANTI_LAG_PRESENTATION_INFO_AMD, nullptr, stage, frameIndex};
            VkAntiLagDataAMD data{VK_STRUCTURE_TYPE_ANTI_LAG_DATA_AMD, nullptr, VK_ANTI_LAG_MODE_ON_AMD, 0, &presInfo};
            pfnAntiLagUpdateAMD(device, &data);
        }
    }

    void MarkReflexMarker(VkDevice device, VkSwapchainKHR swapchain, VkLatencyMarkerNV marker, uint64_t frameId, uint32_t debugOverride = 0) {
        if (debugOverride == 1 || debugOverride == 3 || debugOverride == 4) return;

        if (pfnSetLatencyMarkerNV && device != VK_NULL_HANDLE && swapchain != VK_NULL_HANDLE) {
            VkSetLatencyMarkerInfoNV info{VK_STRUCTURE_TYPE_SET_LATENCY_MARKER_INFO_NV, nullptr, frameId, marker};
            pfnSetLatencyMarkerNV(device, swapchain, &info);
        }
    }

    uint64_t QueryDisplayRefreshNs(VkDevice device, VkSwapchainKHR swapchain, uint32_t debugOverride = 0) {
        if (debugOverride == 4) return 6060606ULL; // Fixed 165Hz

        if (debugOverride != 2 && debugOverride != 3 && pfnGetSwapchainTimingPropertiesEXT && device && swapchain) {
            VkSwapchainTimingPropertiesEXT timingProps{};
            timingProps.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_TIMING_PROPERTIES_EXT;
            uint64_t counter = 0;
            if (pfnGetSwapchainTimingPropertiesEXT(device, swapchain, &timingProps, &counter) == VK_SUCCESS && timingProps.refreshDuration > 0) {
                return timingProps.refreshDuration;
            }
        }

        if (debugOverride != 1 && debugOverride != 3 && pfnGetRefreshCycleDurationGOOGLE && device && swapchain) {
            VkRefreshCycleDurationGOOGLE dur{};
            if (pfnGetRefreshCycleDurationGOOGLE(device, swapchain, &dur) == VK_SUCCESS && dur.refreshDuration > 0) {
                return dur.refreshDuration;
            }
        }

        return 0;
    }

    bool QueryCalibratedTimestamps(VkDevice device, uint64_t& outGpuNs, uint64_t& outCpuNs, uint32_t debugOverride = 0) {
        if (debugOverride == 3 || !device) return false;

        VkCalibratedTimestampInfoKHR infos[2]{};
        infos[0].sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR;
        infos[0].timeDomain = VK_TIME_DOMAIN_DEVICE_KHR;
        infos[1].sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR;
        #ifdef __linux__
        infos[1].timeDomain = VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_KHR;
        #else
        infos[1].timeDomain = VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_KHR;
        #endif

        uint64_t timestamps[2] = {0, 0};
        uint64_t maxDev = 0;

        if (debugOverride != 2 && pfnGetCalibratedTimestampsKHR) {
            if (pfnGetCalibratedTimestampsKHR(device, 2, infos, timestamps, &maxDev) == VK_SUCCESS) {
                outGpuNs = timestamps[0]; outCpuNs = timestamps[1]; return true;
            }
        }
        if (debugOverride != 1 && pfnGetCalibratedTimestampsEXT) {
            if (pfnGetCalibratedTimestampsEXT(device, 2, (const VkCalibratedTimestampInfoEXT*)infos, timestamps, &maxDev) == VK_SUCCESS) {
                outGpuNs = timestamps[0]; outCpuNs = timestamps[1]; return true;
            }
        }

        return false;
    }

private:
    SupportedExtensions m_ext;
    std::vector<std::string> m_availableGpuExtensions;
    ExtensionManager() = default;

    PFN_vkWaitForPresent2KHR pfnWaitForPresent2KHR = nullptr;
    PFN_vkWaitForPresentKHR pfnWaitForPresentKHR = nullptr;
    PFN_vkAntiLagUpdateAMD pfnAntiLagUpdateAMD = nullptr;
    PFN_vkSetLatencyMarkerNV pfnSetLatencyMarkerNV = nullptr;
    PFN_vkLatencySleepNV pfnLatencySleepNV = nullptr;
    PFN_vkGetSwapchainTimingPropertiesEXT pfnGetSwapchainTimingPropertiesEXT = nullptr;
    PFN_vkGetRefreshCycleDurationGOOGLE pfnGetRefreshCycleDurationGOOGLE = nullptr;
    PFN_vkGetCalibratedTimestampsKHR pfnGetCalibratedTimestampsKHR = nullptr;
    PFN_vkGetCalibratedTimestampsEXT pfnGetCalibratedTimestampsEXT = nullptr;
};

} // namespace FrameFlux