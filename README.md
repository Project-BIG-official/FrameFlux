# ⚡ PB FrameFlux

**Universal, vendor-agnostic, and API-agnostic Frame Generation Runtime for Linux.**

[![License: LGPL v2.1](https://img.shields.io/badge/License-LGPL_v2.1-blue.svg)](LICENSE)
[![Release](https://img.shields.io/badge/Release-v2.1%20%7C%20v1.2-brightgreen.svg)](https://github.com/Project-BIG-official/FrameFlux/releases/tag/v2.1%2Cv1.2)
[![Vulkan](https://img.shields.io/badge/Vulkan-1.3%2B-red.svg)](https://www.vulkan.org/)
[![Shaders: Slang](https://img.shields.io/badge/Shaders-Slang-orange.svg)](https://shader-slang.com/)
[![Architecture](https://img.shields.io/badge/Arch-x86--64--v2%20%7C%20v3%20%7C%20v4-success.svg)](#-archive-selection--cpu-architecture-tiers)

PB FrameFlux is a high-performance, open-source implicit Vulkan presentation layer that injects real-time frame generation into **any** game running on Linux. Because interception occurs directly at the Vulkan presentation boundary (`vkQueuePresentKHR`), it requires zero game mods, engine hooks, or game-specific integrations.

---

## 🎮 Translation Layer & API Coverage

* **DirectX 3 / 5 / 6 / 7** via D7VK
* **DirectX 8 / 9 / 10 / 11** via DXVK
* **DirectX 12** via VKD3D-Proton
* **OpenGL / OpenGL ES** via Zink
* **Native Vulkan** games
* **Steam / Proton / Wine / Heroic / Lutris / Bottles**

---

## 🚀 What's New in v2.1 & v1.2

* **Low-Latency Pipeline (Anti-Lag & Reflex):** Native driver integration for `VK_AMD_anti_lag` and `VK_NV_low_latency2` with automatic host-side JIT queue pacing fallback on unsupported drivers (e.g. Mesa RADV).
* **Interactive In-Game Menu (Dual-Page OSD):** Real-time tuning overlay (<kbd>Insert</kbd> / <kbd>F8</kbd>) featuring a **Main Settings Page** and an advanced **Vulkan Debug Panel** (<kbd>Tab</kbd>).
* **OptiScaler-Style Optical Flow Vector Arrows:** Full-screen directional vector arrow grid, Middlebury pastel flow color encoding, and an inset directional calibration compass legend.
* **Independent Target FPS Governor:** Frame generation dynamically matches your monitor or desired framerate (30–360 FPS) completely independent of the fixed multiplier.
* **Universal Multi-Level Fallback Engine:** Multi-tier fail-safes for Present Wait, Swapchain Maintenance 1, Calibrated Timestamps (`KHR` / `EXT` / `QueryPool`), and Display Timing (Vulkan $\to$ Linux DRM sysfs $\to$ 165 Hz).
* **Rock-Solid Multi-Frame Stability:** Non-blocking swapchain image acquisition supporting up to **6× generation** (`2×`, `3×`, `4×`, `5×`, `6×`) without deadlock or presentation queue starvation.
* **Static UI Protection & Anti-Ghosting:** Motion vector dampening prevents shimmering and smearing on crosshairs, minimaps, and static HUD text.

---

## 🌟 Dual-Engine Architecture

Choose the ideal generator for your game and hardware:

### Mode v2.1 — True Motion-Compensated Frame Generation

* Optical flow block matcher powered by hardware **DP4A** (`dot4add_u8packed`) integer dot products.
* **Wave32 Subgroup SIMD Acceleration:** Parallel evaluation of 25 search offsets per block with graceful fallback to standard compute pipelines if driver size control is rejected.
* **Zero-Motion Bias:** Prevents texture hallucination, crawling, or drifting on static backgrounds and interface elements.
* **Anti-Halo Disocclusion Clamping:** Color-box clamping and 5-tap median filtering eradicate silhouette ghosting around moving characters.

### Mode v1.2 — Legacy Frame Blending

* Ultra-fast, ultra-low-power linear interpolation (`≈ 0.05–0.15 ms` compute overhead).
* Bypasses optical flow entirely to maximize battery life on handhelds (**Steam Deck**, **ROG Ally**, **Legion Go**).
* Zero geometric warping artifacts — crystal clear for 2D titles, pixel-art games, visual novels, and grand strategy games.
* Full integration with the new diagnostics HUD, telemetry, and Target FPS pacing.

---

## ⚡ Multipliers & Frame Scheduling

Supported multipliers: **`2×`, `3×`, `4×`, `5×`, `6×`**

Multi-frame generation is computed strictly from pristine anchor frames:

$$
A \xrightarrow{\hspace{60pt}} B \implies G_1(t),\quad G_2(t),\quad \dots,\quad G_n(t)
$$

* Optical flow is evaluated **once** per game frame; intermediate subframes are synthesized without recursive degradation or cumulative error.
* **Non-Blocking Swapchain Pool:** Expands buffer pools up to 8 images, allowing `4×`, `5×`, and `6×` pipelines to acquire subframes without blocking the game render thread.

---

## ⏱️ Precision Pacing & Target FPS

* **Target FPS Governor:** Set a target rate (e.g. 60, 120, 144, 165, 240, 360 FPS). The governor calculates the fractional ratio of required subframes, automatically generating only as many frames as needed. If native performance reaches the target, subframe generation drops to zero.
* **Isolated Render Timing:** Isolates present calls from engine sleep loops to prevent stuttering.
* **Dynamic DRM Display Query:** Automatically probes `/sys/class/drm/*/modes` on Linux to determine true monitor refresh rates when Vulkan display timing extensions are unavailable.

---

## 🕹️ In-Game Menu & HUD

FrameFlux features a built-in interactive menu and telemetry HUD rendered directly onto the swapchain.

### Hotkeys

| Key | Action |
| :--- | :--- |
| <kbd>Insert</kbd> / <kbd>F8</kbd> / <kbd>F11</kbd> / <kbd>Home</kbd> | Toggle Menu (Open / Close) |
| <kbd>Tab</kbd> | Switch Menu Page (**Main Configuration** $\leftrightarrow$ **Debug Panel**) |
| <kbd>↑</kbd> / <kbd>↓</kbd> | Navigate Menu Items |
| <kbd>←</kbd> / <kbd>→</kbd> / <kbd>Enter</kbd> | Change Value / Toggle Option |
| <kbd>Esc</kbd> | Close Menu & Save to `frameflux.ini` |

### Menu Pages

```
+------------------------------------+     +------------------------------------+
|       [-- PB FRAMEFLUX MENU --]    |     |       [-- PB DEBUG PANEL --]       |
| MODE:         < TRUE FG (v2) >     |     | PRESENT:     < AUTO >              |
| TARGET FPS:   < 120 >              |     | LATENCY:     < JIT EMULATED >      |
| MULTIPLIER:   < 2X >               |     | TIMING:      < DRM SYSFS >         |
| SCHEDULER:    < TARGET FPS >       |     | CLOCKS:      < CALIBRATED KHR >    |
| LOW LATENCY:  < BOOST >            |     | SWAPCHAIN:   < MAINT1 (KHR) >      |
| PROFILE:      < QUALITY >          |     | FORCE FG:    < AUTO >              |
| SEARCH:       < HIGH (+-24PX) >    |     | CONF:        < AUTO (0.55) >       |
| FALLBACK:     < REPEAT >           |     | STRIDE:      < AUTO >              |
| HUD MODE:     < FULL >             |     | VISUAL:      < FLOW VECTORS >      |
| HUD CORNER:   < TOP-LEFT >         |     | SWITCH:      < [TAB] MAIN PAGE >   |
| [ARROWS] ADJUST  [TAB] DEBUG PAGE  |     | [ARROWS] ADJUST  [TAB] MAIN PAGE   |
+------------------------------------+     +------------------------------------+
              (Page 0)                                   (Page 1)
```

---

## 🔍 Debug Visualizers

Toggle live diagnostic modes inside the **Debug Panel** under `VISUAL`:

1. **`[FG]` Badge:** Displays a corner indicator verifying that interpolated frames are actively being presented.
2. **Confidence Heatmap:** Full-screen thermal diagnostic view visualizing motion reliability and occlusion zones (Green = High Confidence, Red = Fallback).
3. **Flow Vectors (Arrows):** Displays an Middlebury-style directional arrow grid and vector field, complete with a circular directional compass legend in the bottom-right corner.

---

## 📦 Installation

### One-Line Smart Installer

Automatically detects your CPU architecture tier and downloads the latest matching release:

```bash
curl -sSL https://github.com/Project-BIG-official/FrameFlux/releases/latest/download/install.sh | bash
```

### Manual Installation

1. Download the archive for your CPU tier from [**Releases**](https://github.com/Project-BIG-official/FrameFlux/releases/latest):
   * **x86-64-v4**: Modern enthusiast CPUs with AVX-512 (AMD Zen 4/5, Intel 11th Gen+)
   * **x86-64-v3**: Modern gaming PCs & **Steam Deck** (AVX2, FMA3, BMI2 — Zen 2/3, Intel Haswell–10th Gen)
   * **x86-64-v2**: Older systems (SSE4.2 baseline, pre-2013)

2. Extract and copy `libVkLayer_projectbig_frameflux.so` to:
   ```bash
   mkdir -p ~/.local/lib/frameflux/
   cp libVkLayer_projectbig_frameflux.so ~/.local/lib/frameflux/
   ```

3. Copy `VkLayer_projectbig_frameflux.json` to:
   ```bash
   mkdir -p ~/.local/share/vulkan/implicit_layer.d/
   cp VkLayer_projectbig_frameflux.json ~/.local/share/vulkan/implicit_layer.d/
   ```

---

## 🚀 How to Launch Games

PB FrameFlux sits dormant as an implicit Vulkan layer and will **never** affect your system until explicitly activated via environment variables.

### Steam

Right-click your game: **Properties → General → Launch Options**:

```bash
ENABLE_FRAMEFLUX=1 %command%
```

To run with MangoHud:

```bash
ENABLE_FRAMEFLUX=1 MANGOHUD=1 %command%
```

To force **Legacy Ultra-Low-Power Mode** (v1.2):

```bash
ENABLE_FRAMEFLUX=legacy %command%
```

### Heroic Games Launcher / Lutris / Bottles

Add the following environment variable in the game configuration:

| Key | Value |
| :--- | :--- |
| `ENABLE_FRAMEFLUX` | `1` |

---

## ⚙️ Configuration (`frameflux.ini`)

Upon the first launch of any game, a self-documented `frameflux.ini` is automatically created in the game directory:

```ini
# PB FrameFlux Configuration File

[general]
# Mode: 'v2' (True Optical Flow FG), 'v1' (Legacy Frame Blend), 'off'
mode = v2

# Quality Profile: 'quality' (2-pass hierarchical refinement), 'performance' (fast single-pass)
profile = quality

# Frame multiplier factor: 2, 3, 4, 5, or 6
multiplier = 2

# Scheduler Mode: 'auto', 'fixed', 'target_fps'
scheduler_mode = auto

# Target FPS: locks cadence to a specific framerate (e.g. 60, 120, 144, 165). 0 = off
target_fps = 0

# Low latency mode: 'on', 'boost', 'off'
low_latency = on

[hud]
# HUD Mode: 0 = Off, 1 = Compact, 2 = Full, 3 = Latency Stats
hud_mode = 2

# HUD Corner: 0 = Top-Left, 1 = Top-Right, 2 = Bottom-Left, 3 = Bottom-Right
hud_corner = 0

[debug]
# Master debug mode toggle
debug_mode = false

# Optical flow search radius: 'high' (+-24px), 'standard' (+-8px)
search_mode = high

# Fallback on low confidence: 'repeat', 'blend', 'drop'
fallback_action = repeat

# Force algorithm override: 'auto', 'repeat', 'blend', 'drop'
force_fallback = auto

# Debug visualizer: 0 = Off, 1 = Badge, 2 = Confidence Map, 3 = Flow Vectors
debug_visual = 0

# Confidence override: 0.0 (Auto), or 0.20 to 0.95
conf_override = 0.0

# Search stride override: 0 = Auto, 1 = 1px, 2 = 2px, 3 = 3px
stride_override = 0

# Extension Overrides: 0 = Auto, or manually force driver paths
ext_present_wait = 0
ext_low_latency = 0
ext_display_timing = 0
ext_timestamps = 0
ext_swapchain_maint = 0

# Precision microsecond pacing
enable_pacing = true
```

### Environment Variable Overrides

Environment variables take precedence over `frameflux.ini`:

| Variable | Values | Description |
| :--- | :--- | :--- |
| `ENABLE_FRAMEFLUX` | `1` / `v2`, `legacy` / `v1`, `0` / `off` | Activates FrameFlux engine mode |
| `FRAMEFLUX_MULTIPLIER` | `2`, `3`, `4`, `5`, `6` | Frame generation multiplier factor |
| `FRAMEFLUX_TARGET_FPS` | `0` (off) or integer (e.g. `120`, `144`) | Locks presentation cadence to target FPS |
| `FRAMEFLUX_PROFILE` | `quality`, `performance` | Selects optical flow pyramid profile |
| `FRAMEFLUX_DEBUG` | `1`, `0` | Enables debug telemetry and logging |

---

## 🛠️ Building from Source

### Prerequisites

* GCC 12+ or Clang 15+ (with C++20 support)
* CMake 3.22+
* Vulkan SDK 1.3+ headers & loader
* [Slang compiler (`slangc`)](https://github.com/shader-slang/slang) in your `PATH`
* `libX11-dev` (optional for Linux hotkey interception)

### Build Steps

```bash
git clone https://github.com/Project-BIG-official/FrameFlux.git
cd FrameFlux

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel $(nproc)
```

### Install Implicit Layer

```bash
mkdir -p ~/.local/share/vulkan/implicit_layer.d
mkdir -p ~/.local/lib/frameflux
cp build/libVkLayer_projectbig_frameflux.so ~/.local/lib/frameflux/
cp build/VkLayer_projectbig_frameflux.json ~/.local/share/vulkan/implicit_layer.d/
```

---

## 📜 License

PB FrameFlux is distributed under the terms of the **GNU Lesser General Public License v2.1 (LGPL-2.1)**.

See [**LICENSE**](LICENSE) for details.
