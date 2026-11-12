/*
 * Copyright (c) 2026 Imre Városi
 * Licensed under the MIT License with the Beer-Ware Clause (Revision 42).
 * If we meet some day, and you think this stuff is worth it, you can buy me a beer in return.
 * See LICENSE file in the project root for full license text.
 */

#include "Reiter3D.hpp"
#include <algorithm>
#include <cmath>

namespace {
// 6 basal neighbors in axial (q, r) coordinates
constexpr int dQ[6] = {+1, -1,  0,  0, +1, -1};
constexpr int dR[6] = { 0,  0, +1, -1, -1, +1};

struct ColorRGB {
    std::uint8_t r, g, b;
};

ColorRGB lerpRGB(ColorRGB a, ColorRGB b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return {
        static_cast<std::uint8_t>(a.r + (b.r - a.r) * t),
        static_cast<std::uint8_t>(a.g + (b.g - a.g) * t),
        static_cast<std::uint8_t>(a.b + (b.b - a.b) * t)
    };
}
} // namespace

const std::vector<Preset3D>& Reiter3D::getPresets() {
    static const std::vector<Preset3D> presets = {
        {
            "1: Planar Stellar Dendrite",
            "Thin 6-fold branched star crystal (-15 C, high humidity)",
            1.00f, 0.15f, 0.44f, 0.0006f, 0.08f, 0.015f, 0
        },
        {
            "2: Double-Deck Capped Column",
            "Tsuzumi-type crystal: hexagonal column capped by two dendritic plates",
            0.95f, 0.35f, 0.46f, 0.0012f, 0.45f, 0.012f, 4
        },
        {
            "3: Hollow Hexagonal Prism Column",
            "Elongated c-axis hexagonal prism with terraced facets (-6 C)",
            0.80f, 0.50f, 0.38f, 0.0025f, 1.85f, 0.006f, 5
        },
        {
            "4: 3D Fern-Like Spatial Dendrite",
            "Multi-layered feathery dendritic branches with rich side-branching",
            1.00f, 0.25f, 0.62f, 0.0003f, 0.22f, 0.025f, 1
        },
        {
            "5: Sectored Ridged Plate",
            "Thick faceted hexagonal plate with internal structural ridges",
            0.85f, 0.20f, 0.36f, 0.0040f, 0.18f, 0.005f, 1
        },
        {
            "6: Ice Needle / Whisker Cluster",
            "Slender vertical ice spikes growing rapidly along the c-axis",
            0.75f, 0.55f, 0.52f, 0.0008f, 2.40f, 0.018f, 6
        }
    };
    return presets;
}

Reiter3D::Reiter3D(int width, int height, int depth)
    : m_width(width),
      m_height(height),
      m_depth(depth),
      m_rng(std::random_device{}()) {
    resize(width, height, depth);
}

void Reiter3D::resize(int width, int height, int depth) {
    m_width = std::max(16, width);
    m_height = std::max(16, height);
    m_depth = std::max(5, depth);
    const std::size_t total = static_cast<std::size_t>(m_width) * m_height * m_depth;
    m_s.assign(total, beta);
    m_u.assign(total, beta);
    m_receptive.assign(total, 0);
    m_birthStep.assign(total, 0.0f);
    reset();
}

void Reiter3D::applyPreset(int index) {
    const auto& presets = getPresets();
    if (index >= 0 && index < static_cast<int>(presets.size())) {
        alphaXY = presets[index].alphaXY;
        alphaZ = presets[index].alphaZ;
        beta = presets[index].beta;
        gamma = presets[index].gamma;
        gammaZRatio = presets[index].gammaZRatio;
        noiseAmp = presets[index].noiseAmp;
        seedHeight = presets[index].seedHeight;
    }
}

bool Reiter3D::isBoundary(int q, int r, int z) const {
    return q <= 1 || q >= m_width - 2 ||
           r <= 1 || r >= m_height - 2 ||
           z <= 0 || z >= m_depth - 1;
}

