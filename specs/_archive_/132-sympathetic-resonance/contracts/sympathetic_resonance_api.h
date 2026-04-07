// ==============================================================================
// API Contract: SympatheticResonance
// ==============================================================================
// This file documents the public API contract for the SympatheticResonance
// Layer 3 system component. The actual implementation will be at:
//   dsp/include/krate/dsp/systems/sympathetic_resonance.h
//
// NOTE: This is a DESIGN DOCUMENT, not compilable code. The implementation
// will refine types and signatures as needed while preserving this contract.
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/smoother.h>

#include <array>
#include <cstdint>

namespace Krate {
namespace DSP {

/// Compile-time constant: number of partials per voice for sympathetic resonance.
/// Increasing requires rebuild. 4 captures ~95% of audible sympathetic energy.
inline constexpr int kSympatheticPartialCount = 4;

/// Maximum resonator pool capacity (standard mode).
inline constexpr int kMaxSympatheticResonators = 64;

/// Per-voice partial frequency data passed on noteOn events.
struct SympatheticPartialInfo {
    std::array<float, kSympatheticPartialCount> frequencies{};
};

/// =============================================================================
/// SympatheticResonance - Global Shared Resonance Field
/// =============================================================================
/// Layer 3 system component. Manages a pool of driven second-order resonators
/// tuned to the union of all active voices' low-order partials.
///
/// Signal flow:
///   Input: global voice sum (post polyphony gain compensation)
///   Output: sympathetic signal (post anti-mud filter, additive to master)
///
/// Lifecycle:
///   1. prepare(sampleRate)  -- allocate nothing, compute coefficients
///   2. noteOn/noteOff       -- manage resonator pool (called from MIDI handler)
///   3. process(input)       -- per-sample driven resonance + anti-mud HPF
///   4. reset()              -- clear all state
///
class SympatheticResonance {
public:
    SympatheticResonance() noexcept = default;

    // Non-copyable (contains filter state)
    SympatheticResonance(const SympatheticResonance&) = delete;
    SympatheticResonance& operator=(const SympatheticResonance&) = delete;
    SympatheticResonance(SympatheticResonance&&) noexcept = default;
    SympatheticResonance& operator=(SympatheticResonance&&) noexcept = default;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// Prepare for processing. Computes envelope coefficients, configures HPF.
    /// Must be called before process() or any noteOn/noteOff calls.
    /// @param sampleRate  Sample rate in Hz (44100-192000)
    void prepare(double sampleRate) noexcept;

    /// Reset all resonator state and filters. Keeps sample rate.
    void reset() noexcept;

    // =========================================================================
    // Parameters (called once per audio block, before process loop)
    // =========================================================================

    /// Set the coupling amount (0.0 = bypassed, 1.0 = maximum coupling).
    /// Maps to approximately -40 dB (low) to -20 dB (high) coupling gain.
    /// Smoothed internally to prevent clicks.
    /// @param amount  Normalized amount [0.0, 1.0]
    void setAmount(float amount) noexcept;

    /// Set the decay parameter controlling Q-factor.
    /// Maps to Q range [100, 1000]. Affects newly added resonators only.
    /// @param decay  Normalized decay [0.0, 1.0]
    void setDecay(float decay) noexcept;

    // =========================================================================
    // Voice Events (called from MIDI handlers, on audio thread)
    // =========================================================================

    /// Add resonators for a new voice's partials.
    /// Merges with existing resonators when frequencies are within ~0.3 Hz.
    /// True duplicates (same voiceId re-triggered) always merge.
    /// @param voiceId   Unique voice identifier
    /// @param partials  Inharmonicity-adjusted partial frequencies
    void noteOn(int32_t voiceId, const SympatheticPartialInfo& partials) noexcept;

    /// Mark a voice as released. Existing resonators continue to ring out.
    /// Resonators are reclaimed only when amplitude drops below -96 dB.
    /// @param voiceId   Voice identifier that was released
    void noteOff(int32_t voiceId) noexcept;

    // =========================================================================
    // Processing (called per sample in the audio loop)
    // =========================================================================

    /// Process one sample of sympathetic resonance.
    /// Feeds the input (scaled by coupling amount) into all active resonators,
    /// sums their outputs, applies anti-mud HPF, and returns the result.
    ///
    /// When amount is 0.0, returns 0.0 with zero computation (bypassed).
    ///
    /// @param input  Mono input sample (global voice sum, post poly-gain comp)
    /// @return       Sympathetic output sample (to be added to master output)
    float process(float input) noexcept;

    // =========================================================================
    // Query (for testing and diagnostics)
    // =========================================================================

    /// @return Number of currently active resonators in the pool.
    [[nodiscard]] int getActiveResonatorCount() const noexcept;

    /// @return Whether the component is completely bypassed (amount == 0.0).
    [[nodiscard]] bool isBypassed() const noexcept;
};

}  // namespace DSP
}  // namespace Krate
