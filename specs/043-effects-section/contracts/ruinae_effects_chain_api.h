// ==============================================================================
// API Contract: RuinaeEffectsChain
// ==============================================================================
// This file defines the public API contract for the RuinaeEffectsChain class.
// It is NOT compiled -- it serves as a reference for the implementation.
//
// Feature: 043-effects-section
// Layer: 3 (Systems)
// Location: dsp/include/krate/dsp/systems/ruinae_effects_chain.h
// ==============================================================================

#pragma once

// Dependencies (verified signatures in plan.md)
#include <krate/dsp/core/block_context.h>
#include <krate/dsp/effects/reverb.h>  // ReverbParams struct

#include <cstddef>
#include <cstdint>

namespace Krate::DSP {

// Forward declarations (actual types live in their respective headers)
enum class RuinaeDelayType : uint8_t;

/// @brief Stereo effects chain for the Ruinae synthesizer (Layer 3).
///
/// Composes existing Layer 4 effects into a fixed-order processing chain:
///   Voice Sum -> Spectral Freeze -> Delay -> Reverb -> Output
///
/// Features:
/// - Five selectable delay types with click-free crossfade switching
/// - Spectral freeze with pitch shifting, shimmer, and decay
/// - Dattorro plate reverb
/// - Constant worst-case latency reporting with per-delay compensation
/// - Fully real-time safe (all runtime methods noexcept, zero allocations)
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in processBlock)
/// - Principle III: Modern C++ (C++20, RAII, pre-allocated buffers)
/// - Principle IX: Layer 3 (composes Layer 4 effects -- documented exception)
/// - Principle XIV: ODR Prevention (unique class name verified)
class RuinaeEffectsChain {
public:
    // =========================================================================
    // Lifecycle (FR-002, FR-003)
    // =========================================================================

    RuinaeEffectsChain() noexcept = default;
    ~RuinaeEffectsChain() = default;

    // Non-copyable, movable
    RuinaeEffectsChain(const RuinaeEffectsChain&) = delete;
    RuinaeEffectsChain& operator=(const RuinaeEffectsChain&) = delete;
    RuinaeEffectsChain(RuinaeEffectsChain&&) noexcept = default;
    RuinaeEffectsChain& operator=(RuinaeEffectsChain&&) noexcept = default;

    /// @brief Prepare all internal effects for processing (FR-002).
    ///
    /// Allocates all temporary buffers and prepares all five delay types,
    /// the freeze effect, reverb, and latency compensation delays.
    /// May allocate memory. NOT real-time safe.
    ///
    /// @param sampleRate Sample rate in Hz
    /// @param maxBlockSize Maximum samples per processBlock() call
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;

    /// @brief Clear all internal state without re-preparation (FR-003).
    ///
    /// Clears delay lines, reverb tank, freeze buffers, and crossfade state.
    /// Does not deallocate memory.
    void reset() noexcept;

    // =========================================================================
    // Processing (FR-004, FR-005, FR-028)
    // =========================================================================

    /// @brief Process stereo audio in-place through the effects chain (FR-004).
    ///
    /// Processing order (FR-005):
    /// 1. Spectral freeze (if enabled)
    /// 2. Active delay type (+ crossfade partner during transitions)
    /// 3. Reverb
    ///
    /// @param left Left channel buffer (modified in-place)
    /// @param right Right channel buffer (modified in-place)
    /// @param numSamples Number of samples per channel
    /// @pre prepare() has been called
    /// @note Real-time safe: noexcept, zero allocations (FR-028)
    void processBlock(float* left, float* right, size_t numSamples) noexcept;

    // =========================================================================
    // Delay Type Selection (FR-009 through FR-014)
    // =========================================================================

    /// @brief Select the active delay algorithm (FR-009).
    ///
    /// When the requested type differs from the current type, initiates a
    /// crossfade transition. When called during an active crossfade,
    /// fast-tracks the current crossfade (FR-012).
    ///
    /// @param type The delay type to activate
    void setDelayType(RuinaeDelayType type) noexcept;

    /// @brief Get the currently active delay type.
    [[nodiscard]] RuinaeDelayType getActiveDelayType() const noexcept;

    // =========================================================================
    // Delay Parameter Forwarding (FR-015 through FR-017)
    // =========================================================================

    /// @brief Set delay time in milliseconds (FR-015).
    /// Forwarded to active delay and crossfade partner.
    void setDelayTime(float ms) noexcept;

    /// @brief Set delay feedback amount (FR-015).
    /// Forwarded to active delay and crossfade partner.
    void setDelayFeedback(float amount) noexcept;

    /// @brief Set delay dry/wet mix (FR-015).
    /// Forwarded to active delay and crossfade partner.
    void setDelayMix(float mix) noexcept;

    /// @brief Set tempo for synced delay modes (FR-016).
    void setDelayTempo(double bpm) noexcept;

    // =========================================================================
    // Freeze Control (FR-018 through FR-020)
    // =========================================================================

    /// @brief Activate/deactivate the freeze slot in the chain (FR-018).
    void setFreezeEnabled(bool enabled) noexcept;

    /// @brief Toggle the freeze capture state (FR-018).
    void setFreeze(bool frozen) noexcept;

    /// @brief Set freeze pitch shift in semitones [-24, +24] (FR-018).
    void setFreezePitchSemitones(float semitones) noexcept;

    /// @brief Set freeze shimmer mix [0.0, 1.0] (FR-018).
    void setFreezeShimmerMix(float mix) noexcept;

    /// @brief Set freeze decay [0.0, 1.0] (FR-018).
    void setFreezeDecay(float decay) noexcept;

    // =========================================================================
    // Reverb Control (FR-021 through FR-023)
    // =========================================================================

    /// @brief Set all reverb parameters (FR-021).
    void setReverbParams(const ReverbParams& params) noexcept;

    // =========================================================================
    // Latency (FR-026, FR-027)
    // =========================================================================

    /// @brief Get total processing latency in samples (FR-026).
    ///
    /// Returns the worst-case latency (spectral delay FFT size),
    /// constant regardless of active delay type (FR-027).
    [[nodiscard]] size_t getLatencySamples() const noexcept;
};

} // namespace Krate::DSP
