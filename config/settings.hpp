// PB FrameFlux - LGPL-2.1
// config/settings.hpp: Complete Configuration System & Live Persistence

#pragma once

#include <string>
#include <cstdint>
#include <filesystem>

namespace FrameFlux {

struct AppSettings {
    // Main Settings
    std::string mode = "v2";              // "v2", "v1", "off"
    std::string profile = "quality";      // "quality", "performance"
    uint32_t multiplier = 2;              // 2..6
    std::string schedulerMode = "auto";   // "auto", "fixed", "target_fps"
    uint32_t targetFps = 0;               // 0 = off, or 30..360
    std::string lowLatency = "on";        // "on", "boost", "off"
    uint32_t hudMode = 2;                 // 0 = Off, 1 = Compact, 2 = Full, 3 = Latency/Driver
    uint32_t hudCorner = 0;               // 0 = Top-Left, 1 = Top-Right, 2 = Bottom-Left, 3 = Bottom-Right
    std::string searchMode = "high";      // "high", "standard"
    std::string fallbackAction = "repeat";// "repeat", "blend", "drop"
    bool isDebugMode = false;             // Activated via FRAMEFLUX_DEBUG=1

    // Advanced Debug & Extension Overrides
    uint32_t menuPage = 0;                // 0 = Main Menu, 1 = Debug Menu
    uint32_t extPresentWait = 0;          // 0 = Auto, 1 = Wait2, 2 = Wait1, 3 = Off
    uint32_t extLowLatency = 0;           // 0 = Auto, 1 = AntiLag, 2 = Reflex, 3 = JIT, 4 = Off
    uint32_t extDisplayTiming = 0;        // 0 = Auto, 1 = EXT, 2 = Google, 3 = Sysfs, 4 = Fixed 165Hz
    uint32_t extTimestamps = 0;           // 0 = Auto, 1 = KHR, 2 = EXT, 3 = QueryPool
    uint32_t extSwapchainMaint = 0;       // 0 = Auto, 1 = Maint1 KHR, 2 = Standard Fences
    std::string forceFallback = "auto";   // "auto", "repeat", "blend", "drop"
    uint32_t debugVisual = 0;             // 0 = Off, 1 = Badge, 2 = Conf Map, 3 = Flow Vectors
    float confOverride = 0.0f;            // 0.0 = Auto, or 0.20, 0.40, 0.60, 0.80, 0.95
    uint32_t strideOverride = 0;          // 0 = Auto, 1 = 1px, 2 = 2px, 3 = 3px
    bool enablePacingWait = true;         // Precision microsecond frame pacing
};

class SettingsManager {
public:
    static SettingsManager& Get();

    const AppSettings& GetSettings() const { return m_settings; }
    AppSettings& GetMutableSettings() { return m_settings; }
    const AppSettings& GetConfig() const { return m_settings; }

    void LoadOrCreate();
    void CheckHotReload();
    void SaveToFile();

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