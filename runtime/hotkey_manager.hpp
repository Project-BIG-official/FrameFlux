// PB FrameFlux - LGPL-2.1
// runtime/hotkey_manager.hpp: Complete Uncut Hotkey & Menu Navigator

#pragma once

#include <iostream>
#include <chrono>
#include <cstring>
#include "settings.hpp"

#ifdef __linux__
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <dlfcn.h>
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
        if (m_display && pfnXCloseDisplay) {
            pfnXCloseDisplay(m_display);
            m_display = nullptr;
        }
        if (m_x11Handle) {
            dlclose(m_x11Handle);
            m_x11Handle = nullptr;
        }
        #endif
    }

    bool IsMenuOpen() const { return m_menuOpen; }
    uint32_t GetSelectedItem() const { return m_selectedItem; }

    void PollHotkeys() {
        #ifdef __linux__
        if (!m_x11Loaded) {
            InitDynamicX11();
        }

        if (!m_display || !pfnXQueryKeymap || !pfnXKeysymToKeycode) {
            return;
        }

        char currentKeys[32];
        pfnXQueryKeymap(m_display, currentKeys);

        auto isDown = [&](KeyCode kc) -> bool {
            if (kc == 0 || kc >= 256) return false;
            return (currentKeys[kc / 8] & (1 << (kc % 8))) != 0;
        };

        auto wasDown = [&](KeyCode kc) -> bool {
            if (kc == 0 || kc >= 256) return false;
            return (m_prevKeys[kc / 8] & (1 << (kc % 8))) != 0;
        };

        auto justPressed = [&](int code, KeySym sym) -> bool {
            KeyCode kc = (code != 0) ? (KeyCode)code : pfnXKeysymToKeycode(m_display, sym);
            if (kc == 0) return false;
            return isDown(kc) && !wasDown(kc);
        };

        // ESC: Close menu immediately
        if (justPressed(9, XK_Escape)) {
            if (m_menuOpen) {
                m_menuOpen = false;
                SettingsManager::Get().SaveToFile();
                std::memcpy(m_prevKeys, currentKeys, 32);
                return;
            }
        }

        // Toggle Menu: [Insert] (118), [F8] (74), [F11] (95), [Home] (110)
        if (justPressed(118, XK_Insert) || justPressed(74, XK_F8) || 
            justPressed(95, XK_F11)     || justPressed(110, XK_Home)) {
            m_menuOpen = !m_menuOpen;
            std::cout << "[PB FrameFlux Menu] Toggled: " << (m_menuOpen ? "OPEN" : "CLOSED") << std::endl;
            if (!m_menuOpen) {
                SettingsManager::Get().SaveToFile();
            }
            std::memcpy(m_prevKeys, currentKeys, 32);
            return;
        }

        if (m_menuOpen) {
            auto& cfg = SettingsManager::Get().GetMutableSettings();

            // Up Arrow
            if (justPressed(111, XK_Up) || justPressed(0, XK_KP_Up)) {
                m_selectedItem = (m_selectedItem == 0) ? 7 : (m_selectedItem - 1);
            }
            // Down Arrow
            else if (justPressed(116, XK_Down) || justPressed(0, XK_KP_Down)) {
                m_selectedItem = (m_selectedItem >= 7) ? 0 : (m_selectedItem + 1);
            }
            // Right / Left / Enter navigation
            else if (justPressed(114, XK_Right) || justPressed(0, XK_KP_Right) ||
                     justPressed(113, XK_Left)  || justPressed(0, XK_KP_Left)  ||
                     justPressed(36, XK_Return) || justPressed(0, XK_KP_Enter)) {

                bool forward = isDown(114) || isDown(pfnXKeysymToKeycode(m_display, XK_KP_Right)) ||
                               isDown(36)  || isDown(pfnXKeysymToKeycode(m_display, XK_KP_Enter));

                switch (m_selectedItem) {
                    case 0: // MODE
                        if (cfg.mode == "v2") cfg.mode = forward ? "v1" : "off";
                        else if (cfg.mode == "v1") cfg.mode = forward ? "off" : "v2";
                        else cfg.mode = forward ? "v2" : "v1";
                        break;

                    case 1: // MULTIPLIER (Uncut full 2x..6x cycle)
                        if (forward) {
                            cfg.multiplier = (cfg.multiplier >= 6) ? 2 : (cfg.multiplier + 1);
                        } else {
                            cfg.multiplier = (cfg.multiplier <= 2) ? 6 : (cfg.multiplier - 1);
                        }
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

                    case 5: // TARGET FPS (Full uncut spectrum: 0, 30, 45, 60, 75, 90, 120, 144, 165, 240, 360)
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
            }
        }

        std::memcpy(m_prevKeys, currentKeys, 32);
        #endif
    }

private:
    #ifdef __linux__
    using PFN_XOpenDisplay = Display* (*)(const char*);
    using PFN_XCloseDisplay = int (*)(Display*);
    using PFN_XQueryKeymap = int (*)(Display*, char[32]);
    using PFN_XKeysymToKeycode = KeyCode (*)(Display*, KeySym);
    using PFN_XInitThreads = int (*)();

    void* m_x11Handle = nullptr;
    PFN_XOpenDisplay pfnXOpenDisplay = nullptr;
    PFN_XCloseDisplay pfnXCloseDisplay = nullptr;
    PFN_XQueryKeymap pfnXQueryKeymap = nullptr;
    PFN_XKeysymToKeycode pfnXKeysymToKeycode = nullptr;
    PFN_XInitThreads pfnXInitThreads = nullptr;

    Display* m_display = nullptr;
    char m_prevKeys[32]{};
    bool m_x11Loaded = false;

    void InitDynamicX11() {
        m_x11Loaded = true;
        m_x11Handle = dlopen("libX11.so.6", RTLD_NOW | RTLD_GLOBAL);
        if (!m_x11Handle) {
            m_x11Handle = dlopen("libX11.so", RTLD_NOW | RTLD_GLOBAL);
        }
        if (!m_x11Handle) return;

        pfnXOpenDisplay = (PFN_XOpenDisplay)dlsym(m_x11Handle, "XOpenDisplay");
        pfnXCloseDisplay = (PFN_XCloseDisplay)dlsym(m_x11Handle, "XCloseDisplay");
        pfnXQueryKeymap = (PFN_XQueryKeymap)dlsym(m_x11Handle, "XQueryKeymap");
        pfnXKeysymToKeycode = (PFN_XKeysymToKeycode)dlsym(m_x11Handle, "XKeysymToKeycode");
        pfnXInitThreads = (PFN_XInitThreads)dlsym(m_x11Handle, "XInitThreads");

        if (pfnXInitThreads) pfnXInitThreads();
        if (pfnXOpenDisplay) m_display = pfnXOpenDisplay(nullptr);
    }
    #endif

    bool m_menuOpen = false;
    uint32_t m_selectedItem = 0;

    HotkeyManager() {
        #ifdef __linux__
        std::memset(m_prevKeys, 0, 32);
        #endif
    }
};

} // namespace FrameFlux