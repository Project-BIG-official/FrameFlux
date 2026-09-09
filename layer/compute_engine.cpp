// PB FrameFlux - LGPL-2.1
// layer/compute_engine.cpp: 2-Pass Refinement & True GPU Timers

#include "compute_engine.hpp"

#include "warp_interpolate_spv.hpp"
#include "flow_search_dp4a_spv.hpp"
#include "luma_pack_spv.hpp"
#include "flow_downsample_spv.hpp"
#include "flow_refine_subgroup_spv.hpp"
#include "vulkan_extensions.hpp"
#include "overlay_hud_spv.hpp"

#include <iostream>
#include <vector>
#include <cstring>

namespace FrameFlux {

static VkShaderModule CreateShaderModule(VkDevice device, const uint8_t* byteCode, size_t codeSize) {
    if (!byteCode || codeSize == 0) return VK_NULL_HANDLE;

    std::vector<uint32_t> alignedCode((codeSize + 3) / 4);
    std::memcpy(alignedCode.data(), byteCode, codeSize);

    VkShaderModuleCreateInfo smInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0, codeSize, alignedCode.data()};
    VkShaderModule module = VK_NULL_HANDLE;
    vkCreateShaderModule(device, &smInfo, nullptr, &module);
    return module;
}

ComputeEngine::~ComputeEngine() {
    Cleanup();
}

void ComputeEngine::InitQueryPool(VkPhysicalDevice physicalDevice) {
    VkPhysicalDeviceProperties props;
    if (physicalDevice != VK_NULL_HANDLE) {
        vkGetPhysicalDeviceProperties(physicalDevice, &props);
        m_timestampPeriod = props.limits.timestampPeriod; // in nanoseconds
    }

    VkQueryPoolCreateInfo qpInfo{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, nullptr, 0, VK_QUERY_TYPE_TIMESTAMP, 2, 0};
    vkCreateQueryPool(m_device, &qpInfo, nullptr, &m_queryPool);
}

bool ComputeEngine::Initialize(VkDevice device, VkPhysicalDevice physicalDevice) {
    m_device = device;
    InitQueryPool(physicalDevice);

    return CreateLumaPipeline() && 
           CreateDownsamplePipeline() && 
           CreateFlowPipeline() && 
           CreateRefinePipeline() && 
           CreateWarpPipeline() && 
           CreateOverlayPipeline();
}

bool ComputeEngine::CreateOverlayPipeline() {
    m_overlayShader = CreateShaderModule(m_device, overlay_hud_spv, overlay_hud_spv_size);
    if (!m_overlayShader) return false;

    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}
    };
    VkDescriptorSetLayoutCreateInfo dlInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, (uint32_t)bindings.size(), bindings.data()};
    vkCreateDescriptorSetLayout(m_device, &dlInfo, nullptr, &m_overlayDescLayout);

    VkPushConstantRange pcRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(OverlayPushConstants)};
    VkPipelineLayoutCreateInfo plInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &m_overlayDescLayout, 1, &pcRange};
    vkCreatePipelineLayout(m_device, &plInfo, nullptr, &m_overlayPipeLayout);

    VkPipelineShaderStageCreateInfo stageInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, m_overlayShader, "main", nullptr};
    VkComputePipelineCreateInfo pipeInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0, stageInfo, m_overlayPipeLayout, VK_NULL_HANDLE, -1};
    return vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &m_overlayPipeline) == VK_SUCCESS;
}

void ComputeEngine::RecordOverlayPass(VkCommandBuffer cmd, VkDescriptorSet descSet, const OverlayPushConstants& pc, uint32_t width, uint32_t height) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_overlayPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_overlayPipeLayout, 0, 1, &descSet, 0, nullptr);
    vkCmdPushConstants(cmd, m_overlayPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(OverlayPushConstants), &pc);
    vkCmdDispatch(cmd, (width + 15) / 16, (height + 15) / 16, 1);
}

void ComputeEngine::Cleanup() {
    if (m_device == VK_NULL_HANDLE) return;

    if (m_queryPool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(m_device, m_queryPool, nullptr);
        m_queryPool = VK_NULL_HANDLE;
    }

    auto destroyPipe = [this](VkPipeline& p, VkPipelineLayout& pl, VkDescriptorSetLayout& dl, VkShaderModule& sm) {
        if (p != VK_NULL_HANDLE) { vkDestroyPipeline(m_device, p, nullptr); p = VK_NULL_HANDLE; }
        if (pl != VK_NULL_HANDLE) { vkDestroyPipelineLayout(m_device, pl, nullptr); pl = VK_NULL_HANDLE; }
        if (dl != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(m_device, dl, nullptr); dl = VK_NULL_HANDLE; }
        if (sm != VK_NULL_HANDLE) { vkDestroyShaderModule(m_device, sm, nullptr); sm = VK_NULL_HANDLE; }
    };

    destroyPipe(m_lumaPipeline, m_lumaPipeLayout, m_lumaDescLayout, m_lumaShader);
    destroyPipe(m_downsamplePipeline, m_downsamplePipeLayout, m_downsampleDescLayout, m_downsampleShader);
    destroyPipe(m_flowPipeline, m_flowPipeLayout, m_flowDescLayout, m_flowShader);
    destroyPipe(m_refinePipeline, m_refinePipeLayout, m_refineDescLayout, m_refineShader);
    destroyPipe(m_warpPipeline, m_warpPipeLayout, m_warpDescLayout, m_warpShader);
}

