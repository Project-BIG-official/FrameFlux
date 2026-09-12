// PB FrameFlux - LGPL-2.1
// runtime/scheduler/display_timing.hpp: Display Refresh Rate & VBlank Prober

#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <chrono>

namespace FrameFlux {

class DisplayTiming {
public:
    static DisplayTiming& Get();

    uint32_t QueryDisplayRefreshRateHz(VkDevice device, VkSwapchainKHR swapchain);

private:
    DisplayTiming() = default;
    uint32_t m_cachedHz = 165;
    std::chrono::steady_clock::time_point m_lastQueryTime{};
};

} // namespace FrameFlux