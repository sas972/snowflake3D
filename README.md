# Snowflake 3D – Reiter Cellular Automaton (Dawn + Dear ImGui, Native & Web)

Interactive 3D snowflake crystal growth simulator based on Cliff Reiter's hexagonal cellular automaton model, extended to a 3D hexagonal-prism lattice (6 basal + 2 axial c-axis neighbors, anisotropic diffusion/condensation rates, and thermal noise perturbations).

Rendered with instanced 3D hexagonal prisms via WebGPU (**Google Dawn** on desktop / native, and **emdawnwebgpu** in the browser) with a modern **Dear ImGui** (SDL3 + WebGPU) interactive control interface.

---

## 🌐 Live Demo

Experience the interactive 3D simulation directly in your browser:  
👉 **[https://sas972.github.io/snowflake3D/](https://sas972.github.io/snowflake3D/)**

*(Requires a WebGPU-enabled browser such as Google Chrome 113+, Microsoft Edge 113+, or Safari 18+).*

---

## ✨ Features

- **3D Hexagonal-Prism Lattice:**
  - Axial $(q, r, z)$ coordinate system with 6 basal and 2 vertical c-axis neighbors.
  - Anisotropic 3D discrete Laplacian diffusion and facet-dependent condensation ($\gamma_{XY}$ vs $\gamma_Z$).
  - Surface voxel extraction with interior occlusion culling for high-performance rendering.
- **Dynamic Crystal Growth:**
  - 6 morphological presets (Planar Stellar Dendrite, Capped Column, Hollow Prism, 3D Fern, Sectored Plate, Needles).
  - Dynamic atmospheric flight mode (cycles humidity/temperature to simulate crystals falling through cloud layers).
- **Interactive WebGPU & Dear ImGui Dashboard:**
  - Orbit camera (left-click drag to rotate, scroll wheel to zoom).
  - Real-time parameter tuning ($\alpha_{XY}, \alpha_Z, \beta, \gamma, \eta$, grid dimensions).
  - Interactive 2D cross-section slice inspector.
  - Z-clipping and multiple color visualization modes (Ice, Heatmap, Growth Age, Height).

---

## 🛠️ Build & Run

### Native Build (macOS / Linux / Windows)

Prerequisites:
- **C++20** compatible compiler
- **CMake** ($\ge 3.22$)
- **SDL3** (`brew install sdl3` on macOS)
- Google Dawn prebuilt binaries (fetched automatically or cached from `.cache/dawn-prebuilt`)

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/snowflake3d
```

### WebAssembly / Web Build (WebGPU)

Prerequisites:
- **Emscripten SDK** ($\ge 4.0.10$)

```bash
emcmake cmake -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-web
python3 -m http.server 8080 -d web
```
Then open **[http://localhost:8080](http://localhost:8080)** in your WebGPU-supported browser.

---

## 🎮 Controls

- **Mouse Drag (Left Button):** Orbit camera view
- **Mouse Wheel:** Zoom in / out
- **ImGui Dashboard:** Switch presets, pause/step simulation, tune physical parameters, adjust Z-clipping layer, inspect 2D slices.

---

## 📜 License

Copyright (c) 2026 **Imre Városi**

Licensed under the **MIT License with the Beer-Ware Clause (Revision 42)** — see [LICENSE](LICENSE) for details.  
If we meet some day, and you think this stuff is worth it, you can buy me a beer in return! 🍺
