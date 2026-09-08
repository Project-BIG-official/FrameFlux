// PB FrameFlux - LGPL-2.1
// runtime/motion/nv_optical_flow.cpp: NVIDIA OFA Implementation

#include "nv_optical_flow.hpp"
#include <iostream>
#include <vector>
#include <cstring>

namespace FrameFlux {

NvidiaOpticalFlow::~NvidiaOpticalFlow() {
    Cleanup();
}

bool NvidiaOpticalFlow::Initialize(VkDevice device, VkPhysicalDevice physicalDevice) {
    m_device = device;

    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> extensions(extCount);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, extensions.data());

    for (const auto& ext : extensions) {
        if (strcmp(ext.extensionName, "VK_NV_optical_flow") == 0) {
            m_supported = true;
            std::cout << "[PB FrameFlux] NVIDIA Hardware Optical Flow Accelerator (OFA) detected!" << std::endl;
            return true;
        }
    }

    return false;
}

void NvidiaOpticalFlow::Cleanup() {
    m_supported = false;
}

void NvidiaOpticalFlow::DispatchMotionEstimation(
    VkCommandBuffer cmd,
    VkImageView frameA,
    VkImageView frameB,
    VkImageView outMotionVectors,
    VkImageView outConfidence,
    uint32_t width,
    uint32_t height
) {
}

} // namespace FrameFlux