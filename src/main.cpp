/*
 * Copyright (c) 2026 Imre Városi
 * Licensed under the MIT License with the Beer-Ware Clause (Revision 42).
 * If we meet some day, and you think this stuff is worth it, you can buy me a beer in return.
 * See LICENSE file in the project root for full license text.
 */

// 3D Reiter snowflake simulator: SDL3 + Dawn (native) / emdawnwebgpu (web) + Dear ImGui.

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_wgpu.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <webgpu/webgpu.h>
#include <webgpu/webgpu_cpp.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include "Reiter3D.hpp"
#include "Renderer3D.hpp"

namespace {

// ---- WebGPU state ----
WGPUInstance g_instance = nullptr;
WGPUDevice g_device = nullptr;
WGPUSurface g_surface = nullptr;
WGPUQueue g_queue = nullptr;
WGPUSurfaceConfiguration g_surfaceCfg = {};
int g_width = 1400;
int g_height = 900;

SDL_Window* g_window = nullptr;
bool g_running = true;

// ---- App state ----
struct App {
    Reiter3D sim{97, 97, 41};
    Renderer3D renderer;
    std::vector<VoxelInstance> instances;
    std::vector<std::uint8_t> slicePixels;

    int preset = 0;
    bool paused = false;
    int stepsPerFrame = 2;
    int colorMode = 0;
    bool surfaceOnly = true;
    bool clipZ = false;
    int clipLayer = 20;
    bool autoRotate = true;
    float voxelScale = 1.0f;
    float brightness = 1.0f;
    int sliceLayer = 20;
    bool showSlice = true;
    int gridXY = 97;
    int gridZ = 41;
    int instanceCount = 0;
    bool dirty = true;

    float yaw = 0.6f, pitch = 0.45f, dist = 140.0f;
    float time = 0.0f;
} g_app;

float g_clearColor[3] = {0.015f, 0.02f, 0.045f};

WGPUTextureFormat selectFormat(const WGPUSurfaceCapabilities& caps) {
    const WGPUTextureFormat pref[] = {WGPUTextureFormat_BGRA8Unorm, WGPUTextureFormat_RGBA8Unorm};
    for (auto p : pref)
        for (size_t i = 0; i < caps.formatCount; ++i)
            if (caps.formats[i] == p) return p;
    return caps.formatCount ? caps.formats[0] : WGPUTextureFormat_Undefined;
}

WGPUSurface createSurface(wgpu::Instance& instance, SDL_Window* window) {
#ifdef __EMSCRIPTEN__
    (void)window;
    wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector canvas = {};
    canvas.selector = "#canvas";
    wgpu::SurfaceDescriptor sd = {};
    sd.nextInChain = &canvas;
    return instance.CreateSurface(&sd).MoveToCHandle();
#else
    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    ImGui_ImplWGPU_CreateSurfaceInfo info = {};
    info.Instance = instance.Get();
#if defined(SDL_PLATFORM_MACOS)
    info.System = "cocoa";
    info.RawWindow = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
    return ImGui_ImplWGPU_CreateWGPUSurfaceHelper(&info);
#elif defined(SDL_PLATFORM_WIN32)
    info.System = "win32";
    info.RawWindow = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    info.RawInstance = GetModuleHandle(nullptr);
    return ImGui_ImplWGPU_CreateWGPUSurfaceHelper(&info);
#else
    if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0) {
        info.System = "wayland";
        info.RawDisplay = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
        info.RawSurface = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
    } else {
        info.System = "x11";
        info.RawWindow = (void*)SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
        info.RawDisplay = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
    }
    return ImGui_ImplWGPU_CreateWGPUSurfaceHelper(&info);
#endif
#endif
}

