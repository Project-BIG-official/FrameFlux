// PB FrameFlux - LGPL-2.1
// layer/compute_engine.hpp: Slang Compute Engine Declarations

#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include "settings.hpp"

namespace FrameFlux {

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
    uint32_t searchStride;
    float invPyramidScale;
};

struct RefinePushConstants {
    uint32_t blockGridDimX;
    uint32_t blockGridDimY;
    uint32_t frameResX;
    uint32_t frameResY;
};

struct DownsamplePushConstants {
    uint32_t srcWidth;
    uint32_t srcHeight;
    uint32_t dstWidth;
    uint32_t dstHeight;
};

struct LumaPushConstants {
    uint32_t resolutionX;
    uint32_t resolutionY;
};

struct OverlayPushConstants {
    uint32_t resolutionX;    // Offset 0
    uint32_t resolutionY;    // Offset 4
    uint32_t isMenuOpen;     // Offset 8
    uint32_t isHudVisible;   // Offset 12
    uint32_t selectedItem;   // Offset 16
    uint32_t mode;           // Offset 20
    uint32_t multiplier;     // Offset 24
    uint32_t profile;        // Offset 28
    uint32_t searchMode;     // Offset 32
    uint32_t schedulerMode;  // Offset 36
    uint32_t targetFps;      // Offset 40
    uint32_t fallbackAction; // Offset 44
    uint32_t hudCorner;      // Offset 48
    uint32_t nativeFpsX10;   // Offset 52
    uint32_t outputFpsX10;   // Offset 56
    uint32_t gpuTimeUs;      // Offset 60
    uint32_t cpuTimeUs;      // Offset 64
    uint32_t latencyUs;      // Offset 68
    uint32_t confidenceX10;  // Offset 72
    uint32_t fallbackX10;    // Offset 76
    uint32_t vramMb;         // Offset 80
};

class ComputeEngine {
public:
    ComputeEngine() = default;
    ~ComputeEngine();

    bool Initialize(VkDevice device, VkPhysicalDevice physicalDevice);
    void Cleanup();

    void RecordLumaPass(VkCommandBuffer cmd, VkDescriptorSet descSet, uint32_t width, uint32_t height);
    void RecordDownsamplePass(VkCommandBuffer cmd, VkDescriptorSet descSet, uint32_t srcW, uint32_t srcH, uint32_t dstW, uint32_t dstH);
    void RecordFlowPass(VkCommandBuffer cmd, VkDescriptorSet descSet, uint32_t width, uint32_t height, uint32_t stride);
    void RecordRefinePass(VkCommandBuffer cmd, VkDescriptorSet descSet, uint32_t width, uint32_t height);
    void RecordWarpPass(VkCommandBuffer cmd, VkDescriptorSet descSet, const WarpPushConstants& pc, uint32_t width, uint32_t height);
    void RecordOverlayPass(VkCommandBuffer cmd, VkDescriptorSet descSet, const OverlayPushConstants& pc, uint32_t width, uint32_t height);

    void BeginTimestamp(VkCommandBuffer cmd);
    void EndTimestamp(VkCommandBuffer cmd);
    float QueryLastGpuTimeMs();

    VkDescriptorSetLayout GetWarpDescLayout() const { return m_warpDescLayout; }
    VkDescriptorSetLayout GetFlowDescLayout() const { return m_flowDescLayout; }
    VkDescriptorSetLayout GetLumaDescLayout() const { return m_lumaDescLayout; }
    VkDescriptorSetLayout GetDownsampleDescLayout() const { return m_downsampleDescLayout; }
    VkDescriptorSetLayout GetRefineDescLayout() const { return m_refineDescLayout; }
    VkDescriptorSetLayout GetOverlayDescLayout() const { return m_overlayDescLayout; }

private:
    VkDevice m_device = VK_NULL_HANDLE;

    VkShaderModule m_lumaShader = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_lumaDescLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_lumaPipeLayout = VK_NULL_HANDLE;
    VkPipeline m_lumaPipeline = VK_NULL_HANDLE;

    VkShaderModule m_downsampleShader = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_downsampleDescLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_downsamplePipeLayout = VK_NULL_HANDLE;
    VkPipeline m_downsamplePipeline = VK_NULL_HANDLE;

    VkShaderModule m_flowShader = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_flowDescLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_flowPipeLayout = VK_NULL_HANDLE;
    VkPipeline m_flowPipeline = VK_NULL_HANDLE;

    VkShaderModule m_refineShader = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_refineDescLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_refinePipeLayout = VK_NULL_HANDLE;
    VkPipeline m_refinePipeline = VK_NULL_HANDLE;

    VkShaderModule m_warpShader = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_warpDescLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_warpPipeLayout = VK_NULL_HANDLE;
    VkPipeline m_warpPipeline = VK_NULL_HANDLE;

    VkShaderModule m_overlayShader = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_overlayDescLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_overlayPipeLayout = VK_NULL_HANDLE;
    VkPipeline m_overlayPipeline = VK_NULL_HANDLE;

    VkQueryPool m_queryPool = VK_NULL_HANDLE;
    float m_timestampPeriod = 1.0f;
    float m_lastGpuTimeMs = 0.0f;

    bool CreateLumaPipeline();
    bool CreateDownsamplePipeline();
    bool CreateFlowPipeline();
    bool CreateRefinePipeline();
    bool CreateWarpPipeline();
    bool CreateOverlayPipeline();
    void InitQueryPool(VkPhysicalDevice physicalDevice);
};

} // namespace FrameFlux