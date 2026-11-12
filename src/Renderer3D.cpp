/*
 * Copyright (c) 2026 Imre Városi
 * Licensed under the MIT License with the Beer-Ware Clause (Revision 42).
 * If we meet some day, and you think this stuff is worth it, you can buy me a beer in return.
 * See LICENSE file in the project root for full license text.
 */

#include "Renderer3D.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

const char* kShaderWGSL = R"(
struct Uniforms {
    viewProj : mat4x4<f32>,
    camPos   : vec4<f32>,
    lightDir : vec4<f32>,
    params   : vec4<f32>, // x: voxelScale, y: colorMode, z: brightness, w: time
};
@group(0) @binding(0) var<uniform> u : Uniforms;

struct VIn {
    @location(0) pos  : vec3<f32>,
    @location(1) nor  : vec3<f32>,
    @location(2) ipos : vec3<f32>,
    @location(3) attr : vec4<f32>, // mass, distRatio, zNorm, birth
};
struct VOut {
    @builtin(position) clip : vec4<f32>,
    @location(0) nor  : vec3<f32>,
    @location(1) wpos : vec3<f32>,
    @location(2) attr : vec4<f32>,
};

@vertex
fn vs_main(in : VIn) -> VOut {
    var o : VOut;
    let w = in.ipos + in.pos * u.params.x;
    o.clip = u.viewProj * vec4<f32>(w, 1.0);
    o.nor = in.nor;
    o.wpos = w;
    o.attr = in.attr;
    return o;
}

fn palette(t : f32) -> vec3<f32> {
    let a = vec3<f32>(0.10, 0.25, 0.55);
    let b = vec3<f32>(0.35, 0.80, 1.00);
    let c = vec3<f32>(0.95, 0.98, 1.00);
    if (t < 0.5) { return mix(a, b, t * 2.0); }
    return mix(b, c, (t - 0.5) * 2.0);
}

fn heat(t : f32) -> vec3<f32> {
    let x = clamp(t, 0.0, 1.0);
    return vec3<f32>(clamp(1.5 - abs(4.0 * x - 3.0), 0.0, 1.0),
                     clamp(1.5 - abs(4.0 * x - 2.0), 0.0, 1.0),
                     clamp(1.5 - abs(4.0 * x - 1.0), 0.0, 1.0));
}

@fragment
fn fs_main(in : VOut) -> @location(0) vec4<f32> {
    let n = normalize(in.nor);
    let l = normalize(u.lightDir.xyz);
    let v = normalize(u.camPos.xyz - in.wpos);
    let mode = i32(u.params.y + 0.5);

    var base : vec3<f32>;
    if (mode == 0) {
        base = palette(1.0 - in.attr.y * 0.9);
    } else if (mode == 1) {
        base = heat(1.0 - in.attr.y);
    } else if (mode == 2) {
        base = heat(in.attr.w);
    } else {
        base = heat(0.5 + 0.5 * in.attr.z);
    }

    let diff = max(dot(n, l), 0.0);
    let hemi = 0.35 + 0.25 * n.y;
    let fres = pow(1.0 - max(dot(n, v), 0.0), 3.0);
    let spec = pow(max(dot(reflect(-l, n), v), 0.0), 40.0);
    var col = base * (hemi + 0.75 * diff) + vec3<f32>(0.55, 0.8, 1.0) * fres * 0.35 + vec3<f32>(spec) * 0.6;
    col = col * u.params.z;
    return vec4<f32>(clamp(col, vec3<f32>(0.0), vec3<f32>(1.0)), 1.0);
}
)";

WGPUStringView sv(const char* s) { return WGPUStringView{s, WGPU_STRLEN}; }

void normalize3(float v[3]) {
    float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-8f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}
void cross3(const float a[3], const float b[3], float o[3]) {
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}
float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

} // namespace

Renderer3D::~Renderer3D() { shutdown(); }