void Reiter3D::reset() {
    std::fill(m_s.begin(), m_s.end(), beta);
    std::fill(m_u.begin(), m_u.end(), beta);
    std::fill(m_receptive.begin(), m_receptive.end(), 0);
    std::fill(m_birthStep.begin(), m_birthStep.end(), 0.0f);

    const int cq = m_width / 2;
    const int cr = m_height / 2;
    const int cz = m_depth / 2;

    int clampedSeedH = std::clamp(seedHeight, 0, std::max(0, (m_depth / 2) - 2));
    int count = 0;

    for (int dz = -clampedSeedH; dz <= clampedSeedH; ++dz) {
        int z = cz + dz;
        if (z > 0 && z < m_depth - 1) {
            int i = idx(cq, cr, z);
            m_s[i] = 1.0f;
            m_u[i] = 0.0f;
            m_birthStep[i] = 1.0f;
            ++count;
        }
    }

    // For double-deck capped column preset (seedHeight == 4), also seed small basal caps at the ends
    if (seedHeight == 4 && clampedSeedH >= 2) {
        for (int sign = -1; sign <= 1; sign += 2) {
            int zCap = cz + sign * clampedSeedH;
            for (int d = 0; d < 6; ++d) {
                int i = idx(cq + dQ[d], cr + dR[d], zCap);
                m_s[i] = 1.0f;
                m_u[i] = 0.0f;
                m_birthStep[i] = 1.0f;
                ++count;
            }
        }
    }

    m_steps = 0;
    m_iceCount = count;
    m_maxRadius = 1;
}

void Reiter3D::step() {
    if (dynamicFlight) {
        // Simulate crystal falling through varying atmospheric humidity/temperature layers
        float phase = static_cast<float>(m_steps) * 0.025f;
        beta = std::clamp(0.42f + 0.16f * std::sin(phase), 0.18f, 0.75f);
        gammaZRatio = std::clamp(0.35f + 0.30f * std::cos(phase * 0.7f), 0.05f, 1.5f);
    }

    int iceCounter = 0;
    int maxRad = 1;
    const int cq = m_width / 2;
    const int cr = m_height / 2;
    const int sliceStride = m_width * m_height;

    // 1. Classify receptive cells on the 3D hexagonal prism lattice (6 basal + 2 axial neighbors)
    //    Bit 0 (1): cell is frozen (s >= 1)
    //    Bit 1 (2): cell has at least one frozen basal (hexagonal XY) neighbor
    //    Bit 2 (4): cell has at least one frozen vertical (prism Z) neighbor
    for (int z = 1; z < m_depth - 1; ++z) {
        for (int r = 1; r < m_height - 1; ++r) {
            for (int q = 1; q < m_width - 1; ++q) {
                const int i = idx(q, r, z);
                std::uint8_t mask = 0;

                if (m_s[i] >= 1.0f) {
                    mask = 1;
                    ++iceCounter;
                    if (m_birthStep[i] == 0.0f) {
                        m_birthStep[i] = static_cast<float>(m_steps + 1);
                    }
                    int dq = q - cq;
                    int dr = r - cr;
                    int hexDist = (std::abs(dq) + std::abs(dr) + std::abs(dq + dr)) / 2;
                    if (hexDist > maxRad) {
                        maxRad = hexDist;
                    }
                } else {
                    // Check 6 basal neighbors in the same z-plane
                    for (int d = 0; d < 6; ++d) {
                        if (m_s[i + dR[d] * m_width + dQ[d]] >= 1.0f) {
                            mask |= 2;
                            break;
                        }
                    }
                    // Check 2 vertical c-axis neighbors (z - 1 and z + 1)
                    if (m_s[i - sliceStride] >= 1.0f || m_s[i + sliceStride] >= 1.0f) {
                        mask |= 4;
                    }
                }

                m_receptive[i] = mask;
                m_u[i] = (mask != 0) ? 0.0f : m_s[i];
            }
        }
    }

    m_iceCount = iceCounter;
    m_maxRadius = maxRad;

    // Boundary condition: constant background vapor (beta) on domain boundaries
    for (int z = 0; z < m_depth; ++z) {
        for (int r = 0; r < m_height; ++r) {
            for (int q = 0; q < m_width; ++q) {
                if (isBoundary(q, r, z)) {
                    int i = idx(q, r, z);
                    m_u[i] = beta;
                    m_s[i] = beta;
                    m_receptive[i] = 0;
                }
            }
        }
    }

    std::normal_distribution<float> dist(0.0f, 1.0f);
    const float globalNoise = dist(m_rng);

    // 2. 3D Anisotropic Hexagonal-Prism Laplacian Diffusion + Condensation
    //    Ensure stability: selfWeight >= 0
    const float safeAlphaXY = std::clamp(alphaXY, 0.05f, 1.0f);
    const float safeAlphaZ = std::clamp(alphaZ, 0.0f, 0.95f - safeAlphaXY * 0.5f);
    const float wXY = safeAlphaXY / 12.0f;
    const float wZ = safeAlphaZ / 4.0f;
    const float selfWeight = 1.0f - (safeAlphaXY * 0.5f) - (safeAlphaZ * 0.5f);

    for (int z = 1; z < m_depth - 1; ++z) {
        for (int r = 1; r < m_height - 1; ++r) {
            for (int q = 1; q < m_width - 1; ++q) {
                const int i = idx(q, r, z);

                const float sumBasal =
                    m_u[i + 1] +
                    m_u[i - 1] +
                    m_u[i + m_width] +
                    m_u[i - m_width] +
                    m_u[i - m_width + 1] +
                    m_u[i + m_width - 1];

                const float sumAxial =
                    m_u[i + sliceStride] +
                    m_u[i - sliceStride];

                float uDiffused = selfWeight * m_u[i] + wXY * sumBasal + wZ * sumAxial;
                const std::uint8_t mask = m_receptive[i];

                if (mask != 0) {
                    float xi = symmetricNoise ? globalNoise : dist(m_rng);
                    // Anisotropic condensation rate depending on whether attachment is basal or axial
                    float effGamma = gamma;
                    if ((mask & 2) && (mask & 4)) {
                        // Corner/edge site between basal and prism facet
                        effGamma = gamma * (0.65f + 0.35f * gammaZRatio);
                    } else if (mask & 4) {
                        // Purely vertical prism facet growth (c-axis)
                        effGamma = gamma * gammaZRatio;
                    }

                    float thermalGamma = std::max(0.0f, effGamma * (1.0f + noiseAmp * xi));
                    m_s[i] = m_s[i] + uDiffused + thermalGamma;
                } else {
                    if (!symmetricNoise && noiseAmp > 0.0f) {
                        float vaporNoise = dist(m_rng) * (noiseAmp * 0.0012f * beta);
                        uDiffused = std::clamp(uDiffused + vaporNoise, 0.0f, 0.999f);
                    }
                    m_s[i] = uDiffused;
                }
            }
        }
    }

    ++m_steps;
}

