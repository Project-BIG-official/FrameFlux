// PB FrameFlux - LGPL-2.1
// runtime/scheduler/frametime_smoother.cpp: Scheduler Implementation

#include "frametime_smoother.hpp"

namespace FrameFlux {

FrametimeSmoother::FrametimeSmoother() {
    m_lastPresentExit = std::chrono::high_resolution_clock::now();
}

void FrametimeSmoother::SetMode(const std::string& modeStr, uint32_t multiplier, uint32_t targetFps) {
    m_multiplier = std::clamp(multiplier, 2u, 6u); // Support up to 6x!
    m_targetFps = targetFps;

    if (modeStr == "target_fps" || targetFps > 0) {
        m_mode = SchedulerMode::TargetFps;
    } else if (modeStr == "auto") {
        m_mode = SchedulerMode::Auto;
    } else {
        m_mode = SchedulerMode::Fixed;
    }
}

void FrametimeSmoother::UpdateOnPresentExit() {
    auto now = std::chrono::high_resolution_clock::now();
    float pureGameRenderMs = std::chrono::duration<float, std::milli>(now - m_lastPresentExit).count();
    pureGameRenderMs = std::clamp(pureGameRenderMs, 1.0f, 100.0f);

    constexpr float alpha = 0.12f;
    m_smoothedFrametimeMs = alpha * pureGameRenderMs + (1.0f - alpha) * m_smoothedFrametimeMs;
    m_lastPresentExit = std::chrono::high_resolution_clock::now();
}

void FrametimeSmoother::ComputeFramePacing(float& outNormalizedT, uint32_t& outIntervalUs, uint32_t currentStep, uint32_t totalSteps) {
    outNormalizedT = static_cast<float>(currentStep) / static_cast<float>(totalSteps);

    switch (m_mode) {
        case SchedulerMode::TargetFps:
            if (m_targetFps > 0) {
                outIntervalUs = 1000000u / m_targetFps;
            } else {
                outIntervalUs = static_cast<uint32_t>((m_smoothedFrametimeMs * 1000.0f) / totalSteps);
            }
            break;

        case SchedulerMode::Auto:
        case SchedulerMode::Fixed:
        default:
            outIntervalUs = static_cast<uint32_t>((m_smoothedFrametimeMs * 1000.0f) / totalSteps);
            break;
    }

    outIntervalUs = std::clamp(outIntervalUs, 1500u, 35000u);
}

} // namespace FrameFlux