void Renderer3D::computeOrbitViewProj(float yaw, float pitch, float distance, const float target[3],
                                      float aspect, float outViewProj[16], float outCamPos[4]) {
    const float cp = std::cos(pitch), sp = std::sin(pitch);
    const float eye[3] = {
        target[0] + distance * cp * std::sin(yaw),
        target[1] + distance * sp,
        target[2] + distance * cp * std::cos(yaw)
    };
    float f[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
    normalize3(f);
    const float up[3] = {0.0f, 1.0f, 0.0f};
    float s[3];
    cross3(f, up, s);
    normalize3(s);
    float uu[3];
    cross3(s, f, uu);

    float V[16] = {};
    V[0] = s[0];  V[4] = s[1];  V[8] = s[2];
    V[1] = uu[0]; V[5] = uu[1]; V[9] = uu[2];
    V[2] = -f[0]; V[6] = -f[1]; V[10] = -f[2];
    V[12] = -dot3(s, eye);
    V[13] = -dot3(uu, eye);
    V[14] = dot3(f, eye);
    V[15] = 1.0f;

    const float fov = 0.7853982f; // 45 deg
    const float nearP = 0.5f, farP = 2000.0f;
    const float t = 1.0f / std::tan(fov * 0.5f);
    float P[16] = {};
    P[0] = t / std::max(aspect, 0.01f);
    P[5] = t;
    P[10] = farP / (nearP - farP);
    P[11] = -1.0f;
    P[14] = nearP * farP / (nearP - farP);

    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += P[k * 4 + r] * V[c * 4 + k];
            outViewProj[c * 4 + r] = sum;
        }
    }
    outCamPos[0] = eye[0]; outCamPos[1] = eye[1]; outCamPos[2] = eye[2]; outCamPos[3] = 1.0f;
}

void Renderer3D::createHexPrismGeometry() {
    // Hex prism: circumradius = 1/sqrt(3) so adjacent cells (distance 1) share flat faces.
    // Flat faces face angles 0,60,...; vertices at 30,90,...
    const float R = 0.57735027f;
    const float H = 0.41f; // half height (zSpacing 0.82)
    std::vector<float> vtx; // pos(3) + normal(3)
    std::vector<std::uint16_t> idx;

    auto push = [&](float x, float y, float z, float nx, float ny, float nz) {
        vtx.insert(vtx.end(), {x, y, z, nx, ny, nz});
        return static_cast<std::uint16_t>(vtx.size() / 6 - 1);
    };

    const float pi = 3.14159265f;
    for (int i = 0; i < 6; ++i) {
        float a0 = pi / 6.0f + i * pi / 3.0f;
        float a1 = a0 + pi / 3.0f;
        float am = (a0 + a1) * 0.5f;
        float nx = std::cos(am), nz = std::sin(am);
        auto v0 = push(R * std::cos(a0), -H, R * std::sin(a0), nx, 0, nz);
        auto v1 = push(R * std::cos(a1), -H, R * std::sin(a1), nx, 0, nz);
        auto v2 = push(R * std::cos(a1), H, R * std::sin(a1), nx, 0, nz);
        auto v3 = push(R * std::cos(a0), H, R * std::sin(a0), nx, 0, nz);
        // CCW seen from outside
        idx.insert(idx.end(), {v0, v1, v2, v0, v2, v3});
    }
    // Caps
    for (int cap = 0; cap < 2; ++cap) {
        float y = cap == 0 ? H : -H;
        float ny = cap == 0 ? 1.0f : -1.0f;
        auto c = push(0, y, 0, 0, ny, 0);
        std::uint16_t ring[6];
        for (int i = 0; i < 6; ++i) {
            float a = pi / 6.0f + i * pi / 3.0f;
            ring[i] = push(R * std::cos(a), y, R * std::sin(a), 0, ny, 0);
        }
        for (int i = 0; i < 6; ++i) {
            auto a = ring[i], b = ring[(i + 1) % 6];
            if (cap == 0) idx.insert(idx.end(), {c, b, a});
            else          idx.insert(idx.end(), {c, a, b});
        }
    }
    m_indexCount = static_cast<std::uint32_t>(idx.size());

    WGPUBufferDescriptor vb = {};
    vb.label = sv("hex vertices");
    vb.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    vb.size = vtx.size() * sizeof(float);
    m_vertexBuffer = wgpuDeviceCreateBuffer(m_device, &vb);
    wgpuQueueWriteBuffer(m_queue, m_vertexBuffer, 0, vtx.data(), vb.size);

    // Index buffer size must be multiple of 4
    std::size_t ibytes = (idx.size() * sizeof(std::uint16_t) + 3) & ~std::size_t(3);
    idx.resize(ibytes / sizeof(std::uint16_t), 0);
    WGPUBufferDescriptor ib = {};
    ib.label = sv("hex indices");
    ib.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
    ib.size = ibytes;
    m_indexBuffer = wgpuDeviceCreateBuffer(m_device, &ib);
    wgpuQueueWriteBuffer(m_queue, m_indexBuffer, 0, idx.data(), ibytes);
}

