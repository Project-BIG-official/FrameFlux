// PB FrameFlux - LGPL-2.1
// runtime/scheduler/display_timing.hpp: Display Refresh Rate & VBlank Prober

#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

namespace FrameFlux {

class DisplayTiming {
public:
    static DisplayTiming& Get();

    uint32_t QueryDisplayRefreshRateHz(VkDevice device, VkSwapchainKHR swapchain);

private:
    DisplayTiming() = default;
    uint32_t m_cachedHz = 60;
};

} // namespace FrameFlux