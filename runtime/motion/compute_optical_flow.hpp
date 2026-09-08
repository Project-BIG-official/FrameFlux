// PB FrameFlux - LGPL-2.1
// runtime/motion/compute_optical_flow.hpp: Generic/AMD Subgroup Optical Flow

#pragma once

#include "motion_provider.hpp"

namespace FrameFlux {

class ComputeOpticalFlow : public IMotionProvider {
public:
    ComputeOpticalFlow() = default;
    ~ComputeOpticalFlow() override;

    bool Initialize(VkDevice device, VkPhysicalDevice physicalDevice) override;
    void Cleanup() override;

    void DispatchMotionEstimation(
        VkCommandBuffer cmd,
        VkImageView frameA,
        VkImageView frameB,
        VkImageView outMotionVectors,
        VkImageView outConfidence,
        uint32_t width,
        uint32_t height
    ) override;

    MotionBackendType GetType() const override { return MotionBackendType::GenericCompute; }

private:
    VkDevice m_device = VK_NULL_HANDLE;
    bool m_initialized = false;
};

} // namespace FrameFlux