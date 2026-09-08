// PB FrameFlux - LGPL-2.1
// runtime/motion/game_vectors.hpp: Native Engine Motion Vectors Bridge

#pragma once

#include "motion_provider.hpp"

namespace FrameFlux {

class GameVectorsProvider : public IMotionProvider {
public:
    GameVectorsProvider() = default;
    ~GameVectorsProvider() override = default;

    bool Initialize(VkDevice device, VkPhysicalDevice physicalDevice) override { return false; } // Disabled until bridge attached
    void Cleanup() override {}

    void DispatchMotionEstimation(
        VkCommandBuffer cmd,
        VkImageView frameA,
        VkImageView frameB,
        VkImageView outMotionVectors,
        VkImageView outConfidence,
        uint32_t width,
        uint32_t height
    ) override {}

    MotionBackendType GetType() const override { return MotionBackendType::GameVectors; }
};

} // namespace FrameFlux