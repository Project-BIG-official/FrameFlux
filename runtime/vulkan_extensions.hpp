// PB FrameFlux - LGPL-2.1
// runtime/vulkan_extensions.hpp: Crash-Safe Vulkan Extension Prober

#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <cstring>
#include <iostream>

namespace FrameFlux {

struct SupportedExtensions {
    bool hasSwapchainMaintenance1 = false;
    bool hasPresentWait2 = false;
    bool hasPresentWait = false;
    bool hasPresentId2 = false;
    bool hasPresentId = false;
    bool hasExtPresentTiming = false;
    bool hasGoogleDisplayTiming = false;
    bool hasCalibratedTimestamps = false;
    bool hasFrameBoundary = false;
    bool hasSubgroupPartitionedEXT = false;
    bool hasSubgroupPartitionedNV = false;
    bool hasSubgroupSizeControl = false;
    bool hasNvidiaOFA = false;
    bool hasLowLatencyNV = false;
    bool hasComputeOccupancyNV = false;
    bool hasAntiLagAMD = false;
    bool hasCorePropertiesAMD = false;
};

class ExtensionManager {
public:
    static ExtensionManager& Get() {
        static ExtensionManager instance;
        return instance;
    }

    const SupportedExtensions& GetSupported() const { return m_ext; }

    void CheckExtensionName(const char* name) {
        if (!name) return;

        if (strcmp(name, "VK_EXT_swapchain_maintenance1") == 0) m_ext.hasSwapchainMaintenance1 = true;
        else if (strcmp(name, "VK_KHR_present_wait2") == 0) m_ext.hasPresentWait2 = true;
        else if (strcmp(name, "VK_KHR_present_wait") == 0) m_ext.hasPresentWait = true;
        else if (strcmp(name, "VK_KHR_present_id2") == 0) m_ext.hasPresentId2 = true;
        else if (strcmp(name, "VK_KHR_present_id") == 0) m_ext.hasPresentId = true;
        else if (strcmp(name, "VK_EXT_present_timing") == 0) m_ext.hasExtPresentTiming = true;
        else if (strcmp(name, "VK_GOOGLE_display_timing") == 0) m_ext.hasGoogleDisplayTiming = true;
        else if (strcmp(name, "VK_KHR_calibrated_timestamps") == 0 || strcmp(name, "VK_EXT_calibrated_timestamps") == 0) m_ext.hasCalibratedTimestamps = true;
        else if (strcmp(name, "VK_EXT_frame_boundary") == 0) m_ext.hasFrameBoundary = true;
        else if (strcmp(name, "VK_EXT_shader_subgroup_partitioned") == 0) m_ext.hasSubgroupPartitionedEXT = true;
        else if (strcmp(name, "VK_NV_shader_subgroup_partitioned") == 0) m_ext.hasSubgroupPartitionedNV = true;
        else if (strcmp(name, "VK_EXT_subgroup_size_control") == 0) m_ext.hasSubgroupSizeControl = true;
        else if (strcmp(name, "VK_NV_optical_flow") == 0) m_ext.hasNvidiaOFA = true;
        else if (strcmp(name, "VK_NV_low_latency2") == 0) m_ext.hasLowLatencyNV = true;
        else if (strcmp(name, "VK_NV_compute_occupancy_priority") == 0) m_ext.hasComputeOccupancyNV = true;
        else if (strcmp(name, "VK_AMD_anti_lag") == 0) m_ext.hasAntiLagAMD = true;
        else if (strcmp(name, "VK_AMD_shader_core_properties2") == 0) m_ext.hasCorePropertiesAMD = true;
    }

    void ProbeDeviceExtensions(
        VkPhysicalDevice physicalDevice,
        const VkDeviceCreateInfo* pCreateInfo,
        PFN_vkEnumerateDeviceExtensionProperties pfnEnumDeviceExt
    ) {
        if (pCreateInfo && pCreateInfo->ppEnabledExtensionNames) {
            for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; ++i) {
                CheckExtensionName(pCreateInfo->ppEnabledExtensionNames[i]);
            }
        }

        if (pfnEnumDeviceExt && physicalDevice != VK_NULL_HANDLE) {
            uint32_t count = 0;
            if (pfnEnumDeviceExt(physicalDevice, nullptr, &count, nullptr) == VK_SUCCESS && count > 0) {
                std::vector<VkExtensionProperties> extensions(count);
                if (pfnEnumDeviceExt(physicalDevice, nullptr, &count, extensions.data()) == VK_SUCCESS) {
                    for (const auto& ext : extensions) {
                        CheckExtensionName(ext.extensionName);
                    }
                }
            }
        }

        std::cout << "[PB FrameFlux Extensions] Detected Features:"
                  << "\n  - Swapchain Maintenance 1 (Present Fences): " << (m_ext.hasSwapchainMaintenance1 ? "YES" : "NO")
                  << "\n  - Hardware Present Wait (v2 / v1):          " << (m_ext.hasPresentWait2 ? "YES (v2)" : (m_ext.hasPresentWait ? "YES (v1)" : "NO"))
                  << "\n  - Hardware Present ID (v2 / v1):            " << (m_ext.hasPresentId2 ? "YES (v2)" : (m_ext.hasPresentId ? "YES (v1)" : "NO"))
                  << "\n  - VK_EXT_present_timing:                    " << (m_ext.hasExtPresentTiming ? "YES" : (m_ext.hasGoogleDisplayTiming ? "LEGACY (GOOGLE)" : "NO"))
                  << "\n  - Calibrated Timestamps:                    " << (m_ext.hasCalibratedTimestamps ? "YES" : "NO")
                  << "\n  - AMD Radeon Anti-Lag 2:                    " << (m_ext.hasAntiLagAMD ? "YES" : "NO")
                  << "\n  - Subgroup Size Control (Wave32):           " << (m_ext.hasSubgroupSizeControl ? "YES" : "NO")
                  << std::endl;
    }

    void AttachPresentFence(VkPresentInfoKHR& presentInfo, const VkFence* pFences, uint32_t swapchainCount) {
        #ifdef VK_EXT_swapchain_maintenance1
        if (m_ext.hasSwapchainMaintenance1 && pFences) {
            static VkSwapchainPresentFenceInfoEXT fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT;
            fenceInfo.pNext = (void*)presentInfo.pNext;
            fenceInfo.swapchainCount = swapchainCount;
            fenceInfo.pFences = pFences;
            presentInfo.pNext = &fenceInfo;
        }
        #endif
    }

private:
    SupportedExtensions m_ext;
    ExtensionManager() = default;
};

} // namespace FrameFlux
