// PB FrameFlux - LGPL-2.1
// runtime/scheduler/frametime_smoother.hpp: Adaptive Timing & Multiplier Scheduler

#pragma once

#include <chrono>
#include <algorithm>
#include <cstdint>
#include <string>

namespace FrameFlux {

enum class SchedulerMode {
    Fixed,      // Fixed multiplier pacing (e.g. 2x, 3x, 4x, 6x)
    TargetFps,  // Target framerate lock (e.g. 144Hz, 165Hz)
    Auto        // Dynamic adaptive display cadence
};

class FrametimeSmoother {
public:
    FrametimeSmoother();

    void SetMode(const std::string& modeStr, uint32_t multiplier, uint32_t targetFps);
    void UpdateOnPresentExit();
    void ComputeFramePacing(float& outNormalizedT, uint32_t& outIntervalUs, uint32_t currentStep, uint32_t totalSteps);

    float GetSmoothedFrametimeMs() const { return m_smoothedFrametimeMs; }

private:
    SchedulerMode m_mode = SchedulerMode::Fixed;
    uint32_t m_multiplier = 2;
    uint32_t m_targetFps = 0;

    std::chrono::high_resolution_clock::time_point m_lastPresentExit;
    float m_smoothedFrametimeMs = 16.6f;
};

} // namespace FrameFlux