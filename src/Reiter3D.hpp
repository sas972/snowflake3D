/*
 * Copyright (c) 2026 Imre Városi
 * Licensed under the MIT License with the Beer-Ware Clause (Revision 42).
 * If we meet some day, and you think this stuff is worth it, you can buy me a beer in return.
 * See LICENSE file in the project root for full license text.
 */

#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <vector>

struct Preset3D {
    std::string name;
    std::string description;
    float alphaXY;     // Basal plane diffusion rate
    float alphaZ;      // Prism (c-axis) diffusion rate
    float beta;        // Background supersaturated vapor level
    float gamma;       // Basal condensation rate
    float gammaZRatio; // Axial vs basal condensation ratio (>1 favors columns/needles, <1 favors plates/dendrites)
    float noiseAmp;    // Thermal noise amplitude
    int seedHeight;    // Initial prism seed height along Z
};

struct VoxelInstance {
    float x, y, z;     // 3D Cartesian center of hexagonal prism cell
    float mass;        // Frozen ice mass (s >= 1.0)
    float distRatio;   // Normalized radial distance from center
    float zNorm;       // Normalized signed z height (-1 .. +1)
    float stepBorn;    // Normalized age / growth stage
    float pad;
};

class Reiter3D {
public:
    Reiter3D(int width, int height, int depth);

    void reset();
    void resize(int width, int height, int depth);
    void step();
    void applyPreset(int index);

    // Extract active ice crystals (surface or all frozen cells) for instanced 3D rendering
    void buildInstances(std::vector<VoxelInstance>& outInstances, bool surfaceOnly = true, bool sliceZ = false, int maxZLayer = 0) const;

    // Render 2D mid-plane cross section (z = depth / 2) to RGBA buffer for mini-inspector
    void renderCrossSectionRGBA(std::vector<std::uint8_t>& pixels, int zLayer, int colorMode) const;

    int getWidth() const { return m_width; }
    int getHeight() const { return m_height; }
    int getDepth() const { return m_depth; }
    std::uint64_t getStepCount() const { return m_steps; }
    int getIceCount() const { return m_iceCount; }
    int getMaxRadius() const { return m_maxRadius; }

    // Physical parameters (3D Reiter Cellular Automaton)
    float alphaXY = 1.0f;        // Basal hexagonal diffusion constant (0.1 .. 1.0)
    float alphaZ = 0.25f;        // Vertical c-axis diffusion constant (0.0 .. 0.6)
    float beta = 0.42f;          // Background vapor supersaturation (0.10 .. 0.90)
    float gamma = 0.0012f;       // Condensation / attachment rate
    float gammaZRatio = 0.20f;   // Anisotropy of attachment along c-axis vs basal plane
    float noiseAmp = 0.015f;     // Thermal fluctuation amplitude (eta)
    bool symmetricNoise = true;  // Preserve D6h symmetry via global temporal noise per step
    int seedHeight = 1;          // Initial seed half-height along Z

    // Dynamic atmospheric profiling (Nakaya diagram flight path)
    bool dynamicFlight = false;  // Automatically cycle humidity/temperature as crystal falls

    static const std::vector<Preset3D>& getPresets();

private:
    int m_width;
    int m_height;
    int m_depth;
    std::uint64_t m_steps = 0;
    int m_iceCount = 0;
    int m_maxRadius = 1;

    std::vector<float> m_s;                // Total water mass (ice + bound + vapor)
    std::vector<float> m_u;                // Diffusing vapor mass
    std::vector<std::uint8_t> m_receptive; // Bitmask: bit 0 = receptive, bit 1 = basal neighbor, bit 2 = axial neighbor
    std::vector<float> m_birthStep;        // Step when cell froze (s >= 1.0)

    mutable std::mt19937 m_rng;

    inline int idx(int q, int r, int z) const {
        return (z * m_height + r) * m_width + q;
    }

    bool isBoundary(int q, int r, int z) const;
};

