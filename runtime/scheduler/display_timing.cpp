// PB FrameFlux - LGPL-2.1
// runtime/scheduler/display_timing.cpp: Display Refresh Query Implementation

#include "display_timing.hpp"
#include <fstream>
#include <string>

namespace FrameFlux {

DisplayTiming& DisplayTiming::Get() {
    static DisplayTiming instance;
    return instance;
}

uint32_t DisplayTiming::QueryDisplayRefreshRateHz(VkDevice device, VkSwapchainKHR swapchain) {
    // 1. Try reading DRM modes from sysfs on Linux
    std::ifstream modeFile("/sys/class/drm/card0-DP-1/modes");
    if (!modeFile.is_open()) {
        modeFile.open("/sys/class/drm/card0-HDMI-A-1/modes");
    }

    if (modeFile.is_open()) {
        std::string modeLine;
        if (std::getline(modeFile, modeLine)) {
            // e.g. 1920x1080@144 -> extract 144
            size_t atPos = modeLine.find('@');
            if (atPos != std::string::npos) {
                uint32_t hz = std::stoi(modeLine.substr(atPos + 1));
                if (hz >= 30 && hz <= 500) {
                    m_cachedHz = hz;
                    return m_cachedHz;
                }
            }
        }
    }

    // Default fallback
    return m_cachedHz;
}

} // namespace FrameFlux