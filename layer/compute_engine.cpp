// PB FrameFlux - LGPL-2.1
// layer/compute_engine.cpp: Slang Compute Engine Implementation

#include "compute_engine.hpp"
#include "settings.hpp"

#include "warp_interpolate_spv.hpp"
#include "flow_search_dp4a_spv.hpp"
#include "luma_pack_spv.hpp"

#include <iostream>
#include <vector>
#include <cstring>

namespace FrameFlux {

static VkShaderModule CreateShaderModule(VkDevice device, const uint8_t* byteCode, size_t codeSize) {
    if (!byteCode || codeSize == 0) return VK_NULL_HANDLE;

    std::vector<uint32_t> alignedCode((codeSize + 3) / 4);
    std::memcpy(alignedCode.data(), byteCode, codeSize);

    VkShaderModuleCreateInfo smInfo{};
    smInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smInfo.codeSize = codeSize;
    smInfo.pCode = alignedCode.data();

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &smInfo, nullptr, &module) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return module;
}

ComputeEngine::~ComputeEngine() {
    Cleanup();
}

bool ComputeEngine::Initialize(VkDevice device, VkPhysicalDevice physicalDevice) {
    m_device = device;
    return CreateLumaPipeline() && CreateFlowPipeline() && CreateWarpPipeline();
}

void ComputeEngine::Cleanup() {
    if (m_device == VK_NULL_HANDLE) return;

    auto destroyPipe = [this](VkPipeline& p, VkPipelineLayout& pl, VkDescriptorSetLayout& dl, VkShaderModule& sm) {
        if (p != VK_NULL_HANDLE) { vkDestroyPipeline(m_device, p, nullptr); p = VK_NULL_HANDLE; }
        if (pl != VK_NULL_HANDLE) { vkDestroyPipelineLayout(m_device, pl, nullptr); pl = VK_NULL_HANDLE; }
        if (dl != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(m_device, dl, nullptr); dl = VK_NULL_HANDLE; }
        if (sm != VK_NULL_HANDLE) { vkDestroyShaderModule(m_device, sm, nullptr); sm = VK_NULL_HANDLE; }
    };

    destroyPipe(m_lumaPipeline, m_lumaPipeLayout, m_lumaDescLayout, m_lumaShader);
    destroyPipe(m_flowPipeline, m_flowPipeLayout, m_flowDescLayout, m_flowShader);
    destroyPipe(m_warpPipeline, m_warpPipeLayout, m_warpDescLayout, m_warpShader);
}

bool ComputeEngine::CreateLumaPipeline() {
    m_lumaShader = CreateShaderModule(m_device, luma_pack_spv, luma_pack_spv_size);
    if (m_lumaShader == VK_NULL_HANDLE) return false;

    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}
    };
    VkDescriptorSetLayoutCreateInfo dlInfo{};
    dlInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dlInfo.bindingCount = (uint32_t)bindings.size();
    dlInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(m_device, &dlInfo, nullptr, &m_lumaDescLayout) != VK_SUCCESS) return false;

    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(LumaPushConstants);

    VkPipelineLayoutCreateInfo plInfo{};
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &m_lumaDescLayout;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &pcRange;
    if (vkCreatePipelineLayout(m_device, &plInfo, nullptr, &m_lumaPipeLayout) != VK_SUCCESS) return false;

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = m_lumaShader;
    stageInfo.pName = "main";
    stageInfo.pSpecializationInfo = nullptr;

    VkComputePipelineCreateInfo pipeInfo{};
    pipeInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeInfo.stage = stageInfo;
    pipeInfo.layout = m_lumaPipeLayout;
    pipeInfo.basePipelineIndex = -1;

    return vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &m_lumaPipeline) == VK_SUCCESS;
}