bool initWGPU(SDL_Window* window) {
    wgpu::InstanceDescriptor id = {};
    static constexpr wgpu::InstanceFeatureName timedWaitAny = wgpu::InstanceFeatureName::TimedWaitAny;
    id.requiredFeatureCount = 1;
    id.requiredFeatures = &timedWaitAny;
    wgpu::Instance instance = wgpu::CreateInstance(&id);

    wgpu::Adapter adapter;
    {
        wgpu::RequestAdapterOptions opts;
        auto cb = [&](wgpu::RequestAdapterStatus st, wgpu::Adapter a, wgpu::StringView msg) {
            if (st != wgpu::RequestAdapterStatus::Success) {
                std::printf("Adapter error: %.*s\n", (int)msg.length, msg.data);
                return;
            }
            adapter = std::move(a);
        };
        wgpu::Future f = instance.RequestAdapter(&opts, wgpu::CallbackMode::WaitAnyOnly, cb);
        instance.WaitAny(f, UINT64_MAX);
    }
    if (!adapter) return false;
    ImGui_ImplWGPU_DebugPrintAdapterInfo(adapter.Get());

    wgpu::Device device;
    {
        wgpu::DeviceDescriptor dd;
        dd.SetDeviceLostCallback(wgpu::CallbackMode::AllowSpontaneous,
            [](const wgpu::Device&, wgpu::DeviceLostReason r, wgpu::StringView m) {
                std::fprintf(stderr, "Device lost (%s): %.*s\n",
                             ImGui_ImplWGPU_GetDeviceLostReasonName((WGPUDeviceLostReason)r), (int)m.length, m.data);
            });
        dd.SetUncapturedErrorCallback(
            [](const wgpu::Device&, wgpu::ErrorType t, wgpu::StringView m) {
                std::fprintf(stderr, "WebGPU %s error: %.*s\n",
                             ImGui_ImplWGPU_GetErrorTypeName((WGPUErrorType)t), (int)m.length, m.data);
            });
        auto cb = [&](wgpu::RequestDeviceStatus st, wgpu::Device d, wgpu::StringView msg) {
            if (st != wgpu::RequestDeviceStatus::Success) {
                std::printf("Device error: %.*s\n", (int)msg.length, msg.data);
                return;
            }
            device = std::move(d);
        };
        wgpu::Future f = adapter.RequestDevice(&dd, wgpu::CallbackMode::WaitAnyOnly, cb);
        instance.WaitAny(f, UINT64_MAX);
    }
    if (!device) return false;
    g_device = device.MoveToCHandle();

    g_surface = createSurface(instance, window);
    if (!g_surface) return false;

    WGPUSurfaceCapabilities caps = {};
    wgpuSurfaceGetCapabilities(g_surface, adapter.Get(), &caps);
    g_surfaceCfg.format = selectFormat(caps);
    g_surfaceCfg.presentMode = WGPUPresentMode_Fifo;
    g_surfaceCfg.alphaMode = WGPUCompositeAlphaMode_Auto;
    g_surfaceCfg.usage = WGPUTextureUsage_RenderAttachment;
    g_surfaceCfg.width = g_width;
    g_surfaceCfg.height = g_height;
    g_surfaceCfg.device = g_device;
    wgpuSurfaceConfigure(g_surface, &g_surfaceCfg);
    g_queue = wgpuDeviceGetQueue(g_device);

    g_instance = instance.MoveToCHandle();
    return true;
}

void resizeSurface(int w, int h) {
    g_surfaceCfg.width = g_width = w;
    g_surfaceCfg.height = g_height = h;
    wgpuSurfaceConfigure(g_surface, &g_surfaceCfg);
    g_app.renderer.resize(w, h);
}

void rebuildInstances() {
    App& a = g_app;
    a.sim.buildInstances(a.instances, a.surfaceOnly, a.clipZ, a.clipLayer);
    a.renderer.updateInstances(a.instances);
    a.instanceCount = static_cast<int>(a.instances.size());
    a.dirty = false;
}

