// PB FrameFlux - LGPL-2.1
// config/settings.cpp: Smart INI Synchronizer & Live In-Game Saver

#include "settings.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace FrameFlux {

SettingsManager& SettingsManager::Get() {
    static SettingsManager instance;
    return instance;
}

static std::string Trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

void SettingsManager::SaveToFile() {
    std::ofstream outFile(m_activeConfigPath);
    if (!outFile.is_open()) return;

    outFile << "# PB FrameFlux Configuration File\n"
            << "# Auto-generated & synced on launch. Edit parameters to customize.\n\n"
            << "[general]\n"
            << "mode = " << m_settings.mode << "\n\n"
            << "profile = " << m_settings.profile << "\n\n"
            << "multiplier = " << m_settings.multiplier << "\n\n"
            << "scheduler_mode = " << m_settings.schedulerMode << "\n\n"
            << "target_fps = " << m_settings.targetFps << "\n\n"
            << "[quality]\n"
            << "search_mode = " << m_settings.searchMode << "\n\n"
            << "fallback_action = " << m_settings.fallbackAction << "\n\n"
            << "[hud]\n"
            << "show_hud = " << (m_settings.showWatermark ? "true" : "false") << "\n\n"
            << "hud_corner = " << m_settings.hudCorner << "\n";
    outFile.close();

    try {
        m_lastConfigWriteTime = std::filesystem::last_write_time(m_activeConfigPath);
    } catch (...) {}
}

void SettingsManager::SyncAndLoadFile(const std::string& path) {
    std::unordered_map<std::string, std::string> userValues;

    std::ifstream inFile(path);
    if (inFile.is_open()) {
        std::string line;
        while (std::getline(inFile, line)) {
            line = Trim(line);
            if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;

            size_t eqPos = line.find('=');
            if (eqPos == std::string::npos) continue;

            std::string key = Trim(line.substr(0, eqPos));
            std::string val = Trim(line.substr(eqPos + 1));
            userValues[key] = val;
        }
        inFile.close();
    }

    auto getOrDef = [&](const std::string& key, const std::string& defVal) -> std::string {
        auto it = userValues.find(key);
        return (it != userValues.end()) ? it->second : defVal;
    };

    m_settings.mode = getOrDef("mode", "v2");
    m_settings.profile = getOrDef("profile", "quality");
    m_settings.multiplier = std::clamp((uint32_t)atoi(getOrDef("multiplier", "2").c_str()), 2u, 6u);
    m_settings.schedulerMode = getOrDef("scheduler_mode", "auto");
    m_settings.targetFps = (uint32_t)atoi(getOrDef("target_fps", "0").c_str());
    m_settings.searchMode = getOrDef("search_mode", "high");
    m_settings.fallbackAction = getOrDef("fallback_action", "repeat");
    m_settings.showWatermark = (getOrDef("show_hud", "false") == "true" || getOrDef("show_watermark", "false") == "true");
    m_settings.hudCorner = std::clamp((uint32_t)atoi(getOrDef("hud_corner", "0").c_str()), 0u, 3u);

    SaveToFile();
}

void SettingsManager::CheckHotReload() {
    if (!std::filesystem::exists(m_activeConfigPath)) return;

    try {
        auto currentWriteTime = std::filesystem::last_write_time(m_activeConfigPath);
        if (m_lastConfigWriteTime.time_since_epoch().count() == 0) {
            m_lastConfigWriteTime = currentWriteTime;
            return;
        }

        // Only reload from disk if external editor changed the file (do NOT reapply env overrides)
        if (currentWriteTime > m_lastConfigWriteTime) {
            m_lastConfigWriteTime = currentWriteTime;
            SyncAndLoadFile(m_activeConfigPath);
            std::cout << "\n[PB FrameFlux Live Reload] Config reloaded from disk!" << std::endl;
        }
    } catch (...) {}
}

void SettingsManager::LoadOrCreate() {
    std::string localPath = "frameflux.ini";
    std::string fallbackDir = std::string(getenv("HOME") ? getenv("HOME") : ".") + "/.config/frameflux";
    std::string fallbackPath = fallbackDir + "/config.ini";

    std::ofstream testLocal("frameflux.test", std::ios::out);
    if (testLocal.is_open()) {
        testLocal.close();
        std::filesystem::remove("frameflux.test");
        m_activeConfigPath = localPath;
    } else {
        std::filesystem::create_directories(fallbackDir);
        m_activeConfigPath = fallbackPath;
    }

    SyncAndLoadFile(m_activeConfigPath);
    // Environment variables only set initial values on first launch
    ApplyEnvironmentOverrides();
}

void SettingsManager::ApplyEnvironmentOverrides() {
    const char* envMode = getenv("ENABLE_FRAMEFLUX");
    if (envMode) {
        if (strcasecmp(envMode, "legacy") == 0 || strcmp(envMode, "1.0") == 0 || strcasecmp(envMode, "blend") == 0) {
            m_settings.mode = "v1";
        } else if (strcmp(envMode, "0") == 0 || strcasecmp(envMode, "off") == 0) {
            m_settings.mode = "off";
        } else {
            m_settings.mode = "v2";
        }
    }

    const char* envProfile = getenv("FRAMEFLUX_PROFILE");
    if (envProfile) m_settings.profile = envProfile;

    const char* envMult = getenv("FRAMEFLUX_MULTIPLIER");
    if (envMult) {
        int m = atoi(envMult);
        if (m >= 2 && m <= 6) m_settings.multiplier = (uint32_t)m;
    }

    const char* envSched = getenv("FRAMEFLUX_SCHEDULER");
    if (envSched) m_settings.schedulerMode = envSched;

    const char* envFps = getenv("FRAMEFLUX_TARGET_FPS");
    if (envFps) m_settings.targetFps = (uint32_t)atoi(envFps);

    const char* envSearch = getenv("FRAMEFLUX_SEARCH_MODE");
    if (envSearch) m_settings.searchMode = envSearch;

    const char* envFallback = getenv("FRAMEFLUX_FALLBACK");
    if (envFallback) m_settings.fallbackAction = envFallback;
}

} // namespace FrameFlux