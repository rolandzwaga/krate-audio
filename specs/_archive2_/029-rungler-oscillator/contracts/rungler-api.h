// ==============================================================================
// API Contract: Rungler / Shift Register Oscillator
// ==============================================================================
// Layer 2: DSP Processor
// Location: dsp/include/krate/dsp/processors/rungler.h
// Spec: specs/029-rungler-oscillator/spec.md
//
// This file defines the public API contract. Implementation follows this
// contract exactly. Changes to this contract require spec amendment.
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/primitives/one_pole.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

/// @brief Benjolin-inspired Rungler / Shift Register Oscillator.
///
/// Two cross-modulating triangle oscillators and an 8-bit shift register
/// with XOR feedback, creating chaotic stepped sequences via 3-bit DAC.
///
/// @par Layer: 2 (processors/)
/// @par Dependencies: Layer 0 (random.h, db_utils.h), Layer 1 (one_pole.h)
///
/// @par Signal Flow
/// Oscillator 1's pulse feeds data into the shift register (XOR'd with the
/// register's last bit in chaos mode). Oscillator 2's rising edge clocks
/// the register. The last 3 bits are converted to an 8-level stepped voltage
/// via a 3-bit DAC, which modulates both oscillators' frequencies.
///
/// @par Memory Model
/// All state is pre-allocated. No heap allocation during processing.
///
/// @par Thread Safety
/// Single-threaded. All methods must be called from the same thread.
///
/// @par Real-Time Safety
/// - prepare(): NOT real-time safe (prepares OnePoleLP filter)
/// - All other methods: Real-time safe (noexcept, no allocations)
class Rungler {
public:
    // =========================================================================
    // Output Structure (FR-012)
    // =========================================================================

    /// @brief Multi-output sample from the Rungler processor.
    struct Output {
        float osc1 = 0.0f;    ///< Oscillator 1 triangle wave [-1, +1]
        float osc2 = 0.0f;    ///< Oscillator 2 triangle wave [-1, +1]
        float rungler = 0.0f; ///< Rungler CV (filtered DAC output) [0, +1]
        float pwm = 0.0f;     ///< PWM comparator output [-1, +1]
        float mixed = 0.0f;   ///< Equal mix of osc1 + osc2, scaled to [-1, +1]
    };

    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinFrequency = 0.1f;
    static constexpr float kMaxFrequency = 20000.0f;
    static constexpr float kDefaultOsc1Freq = 200.0f;
    static constexpr float kDefaultOsc2Freq = 300.0f;
    static constexpr size_t kMinBits = 4;
    static constexpr size_t kMaxBits = 16;
    static constexpr size_t kDefaultBits = 8;
    static constexpr float kDefaultModulationOctaves = 4.0f;
    static constexpr float kMinFilterCutoff = 5.0f;  // Hz, at filterAmount = 1.0

    // =========================================================================
    // Lifecycle (FR-013, FR-014)
    // =========================================================================

    /// @brief Default constructor.
    Rungler() noexcept = default;

    /// @brief Prepare the Rungler for processing.
    ///
    /// Stores sample rate, seeds the shift register, and prepares the
    /// CV smoothing filter. Must be called before any processing.
    ///
    /// @param sampleRate Sample rate in Hz
    void prepare(double sampleRate) noexcept;

    /// @brief Reset processing state while preserving parameters.
    ///
    /// Resets oscillator phases to zero with direction +1,
    /// re-seeds the shift register, and resets the CV filter.
    void reset() noexcept;

    // =========================================================================
    // Parameter Setters (FR-002, FR-009, FR-010, FR-015, FR-016, FR-017)
    // =========================================================================

    /// @brief Set Oscillator 1 base frequency.
    /// @param hz Frequency in Hz (clamped to [0.1, 20000]). NaN -> 200 Hz.
    void setOsc1Frequency(float hz) noexcept;

    /// @brief Set Oscillator 2 base frequency.
    /// @param hz Frequency in Hz (clamped to [0.1, 20000]). NaN -> 300 Hz.
    void setOsc2Frequency(float hz) noexcept;

    /// @brief Set Rungler CV modulation depth for Oscillator 1.
    /// @param depth Modulation depth [0, 1] (clamped)
    void setOsc1RunglerDepth(float depth) noexcept;

    /// @brief Set Rungler CV modulation depth for Oscillator 2.
    /// @param depth Modulation depth [0, 1] (clamped)
    void setOsc2RunglerDepth(float depth) noexcept;

    /// @brief Set Rungler CV modulation depth for both oscillators.
    /// @param depth Modulation depth [0, 1] (clamped)
    void setRunglerDepth(float depth) noexcept;

    /// @brief Set CV smoothing filter amount.
    /// @param amount Filter amount [0, 1]. 0 = no filtering, 1 = max smoothing.
    void setFilterAmount(float amount) noexcept;

    /// @brief Set shift register length.
    /// @param bits Register length in bits (clamped to [4, 16])
    void setRunglerBits(size_t bits) noexcept;

    /// @brief Toggle between chaos mode and loop mode.
    /// @param loop true = loop mode (recycled patterns), false = chaos mode (XOR feedback)
    void setLoopMode(bool loop) noexcept;

    /// @brief Set the PRNG seed for deterministic initialization.
    /// @param seedValue Seed value (0 is replaced with default)
    void seed(uint32_t seedValue) noexcept;

    // =========================================================================
    // Processing (FR-018, FR-019)
    // =========================================================================

    /// @brief Process a single sample and return all outputs.
    /// @return Multi-output sample with osc1, osc2, rungler, pwm, mixed
    [[nodiscard]] Output process() noexcept;

    /// @brief Process a block of samples into an Output array.
    /// @param output Array of Output structs (must be at least numSamples long)
    /// @param numSamples Number of samples to process
    void processBlock(Output* output, size_t numSamples) noexcept;

    /// @brief Process a block writing only the mixed output.
    /// @param output Float buffer (must be at least numSamples long)
    /// @param numSamples Number of samples to process
    void processBlockMixed(float* output, size_t numSamples) noexcept;

    /// @brief Process a block writing only the rungler CV output.
    /// @param output Float buffer (must be at least numSamples long)
    /// @param numSamples Number of samples to process
    void processBlockRungler(float* output, size_t numSamples) noexcept;
};

}  // namespace DSP
}  // namespace Krate
