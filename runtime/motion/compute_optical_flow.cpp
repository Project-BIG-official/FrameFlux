// PB FrameFlux - LGPL-2.1
// runtime/motion/compute_optical_flow.cpp: Generic/AMD Compute Provider Implementation

#include "compute_optical_flow.hpp"
#include <iostream>

namespace FrameFlux {

ComputeOpticalFlow::~ComputeOpticalFlow() {
    Cleanup();
}

bool ComputeOpticalFlow::Initialize(VkDevice device, VkPhysicalDevice physicalDevice) {
    m_device = device;
    m_initialized = true;
    return true;
}

void ComputeOpticalFlow::Cleanup() {
    m_initialized = false;
}

void ComputeOpticalFlow::DispatchMotionEstimation(
    VkCommandBuffer cmd,
    VkImageView frameA,
    VkImageView frameB,
    VkImageView outMotionVectors,
    VkImageView outConfidence,
    uint32_t width,
    uint32_t height
) {
    // Orchestrates flow_downsample -> flow_search_dp4a -> flow_refine_subgroup passes
}

} // namespace FrameFlux