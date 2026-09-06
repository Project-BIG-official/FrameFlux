# PB FrameFlux

**Open-source, vendor-agnostic, and API-agnostic Frame Generation Runtime for Linux.**

[![License: LGPL v2.1](https://img.shields.io/badge/License-LGPL_v2.1-blue.svg)](LICENSE)
[![Vulkan](https://img.shields.io/badge/Vulkan-1.3%2B-red.svg)](https://www.vulkan.org/)
[![Slang](https://img.shields.io/badge/Shaders-Slang-orange.svg)](https://shader-slang.com/)

PB FrameFlux is an open-source Vulkan presentation layer that injects frame generation into **any** game running on Linux. Because it intercepts the rendering pipeline at the final presentation boundary (`vkQueuePresentKHR`), it does not care what original API the game was built for.

### 🎮 Fully Supported Translation Layers / APIs

* **DirectX 3 / 5 / 6 / 7** via D7VK
* **DirectX 8 / 9 / 10 / 11** via DXVK
* **DirectX 12** via VKD3D-Proton / VKD3D
* **Native Vulkan** games
* **OpenGL / OpenGL ES** via Zink
* **Wine / Proton / Heroic / Lutris / Steam**

---

## ✨ Features

* **True 2× Frame Presentation Injection:** Dynamically generates intermediate frames and acquires display slots seamlessly.
* **Slang Compute Shaders:** Powered by modern high-performance SPIR-V compute pipelines compiled via Slang.
* **Hardware-Aware Optical Flow:**

  * Fast packed 8-bit luminance downsampling.
  * Accelerated subgroup SIMD block matching using `dot4add_u8packed` (DP4A) hardware instructions.
* **Bidirectional Warping & Fallback:** Smooth motion estimation with fallback protection against disocclusion artifacts.
* **Zero Configuration Necessary:** Works out-of-the-box with a simple environment variable toggle.
* **Portability:** Built with statically linked standard libraries to run across virtually any Linux distribution and SteamOS.

---

## 🚀 Quick Install

### One-Line Install

The installer automatically detects your CPU architecture:

```bash
curl -sSL https://github.com/ProjectBig/PB-FrameFlux/raw/main/install.sh | bash
```

### Manual Install

1. Download the archive for your CPU architecture from [**Releases**](https://github.com/ProjectBig/PB-FrameFlux/releases/latest).

   Available builds:

   * **x86-64-v4** — Modern CPUs with AVX-512 (AMD Zen 4/5, Intel 11th Gen+)
   * **x86-64-v3** — Mainstream gaming PCs & **Steam Deck** (AMD Zen 2/3, Intel Haswell to 10th Gen)
   * **x86-64-v2** — Older CPUs (SSE4.2 baseline)

2. Extract the archive and copy `libVkLayer_projectbig_frameflux.so` to:

   ```text
   ~/.local/lib/frameflux/
   ```

3. Copy `VkLayer_projectbig_frameflux.json` to:

   ```text
   ~/.local/share/vulkan/implicit_layer.d/
   ```

4. Update the `library_path` inside the JSON file if necessary.

---

## 🕹️ How to Use

PB FrameFlux runs as a dormant implicit Vulkan layer and will **never interfere with your system until explicitly activated**.

### Steam

1. Right-click your game.
2. Select **Properties**.
3. Open **Launch Options**.
4. Add:

   ```bash
   ENABLE_FRAMEFLUX=1 %command%
   ```

   Optionally, combine it with MangoHud:

   ```bash
   ENABLE_FRAMEFLUX=1 MANGOHUD=1 %command%
   ```

### Heroic Games Launcher

1. Open **Game Settings**.
2. Go to **Environment Variables**.
3. Add:

   | Key                | Value |
   | ------------------ | ----- |
   | `ENABLE_FRAMEFLUX` | `1`   |

### Lutris

1. Open the game's configuration.
2. Go to **System Options**.
3. Find **Environment variables**.
4. Add:

   | Variable           | Value |
   | ------------------ | ----- |
   | `ENABLE_FRAMEFLUX` | `1`   |

---

## 🛠️ Building from Source

### Clone the repository

```bash
git clone https://github.com/ProjectBig/PB-FrameFlux.git
cd PB-FrameFlux
```

### Configure & Build

For a fast local development build:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Install locally

```bash
mkdir -p ~/.local/share/vulkan/implicit_layer.d
cp build/VkLayer_projectbig_frameflux.json ~/.local/share/vulkan/implicit_layer.d/
```

---

## 📜 License

PB FrameFlux is distributed under the terms of the **GNU Lesser General Public License v2.1 (LGPL-2.1)**.

See the [**LICENSE**](LICENSE) file for details.
