// PB FrameFlux - LGPL-2.1
// config/settings.hpp: Complete Configuration System & Live Persistence

#pragma once

#include <string>
#include <cstdint>
#include <filesystem>

namespace FrameFlux {

struct AppSettings {
    std::string mode = "v2";              // "v2", "v1", "off"
    std::string profile = "quality";      // "quality", "performance"
    uint32_t multiplier = 2;              // 2..6
    std::string schedulerMode = "auto";   // "auto", "fixed", "target_fps"
    uint32_t targetFps = 0;               // 0 = off, or 60, 120, 144, 165, 240
    std::string searchMode = "high";      // "high", "standard"
    std::string fallbackAction = "blend"; // "blend", "repeat", "drop"
    bool showWatermark = false;           // Telemetry HUD visible
    uint32_t hudCorner = 0;               // 0 = Top-Left, 1 = Top-Right, 2 = Bottom-Left, 3 = Bottom-Right
};

using FrameFluxConfig = AppSettings;

class SettingsManager {
public:
    static SettingsManager& Get();

    const AppSettings& GetSettings() const { return m_settings; }
    AppSettings& GetMutableSettings() { return m_settings; }
    const AppSettings& GetConfig() const { return m_settings; }

    void LoadOrCreate();
    void CheckHotReload();
    void SaveToFile(); // Persists live in-game changes into frameflux.ini

private:
    AppSettings m_settings;
    std::filesystem::file_time_type m_lastConfigWriteTime{};
    std::string m_activeConfigPath = "frameflux.ini";

    SettingsManager() = default;
    void SyncAndLoadFile(const std::string& path);
    void ApplyEnvironmentOverrides();
};

using ConfigManager = SettingsManager;

} // namespace FrameFlux