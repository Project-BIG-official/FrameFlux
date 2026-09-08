// PB FrameFlux - LGPL-2.1
// runtime/motion/nv_optical_flow.hpp: NVIDIA Hardware OFA Backend

#pragma once

#include "motion_provider.hpp"

namespace FrameFlux {

class NvidiaOpticalFlow : public IMotionProvider {
public:
    NvidiaOpticalFlow() = default;
    ~NvidiaOpticalFlow() override;

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

    MotionBackendType GetType() const override { return MotionBackendType::NvidiaOFA; }

private:
    VkDevice m_device = VK_NULL_HANDLE;
    bool m_supported = false;
};

} // namespace FrameFlux