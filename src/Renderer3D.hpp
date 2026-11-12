/*
 * Copyright (c) 2026 Imre Városi
 * Licensed under the MIT License with the Beer-Ware Clause (Revision 42).
 * If we meet some day, and you think this stuff is worth it, you can buy me a beer in return.
 * See LICENSE file in the project root for full license text.
 */

#pragma once

#include "Reiter3D.hpp"
#include <webgpu/webgpu.h>
#include <cstdint>
#include <vector>

struct CameraUniforms {
    float viewProj[16];
    float cameraPos[4];
    float lightDir[4];
    float params[4]; // x: voxelScale, y: colorMode, z: opacity, w: time
};

class Renderer3D {
public:
    Renderer3D() = default;
    ~Renderer3D();

    bool init(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat colorFormat, int width, int height);
    void shutdown();

    void resize(int width, int height);
    void updateInstances(const std::vector<VoxelInstance>& instances);
    void updateCrossSectionTexture(const std::vector<std::uint8_t>& rgbaPixels, int width, int height);

    void render(
        WGPURenderPassEncoder passEncoder,
        const CameraUniforms& uniforms,
        int instanceCount
    );

    WGPUTextureView getDepthTextureView() const { return m_depthTextureView; }
    WGPUTextureFormat getDepthFormat() const { return m_depthFormat; }
    WGPUTextureView getCrossSectionTextureView() const { return m_sliceTextureView; }

    // Helper math for 3D orbit camera
    static void computeOrbitViewProj(
        float yaw,
        float pitch,
        float distance,
        const float target[3],
        float aspect,
        float outViewProj[16],
        float outCamPos[4]
    );

private:
    void createDepthBuffer(int width, int height);
    void createHexPrismGeometry();

    WGPUDevice m_device = nullptr;
    WGPUQueue m_queue = nullptr;
    WGPUTextureFormat m_colorFormat = WGPUTextureFormat_Undefined;
    WGPUTextureFormat m_depthFormat = WGPUTextureFormat_Depth24Plus;

    WGPURenderPipeline m_pipeline = nullptr;
    WGPUPipelineLayout m_pipelineLayout = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;

    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUBuffer m_vertexBuffer = nullptr;
    WGPUBuffer m_indexBuffer = nullptr;
    WGPUBuffer m_instanceBuffer = nullptr;

    std::uint32_t m_indexCount = 0;
    std::size_t m_instanceCapacity = 0;

    WGPUTexture m_depthTexture = nullptr;
    WGPUTextureView m_depthTextureView = nullptr;

    WGPUTexture m_sliceTexture = nullptr;
    WGPUTextureView m_sliceTextureView = nullptr;
    int m_sliceTexWidth = 0;
    int m_sliceTexHeight = 0;
};

