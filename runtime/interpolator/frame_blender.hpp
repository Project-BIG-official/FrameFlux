// PB FrameFlux - LGPL-2.1
// runtime/interpolator/frame_blender.hpp: Blending & Warping Orchestrator

#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>

namespace FrameFlux {

enum class BlendFallbackMode : uint32_t {
    RepeatFrame = 0,
    BlendFrame  = 1,
    DropFrame   = 2
};

class FrameBlender {
public:
    static BlendFallbackMode ParseFallbackMode(const std::string& modeStr) {
        if (modeStr == "repeat") return BlendFallbackMode::RepeatFrame;
        if (modeStr == "drop") return BlendFallbackMode::DropFrame;
        return BlendFallbackMode::BlendFrame;
    }
};

} // namespace FrameFlux