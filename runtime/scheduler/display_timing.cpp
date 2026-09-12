// PB FrameFlux - LGPL-2.1
// runtime/scheduler/display_timing.cpp: Hardware Vulkan Display Timing & DRM Probe

#include "display_timing.hpp"
#include "vulkan_extensions.hpp"
#include <cmath>
#include <fstream>
#include <string>
#include <filesystem>
#include <chrono>

namespace FrameFlux {

DisplayTiming& DisplayTiming::Get() {
    static DisplayTiming instance;
    return instance;
}

uint32_t DisplayTiming::QueryDisplayRefreshRateHz(VkDevice device, VkSwapchainKHR swapchain) {
    auto now = std::chrono::steady_clock::now();
    
    // Кэшируем результат на 3 секунды, чтобы не дергать sysfs каждый кадр
    if (m_lastQueryTime.time_since_epoch().count() != 0 &&
        std::chrono::duration_cast<std::chrono::seconds>(now - m_lastQueryTime).count() < 3 &&
        m_cachedHz > 0) 
    {
        return m_cachedHz;
    }
    m_lastQueryTime = now;

    // 1. Аппаратный Vulkan Display Timing (EXT -> GOOGLE)
    uint64_t refreshNs = ExtensionManager::Get().QueryDisplayRefreshNs(device, swapchain);
    if (refreshNs > 0) {
        uint32_t hz = static_cast<uint32_t>(std::round(1000000000.0 / static_cast<double>(refreshNs)));
        if (hz >= 30 && hz <= 500) {
            m_cachedHz = hz;
            return m_cachedHz;
        }
    }

    // 2. Сканер Linux DRM sysfs
    #ifdef __linux__
    try {
        const std::string drmPath = "/sys/class/drm";
        if (std::filesystem::exists(drmPath)) {
            for (const auto& entry : std::filesystem::directory_iterator(drmPath)) {
                std::string filename = entry.path().filename().string();
                if (filename.find("-DP-") != std::string::npos || 
                    filename.find("-HDMI-") != std::string::npos || 
                    filename.find("-eDP-") != std::string::npos) 
                {
                    std::string statusFile = entry.path().string() + "/status";
                    std::ifstream statusIn(statusFile);
                    std::string status;
                    if (statusIn >> status && status == "connected") {
                        std::string modeFile = entry.path().string() + "/modes";
                        std::ifstream modesIn(modeFile);
                        std::string modeLine;
                        if (std::getline(modesIn, modeLine)) {
                            size_t atPos = modeLine.find('@');
                            if (atPos != std::string::npos) {
                                uint32_t hz = std::stoi(modeLine.substr(atPos + 1));
                                if (hz >= 30 && hz <= 500) {
                                    m_cachedHz = hz;
                                    return m_cachedHz;
                                }
                            } else {
                                size_t pPos = modeLine.find('p');
                                if (pPos != std::string::npos && pPos + 1 < modeLine.size() && isdigit(modeLine[pPos + 1])) {
                                    uint32_t hz = std::stoi(modeLine.substr(pPos + 1));
                                    if (hz >= 30 && hz <= 500) {
                                        m_cachedHz = hz;
                                        return m_cachedHz;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    } catch (...) {}
    #endif

    if (m_cachedHz == 0) m_cachedHz = 165;
    return m_cachedHz;
}

} // namespace FrameFlux