void Reiter3D::buildInstances(std::vector<VoxelInstance>& outInstances, bool surfaceOnly, bool sliceZ, int maxZLayer) const {
    outInstances.clear();

    const int cq = m_width / 2;
    const int cr = m_height / 2;
    const int cz = m_depth / 2;
    const int sliceStride = m_width * m_height;
    const float sqrt3_2 = 0.86602540378f;
    const float zSpacing = 0.82f; // Aspect ratio of hexagonal prism layer
    const float maxR = std::max(1.0f, static_cast<float>(m_maxRadius));
    const float maxStep = std::max(1.0f, static_cast<float>(m_steps));
    const float halfDepth = std::max(1.0f, static_cast<float>(m_depth / 2));

    const int zLimit = sliceZ ? std::clamp(maxZLayer, 1, m_depth - 2) : (m_depth - 2);

    for (int z = 1; z <= zLimit; ++z) {
        for (int r = 1; r < m_height - 1; ++r) {
            for (int q = 1; q < m_width - 1; ++q) {
                const int i = idx(q, r, z);
                const float sVal = m_s[i];
                if (sVal < 1.0f) {
                    continue;
                }

                if (surfaceOnly) {
                    // Check if completely enclosed by other frozen cells (interior occlusion culling)
                    bool exposed = (z == zLimit && sliceZ);
                    if (!exposed) {
                        if (m_s[i + 1] < 1.0f || m_s[i - 1] < 1.0f ||
                            m_s[i + m_width] < 1.0f || m_s[i - m_width] < 1.0f ||
                            m_s[i - m_width + 1] < 1.0f || m_s[i + m_width - 1] < 1.0f ||
                            m_s[i + sliceStride] < 1.0f || m_s[i - sliceStride] < 1.0f) {
                            exposed = true;
                        }
                    }
                    if (!exposed) {
                        continue;
                    }
                }

                const float dq = static_cast<float>(q - cq);
                const float dr = static_cast<float>(r - cr);
                const float dz = static_cast<float>(z - cz);

                // Axial (q, r) to 3D Cartesian coordinates (Y is up along c-axis, XZ is basal plane)
                const float x = dq + dr * 0.5f;
                const float zCart = dr * sqrt3_2;
                const float y = dz * zSpacing;

                const float radialDist = std::sqrt(x * x + zCart * zCart);
                const float distRatio = std::clamp(radialDist / maxR, 0.0f, 1.0f);
                const float zNorm = std::clamp(dz / halfDepth, -1.0f, 1.0f);
                const float birthRatio = std::clamp(m_birthStep[i] / maxStep, 0.0f, 1.0f);

                outInstances.push_back(VoxelInstance{
                    x, y, zCart,
                    sVal,
                    distRatio,
                    zNorm,
                    birthRatio,
                    0.0f
                });
            }
        }
    }
}

