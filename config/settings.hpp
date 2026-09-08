// PB FrameFlux - LGPL-2.1
// config/settings.hpp: Complete Configuration System & Backward-Compatible Aliases

#pragma once

#include <string>
#include <cstdint>

namespace FrameFlux {

struct AppSettings {
    std::string mode = "v2";              // "v2" (True FG), "v1" (Legacy Blend), "off"
    std::string profile = "quality";      // "quality" (Pyramid Refinement), "performance" (Fast Single-Pass)
    uint32_t multiplier = 2;              // 2, 3, 4, 5, 6
    std::string schedulerMode = "auto";   // "auto" (VBlank sync), "fixed" (strict pace), "target_fps"
    uint32_t targetFps = 0;               // >0 = Target FPS lock (e.g. 120, 144, 165)
    std::string searchMode = "high";      // "high" (+-24px wide motion), "standard" (+-8px)
    std::string fallbackAction = "blend"; // "blend", "repeat", "drop" (instant VRR bypass)
    bool showWatermark = false;           // Neon-green debug square
};

using FrameFluxConfig = AppSettings;

class SettingsManager {
public:
    static SettingsManager& Get();

    const AppSettings& GetSettings() const { return m_settings; }
    const AppSettings& GetConfig() const { return m_settings; } // Backward-compatible alias

    void LoadOrCreate();

private:
    AppSettings m_settings;

    SettingsManager() = default;
    void SyncAndLoadFile(const std::string& path);
    void ApplyEnvironmentOverrides();
};

// Aliases: both ConfigManager::Get() and SettingsManager::Get() are 100% valid!
using ConfigManager = SettingsManager;

} // namespace FrameFlux