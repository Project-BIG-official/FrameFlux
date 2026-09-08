// PB FrameFlux - LGPL-2.1
// config/settings.cpp: Smart INI Synchronizer (Preserves User Edits & Injects Missing Keys)

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

void SettingsManager::SyncAndLoadFile(const std::string& path) {
    std::unordered_map<std::string, std::string> userValues;

    // 1. Read existing config if present and extract user-defined values
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

    // Helper: "есть строка или нету" -> returns user value if present, else default value
    auto getOrDef = [&](const std::string& key, const std::string& defVal) -> std::string {
        auto it = userValues.find(key);
        return (it != userValues.end()) ? it->second : defVal;
    };

    // 2. Populate in-memory settings (User values take precedence over defaults)
    m_settings.mode = getOrDef("mode", "v2");
    m_settings.profile = getOrDef("profile", "quality");
    m_settings.multiplier = std::clamp((uint32_t)atoi(getOrDef("multiplier", "2").c_str()), 2u, 6u);
    m_settings.schedulerMode = getOrDef("scheduler_mode", "auto");
    m_settings.targetFps = (uint32_t)atoi(getOrDef("target_fps", "0").c_str());
    m_settings.searchMode = getOrDef("search_mode", "high");
    m_settings.fallbackAction = getOrDef("fallback_action", "blend");
    m_settings.showWatermark = (getOrDef("show_watermark", "false") == "true" || getOrDef("show_watermark", "0") == "1");

    // 3. Write back clean, fully-documented INI while PRESERVING all user's custom settings!
    std::ofstream outFile(path);
    if (outFile.is_open()) {
        outFile << "# PB FrameFlux Configuration File\n"
                << "# Auto-generated & synced on launch. Edit parameters to customize.\n\n"
                << "[general]\n"
                << "# Generation mode: 'v2' (True Optical Flow FG), 'v1' (Legacy Frame Blend), 'off'\n"
                << "mode = " << m_settings.mode << "\n\n"
                << "# Quality profile: 'quality' (2-pass pyramid refinement), 'performance' (fast single-pass for iGPU/APU)\n"
                << "profile = " << m_settings.profile << "\n\n"
                << "# Frame generation multiplier: 2, 3, 4, 5, or 6\n"
                << "multiplier = " << m_settings.multiplier << "\n\n"
                << "# Scheduler mode: 'auto' (syncs to display VBlank), 'fixed' (strict pacing), 'target_fps' (locks to target_fps)\n"
                << "scheduler_mode = " << m_settings.schedulerMode << "\n\n"
                << "# Target FPS: used when scheduler_mode = 'target_fps' (e.g. 60, 120, 144, 165). 0 = auto\n"
                << "target_fps = " << m_settings.targetFps << "\n\n"
                << "[quality]\n"
                << "# Search mode: 'high' (+-24px wide motion search), 'standard' (+-8px fast search)\n"
                << "search_mode = " << m_settings.searchMode << "\n\n"
                << "# Fallback action when motion confidence drops: 'blend', 'repeat', 'drop' (for VRR/FreeSync displays)\n"
                << "fallback_action = " << m_settings.fallbackAction << "\n\n"
                << "[debug]\n"
                << "# Show small neon-green watermark square on generated frames to verify presentation\n"
                << "show_watermark = " << (m_settings.showWatermark ? "true" : "false") << "\n";
        outFile.close();
    }
}

void SettingsManager::LoadOrCreate() {
    std::string localPath = "frameflux.ini";
    std::string fallbackDir = std::string(getenv("HOME") ? getenv("HOME") : ".") + "/.config/frameflux";
    std::string fallbackPath = fallbackDir + "/config.ini";

    // Check game directory first
    std::ofstream testLocal("frameflux.test", std::ios::out);
    if (testLocal.is_open()) {
        testLocal.close();
        std::filesystem::remove("frameflux.test");
        SyncAndLoadFile(localPath);
        std::cout << "[PB FrameFlux] Synchronized config: " << localPath << std::endl;
    } else {
        // Fallback directory if game folder is read-only
        std::filesystem::create_directories(fallbackDir);
        SyncAndLoadFile(fallbackPath);
        std::cout << "[PB FrameFlux] Synchronized fallback config: " << fallbackPath << std::endl;
    }

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
    if (envProfile) {
        m_settings.profile = envProfile;
    }

    const char* envMult = getenv("FRAMEFLUX_MULTIPLIER");
    if (envMult) {
        int m = atoi(envMult);
        if (m >= 2 && m <= 6) m_settings.multiplier = (uint32_t)m;
    }

    const char* envSched = getenv("FRAMEFLUX_SCHEDULER");
    if (envSched) {
        m_settings.schedulerMode = envSched;
    }

    const char* envFps = getenv("FRAMEFLUX_TARGET_FPS");
    if (envFps) {
        m_settings.targetFps = (uint32_t)atoi(envFps);
    }

    const char* envSearch = getenv("FRAMEFLUX_SEARCH_MODE");
    if (envSearch) {
        m_settings.searchMode = envSearch;
    }

    const char* envFallback = getenv("FRAMEFLUX_FALLBACK");
    if (envFallback) {
        m_settings.fallbackAction = envFallback;
    }

    const char* envDebug = getenv("FRAMEFLUX_DEBUG");
    if (envDebug) {
        m_settings.showWatermark = (strcmp(envDebug, "1") == 0 || strcasecmp(envDebug, "true") == 0);
    }
}

} // namespace FrameFlux