void drawUI() {
    App& a = g_app;
    Reiter3D& s = a.sim;

    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("Reiter 3D Snowflake");

    ImGui::Text("Step: %llu   Ice cells: %d   Radius: %d",
                (unsigned long long)s.getStepCount(), s.getIceCount(), s.getMaxRadius());
    ImGui::Text("Rendered voxels: %d   FPS: %.0f", a.instanceCount, ImGui::GetIO().Framerate);
    ImGui::Separator();

    const auto& presets = Reiter3D::getPresets();
    if (ImGui::BeginCombo("Preset", presets[a.preset].name.c_str())) {
        for (int i = 0; i < (int)presets.size(); ++i) {
            if (ImGui::Selectable(presets[i].name.c_str(), i == a.preset)) {
                a.preset = i;
                s.applyPreset(i);
                s.reset();
                a.dirty = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::TextWrapped("%s", presets[a.preset].description.c_str());

    if (ImGui::Button(a.paused ? "Resume" : "Pause")) a.paused = !a.paused;
    ImGui::SameLine();
    if (ImGui::Button("Reset")) { s.reset(); a.dirty = true; }
    ImGui::SameLine();
    if (ImGui::Button("Step")) { s.step(); a.dirty = true; }
    ImGui::SliderInt("Steps / frame", &a.stepsPerFrame, 1, 20);

    if (ImGui::CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("alpha XY (basal diffusion)", &s.alphaXY, 0.1f, 1.0f);
        ImGui::SliderFloat("alpha Z (c-axis diffusion)", &s.alphaZ, 0.0f, 0.6f);
        ImGui::SliderFloat("beta (vapor)", &s.beta, 0.10f, 0.90f);
        ImGui::SliderFloat("gamma (condensation)", &s.gamma, 0.00005f, 0.02f, "%.5f", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("gamma Z ratio", &s.gammaZRatio, 0.02f, 3.0f);
        ImGui::SliderFloat("noise eta", &s.noiseAmp, 0.0f, 0.25f);
        ImGui::Checkbox("Symmetric (D6h) noise", &s.symmetricNoise);
        ImGui::Checkbox("Dynamic flight (humidity cycle)", &s.dynamicFlight);
    }

    if (ImGui::CollapsingHeader("Grid (applied on Apply)")) {
        ImGui::SliderInt("XY size", &a.gridXY, 41, 161);
        ImGui::SliderInt("Z layers", &a.gridZ, 11, 81);
        if (ImGui::Button("Apply & reset")) {
            s.resize(a.gridXY, a.gridXY, a.gridZ);
            a.clipLayer = std::clamp(a.clipLayer, 1, a.gridZ - 2);
            a.sliceLayer = a.gridZ / 2;
            a.dirty = true;
        }
    }

    if (ImGui::CollapsingHeader("View", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* modes[] = {"Ice (radial)", "Heat (radial)", "Growth age", "Height"};
        ImGui::Combo("Color", &a.colorMode, modes, 4);
        if (ImGui::Checkbox("Surface only (culling)", &a.surfaceOnly)) a.dirty = true;
        if (ImGui::Checkbox("Clip above Z layer", &a.clipZ)) a.dirty = true;
        if (a.clipZ && ImGui::SliderInt("Clip layer", &a.clipLayer, 1, s.getDepth() - 2)) a.dirty = true;
        ImGui::SliderFloat("Voxel scale", &a.voxelScale, 0.5f, 1.05f);
        ImGui::SliderFloat("Brightness", &a.brightness, 0.4f, 1.6f);
        ImGui::Checkbox("Auto-rotate", &a.autoRotate);
        ImGui::TextDisabled("Drag: orbit | Wheel: zoom");
    }

    if (ImGui::CollapsingHeader("Cross-section", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Show", &a.showSlice);
        a.sliceLayer = std::clamp(a.sliceLayer, 0, s.getDepth() - 1);
        ImGui::SliderInt("Z layer", &a.sliceLayer, 0, s.getDepth() - 1);
        if (a.showSlice) {
            s.renderCrossSectionRGBA(a.slicePixels, a.sliceLayer, a.colorMode == 0 ? 0 : 1);
            a.renderer.updateCrossSectionTexture(a.slicePixels, s.getWidth(), s.getHeight());
            if (a.renderer.getCrossSectionTextureView()) {
                float w = ImGui::GetContentRegionAvail().x;
                ImGui::Image(ImTextureRef((ImTextureID)(intptr_t)a.renderer.getCrossSectionTextureView()), ImVec2(w, w));
            }
        }
    }
    ImGui::End();
}

void frame() {
    App& a = g_app;

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        ImGui_ImplSDL3_ProcessEvent(&e);
        if (e.type == SDL_EVENT_QUIT) g_running = false;
        if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && e.window.windowID == SDL_GetWindowID(g_window))
            g_running = false;
    }
    if (SDL_GetWindowFlags(g_window) & SDL_WINDOW_MINIMIZED) {
        SDL_Delay(10);
        return;
    }

    int w, h;
    SDL_GetWindowSizeInPixels(g_window, &w, &h);
    if (w < 1 || h < 1) return;
    if (w != g_width || h != g_height) resizeSurface(w, h);

    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantCaptureMouse) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
            a.yaw -= io.MouseDelta.x * 0.008f;
            a.pitch = std::clamp(a.pitch + io.MouseDelta.y * 0.008f, -1.5f, 1.5f);
        }
        if (io.MouseWheel != 0.0f) a.dist = std::clamp(a.dist * std::pow(0.9f, io.MouseWheel), 8.0f, 800.0f);
    }
    if (a.autoRotate && !(ImGui::IsMouseDown(ImGuiMouseButton_Left) && !io.WantCaptureMouse)) a.yaw += 0.003f;

    if (!a.paused) {
        for (int i = 0; i < a.stepsPerFrame; ++i) a.sim.step();
        a.dirty = true;
    }
    if (a.dirty) rebuildInstances();

    ImGui_ImplWGPU_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    drawUI();
    ImGui::Render();

    WGPUSurfaceTexture st;
    wgpuSurfaceGetCurrentTexture(g_surface, &st);
    if (ImGui_ImplWGPU_IsSurfaceStatusError(st.status)) {
        std::fprintf(stderr, "Surface texture error %#x\n", st.status);
        g_running = false;
        return;
    }
    if (ImGui_ImplWGPU_IsSurfaceStatusSubOptimal(st.status)) {
        if (st.texture) wgpuTextureRelease(st.texture);
        resizeSurface(g_width, g_height);
        return;
    }

    WGPUTextureViewDescriptor vd = {};
    vd.format = g_surfaceCfg.format;
    vd.dimension = WGPUTextureViewDimension_2D;
    vd.mipLevelCount = WGPU_MIP_LEVEL_COUNT_UNDEFINED;
    vd.arrayLayerCount = WGPU_ARRAY_LAYER_COUNT_UNDEFINED;
    vd.aspect = WGPUTextureAspect_All;
    WGPUTextureView view = wgpuTextureCreateView(st.texture, &vd);

    WGPURenderPassColorAttachment ca = {};
    ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    ca.view = view;
    ca.loadOp = WGPULoadOp_Clear;
    ca.storeOp = WGPUStoreOp_Store;
    ca.clearValue = {g_clearColor[0], g_clearColor[1], g_clearColor[2], 1.0};

    WGPURenderPassDepthStencilAttachment da = {};
    da.view = a.renderer.getDepthTextureView();
    da.depthLoadOp = WGPULoadOp_Clear;
    da.depthStoreOp = WGPUStoreOp_Store;
    da.depthClearValue = 1.0f;

    WGPURenderPassDescriptor rp = {};
    rp.colorAttachmentCount = 1;
    rp.colorAttachments = &ca;
    rp.depthStencilAttachment = &da;

    WGPUCommandEncoderDescriptor ed = {};
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g_device, &ed);
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);

    // 3D snowflake
    CameraUniforms cu = {};
    const float target[3] = {0, 0, 0};
    Renderer3D::computeOrbitViewProj(a.yaw, a.pitch, a.dist, target, (float)g_width / (float)g_height,
                                     cu.viewProj, cu.cameraPos);
    cu.lightDir[0] = 0.5f; cu.lightDir[1] = 0.8f; cu.lightDir[2] = 0.4f; cu.lightDir[3] = 0.0f;
    cu.params[0] = a.voxelScale;
    cu.params[1] = (float)a.colorMode;
    cu.params[2] = a.brightness;
    cu.params[3] = a.time += 0.016f;
    a.renderer.render(pass, cu, a.instanceCount);

    // UI on top
    ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), pass);
    wgpuRenderPassEncoderEnd(pass);

    WGPUCommandBufferDescriptor cd = {};
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, &cd);
    wgpuQueueSubmit(g_queue, 1, &cmd);

