// ==============================================================================
// Residual Types - Data Types for Stochastic (Noise) Component of SMS Model
// ==============================================================================
// Layer 2: Processors
// Spec: specs/116-residual-noise-model/spec.md
// Covers: FR-004 (16-band spectral envelope), FR-005 (log-spaced bands),
//         FR-008 (ResidualFrame output)
//
// These types represent the stochastic (non-harmonic) component extracted during
// SMS (Sinusoidal + Noise Modeling) analysis. Produced by ResidualAnalyzer during
// offline sample analysis, consumed by ResidualSynthesizer during real-time
// playback. Time-aligned with HarmonicFrame: residualFrames[i] corresponds to
// harmonicFrames[i] for the same time position in the source sample.
// ==============================================================================

#pragma once

#include <array>
#include <cstddef>

namespace Krate::DSP {

/// Number of spectral envelope breakpoints for the residual model.
/// 16 bands, log-spaced in frequency (approximately ERB/Bark scale).
inline constexpr size_t kResidualBands = 16;

/// @brief Per-frame representation of the stochastic (noise) component.
///
/// Produced by ResidualAnalyzer during offline sample analysis.
/// Consumed by ResidualSynthesizer during real-time playback.
/// Time-aligned with HarmonicFrame: residualFrames[i] corresponds to
/// harmonicFrames[i] for the same time position in the source sample.
///
/// @note Default-constructed ResidualFrame represents silence (all zeros).
struct ResidualFrame {
    /// Spectral envelope: RMS energy per frequency band.
    /// Bands are log-spaced from ~50 Hz to Nyquist.
    /// All values >= 0.0 (clamped during analysis).
    std::array<float, kResidualBands> bandEnergies{};

    /// Overall residual energy for this frame (RMS of residual signal).
    /// Used to scale the resynthesized noise output.
    /// >= 0.0 (clamped during analysis).
    float totalEnergy = 0.0f;

    /// True if a transient (onset) was detected in this frame.
    /// Used by Transient Emphasis parameter to boost residual during attacks.
    bool transientFlag = false;
};

/// @brief Get the default band center frequencies as normalized ratios (0.0 to 1.0 of Nyquist).
///
/// These are pre-computed log-spaced center frequencies for the 16 residual bands.
/// Sample-rate independent: multiply by Nyquist to get Hz values.
///
/// @return Reference to a static array of 16 normalized frequency ratios
[[nodiscard]] inline const std::array<float, kResidualBands>& getResidualBandCenters() noexcept
{
    // Log-spaced from ~50 Hz to ~21 kHz, normalized to Nyquist (22050 Hz at 44.1 kHz)
    // Actual Hz values at 44.1 kHz: 50, 100, 175, 275, 425, 650, 1000, 1500,
    //                                2300, 3400, 5000, 7500, 11000, 16000, 19500, 21000
    static const std::array<float, kResidualBands> kCenters = {
        0.00227f,  // ~50 Hz / 22050
        0.00454f,  // ~100 Hz
        0.00794f,  // ~175 Hz
        0.01247f,  // ~275 Hz
        0.01927f,  // ~425 Hz
        0.02948f,  // ~650 Hz
        0.04535f,  // ~1000 Hz
        0.06803f,  // ~1500 Hz
        0.10431f,  // ~2300 Hz
        0.15420f,  // ~3400 Hz
        0.22676f,  // ~5000 Hz
        0.34014f,  // ~7500 Hz
        0.49887f,  // ~11000 Hz
        0.72562f,  // ~16000 Hz
        0.88435f,  // ~19500 Hz
        0.95238f   // ~21000 Hz
    };
    return kCenters;
}

/// @brief Get the band edge frequencies (boundaries between adjacent bands).
///
/// There are kResidualBands + 1 edges (including 0.0 and 1.0).
/// Each band spans from edges[i] to edges[i+1].
///
/// @return Reference to a static array of 17 normalized frequency ratios
[[nodiscard]] inline const std::array<float, kResidualBands + 1>& getResidualBandEdges() noexcept
{
    // Edges are geometric means of adjacent centers, with 0.0 and 1.0 at boundaries
    static const std::array<float, kResidualBands + 1> kEdges = {
        0.0f,       // DC
        0.00340f,   // between band 0 and 1
        0.00601f,   // between band 1 and 2
        0.00995f,   // between band 2 and 3
        0.01550f,   // between band 3 and 4
        0.02381f,   // between band 4 and 5
        0.03656f,   // between band 5 and 6
        0.05556f,   // between band 6 and 7
        0.08422f,   // between band 7 and 8
        0.12698f,   // between band 8 and 9
        0.18685f,   // between band 9 and 10
        0.27778f,   // between band 10 and 11
        0.41270f,   // between band 11 and 12
        0.60204f,   // between band 12 and 13
        0.80045f,   // between band 13 and 14
        0.91837f,   // between band 14 and 15
        1.0f        // Nyquist
    };
    return kEdges;
}

} // namespace Krate::DSP