bool ComputeEngine::CreateLumaPipeline() {
    m_lumaShader = CreateShaderModule(m_device, luma_pack_spv, luma_pack_spv_size);
    if (!m_lumaShader) return false;

    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}
    };
    VkDescriptorSetLayoutCreateInfo dlInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, (uint32_t)bindings.size(), bindings.data()};
    vkCreateDescriptorSetLayout(m_device, &dlInfo, nullptr, &m_lumaDescLayout);

    VkPushConstantRange pcRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(LumaPushConstants)};
    VkPipelineLayoutCreateInfo plInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &m_lumaDescLayout, 1, &pcRange};
    vkCreatePipelineLayout(m_device, &plInfo, nullptr, &m_lumaPipeLayout);

    VkPipelineShaderStageCreateInfo stageInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, m_lumaShader, "main", nullptr};
    VkComputePipelineCreateInfo pipeInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0, stageInfo, m_lumaPipeLayout, VK_NULL_HANDLE, -1};
    return vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &m_lumaPipeline) == VK_SUCCESS;
}

bool ComputeEngine::CreateDownsamplePipeline() {
    m_downsampleShader = CreateShaderModule(m_device, flow_downsample_spv, flow_downsample_spv_size);
    if (!m_downsampleShader) return false;

    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}
    };
    VkDescriptorSetLayoutCreateInfo dlInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, (uint32_t)bindings.size(), bindings.data()};
    vkCreateDescriptorSetLayout(m_device, &dlInfo, nullptr, &m_downsampleDescLayout);

    VkPushConstantRange pcRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DownsamplePushConstants)};
    VkPipelineLayoutCreateInfo plInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &m_downsampleDescLayout, 1, &pcRange};
    vkCreatePipelineLayout(m_device, &plInfo, nullptr, &m_downsamplePipeLayout);

    VkPipelineShaderStageCreateInfo stageInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, m_downsampleShader, "main", nullptr};
    VkComputePipelineCreateInfo pipeInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0, stageInfo, m_downsamplePipeLayout, VK_NULL_HANDLE, -1};
    return vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &m_downsamplePipeline) == VK_SUCCESS;
}

bool ComputeEngine::CreateFlowPipeline() {
    m_flowShader = CreateShaderModule(m_device, flow_search_dp4a_spv, flow_search_dp4a_spv_size);
    if (!m_flowShader) return false;

    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}
    };
    VkDescriptorSetLayoutCreateInfo dlInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, (uint32_t)bindings.size(), bindings.data()};
    vkCreateDescriptorSetLayout(m_device, &dlInfo, nullptr, &m_flowDescLayout);

    VkPushConstantRange pcRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(FlowPushConstants)};
    VkPipelineLayoutCreateInfo plInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &m_flowDescLayout, 1, &pcRange};
    vkCreatePipelineLayout(m_device, &plInfo, nullptr, &m_flowPipeLayout);

    VkPipelineShaderStageCreateInfo stageInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, m_flowShader, "main", nullptr};

    #ifdef VK_EXT_subgroup_size_control
    VkPipelineShaderStageRequiredSubgroupSizeCreateInfoEXT sizeControl{};
    sizeControl.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO_EXT;
    sizeControl.requiredSubgroupSize = 32;

    if (ExtensionManager::Get().GetSupported().hasSubgroupSizeControl) {
        stageInfo.pNext = &sizeControl;
        stageInfo.flags |= VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT;
    }
    #endif

    VkComputePipelineCreateInfo pipeInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0, stageInfo, m_flowPipeLayout, VK_NULL_HANDLE, -1};
    return vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &m_flowPipeline) == VK_SUCCESS;
}

bool ComputeEngine::CreateRefinePipeline() {
    m_refineShader = CreateShaderModule(m_device, flow_refine_subgroup_spv, flow_refine_subgroup_spv_size);
    if (!m_refineShader) return false;

    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}
    };
    VkDescriptorSetLayoutCreateInfo dlInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, (uint32_t)bindings.size(), bindings.data()};
    vkCreateDescriptorSetLayout(m_device, &dlInfo, nullptr, &m_refineDescLayout);

    VkPushConstantRange pcRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(RefinePushConstants)};
    VkPipelineLayoutCreateInfo plInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &m_refineDescLayout, 1, &pcRange};
    vkCreatePipelineLayout(m_device, &plInfo, nullptr, &m_refinePipeLayout);

    VkPipelineShaderStageCreateInfo stageInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, m_refineShader, "main", nullptr};
    VkComputePipelineCreateInfo pipeInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0, stageInfo, m_refinePipeLayout, VK_NULL_HANDLE, -1};
    return vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &m_refinePipeline) == VK_SUCCESS;
}