#ifndef __EMSCRIPTEN__
    wgpuSurfacePresent(g_surface);
    wgpuInstanceProcessEvents(g_instance);
#endif
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(st.texture);
    wgpuRenderPassEncoderRelease(pass);
    wgpuCommandEncoderRelease(enc);
    wgpuCommandBufferRelease(cmd);
}

} // namespace

int main(int, char**) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (scale <= 0.0f) scale = 1.0f;
    g_width = (int)(g_width * scale);
    g_height = (int)(g_height * scale);
    g_window = SDL_CreateWindow("Snowflake 3D - Reiter CA (Dawn + ImGui)", g_width, g_height,
                                SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!g_window) {
        std::printf("SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GetWindowSizeInPixels(g_window, &g_width, &g_height);

    if (!initWGPU(g_window)) {
        std::printf("WebGPU initialisation failed\n");
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
#ifdef __EMSCRIPTEN__
    io.IniFilename = nullptr;
#endif
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.FrameRounding = 3.0f;
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;

    ImGui_ImplSDL3_InitForOther(g_window);
    ImGui_ImplWGPU_InitInfo ii;
    ii.Device = g_device;
    ii.NumFramesInFlight = 3;
    ii.RenderTargetFormat = g_surfaceCfg.format;
    ii.DepthStencilFormat = g_app.renderer.getDepthFormat();
    ImGui_ImplWGPU_Init(&ii);

    if (!g_app.renderer.init(g_device, g_queue, g_surfaceCfg.format, g_width, g_height)) {
        std::printf("Renderer init failed\n");
        return 1;
    }
    g_app.sim.applyPreset(0);
    g_app.sim.reset();
    g_app.sliceLayer = g_app.sim.getDepth() / 2;
    g_app.clipLayer = g_app.sim.getDepth() / 2;

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(frame, 0, true);
#else
    while (g_running) frame();
#endif

    g_app.renderer.shutdown();
    ImGui_ImplWGPU_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    wgpuSurfaceUnconfigure(g_surface);
    wgpuSurfaceRelease(g_surface);
    wgpuQueueRelease(g_queue);
    wgpuDeviceRelease(g_device);
    wgpuInstanceRelease(g_instance);
    SDL_DestroyWindow(g_window);
    SDL_Quit();
    return 0;
}

