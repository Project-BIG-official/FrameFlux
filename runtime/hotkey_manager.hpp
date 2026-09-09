// PB FrameFlux - LGPL-2.1
// runtime/hotkey_manager.hpp: Omnivorous In-Game Hotkey & Menu Navigator

#pragma once

#include <iostream>
#include <chrono>
#include "settings.hpp"

#ifdef __linux__
#include <X11/Xlib.h>
#include <X11/keysym.h>
#endif

namespace FrameFlux {

class HotkeyManager {
public:
    static HotkeyManager& Get() {
        static HotkeyManager instance;
        return instance;
    }

    ~HotkeyManager() {
        #ifdef __linux__
        if (m_display) {
            XCloseDisplay(m_display);
            m_display = nullptr;
        }
        #endif
    }

    bool IsMenuOpen() const { return m_menuOpen; }
    uint32_t GetSelectedItem() const { return m_selectedItem; }

    void PollHotkeys() {
        #ifdef __linux__
        if (!m_display) {
            m_display = XOpenDisplay(nullptr);
            if (!m_display) return;
        }

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastPressTime).count() < 140) {
            return;
        }

        char keys[32];
        XQueryKeymap(m_display, keys);

        auto isDown = [&](KeyCode kc) -> bool {
            if (kc == 0 || kc >= 256) return false;
            return (keys[kc / 8] & (1 << (kc % 8))) != 0;
        };

        auto isKeyOrSym = [&](int code, KeySym sym) -> bool {
            if (isDown(code)) return true;
            KeyCode symCode = XKeysymToKeycode(m_display, sym);
            return (symCode != 0 && isDown(symCode));
        };

        // ESC: Close menu immediately
        if (isKeyOrSym(9, XK_Escape)) {
            if (m_menuOpen) {
                m_menuOpen = false;
                SettingsManager::Get().SaveToFile();
                m_lastPressTime = now;
                return;
            }
        }

        // Toggle Menu: [Insert] (118), [F8] (74), [F11] (95) or [Home] (110)
        if (isKeyOrSym(118, XK_Insert) || isKeyOrSym(74, XK_F8) || isKeyOrSym(95, XK_F11) || isKeyOrSym(110, XK_Home)) {
            m_menuOpen = !m_menuOpen;
            std::cout << "[PB FrameFlux Menu] Toggled: " << (m_menuOpen ? "OPEN" : "CLOSED") << std::endl;
            if (!m_menuOpen) {
                SettingsManager::Get().SaveToFile();
            }
            m_lastPressTime = now;
            return;
        }

        if (m_menuOpen) {
            auto& cfg = SettingsManager::Get().GetMutableSettings();

            // Up (111 / XK_Up / XK_KP_Up)
            if (isKeyOrSym(111, XK_Up) || isKeyOrSym(0, XK_KP_Up)) {
                m_selectedItem = (m_selectedItem == 0) ? 7 : (m_selectedItem - 1);
                m_lastPressTime = now;
                return;
            }
            // Down (116 / XK_Down / XK_KP_Down)
            if (isKeyOrSym(116, XK_Down) || isKeyOrSym(0, XK_KP_Down)) {
                m_selectedItem = (m_selectedItem >= 7) ? 0 : (m_selectedItem + 1);
                m_lastPressTime = now;
                return;
            }
            // Right (114) / Left (113) / Enter (36)
            if (isKeyOrSym(114, XK_Right) || isKeyOrSym(0, XK_KP_Right) || 
                isKeyOrSym(113, XK_Left)  || isKeyOrSym(0, XK_KP_Left)  || 
                isKeyOrSym(36, XK_Return) || isKeyOrSym(0, XK_KP_Enter)) {

                bool forward = isKeyOrSym(114, XK_Right) || isKeyOrSym(0, XK_KP_Right) || isKeyOrSym(36, XK_Return) || isKeyOrSym(0, XK_KP_Enter);

                switch (m_selectedItem) {
                    case 0: // MODE
                        if (cfg.mode == "v2") cfg.mode = forward ? "v1" : "off";
                        else if (cfg.mode == "v1") cfg.mode = forward ? "off" : "v2";
                        else cfg.mode = forward ? "v2" : "v1";
                        break;
                    case 1: // MULTIPLIER
                        if (forward) cfg.multiplier = (cfg.multiplier >= 6) ? 2 : (cfg.multiplier + 1);
                        else cfg.multiplier = (cfg.multiplier <= 2) ? 6 : (cfg.multiplier - 1);
                        break;
                    case 2: // PROFILE
                        cfg.profile = (cfg.profile == "quality") ? "performance" : "quality";
                        break;
                    case 3: // SEARCH MODE
                        cfg.searchMode = (cfg.searchMode == "high") ? "standard" : "high";
                        break;
                    case 4: // SCHEDULER
                        if (cfg.schedulerMode == "auto") cfg.schedulerMode = "fixed";
                        else if (cfg.schedulerMode == "fixed") cfg.schedulerMode = "target_fps";
                        else cfg.schedulerMode = "auto";
                        break;
                    case 5: // TARGET FPS
                        if (forward) {
                            if (cfg.targetFps == 0) cfg.targetFps = 30;
                            else if (cfg.targetFps == 30) cfg.targetFps = 45;
                            else if (cfg.targetFps == 45) cfg.targetFps = 60;
                            else if (cfg.targetFps == 60) cfg.targetFps = 75;
                            else if (cfg.targetFps == 75) cfg.targetFps = 90;
                            else if (cfg.targetFps == 90) cfg.targetFps = 120;
                            else if (cfg.targetFps == 120) cfg.targetFps = 144;
                            else if (cfg.targetFps == 144) cfg.targetFps = 165;
                            else if (cfg.targetFps == 165) cfg.targetFps = 240;
                            else if (cfg.targetFps == 240) cfg.targetFps = 360;
                            else cfg.targetFps = 0;
                        } else {
                            if (cfg.targetFps == 360) cfg.targetFps = 240;
                            else if (cfg.targetFps == 240) cfg.targetFps = 165;
                            else if (cfg.targetFps == 165) cfg.targetFps = 144;
                            else if (cfg.targetFps == 144) cfg.targetFps = 120;
                            else if (cfg.targetFps == 120) cfg.targetFps = 90;
                            else if (cfg.targetFps == 90) cfg.targetFps = 75;
                            else if (cfg.targetFps == 75) cfg.targetFps = 60;
                            else if (cfg.targetFps == 60) cfg.targetFps = 45;
                            else if (cfg.targetFps == 45) cfg.targetFps = 30;
                            else if (cfg.targetFps == 30) cfg.targetFps = 0;
                            else cfg.targetFps = 360;
                        }
                        break;
                    case 6: // FALLBACK
                        if (cfg.fallbackAction == "blend") cfg.fallbackAction = "repeat";
                        else if (cfg.fallbackAction == "repeat") cfg.fallbackAction = "drop";
                        else cfg.fallbackAction = "blend";
                        break;
                    case 7: // HUD STATS
                        cfg.showWatermark = !cfg.showWatermark;
                        break;
                }
                SettingsManager::Get().SaveToFile();
                m_lastPressTime = now;
                return;
            }
        }
        #endif
    }

private:
    #ifdef __linux__
    Display* m_display = nullptr;
    #endif
    bool m_menuOpen = false;
    uint32_t m_selectedItem = 0;
    std::chrono::steady_clock::time_point m_lastPressTime;

    HotkeyManager() {
        m_lastPressTime = std::chrono::steady_clock::now();
        #ifdef __linux__
        m_display = XOpenDisplay(nullptr);
        #endif
    }
};

} // namespace FrameFlux