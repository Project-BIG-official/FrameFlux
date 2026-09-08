# ⚡ PB FrameFlux

**Universal, vendor-agnostic, and API-agnostic Frame Generation Runtime for Linux.**

[![License: LGPL v2.1](https://img.shields.io/badge/License-LGPL_v2.1-blue.svg)](LICENSE)
[![Vulkan](https://img.shields.io/badge/Vulkan-1.3%2B-red.svg)](https://www.vulkan.org/)
[![Slang](https://img.shields.io/badge/Shaders-Slang-orange.svg)](https://shader-slang.com/)
[![Architecture](https://img.shields.io/badge/Arch-x86--64--v2%20%7C%20v3%20%7C%20v4-success.svg)](#-architecture-tiers)

PB FrameFlux is an open-source Vulkan presentation layer that injects real-time frame generation into **any** game running on Linux. Because it intercepts execution at the final Vulkan presentation boundary (`vkQueuePresentKHR`), it requires zero game mods, engine hooks, or game-specific integration.

---

## 🎮 Seamless Translation Layer Coverage

* **DirectX 3 / 5 / 6 / 7** via D7VK
* **DirectX 8 / 9 / 10 / 11** via DXVK
* **DirectX 12** via VKD3D-Proton
* **Native Vulkan** games
* **OpenGL / OpenGL ES** via Zink
* **Wine / Proton / Heroic / Lutris / Steam**

---

## 🚀 Key Features

### 🌟 Dual-Engine Architecture

Choose the ideal generator for your game and hardware:

#### Mode v2.0 — True Motion-Compensated Frame Generation

* High-performance optical flow block matcher powered by hardware **DP4A** (`dot4add_u8packed`) integer dot products.
* Subgroup SIMD acceleration: evaluates 25 search offsets in parallel across wave lanes with zero divergent loop overhead.
* **Zero-Motion Bias:** strictly prevents texture hallucination, crawling, or drifting on static surfaces, backgrounds, and HUD elements.
* Continuous hardware-interpolated vector upscaling via bilinear UV sampling.

#### Mode v1.1 — Legacy Frame Blending

* Ultra-fast, ultra-low-power linear interpolation (`≈ 0.05 ms` compute overhead).
* Bypasses optical flow completely to save battery on handhelds (Steam Deck, ROG Ally).
* Zero geometric warping artifacts — crystal clear for 2D, pixel-art, CRPGs, and grand strategy games.

---

## ⚡ Non-Recursive Multipliers

Supported multipliers:

**`2×`, `3×`, `4×`, `5×`, `6×`**

Multi-frame generation is computed strictly from pristine anchor frames:

$$
A \xrightarrow{\hspace{60pt}} B
\implies
G_1(t),\quad G_2(t),\quad \dots,\quad G_n(t)
$$

Optical flow is evaluated **once** per game frame; intermediate frames are synthesized without cumulative temporal degradation or recursive ghosting.

---

## ⏱️ Precision Pacing & Adaptive Target Hz

* **Isolated Render Timing:** completely eliminates sleep-drift feedback loops and micro-stutters.
* **Microsecond Pacer:** hybrid sleep + CPU pause spin (`__builtin_ia32_pause`) locks cadence with `< 0.05 ms` jitter.
* **Target FPS / Display Hz Mode:** locks frame pacing directly to your monitor's physical refresh rate (e.g. 120, 144, 165, 240 Hz).
* **Fast-Motion Fallbacks:** configurable `blend`, `repeat`, or `drop` (latency bypass for VRR/FreeSync panels).

---

## 🛠️ In-Game Auto-Configuration

* Automatically creates a self-documented `frameflux.ini` directly inside the game's folder on first launch.
* Smart auto-migration preserves user edits while seamlessly injecting new parameters.

---

## 📦 Quick Install

### One-Line Smart Install

Automatically detects CPU microarchitecture:

```bash
curl -sSL https://github.com/Project-BIG-official/FrameFlux/releases/latest/download/install.sh | bash
```

### Manual Install

1. Download the archive for your CPU tier from [**Releases**](https://github.com/Project-BIG-official/FrameFlux/releases/latest).

2. Choose the appropriate architecture:

   * **x86-64-v4**: Modern CPUs with AVX-512 (AMD Zen 4/5, Intel 11th Gen+)
   * **x86-64-v3**: Modern gaming PCs & **Steam Deck** (AMD Zen 2/3, Intel Haswell–10th Gen)
   * **x86-64-v2**: Legacy PCs (SSE4.2 baseline)

3. Extract and copy `libVkLayer_projectbig_frameflux.so` to:

   ```text
   ~/.local/lib/frameflux/
   ```

4. Copy `VkLayer_projectbig_frameflux.json` to:

   ```text
   ~/.local/share/vulkan/implicit_layer.d/
   ```

---

## 🕹️ How to Use

PB FrameFlux sits dormant as an implicit Vulkan layer and will **never** affect your system until explicitly invoked.

### Steam

Right-click your game:

**Properties → Launch Options**

Add:

```bash
ENABLE_FRAMEFLUX=1 %command%
```

Optionally combined with MangoHud:

```bash
ENABLE_FRAMEFLUX=1 MANGOHUD=1 %command%
```

### Heroic Games Launcher / Lutris

Add the following under **Environment Variables**:

| Key                | Value |
| ------------------ | ----- |
| `ENABLE_FRAMEFLUX` | `1`   |

---

## ⚙️ Configuration (`frameflux.ini`)

Upon the first launch of any game with `ENABLE_FRAMEFLUX=1`, a `frameflux.ini` file is automatically created in the game directory:

```ini
[general]
# Mode: 'v2' (True Optical Flow FG), 'v1' (Legacy Frame Blend), 'off'
mode = v2

# Quality profile: 'quality' (2-pass refinement), 'performance' (fast single-pass)
profile = quality

# Frame generation multiplier: 2, 3, 4, 5, or 6
multiplier = 2

# Scheduler mode: 'auto' (VBlank sync), 'fixed' (strict pace), 'target_fps'
scheduler_mode = auto

# Target FPS: lock cadence to specific Hz (e.g. 120, 144, 165). 0 = auto
target_fps = 0

[quality]
# Search mode: 'high' (+-24px wide motion search), 'standard' (+-8px fast search)
search_mode = high

# Fallback behavior when motion confidence drops: 'blend', 'repeat', 'drop' (for VRR)
fallback_action = blend

[debug]
# Show small neon-green watermark square on generated frames to verify presentation
show_watermark = false
```

### Environment Variable Overrides

Environment variables take precedence over `frameflux.ini`.

| Variable                | Values                                   | Description                                    |
| ----------------------- | ---------------------------------------- | ---------------------------------------------- |
| `ENABLE_FRAMEFLUX`      | `1` / `v2`, `legacy` / `v1`, `0` / `off` | Toggles generator mode                         |
| `FRAMEFLUX_MULTIPLIER`  | `2`, `3`, `4`, `5`, `6`                  | Frame multiplier factor                        |
| `FRAMEFLUX_TARGET_FPS`  | `0` (auto), or integer (e.g. `144`)      | Locks presentation cadence to specific FPS     |
| `FRAMEFLUX_SEARCH_MODE` | `high`, `standard`                       | Expands optical flow search radius             |
| `FRAMEFLUX_FALLBACK`    | `blend`, `repeat`, `drop`                | Fallback strategy on low confidence            |
| `FRAMEFLUX_DEBUG`       | `1`, `0`                                 | Displays green verification watermark on frame |

---

## 🛠️ Building from Source

### Clone Repository

```bash
git clone https://github.com/Project-BIG-official/FrameFlux.git
cd FrameFlux
```

### Configure & Build

Fast developer build:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Install Locally

```bash
mkdir -p ~/.local/share/vulkan/implicit_layer.d
cp build/VkLayer_projectbig_frameflux.json ~/.local/share/vulkan/implicit_layer.d/
```

---

## 📜 License

PB FrameFlux is distributed under the terms of the **GNU Lesser General Public License v2.1 (LGPL-2.1)**.

See [**LICENSE**](LICENSE) for details.
