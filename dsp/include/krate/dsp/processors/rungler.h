// ==============================================================================
// Layer 2: DSP Processor - Rungler / Shift Register Oscillator
// ==============================================================================
// Benjolin-inspired chaotic stepped-voltage generator. Two cross-modulating
// triangle oscillators drive an N-bit shift register with XOR feedback,
// producing evolving stepped sequences via a 3-bit DAC.
//
// Feature: 029-rungler-oscillator
// Layer: 2 (Processors)
// Dependencies:
//   - Layer 0: db_utils.h (isNaN, isInf, flushDenormal), random.h (Xorshift32)
//   - Layer 1: one_pole.h (OnePoleLP)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, constexpr)
// - Principle IX: Layer 2 (depends only on Layer 0 and Layer 1)
// - Principle XII: Test-First Development
//
// Reference: specs/029-rungler-oscillator/spec.md
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
/// Two cross-modulating triangle oscillators and an N-bit shift register
/// with XOR feedback, creating chaotic stepped sequences via 3-bit DAC.
/// Five simultaneous outputs: osc1 triangle, osc2 triangle, rungler CV,
/// PWM comparator, and mixed.
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
    static constexpr float kMinFilterCutoff = 5.0f;

    // =========================================================================
    // Lifecycle (FR-013, FR-014)
    // =========================================================================

    /// @brief Default constructor.
    Rungler() noexcept = default;

    /// @brief Prepare the Rungler for processing.
    ///
    /// Stores sample rate, seeds the shift register with a random non-zero
    /// value, and prepares the CV smoothing filter. Must be called before
    /// any processing.
    ///
    /// @param sampleRate Sample rate in Hz (FR-013)
    void prepare(double sampleRate) noexcept {
        sampleRate_ = static_cast<float>(sampleRate);

        // Prepare the CV smoothing filter
        cvFilter_.prepare(sampleRate);
        updateFilterCutoff();

        // Seed shift register with random non-zero value (FR-023)
        registerState_ = rng_.next() & registerMask_;
        if (registerState_ == 0) {
            registerState_ = 1;
        }

        // Reset oscillator phases (FR-013)
        osc1Phase_ = 0.0f;
        osc1Direction_ = 1;
        osc2Phase_ = 0.0f;
        osc2Direction_ = 1;
        osc2PrevTriangle_ = 0.0f;

        // Reset CV state
        runglerCV_ = 0.0f;
        rawDacOutput_ = 0.0f;
        cvFilter_.reset();

        prepared_ = true;
    }

    /// @brief Reset processing state while preserving parameters.
    ///
    /// Resets oscillator phases to zero with direction +1,
    /// re-seeds the shift register using the current PRNG, and resets the
    /// CV filter. Preserves sample rate and parameter settings. (FR-014)
    ///
    /// To achieve fully deterministic output: call seed(value) then reset().
    void reset() noexcept {
        // Reset oscillator phases (direction +1, ramping upward)
        osc1Phase_ = 0.0f;
        osc1Direction_ = 1;
        osc2Phase_ = 0.0f;
        osc2Direction_ = 1;
        osc2PrevTriangle_ = 0.0f;

        // Re-seed shift register from PRNG
        registerState_ = rng_.next() & registerMask_;
        if (registerState_ == 0) {
            registerState_ = 1;
        }

        // Reset CV state
        runglerCV_ = 0.0f;
        rawDacOutput_ = 0.0f;
        cvFilter_.reset();
    }

    // =========================================================================
    // Parameter Setters (FR-002, FR-009, FR-010, FR-015, FR-016, FR-017)
    // =========================================================================

    /// @brief Set Oscillator 1 base frequency.
    /// @param hz Frequency in Hz (clamped to [0.1, 20000]). NaN/Inf -> 200 Hz.
    void setOsc1Frequency(float hz) noexcept {
        if (detail::isNaN(hz) || detail::isInf(hz)) {
            hz = kDefaultOsc1Freq;
        }
        osc1BaseFreq_ = std::clamp(hz, kMinFrequency, kMaxFrequency);
    }

    /// @brief Set Oscillator 2 base frequency.
    /// @param hz Frequency in Hz (clamped to [0.1, 20000]). NaN/Inf -> 300 Hz.
    void setOsc2Frequency(float hz) noexcept {
        if (detail::isNaN(hz) || detail::isInf(hz)) {
            hz = kDefaultOsc2Freq;
        }
        osc2BaseFreq_ = std::clamp(hz, kMinFrequency, kMaxFrequency);
    }

    /// @brief Set Rungler CV modulation depth for Oscillator 1.
    /// @param depth Modulation depth [0, 1] (clamped)
    void setOsc1RunglerDepth(float depth) noexcept {
        osc1RunglerDepth_ = std::clamp(depth, 0.0f, 1.0f);
    }

    /// @brief Set Rungler CV modulation depth for Oscillator 2.
    /// @param depth Modulation depth [0, 1] (clamped)
    void setOsc2RunglerDepth(float depth) noexcept {
        osc2RunglerDepth_ = std::clamp(depth, 0.0f, 1.0f);
    }

    /// @brief Set Rungler CV modulation depth for both oscillators.
    /// @param depth Modulation depth [0, 1] (clamped)
    void setRunglerDepth(float depth) noexcept {
        const float d = std::clamp(depth, 0.0f, 1.0f);
        osc1RunglerDepth_ = d;
        osc2RunglerDepth_ = d;
    }

    /// @brief Set CV smoothing filter amount.
    /// @param amount Filter amount [0, 1]. 0 = no filtering, 1 = max smoothing. (FR-008)
    void setFilterAmount(float amount) noexcept {
        filterAmount_ = std::clamp(amount, 0.0f, 1.0f);
        updateFilterCutoff();
    }

    /// @brief Set shift register length.
    /// @param bits Register length in bits (clamped to [4, 16]) (FR-016)
    void setRunglerBits(size_t bits) noexcept {
        bits = std::clamp(bits, kMinBits, kMaxBits);
        runglerBits_ = bits;
        registerMask_ = (1u << static_cast<uint32_t>(bits)) - 1u;
        // Truncate register to new length
        registerState_ &= registerMask_;
    }

    /// @brief Toggle between chaos mode and loop mode.
    /// @param loop true = loop mode (recycled patterns), false = chaos mode (XOR feedback) (FR-017)
    void setLoopMode(bool loop) noexcept {
        loopMode_ = loop;
    }

    /// @brief Set the PRNG seed for deterministic initialization.
    /// @param seedValue Seed value (0 is replaced with default by Xorshift32) (FR-020)
    void seed(uint32_t seedValue) noexcept {
        rng_.seed(seedValue);
    }

    // =========================================================================
    // Processing (FR-018, FR-019)
    // =========================================================================

    /// @brief Process a single sample and return all outputs.
    /// @return Multi-output sample with osc1, osc2, rungler, pwm, mixed (FR-018)
    [[nodiscard]] Output process() noexcept {
        // Unprepared state outputs silence (FR-022)
        if (!prepared_) {
            return Output{};
        }

        // --- Compute effective frequencies with cross-modulation (FR-003) ---
        const float osc1EffFreq = computeEffectiveFrequency(
            osc1BaseFreq_, osc1RunglerDepth_, runglerCV_);
        const float osc2EffFreq = computeEffectiveFrequency(
            osc2BaseFreq_, osc2RunglerDepth_, runglerCV_);

        // --- Update Oscillator 1 triangle phase (FR-001) ---
        // Bipolar triangle [-1, +1] traverses 4 units per cycle, so
        // increment = 4 * freq / sampleRate to match target frequency.
        const float osc1Increment = 4.0f * osc1EffFreq / sampleRate_;
        osc1Phase_ += static_cast<float>(osc1Direction_) * osc1Increment;

        if (osc1Phase_ >= 1.0f) {
            osc1Phase_ = 2.0f - osc1Phase_;
            osc1Direction_ = -1;
        }
        if (osc1Phase_ <= -1.0f) {
            osc1Phase_ = -2.0f - osc1Phase_;
            osc1Direction_ = 1;
        }

        // --- Update Oscillator 2 triangle phase (FR-001) ---
        const float osc2Increment = 4.0f * osc2EffFreq / sampleRate_;
        osc2Phase_ += static_cast<float>(osc2Direction_) * osc2Increment;

        if (osc2Phase_ >= 1.0f) {
            osc2Phase_ = 2.0f - osc2Phase_;
            osc2Direction_ = -1;
        }
        if (osc2Phase_ <= -1.0f) {
            osc2Phase_ = -2.0f - osc2Phase_;
            osc2Direction_ = 1;
        }

        // --- Clock the shift register on Osc2 rising edge (FR-006) ---
        const float osc2Triangle = osc2Phase_;
        if (osc2PrevTriangle_ < 0.0f && osc2Triangle >= 0.0f) {
            clockShiftRegister();
        }
        osc2PrevTriangle_ = osc2Triangle;

        // --- Apply CV filter to DAC output (FR-008) ---
        runglerCV_ = cvFilter_.process(rawDacOutput_);

        // --- Flush denormals ---
        osc1Phase_ = detail::flushDenormal(osc1Phase_);
        osc2Phase_ = detail::flushDenormal(osc2Phase_);

        // --- Build output (FR-012) ---
        Output out;
        out.osc1 = osc1Phase_;                                      // Triangle [-1, +1]
        out.osc2 = osc2Phase_;                                      // Triangle [-1, +1]
        out.rungler = runglerCV_;                                    // Filtered DAC [0, +1]
        out.pwm = (osc2Phase_ > osc1Phase_) ? 1.0f : -1.0f;        // PWM comparator (FR-011)
        out.mixed = (osc1Phase_ + osc2Phase_) * 0.5f;               // Mixed (FR-012)

        return out;
    }

    /// @brief Process a block of samples into an Output array.
    /// @param output Array of Output structs (must be at least numSamples long)
    /// @param numSamples Number of samples to process (FR-019)
    void processBlock(Output* output, size_t numSamples) noexcept {
        if (!prepared_) {
            for (size_t i = 0; i < numSamples; ++i) {
                output[i] = Output{};
            }
            return;
        }
        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = process();
        }
    }

    /// @brief Process a block writing only the mixed output.
    /// @param output Float buffer (must be at least numSamples long)
    /// @param numSamples Number of samples to process (FR-019)
    void processBlockMixed(float* output, size_t numSamples) noexcept {
        if (!prepared_) {
            for (size_t i = 0; i < numSamples; ++i) {
                output[i] = 0.0f;
            }
            return;
        }
        for (size_t i = 0; i < numSamples; ++i) {
            const Output out = process();
            output[i] = out.mixed;
        }
    }

    /// @brief Process a block writing only the rungler CV output.
    /// @param output Float buffer (must be at least numSamples long)
    /// @param numSamples Number of samples to process (FR-019)
    void processBlockRungler(float* output, size_t numSamples) noexcept {
        if (!prepared_) {
            for (size_t i = 0; i < numSamples; ++i) {
                output[i] = 0.0f;
            }
            return;
        }
        for (size_t i = 0; i < numSamples; ++i) {
            const Output out = process();
            output[i] = out.rungler;
        }
    }

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// @brief Compute effective frequency with exponential cross-modulation.
    /// @param baseFreq Base frequency in Hz
    /// @param depth Modulation depth [0, 1]
    /// @param cv Rungler CV [0, 1]
    /// @return Effective frequency clamped to [0.1 Hz, Nyquist]
    [[nodiscard]] float computeEffectiveFrequency(
        float baseFreq, float depth, float cv) const noexcept {
        if (depth <= 0.0f) {
            return baseFreq;
        }
        // effectiveFreq = baseFreq * pow(2.0, depth * modulationOctaves * (cv - 0.5))
        // At cv=0.5 -> factor=1 (base freq)
        // At cv=0.0 -> factor = 1/4 (down 2 octaves) when depth=1
        // At cv=1.0 -> factor = 4 (up 2 octaves) when depth=1
        const float exponent = depth * kDefaultModulationOctaves * (cv - 0.5f);
        const float factor = std::pow(2.0f, exponent);
        const float effective = baseFreq * factor;
        const float nyquist = sampleRate_ * 0.5f;
        return std::clamp(effective, kMinFrequency, nyquist);
    }

    /// @brief Clock the shift register and update DAC output.
    /// Called on Osc2 rising edge. (FR-004, FR-005, FR-006, FR-007)
    void clockShiftRegister() noexcept {
        const uint32_t bitCount = static_cast<uint32_t>(runglerBits_);

        // Determine data bit (FR-005)
        uint32_t dataBit = 0;
        const uint32_t lastBit = (registerState_ >> (bitCount - 1u)) & 1u;

        if (loopMode_) {
            // Loop mode: recycle last bit (no XOR)
            dataBit = lastBit;
        } else {
            // Chaos mode: XOR of osc1 pulse and last bit
            const uint32_t osc1Pulse = (osc1Phase_ >= 0.0f) ? 1u : 0u;
            dataBit = osc1Pulse ^ lastBit;
        }

        // Shift register left by 1, insert new data bit at position 0 (FR-004)
        registerState_ = ((registerState_ << 1u) | dataBit) & registerMask_;

        // 3-bit DAC: read bits N-1 (MSB), N-2, N-3 (LSB) (FR-007)
        const uint32_t msb = (registerState_ >> (bitCount - 1u)) & 1u;
        const uint32_t mid = (registerState_ >> (bitCount - 2u)) & 1u;
        const uint32_t lsb = (registerState_ >> (bitCount - 3u)) & 1u;
        rawDacOutput_ = static_cast<float>(msb * 4u + mid * 2u + lsb) / 7.0f;
    }

    /// @brief Update the CV filter cutoff based on the filter amount parameter.
    /// Exponential mapping: cutoff = 5 * pow(Nyquist/5, 1.0 - amount) (FR-008)
    void updateFilterCutoff() noexcept {
        if (sampleRate_ <= 0.0f) return;
        const float nyquist = sampleRate_ * 0.5f;
        const float cutoff = kMinFilterCutoff *
            std::pow(nyquist / kMinFilterCutoff, 1.0f - filterAmount_);
        cvFilter_.setCutoff(cutoff);
    }

    // =========================================================================
    // Configuration State (persisted across reset)
    // =========================================================================

    float osc1BaseFreq_ = kDefaultOsc1Freq;     ///< Oscillator 1 base frequency
    float osc2BaseFreq_ = kDefaultOsc2Freq;     ///< Oscillator 2 base frequency
    float osc1RunglerDepth_ = 0.0f;             ///< Osc1 modulation depth [0, 1]
    float osc2RunglerDepth_ = 0.0f;             ///< Osc2 modulation depth [0, 1]
    float filterAmount_ = 0.0f;                 ///< CV smoothing filter amount [0, 1]
    bool loopMode_ = false;                      ///< Chaos (false) or loop (true) mode
    size_t runglerBits_ = kDefaultBits;          ///< Shift register length in bits

    // =========================================================================
    // Processing State (reset on prepare/reset)
    // =========================================================================

    float osc1Phase_ = 0.0f;                    ///< Oscillator 1 triangle phase [-1, +1]
    int osc1Direction_ = 1;                      ///< Oscillator 1 ramp direction (+1 or -1)
    float osc2Phase_ = 0.0f;                    ///< Oscillator 2 triangle phase [-1, +1]
    int osc2Direction_ = 1;                      ///< Oscillator 2 ramp direction (+1 or -1)
    float osc2PrevTriangle_ = 0.0f;             ///< Previous osc2 triangle for edge detection
    uint32_t registerState_ = 0;                 ///< Shift register bit state
    float runglerCV_ = 0.0f;                    ///< Current filtered Rungler CV [0, 1]
    float rawDacOutput_ = 0.0f;                 ///< Unfiltered DAC output [0, 1]

    // =========================================================================
    // Derived State
    // =========================================================================

    float sampleRate_ = 0.0f;                   ///< Stored sample rate
    uint32_t registerMask_ = (1u << kDefaultBits) - 1u; ///< Bitmask for register length
    bool prepared_ = false;                      ///< Whether prepare() has been called

    // =========================================================================
    // Internal Components
    // =========================================================================

    OnePoleLP cvFilter_;                         ///< CV smoothing filter (Layer 1)
    Xorshift32 rng_{1};                          ///< PRNG for shift register seeding (Layer 0)
};

} // namespace DSP
} // namespace Krate
