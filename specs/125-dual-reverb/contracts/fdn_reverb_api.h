// ==============================================================================
// API Contract: FDNReverb
// ==============================================================================
// This file defines the public API contract for the FDN reverb.
// Implementation will be at: dsp/include/krate/dsp/effects/fdn_reverb.h
// SIMD implementation: dsp/include/krate/dsp/effects/fdn_reverb_simd.cpp
//
// Layer: 4 (Effects)
// Namespace: Krate::DSP
// ==============================================================================

#pragma once

#include <cstddef>

namespace Krate {
namespace DSP {

// Forward declaration (actual struct lives in reverb.h)
struct ReverbParams;

/// @brief 8-channel Feedback Delay Network reverb (Layer 4).
///
/// Architecture:
///   Input -> Mono sum -> Pre-delay -> Hadamard diffuser (3-4 steps)
///         -> Feedback loop (Householder matrix + 8 delay lines
///            + one-pole damping + DC blockers + 4-channel LFO modulation)
///         -> Stereo output with width control
///
/// SIMD acceleration via Google Highway for:
///   (a) 8-channel one-pole filter bank
///   (b) Hadamard butterfly (FWHT) in diffuser
///   (c) Householder matrix application in feedback
///
/// @par Usage
/// @code
/// FDNReverb reverb;
/// reverb.prepare(44100.0);
///
/// ReverbParams params;
/// params.roomSize = 0.7f;
/// params.mix = 0.4f;
/// reverb.setParams(params);
///
/// // In audio callback:
/// reverb.processBlock(leftBuffer, rightBuffer, numSamples);
/// @endcode
class FDNReverb {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr size_t kNumChannels = 8;
    static constexpr size_t kNumModulatedChannels = 4;
    static constexpr size_t kNumDiffuserSteps = 4;
    static constexpr size_t kSubBlockSize = 16;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    FDNReverb() noexcept = default;

    /// @brief Prepare for processing. Allocates all buffers.
    /// @param sampleRate Sample rate in Hz [8000, 192000]
    void prepare(double sampleRate) noexcept;

    /// @brief Reset all internal state to silence.
    void reset() noexcept;

    // =========================================================================
    // Parameters
    // =========================================================================

    /// @brief Update all reverb parameters.
    /// @param params Parameter struct (same interface as Dattorro Reverb)
    void setParams(const ReverbParams& params) noexcept;

    // =========================================================================
    // Processing (real-time safe)
    // =========================================================================

    /// @brief Process a single stereo sample pair in-place.
    void process(float& left, float& right) noexcept;

    /// @brief Process a block of stereo samples in-place.
    void processBlock(float* left, float* right, size_t numSamples) noexcept;

    // =========================================================================
    // Queries
    // =========================================================================

    /// @brief Check if the reverb has been prepared.
    [[nodiscard]] bool isPrepared() const noexcept;
};

} // namespace DSP
} // namespace Krate
