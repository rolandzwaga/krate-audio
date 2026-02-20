// ==============================================================================
// Layer 3: System Component - OscillatorSlot Interface
// ==============================================================================
// Virtual interface for polymorphic oscillator dispatch in the Ruinae voice
// architecture. Follows the IFeedbackProcessor pattern established at Layer 1.
//
// Per-block virtual dispatch overhead is negligible (~5ns per call for 128-512
// sample blocks at 44.1kHz = ~86 calls/sec per oscillator slot).
//
// Feature: 041-ruinae-voice-architecture
// Layer: 3 (Systems)
// Dependencies: None (pure interface)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept interface)
// - Principle III: Modern C++ (C++20, virtual interface)
// - Principle IX: Layer 3 (no layer dependencies)
// - Principle XIV: ODR Prevention (unique class name verified)
//
// Reference: specs/041-ruinae-voice-architecture/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/systems/oscillator_types.h>

#include <cstddef>

namespace Krate::DSP {

/// @brief Virtual interface for oscillator slots in SelectableOscillator.
///
/// Enables pointer-to-base dispatch for type switching without heap allocation.
/// All 10 oscillator types are pre-allocated at prepare() time, and setType()
/// simply swaps the active pointer (zero-allocation).
///
/// Follows the IFeedbackProcessor pattern (dsp/primitives/i_feedback_processor.h).
///
/// @note All methods must be real-time safe (no allocations in processBlock)
class OscillatorSlot {
public:
    virtual ~OscillatorSlot() = default;

    /// @brief Prepare the oscillator for audio processing.
    /// @param sampleRate The sample rate in Hz
    /// @param maxBlockSize Maximum number of samples per processBlock() call
    /// @note Allocations are permitted here, but not in processBlock()
    virtual void prepare(double sampleRate, size_t maxBlockSize) noexcept = 0;

    /// @brief Reset all internal state without changing configuration.
    virtual void reset() noexcept = 0;

    /// @brief Set the oscillator frequency in Hz.
    /// @param hz Frequency in Hz. Types without frequency control ignore this.
    virtual void setFrequency(float hz) noexcept = 0;

    /// @brief Generate a block of samples.
    /// @param output Output buffer (must hold numSamples floats)
    /// @param numSamples Number of samples to generate
    /// @note Must be real-time safe — no allocations, no blocking
    virtual void processBlock(float* output, size_t numSamples) noexcept = 0;

    /// @brief Set a type-specific parameter on this oscillator.
    /// @param param The parameter identifier
    /// @param value The parameter value (in DSP domain, NOT normalized)
    /// @note Base class implementation is an unconditional silent no-op.
    ///       Subclasses override to handle parameters relevant to their type.
    ///       Unrecognized OscParam values MUST be silently ignored.
    /// @note Must be real-time safe: no allocation, no logging, no assertion.
    virtual void setParam(OscParam param, float value) noexcept {
        (void)param; (void)value;
    }

    /// @brief Report the latency introduced by this oscillator.
    /// @return Latency in samples (0 for most types, FFT size for spectral types)
    [[nodiscard]] virtual size_t getLatencySamples() const noexcept { return 0; }
};

} // namespace Krate::DSP
