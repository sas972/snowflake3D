# Snowflake 3D – Reiter cellular automaton (Dawn + Dear ImGui, native & web)

3D snowflake growth simulator: Reiter's hexagonal CA extended to a hexagonal-prism lattice
(6 basal + 2 c-axis neighbours, anisotropic diffusion/attachment, thermal noise).
Rendered with instanced hex prisms through WebGPU: **Dawn** on desktop, **emdawnwebgpu** in the browser.
UI: Dear ImGui (SDL3 backend + WebGPU backend).

## Build – native (macOS / Linux / Windows)

Requires CMake ≥ 3.22, SDL3 (`brew install sdl3`). Dawn prebuilt is fetched from google/dawn releases
(or reused from `.cache/dawn-prebuilt`).

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/snowflake3d
```

## Build – web (WebAssembly + WebGPU)

Requires Emscripten ≥ 4.0.10.

```bash
emcmake cmake -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-web
python3 -m http.server 8080 -d web     # open http://localhost:8080 in a WebGPU browser
```

## Controls
- Left drag: orbit, wheel: zoom
- ImGui panel: presets (dendrite, capped column, hollow prism, fern, plate, needles), physics parameters,
  grid size, Z-clipping, colour modes, 2D cross-section inspector.

