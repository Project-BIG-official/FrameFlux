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

    outFile << "# PB FrameFlux Configuration File\n\n"
            << "[general]\n"
            << "mode = " << m_settings.mode << "\n"
            << "profile = " << m_settings.profile << "\n"
            << "multiplier = " << m_settings.multiplier << "\n"
            << "scheduler_mode = " << m_settings.schedulerMode << "\n"
            << "target_fps = " << m_settings.targetFps << "\n"
            << "low_latency = " << m_settings.lowLatency << "\n\n"
            << "[hud]\n"
            << "hud_mode = " << m_settings.hudMode << "\n"
            << "hud_corner = " << m_settings.hudCorner << "\n\n"
            << "[debug]\n"
            << "debug_mode = " << (m_settings.isDebugMode ? "true" : "false") << "\n"
            << "search_mode = " << m_settings.searchMode << "\n"
            << "fallback_action = " << m_settings.fallbackAction << "\n"
            << "force_fallback = " << m_settings.forceFallback << "\n"
            << "debug_visual = " << m_settings.debugVisual << "\n"
            << "conf_override = " << m_settings.confOverride << "\n"
            << "stride_override = " << m_settings.strideOverride << "\n"
            << "ext_present_wait = " << m_settings.extPresentWait << "\n"
            << "ext_low_latency = " << m_settings.extLowLatency << "\n"
            << "ext_display_timing = " << m_settings.extDisplayTiming << "\n"
            << "ext_timestamps = " << m_settings.extTimestamps << "\n"
            << "enable_pacing = " << (m_settings.enablePacingWait ? "true" : "false") << "\n";
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
    m_settings.lowLatency = getOrDef("low_latency", "on");
    m_settings.hudMode = std::clamp((uint32_t)atoi(getOrDef("hud_mode", "2").c_str()), 0u, 3u);
    m_settings.hudCorner = std::clamp((uint32_t)atoi(getOrDef("hud_corner", "0").c_str()), 0u, 3u);
    m_settings.searchMode = getOrDef("search_mode", "high");
    m_settings.fallbackAction = getOrDef("fallback_action", "repeat");

    // Debug
    m_settings.isDebugMode = (getOrDef("debug_mode", "false") == "true");
    m_settings.forceFallback = getOrDef("force_fallback", "auto");
    m_settings.debugVisual = (uint32_t)atoi(getOrDef("debug_visual", "0").c_str());
    m_settings.confOverride = (float)atof(getOrDef("conf_override", "0.0").c_str());
    m_settings.strideOverride = (uint32_t)atoi(getOrDef("stride_override", "0").c_str());
    m_settings.extPresentWait = (uint32_t)atoi(getOrDef("ext_present_wait", "0").c_str());
    m_settings.extLowLatency = (uint32_t)atoi(getOrDef("ext_low_latency", "0").c_str());
    m_settings.extDisplayTiming = (uint32_t)atoi(getOrDef("ext_display_timing", "0").c_str());
    m_settings.extTimestamps = (uint32_t)atoi(getOrDef("ext_timestamps", "0").c_str());
    m_settings.enablePacingWait = (getOrDef("enable_pacing", "true") == "true");

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

    const char* envDebug = getenv("FRAMEFLUX_DEBUG");
    if (envDebug && (strcmp(envDebug, "1") == 0 || strcasecmp(envDebug, "true") == 0)) {
        m_settings.isDebugMode = true;
    }

    const char* envProfile = getenv("FRAMEFLUX_PROFILE");
    if (envProfile) m_settings.profile = envProfile;

    const char* envMult = getenv("FRAMEFLUX_MULTIPLIER");
    if (envMult) {
        int m = atoi(envMult);
        if (m >= 2 && m <= 6) m_settings.multiplier = (uint32_t)m;
    }

    const char* envFps = getenv("FRAMEFLUX_TARGET_FPS");
    if (envFps) m_settings.targetFps = (uint32_t)atoi(envFps);
}

} // namespace FrameFlux