bool ComputeEngine::CreateWarpPipeline() {
    m_warpShader = CreateShaderModule(m_device, warp_interpolate_spv, warp_interpolate_spv_size);
    if (!m_warpShader) return false;

    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {4, VK_DESCRIPTOR_TYPE_SAMPLER,       1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}
    };
    VkDescriptorSetLayoutCreateInfo dlInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, (uint32_t)bindings.size(), bindings.data()};
    vkCreateDescriptorSetLayout(m_device, &dlInfo, nullptr, &m_warpDescLayout);

    VkPushConstantRange pcRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(WarpPushConstants)};
    VkPipelineLayoutCreateInfo plInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &m_warpDescLayout, 1, &pcRange};
    vkCreatePipelineLayout(m_device, &plInfo, nullptr, &m_warpPipeLayout);

    VkPipelineShaderStageCreateInfo stageInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, m_warpShader, "main", nullptr};
    VkComputePipelineCreateInfo pipeInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0, stageInfo, m_warpPipeLayout, VK_NULL_HANDLE, -1};
    return vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &m_warpPipeline) == VK_SUCCESS;
}

void ComputeEngine::BeginTimestamp(VkCommandBuffer cmd) {
    if (m_queryPool != VK_NULL_HANDLE) {
        vkCmdResetQueryPool(cmd, m_queryPool, 0, 2);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_queryPool, 0);
    }
}

void ComputeEngine::EndTimestamp(VkCommandBuffer cmd) {
    if (m_queryPool != VK_NULL_HANDLE) {
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_queryPool, 1);
    }
}

float ComputeEngine::QueryLastGpuTimeMs() {
    if (m_queryPool != VK_NULL_HANDLE) {
        uint64_t timestamps[2] = {0, 0};
        VkResult res = vkGetQueryPoolResults(
            m_device, m_queryPool, 0, 2, sizeof(timestamps), timestamps, sizeof(uint64_t), 
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT
        );
        if (res == VK_SUCCESS && timestamps[1] > timestamps[0]) {
            uint64_t diffNs = static_cast<uint64_t>((timestamps[1] - timestamps[0]) * m_timestampPeriod);
            m_lastGpuTimeMs = static_cast<float>(diffNs) / 1000000.0f;
        }
    }
    return m_lastGpuTimeMs;
}

void ComputeEngine::RecordLumaPass(VkCommandBuffer cmd, VkDescriptorSet descSet, uint32_t width, uint32_t height) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_lumaPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_lumaPipeLayout, 0, 1, &descSet, 0, nullptr);
    LumaPushConstants pc{width, height};
    vkCmdPushConstants(cmd, m_lumaPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(LumaPushConstants), &pc);
    uint32_t packedW = (width + 3) / 4;
    vkCmdDispatch(cmd, (packedW + 15) / 16, (height + 15) / 16, 1);
}

void ComputeEngine::RecordDownsamplePass(VkCommandBuffer cmd, VkDescriptorSet descSet, uint32_t srcW, uint32_t srcH, uint32_t dstW, uint32_t dstH) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_downsamplePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_downsamplePipeLayout, 0, 1, &descSet, 0, nullptr);
    DownsamplePushConstants pc{srcW, srcH, dstW, dstH};
    vkCmdPushConstants(cmd, m_downsamplePipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DownsamplePushConstants), &pc);
    vkCmdDispatch(cmd, (dstW + 15) / 16, (dstH + 15) / 16, 1);
}

void ComputeEngine::RecordFlowPass(VkCommandBuffer cmd, VkDescriptorSet descSet, uint32_t width, uint32_t height, uint32_t stride) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_flowPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_flowPipeLayout, 0, 1, &descSet, 0, nullptr);
    uint32_t gridX = (width + 3) / 4;
    uint32_t gridY = (height + 3) / 4;
    FlowPushConstants pc{gridX, gridY, width, height, stride, 1.0f};
    vkCmdPushConstants(cmd, m_flowPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(FlowPushConstants), &pc);
    vkCmdDispatch(cmd, gridX, gridY, 1);
}

void ComputeEngine::RecordRefinePass(VkCommandBuffer cmd, VkDescriptorSet descSet, uint32_t width, uint32_t height) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_refinePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_refinePipeLayout, 0, 1, &descSet, 0, nullptr);
    uint32_t gridX = (width + 3) / 4;
    uint32_t gridY = (height + 3) / 4;
    RefinePushConstants pc{gridX, gridY, width, height};
    vkCmdPushConstants(cmd, m_refinePipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(RefinePushConstants), &pc);
    vkCmdDispatch(cmd, gridX, gridY, 1);
}

void ComputeEngine::RecordWarpPass(VkCommandBuffer cmd, VkDescriptorSet descSet, const WarpPushConstants& pc, uint32_t width, uint32_t height) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_warpPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_warpPipeLayout, 0, 1, &descSet, 0, nullptr);
    vkCmdPushConstants(cmd, m_warpPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(WarpPushConstants), &pc);
    vkCmdDispatch(cmd, (width + 15) / 16, (height + 15) / 16, 1);
}

} // namespace FrameFlux