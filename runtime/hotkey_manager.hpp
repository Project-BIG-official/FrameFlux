// PB FrameFlux - LGPL-2.1
// runtime/hotkey_manager.hpp: Independent Multi-Page Menu & Extension Toggles

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
        // Опрашиваем X11 только каждый 4-й кадр, если меню закрыто
        static uint32_t pollThrottle = 0;
        if ((++pollThrottle & 3) != 0 && !m_menuOpen) {
            return;
        }

        if (!m_x11Loaded) {
            InitDynamicX11();
        }

        if (!m_display || !pfnXQueryKeymap || !pfnXKeysymToKeycode) {
            return;
        }

        char currentKeys[32];
        try {
            pfnXQueryKeymap(m_display, currentKeys);
        } catch (...) {
            return;
        }

        auto isDown = [&](KeyCode kc) -> bool {
            if (kc == 0) return false;
            return (currentKeys[kc / 8] & (1 << (kc % 8))) != 0;
        };

        auto wasDown = [&](KeyCode kc) -> bool {
            if (kc == 0) return false;
            return (m_prevKeys[kc / 8] & (1 << (kc % 8))) != 0;
        };

        auto justPressed = [&](int code, KeySym sym) -> bool {
            KeyCode kc = (code != 0) ? (KeyCode)code : pfnXKeysymToKeycode(m_display, sym);
            if (kc == 0) return false;
            return isDown(kc) && !wasDown(kc);
        };

        if (justPressed(9, XK_Escape)) {
            if (m_menuOpen) {
                m_menuOpen = false;
                SettingsManager::Get().SaveToFile();
                std::memcpy(m_prevKeys, currentKeys, 32);
                return;
            }
        }

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

            if (justPressed(23, XK_Tab)) {
                cfg.menuPage = (cfg.menuPage == 0) ? 1 : 0;
                m_selectedItem = 0;
                SettingsManager::Get().SaveToFile();
                std::memcpy(m_prevKeys, currentKeys, 32);
                return;
            }

            constexpr uint32_t maxItems = 9; // 10 пунктов (0..9)

            if (justPressed(111, XK_Up) || justPressed(0, XK_KP_Up)) {
                m_selectedItem = (m_selectedItem == 0) ? maxItems : (m_selectedItem - 1);
            }
            else if (justPressed(116, XK_Down) || justPressed(0, XK_KP_Down)) {
                m_selectedItem = (m_selectedItem >= maxItems) ? 0 : (m_selectedItem + 1);
            }
            else if (justPressed(114, XK_Right) || justPressed(0, XK_KP_Right) ||
                     justPressed(113, XK_Left)  || justPressed(0, XK_KP_Left)  ||
                     justPressed(36, XK_Return) || justPressed(0, XK_KP_Enter)) {

                bool forward = isDown(114) || isDown(pfnXKeysymToKeycode(m_display, XK_KP_Right)) ||
                               isDown(36)  || isDown(pfnXKeysymToKeycode(m_display, XK_KP_Enter));

                if (cfg.menuPage == 0) {
                    switch (m_selectedItem) {
                        case 0:
                            if (cfg.mode == "v2") cfg.mode = forward ? "v1" : "off";
                            else if (cfg.mode == "v1") cfg.mode = forward ? "off" : "v2";
                            else cfg.mode = forward ? "v2" : "v1";
                            break;

                        case 1:
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
                                if (cfg.targetFps == 0) cfg.targetFps = 360;
                                else if (cfg.targetFps == 360) cfg.targetFps = 240;
                                else if (cfg.targetFps == 240) cfg.targetFps = 165;
                                else if (cfg.targetFps == 165) cfg.targetFps = 144;
                                else if (cfg.targetFps == 144) cfg.targetFps = 120;
                                else if (cfg.targetFps == 120) cfg.targetFps = 90;
                                else if (cfg.targetFps == 90) cfg.targetFps = 75;
                                else if (cfg.targetFps == 75) cfg.targetFps = 60;
                                else if (cfg.targetFps == 60) cfg.targetFps = 45;
                                else if (cfg.targetFps == 45) cfg.targetFps = 30;
                                else cfg.targetFps = 0;
                            }
                            cfg.schedulerMode = (cfg.targetFps > 0) ? "target_fps" : "auto";
                            break;

                        case 2:
                            if (forward) cfg.multiplier = (cfg.multiplier >= 6) ? 2 : (cfg.multiplier + 1);
                            else cfg.multiplier = (cfg.multiplier <= 2) ? 6 : (cfg.multiplier - 1);
                            break;

                        case 3:
                            if (cfg.schedulerMode == "auto") cfg.schedulerMode = forward ? "fixed" : "target_fps";
                            else if (cfg.schedulerMode == "fixed") cfg.schedulerMode = forward ? "target_fps" : "auto";
                            else cfg.schedulerMode = forward ? "auto" : "fixed";
                            break;

                        case 4:
                            if (cfg.lowLatency == "on") cfg.lowLatency = forward ? "boost" : "off";
                            else if (cfg.lowLatency == "boost") cfg.lowLatency = forward ? "off" : "on";
                            else cfg.lowLatency = forward ? "on" : "boost";
                            break;

                        case 5:
                            cfg.profile = (cfg.profile == "quality") ? "performance" : "quality";
                            break;

                        case 6:
                            cfg.searchMode = (cfg.searchMode == "high") ? "standard" : "high";
                            break;

                        case 7:
                            if (cfg.fallbackAction == "repeat") cfg.fallbackAction = forward ? "blend" : "drop";
                            else if (cfg.fallbackAction == "blend") cfg.fallbackAction = forward ? "drop" : "repeat";
                            else cfg.fallbackAction = forward ? "repeat" : "blend";
                            break;

                        case 8:
                            if (forward) cfg.hudMode = (cfg.hudMode >= 3) ? 0 : (cfg.hudMode + 1);
                            else cfg.hudMode = (cfg.hudMode == 0) ? 3 : (cfg.hudMode - 1);
                            break;

                        case 9:
                            if (forward) cfg.hudCorner = (cfg.hudCorner >= 3) ? 0 : (cfg.hudCorner + 1);
                            else cfg.hudCorner = (cfg.hudCorner == 0) ? 3 : (cfg.hudCorner - 1);
                            break;
                    }
                } else {
                    switch (m_selectedItem) {
                        case 0: // PRESENT WAIT: 0=Auto, 1=Wait2, 2=Wait1, 3=Disabled
                            if (forward) cfg.extPresentWait = (cfg.extPresentWait >= 3) ? 0 : (cfg.extPresentWait + 1);
                            else cfg.extPresentWait = (cfg.extPresentWait == 0) ? 3 : (cfg.extPresentWait - 1);
                            break;

                        case 1: // LOW LATENCY: 0=Auto, 1=AntiLag, 2=Reflex, 3=JIT, 4=Off
                            if (forward) cfg.extLowLatency = (cfg.extLowLatency >= 4) ? 0 : (cfg.extLowLatency + 1);
                            else cfg.extLowLatency = (cfg.extLowLatency == 0) ? 4 : (cfg.extLowLatency - 1);
                            break;

                        case 2: // DISPLAY TIMING: 0=Auto, 1=EXT, 2=Google, 3=Sysfs, 4=Fixed
                            if (forward) cfg.extDisplayTiming = (cfg.extDisplayTiming >= 4) ? 0 : (cfg.extDisplayTiming + 1);
                            else cfg.extDisplayTiming = (cfg.extDisplayTiming == 0) ? 4 : (cfg.extDisplayTiming - 1);
                            break;

                        case 3: // TIMESTAMPS: 0=Auto, 1=KHR, 2=EXT, 3=QueryPool
                            if (forward) cfg.extTimestamps = (cfg.extTimestamps >= 3) ? 0 : (cfg.extTimestamps + 1);
                            else cfg.extTimestamps = (cfg.extTimestamps == 0) ? 3 : (cfg.extTimestamps - 1);
                            break;

                        case 4: // SWAPCHAIN MAINT: 0=Auto, 1=Maint1, 2=Fences
                            if (forward) cfg.extSwapchainMaint = (cfg.extSwapchainMaint >= 2) ? 0 : (cfg.extSwapchainMaint + 1);
                            else cfg.extSwapchainMaint = (cfg.extSwapchainMaint == 0) ? 2 : (cfg.extSwapchainMaint - 1);
                            break;

                        case 5: // FORCE FG: auto -> repeat -> blend -> drop
                            if (cfg.forceFallback == "auto") cfg.forceFallback = forward ? "repeat" : "drop";
                            else if (cfg.forceFallback == "repeat") cfg.forceFallback = forward ? "blend" : "auto";
                            else if (cfg.forceFallback == "blend") cfg.forceFallback = forward ? "drop" : "repeat";
                            else cfg.forceFallback = forward ? "auto" : "blend";
                            break;

                        case 6: // CONFIDENCE OVERRIDE: Auto -> 0.20 -> 0.40 -> 0.60 -> 0.80 -> 0.95
                            if (forward) {
                                if (cfg.confOverride < 0.1f) cfg.confOverride = 0.20f;
                                else if (cfg.confOverride < 0.3f) cfg.confOverride = 0.40f;
                                else if (cfg.confOverride < 0.5f) cfg.confOverride = 0.60f;
                                else if (cfg.confOverride < 0.7f) cfg.confOverride = 0.80f;
                                else if (cfg.confOverride < 0.9f) cfg.confOverride = 0.95f;
                                else cfg.confOverride = 0.0f;
                            } else {
                                if (cfg.confOverride > 0.9f) cfg.confOverride = 0.80f;
                                else if (cfg.confOverride > 0.7f) cfg.confOverride = 0.60f;
                                else if (cfg.confOverride > 0.5f) cfg.confOverride = 0.40f;
                                else if (cfg.confOverride > 0.3f) cfg.confOverride = 0.20f;
                                else cfg.confOverride = 0.0f;
                            }
                            break;

                        case 7: // STRIDE: Auto -> 1px -> 2px -> 3px
                            if (forward) cfg.strideOverride = (cfg.strideOverride >= 3) ? 0 : (cfg.strideOverride + 1);
                            else cfg.strideOverride = (cfg.strideOverride == 0) ? 3 : (cfg.strideOverride - 1);
                            break;

                        case 8: // VISUAL: 0=Off, 1=Badge, 2=Conf Mask, 3=Flow Vectors
                            if (forward) cfg.debugVisual = (cfg.debugVisual >= 3) ? 0 : (cfg.debugVisual + 1);
                            else cfg.debugVisual = (cfg.debugVisual == 0) ? 3 : (cfg.debugVisual - 1);
                            break;

                        case 9: // SWITCH TO MAIN
                            cfg.menuPage = 0;
                            m_selectedItem = 0;
                            break;
                    }
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
        if (!m_x11Handle) m_x11Handle = dlopen("libX11.so", RTLD_NOW | RTLD_GLOBAL);
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