void Renderer3D::createDepthBuffer(int width, int height) {
    if (m_depthTextureView) { wgpuTextureViewRelease(m_depthTextureView); m_depthTextureView = nullptr; }
    if (m_depthTexture) { wgpuTextureDestroy(m_depthTexture); wgpuTextureRelease(m_depthTexture); m_depthTexture = nullptr; }

    WGPUTextureDescriptor td = {};
    td.label = sv("depth");
    td.dimension = WGPUTextureDimension_2D;
    td.size = {static_cast<std::uint32_t>(std::max(1, width)), static_cast<std::uint32_t>(std::max(1, height)), 1};
    td.format = m_depthFormat;
    td.mipLevelCount = 1;
    td.sampleCount = 1;
    td.usage = WGPUTextureUsage_RenderAttachment;
    m_depthTexture = wgpuDeviceCreateTexture(m_device, &td);

    WGPUTextureViewDescriptor vd = {};
    vd.format = m_depthFormat;
    vd.dimension = WGPUTextureViewDimension_2D;
    vd.mipLevelCount = 1;
    vd.arrayLayerCount = 1;
    vd.aspect = WGPUTextureAspect_All;
    m_depthTextureView = wgpuTextureCreateView(m_depthTexture, &vd);
}

bool Renderer3D::init(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat colorFormat, int width, int height) {
    m_device = device;
    m_queue = queue;
    m_colorFormat = colorFormat;

    createDepthBuffer(width, height);
    createHexPrismGeometry();

    WGPUBufferDescriptor ub = {};
    ub.label = sv("camera uniforms");
    ub.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    ub.size = sizeof(CameraUniforms);
    m_uniformBuffer = wgpuDeviceCreateBuffer(m_device, &ub);

    WGPUBindGroupLayoutEntry ble = {};
    ble.binding = 0;
    ble.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    ble.buffer.type = WGPUBufferBindingType_Uniform;
    ble.buffer.minBindingSize = sizeof(CameraUniforms);
    WGPUBindGroupLayoutDescriptor bld = {};
    bld.entryCount = 1;
    bld.entries = &ble;
    m_bindGroupLayout = wgpuDeviceCreateBindGroupLayout(m_device, &bld);

    WGPUPipelineLayoutDescriptor pld = {};
    pld.bindGroupLayoutCount = 1;
    pld.bindGroupLayouts = &m_bindGroupLayout;
    m_pipelineLayout = wgpuDeviceCreatePipelineLayout(m_device, &pld);

    WGPUBindGroupEntry bge = {};
    bge.binding = 0;
    bge.buffer = m_uniformBuffer;
    bge.size = sizeof(CameraUniforms);
    WGPUBindGroupDescriptor bgd = {};
    bgd.layout = m_bindGroupLayout;
    bgd.entryCount = 1;
    bgd.entries = &bge;
    m_bindGroup = wgpuDeviceCreateBindGroup(m_device, &bgd);

    WGPUShaderSourceWGSL wgsl = {};
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl.code = sv(kShaderWGSL);
    WGPUShaderModuleDescriptor smd = {};
    smd.nextInChain = &wgsl.chain;
    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(m_device, &smd);

    WGPUVertexAttribute geomAttrs[2] = {};
    geomAttrs[0] = {nullptr, WGPUVertexFormat_Float32x3, 0, 0};
    geomAttrs[1] = {nullptr, WGPUVertexFormat_Float32x3, 12, 1};
    WGPUVertexAttribute instAttrs[2] = {};
    instAttrs[0] = {nullptr, WGPUVertexFormat_Float32x3, 0, 2};
    instAttrs[1] = {nullptr, WGPUVertexFormat_Float32x4, 12, 3};

    WGPUVertexBufferLayout vbl[2] = {};
    vbl[0].stepMode = WGPUVertexStepMode_Vertex;
    vbl[0].arrayStride = 24;
    vbl[0].attributeCount = 2;
    vbl[0].attributes = geomAttrs;
    vbl[1].stepMode = WGPUVertexStepMode_Instance;
    vbl[1].arrayStride = sizeof(VoxelInstance);
    vbl[1].attributeCount = 2;
    vbl[1].attributes = instAttrs;

    WGPUColorTargetState cts = {};
    cts.format = m_colorFormat;
    cts.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fs = {};
    fs.module = shader;
    fs.entryPoint = sv("fs_main");
    fs.targetCount = 1;
    fs.targets = &cts;

    WGPUDepthStencilState dss = {};
    dss.format = m_depthFormat;
    dss.depthWriteEnabled = WGPUOptionalBool_True;
    dss.depthCompare = WGPUCompareFunction_Less;
    dss.stencilFront.compare = WGPUCompareFunction_Always;
    dss.stencilBack.compare = WGPUCompareFunction_Always;

    WGPURenderPipelineDescriptor rpd = {};
    rpd.label = sv("snowflake pipeline");
    rpd.layout = m_pipelineLayout;
    rpd.vertex.module = shader;
    rpd.vertex.entryPoint = sv("vs_main");
    rpd.vertex.bufferCount = 2;
    rpd.vertex.buffers = vbl;
    rpd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    rpd.primitive.frontFace = WGPUFrontFace_CCW;
    rpd.primitive.cullMode = WGPUCullMode_Back;
    rpd.depthStencil = &dss;
    rpd.multisample.count = 1;
    rpd.multisample.mask = 0xFFFFFFFFu;
    rpd.fragment = &fs;
    m_pipeline = wgpuDeviceCreateRenderPipeline(m_device, &rpd);

    wgpuShaderModuleRelease(shader);
    return m_pipeline != nullptr;
}

