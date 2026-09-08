// PB FrameFlux - LGPL-2.1
// runtime/motion/motion_provider.hpp: Abstract Motion Estimation Interface

#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

namespace FrameFlux {

enum class MotionBackendType {
    GenericCompute, // Slang DP4A Subgroup Compute
    NvidiaOFA,      // VK_NV_optical_flow hardware
    GameVectors     // Ingested Motion Vectors & Depth from game engine
};

class IMotionProvider {
public:
    virtual ~IMotionProvider() = default;

    virtual bool Initialize(VkDevice device, VkPhysicalDevice physicalDevice) = 0;
    virtual void Cleanup() = 0;

    virtual void DispatchMotionEstimation(
        VkCommandBuffer cmd,
        VkImageView frameA,
        VkImageView frameB,
        VkImageView outMotionVectors,
        VkImageView outConfidence,
        uint32_t width,
        uint32_t height
    ) = 0;

    virtual MotionBackendType GetType() const = 0;
};

} // namespace FrameFlux