// PB FrameFlux - LGPL-2.1
// layer/compute_engine.hpp: Slang Compute Engine Declarations

#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

namespace FrameFlux {

// Push Constants definitions MUST be before ComputeEngine declaration
struct WarpPushConstants {
    float t;
    float confidenceThreshold;
    uint32_t fallbackAction;
    uint32_t showDebugWatermark;
    uint32_t resolutionX;
    uint32_t resolutionY;
    float invResolutionX;
    float invResolutionY;
};

struct FlowPushConstants {
    uint32_t blockGridDimX;
    uint32_t blockGridDimY;
    uint32_t frameResX;
    uint32_t frameResY;
    uint32_t searchStride; // 1 = Standard, 3 = High
    float invPyramidScale;
};

struct LumaPushConstants {
    uint32_t resolutionX;
    uint32_t resolutionY;
};

class ComputeEngine {
public:
    ComputeEngine() = default;
    ~ComputeEngine();

    bool Initialize(VkDevice device, VkPhysicalDevice physicalDevice);
    void Cleanup();

    void RecordLumaPass(VkCommandBuffer cmd, VkDescriptorSet descSet, uint32_t width, uint32_t height);
    void RecordFlowPass(VkCommandBuffer cmd, VkDescriptorSet descSet, uint32_t width, uint32_t height);
    void RecordWarpPass(VkCommandBuffer cmd, VkDescriptorSet descSet, const WarpPushConstants& pc, uint32_t width, uint32_t height);

    VkDescriptorSetLayout GetWarpDescLayout() const { return m_warpDescLayout; }
    VkDescriptorSetLayout GetFlowDescLayout() const { return m_flowDescLayout; }
    VkDescriptorSetLayout GetLumaDescLayout() const { return m_lumaDescLayout; }

private:
    VkDevice m_device = VK_NULL_HANDLE;

    VkShaderModule m_lumaShader = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_lumaDescLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_lumaPipeLayout = VK_NULL_HANDLE;
    VkPipeline m_lumaPipeline = VK_NULL_HANDLE;

    VkShaderModule m_flowShader = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_flowDescLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_flowPipeLayout = VK_NULL_HANDLE;
    VkPipeline m_flowPipeline = VK_NULL_HANDLE;

    VkShaderModule m_warpShader = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_warpDescLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_warpPipeLayout = VK_NULL_HANDLE;
    VkPipeline m_warpPipeline = VK_NULL_HANDLE;

    bool CreateLumaPipeline();
    bool CreateFlowPipeline();
    bool CreateWarpPipeline();
};

} // namespace FrameFlux