void Renderer3D::resize(int width, int height) { createDepthBuffer(width, height); }

void Renderer3D::updateInstances(const std::vector<VoxelInstance>& instances) {
    if (instances.empty()) return;
    if (instances.size() > m_instanceCapacity) {
        if (m_instanceBuffer) { wgpuBufferDestroy(m_instanceBuffer); wgpuBufferRelease(m_instanceBuffer); }
        m_instanceCapacity = std::max<std::size_t>(instances.size() * 3 / 2, 4096);
        WGPUBufferDescriptor d = {};
        d.label = sv("instances");
        d.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        d.size = m_instanceCapacity * sizeof(VoxelInstance);
        m_instanceBuffer = wgpuDeviceCreateBuffer(m_device, &d);
    }
    wgpuQueueWriteBuffer(m_queue, m_instanceBuffer, 0, instances.data(), instances.size() * sizeof(VoxelInstance));
}

void Renderer3D::updateCrossSectionTexture(const std::vector<std::uint8_t>& rgba, int width, int height) {
    if (!m_sliceTexture || width != m_sliceTexWidth || height != m_sliceTexHeight) {
        if (m_sliceTextureView) { wgpuTextureViewRelease(m_sliceTextureView); m_sliceTextureView = nullptr; }
        if (m_sliceTexture) { wgpuTextureDestroy(m_sliceTexture); wgpuTextureRelease(m_sliceTexture); }
        WGPUTextureDescriptor td = {};
        td.label = sv("cross section");
        td.dimension = WGPUTextureDimension_2D;
        td.size = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
        td.format = WGPUTextureFormat_RGBA8Unorm;
        td.mipLevelCount = 1;
        td.sampleCount = 1;
        td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        m_sliceTexture = wgpuDeviceCreateTexture(m_device, &td);
        WGPUTextureViewDescriptor vd = {};
        vd.format = WGPUTextureFormat_RGBA8Unorm;
        vd.dimension = WGPUTextureViewDimension_2D;
        vd.mipLevelCount = 1;
        vd.arrayLayerCount = 1;
        vd.aspect = WGPUTextureAspect_All;
        m_sliceTextureView = wgpuTextureCreateView(m_sliceTexture, &vd);
        m_sliceTexWidth = width;
        m_sliceTexHeight = height;
    }
    WGPUTexelCopyTextureInfo dst = {};
    dst.texture = m_sliceTexture;
    dst.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferLayout layout = {};
    layout.bytesPerRow = static_cast<std::uint32_t>(width * 4);
    layout.rowsPerImage = static_cast<std::uint32_t>(height);
    WGPUExtent3D ext = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
    wgpuQueueWriteTexture(m_queue, &dst, rgba.data(), rgba.size(), &layout, &ext);
}

void Renderer3D::render(WGPURenderPassEncoder pass, const CameraUniforms& uniforms, int instanceCount) {
    if (instanceCount <= 0 || !m_instanceBuffer) return;
    wgpuQueueWriteBuffer(m_queue, m_uniformBuffer, 0, &uniforms, sizeof(CameraUniforms));
    wgpuRenderPassEncoderSetPipeline(pass, m_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, m_bindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m_vertexBuffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 1, m_instanceBuffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(pass, m_indexBuffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexed(pass, m_indexCount, static_cast<std::uint32_t>(instanceCount), 0, 0, 0);
}

void Renderer3D::shutdown() {
    auto rel = [](auto& h, auto fn) { if (h) { fn(h); h = nullptr; } };
    rel(m_sliceTextureView, wgpuTextureViewRelease);
    rel(m_sliceTexture, wgpuTextureRelease);
    rel(m_depthTextureView, wgpuTextureViewRelease);
    rel(m_depthTexture, wgpuTextureRelease);
    rel(m_instanceBuffer, wgpuBufferRelease);
    rel(m_indexBuffer, wgpuBufferRelease);
    rel(m_vertexBuffer, wgpuBufferRelease);
    rel(m_uniformBuffer, wgpuBufferRelease);
    rel(m_bindGroup, wgpuBindGroupRelease);
    rel(m_pipelineLayout, wgpuPipelineLayoutRelease);
    rel(m_bindGroupLayout, wgpuBindGroupLayoutRelease);
    rel(m_pipeline, wgpuRenderPipelineRelease);
}