void Reiter3D::renderCrossSectionRGBA(std::vector<std::uint8_t>& pixels, int zLayer, int colorMode) const {
    const int z = std::clamp(zLayer, 0, m_depth - 1);
    const int cx = m_width / 2;
    const int cy = m_height / 2;
    const float invSqrt3 = 1.0f / std::sqrt(3.0f);

    if (pixels.size() != static_cast<std::size_t>(m_width * m_height * 4)) {
        pixels.resize(static_cast<std::size_t>(m_width * m_height * 4), 255);
    }

    for (int py = 0; py < m_height; ++py) {
        const float dy = static_cast<float>(py - cy);
        const float rFloat = static_cast<float>(cy) + dy * (2.0f * invSqrt3);
        const int r0 = static_cast<int>(std::floor(rFloat));
        const float fr = rFloat - r0;

        for (int px = 0; px < m_width; ++px) {
            const float dx = static_cast<float>(px - cx);
            const float qFloat = static_cast<float>(cx) + dx - dy * invSqrt3;
            const int q0 = static_cast<int>(std::floor(qFloat));
            const float fq = qFloat - q0;

            const int pixelIndex = (py * m_width + px) * 4;

            if (q0 < 1 || q0 >= m_width - 2 || r0 < 1 || r0 >= m_height - 2) {
                pixels[pixelIndex + 0] = 6;
                pixels[pixelIndex + 1] = 10;
                pixels[pixelIndex + 2] = 22;
                pixels[pixelIndex + 3] = 255;
                continue;
            }

            const float s00 = m_s[idx(q0, r0, z)];
            const float s10 = m_s[idx(q0 + 1, r0, z)];
            const float s01 = m_s[idx(q0, r0 + 1, z)];
            const float s11 = m_s[idx(q0 + 1, r0 + 1, z)];

            const float sVal = (1.0f - fq) * (1.0f - fr) * s00 +
                               fq * (1.0f - fr) * s10 +
                               (1.0f - fq) * fr * s01 +
                               fq * fr * s11;

            const bool isIce = (s00 >= 1.0f || s10 >= 1.0f || s01 >= 1.0f || s11 >= 1.0f) && (sVal >= 0.85f);
            ColorRGB c{6, 10, 22};

            if (colorMode == 0) {
                if (isIce || sVal >= 1.0f) {
                    float iceThickness = std::clamp((sVal - 0.95f) / 0.65f, 0.0f, 1.0f);
                    c = lerpRGB({95, 195, 255}, {245, 252, 255}, iceThickness);
                } else {
                    float v = std::clamp(sVal / std::max(0.05f, beta), 0.0f, 1.2f);
                    c = lerpRGB({5, 9, 20}, {22, 52, 95}, v * 0.85f);
                }
            } else {
                if (isIce || sVal >= 1.0f) {
                    float t = std::clamp((sVal - 1.0f) / 0.8f, 0.0f, 1.0f);
                    c = lerpRGB({0, 230, 255}, {255, 255, 255}, t);
                } else {
                    float ratio = std::clamp(sVal / std::max(0.05f, beta), 0.0f, 1.1f);
                    if (ratio < 0.5f) {
                        c = lerpRGB({8, 6, 25}, {75, 22, 115}, ratio * 2.0f);
                    } else {
                        c = lerpRGB({75, 22, 115}, {235, 95, 40}, (ratio - 0.5f) * 2.0f);
                    }
                }
            }

            pixels[pixelIndex + 0] = c.r;
            pixels[pixelIndex + 1] = c.g;
            pixels[pixelIndex + 2] = c.b;
            pixels[pixelIndex + 3] = 255;
        }
    }
}