bool ComputeEngine::CreateFlowPipeline() {
    m_flowShader = CreateShaderModule(m_device, flow_search_dp4a_spv, flow_search_dp4a_spv_size);
    if (m_flowShader == VK_NULL_HANDLE) return false;

    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}
    };
    VkDescriptorSetLayoutCreateInfo dlInfo{};
    dlInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dlInfo.bindingCount = (uint32_t)bindings.size();
    dlInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(m_device, &dlInfo, nullptr, &m_flowDescLayout) != VK_SUCCESS) return false;

    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(FlowPushConstants);

    VkPipelineLayoutCreateInfo plInfo{};
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &m_flowDescLayout;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &pcRange;
    if (vkCreatePipelineLayout(m_device, &plInfo, nullptr, &m_flowPipeLayout) != VK_SUCCESS) return false;

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = m_flowShader;
    stageInfo.pName = "main";
    stageInfo.pSpecializationInfo = nullptr;

    VkComputePipelineCreateInfo pipeInfo{};
    pipeInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeInfo.stage = stageInfo;
    pipeInfo.layout = m_flowPipeLayout;
    pipeInfo.basePipelineIndex = -1;

    return vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &m_flowPipeline) == VK_SUCCESS;
}

bool ComputeEngine::CreateWarpPipeline() {
    m_warpShader = CreateShaderModule(m_device, warp_interpolate_spv, warp_interpolate_spv_size);
    if (m_warpShader == VK_NULL_HANDLE) return false;

    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {4, VK_DESCRIPTOR_TYPE_SAMPLER,       1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}
    };
    VkDescriptorSetLayoutCreateInfo dlInfo{};
    dlInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dlInfo.bindingCount = (uint32_t)bindings.size();
    dlInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(m_device, &dlInfo, nullptr, &m_warpDescLayout) != VK_SUCCESS) return false;

    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(WarpPushConstants);

    VkPipelineLayoutCreateInfo plInfo{};
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &m_warpDescLayout;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &pcRange;
    if (vkCreatePipelineLayout(m_device, &plInfo, nullptr, &m_warpPipeLayout) != VK_SUCCESS) return false;

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = m_warpShader;
    stageInfo.pName = "main";
    stageInfo.pSpecializationInfo = nullptr;

    VkComputePipelineCreateInfo pipeInfo{};
    pipeInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeInfo.stage = stageInfo;
    pipeInfo.layout = m_warpPipeLayout;
    pipeInfo.basePipelineIndex = -1;

    return vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &m_warpPipeline) == VK_SUCCESS;
}

void ComputeEngine::RecordLumaPass(VkCommandBuffer cmd, VkDescriptorSet descSet, uint32_t width, uint32_t height) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_lumaPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_lumaPipeLayout, 0, 1, &descSet, 0, nullptr);
    LumaPushConstants pc{width, height};
    vkCmdPushConstants(cmd, m_lumaPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(LumaPushConstants), &pc);
    uint32_t packedW = (width + 3) / 4;
    vkCmdDispatch(cmd, (packedW + 15) / 16, (height + 15) / 16, 1);
}

void ComputeEngine::RecordFlowPass(VkCommandBuffer cmd, VkDescriptorSet descSet, uint32_t width, uint32_t height) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_flowPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_flowPipeLayout, 0, 1, &descSet, 0, nullptr);
    uint32_t gridX = (width + 3) / 4;
    uint32_t gridY = (height + 3) / 4;
    
    uint32_t stride = (ConfigManager::Get().GetConfig().searchMode == "high") ? 3 : 1;
    FlowPushConstants pc{gridX, gridY, width, height, stride, 1.0f};

    vkCmdPushConstants(cmd, m_flowPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(FlowPushConstants), &pc);
    vkCmdDispatch(cmd, gridX, gridY, 1);
}

void ComputeEngine::RecordWarpPass(VkCommandBuffer cmd, VkDescriptorSet descSet, const WarpPushConstants& pc, uint32_t width, uint32_t height) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_warpPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_warpPipeLayout, 0, 1, &descSet, 0, nullptr);
    vkCmdPushConstants(cmd, m_warpPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(WarpPushConstants), &pc);
    vkCmdDispatch(cmd, (width + 15) / 16, (height + 15) / 16, 1);
}

} // namespace FrameFlux