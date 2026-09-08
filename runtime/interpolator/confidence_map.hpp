// PB FrameFlux - LGPL-2.1
// runtime/interpolator/confidence_map.hpp: Motion Reliability & UI Mask Analysis

#pragma once

#include <cstdint>
#include <algorithm>

namespace FrameFlux {

class ConfidenceAnalyzer {
public:
    static bool ShouldFallbackToBlend(float averageConfidence, float threshold) {
        return averageConfidence < threshold;
    }

    static float CalculateSafeInterpolationFactor(float normalizedT, float confidence) {
        return normalizedT;
    }
};

} // namespace FrameFlux