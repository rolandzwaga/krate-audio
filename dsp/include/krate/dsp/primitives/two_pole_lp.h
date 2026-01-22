// ==============================================================================
// Layer 1: DSP Primitive - Two-Pole Lowpass Filter
// ==============================================================================
// Butterworth lowpass filter wrapper around Biquad with 12dB/oct slope.
// Designed for excitation filtering and brightness control in physical models.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (RAII, value semantics, C++20)
// - Principle IX: Layer 1 (depends only on Layer 0 and other Layer 1 primitives)
// - Principle XII: Test-First Development
//
// Reference: specs/084-karplus-strong/spec.md (FR-014: 12dB/oct brightness)
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/biquad.h>

#include <cstddef>

namespace Krate {
namespace DSP {

/// @brief Two-pole (12dB/octave) Butterworth lowpass filter.
///
/// Provides a smooth, maximally flat passband response suitable for:
/// - Excitation filtering (brightness control in Karplus-Strong synthesis)
/// - Tone shaping in physical models
/// - General purpose lowpass filtering with moderate slope
///
/// Uses a Biquad internally configured as Butterworth lowpass (Q = 0.7071).
///
/// @note Call prepare() before processing; filter returns input unchanged if unprepared
/// @note NaN/Inf inputs are handled by the underlying Biquad (returns 0, resets state)
/// @note All processing methods are noexcept
///
/// @example
/// ```cpp
/// TwoPoleLP filter;
/// filter.prepare(44100.0);
/// filter.setCutoff(2000.0f);  // 2kHz cutoff
///
/// // Process samples
/// for (auto& sample : buffer) {
///     sample = filter.process(sample);
/// }
/// ```
class TwoPoleLP {
public:
    /// @brief Default constructor.
    /// Filter starts in unprepared state.
    TwoPoleLP() noexcept = default;

    /// @brief Prepare the filter for processing.
    /// @param sampleRate Sample rate in Hz (must be > 0)
    /// @note Must be called before processing; reconfigures the internal biquad
    void prepare(double sampleRate) noexcept {
        sampleRate_ = (sampleRate > 0.0) ? sampleRate : 44100.0;
        prepared_ = true;
        updateCoefficients();
    }

    /// @brief Set the cutoff frequency.
    /// @param hz Cutoff frequency in Hz
    /// @note Clamps to [1.0, Nyquist * 0.495] range (same as Biquad)
    void setCutoff(float hz) noexcept {
        cutoffHz_ = hz;
        if (prepared_) {
            updateCoefficients();
        }
    }

    /// @brief Get the current cutoff frequency.
    /// @return Cutoff frequency in Hz
    [[nodiscard]] float getCutoff() const noexcept {
        return cutoffHz_;
    }

    /// @brief Process a single sample.
    /// @param input Input sample
    /// @return Filtered output sample
    /// @note Returns input unchanged if prepare() not called
    [[nodiscard]] float process(float input) noexcept {
        if (!prepared_) {
            return input;
        }
        return filter_.process(input);
    }

    /// @brief Process a block of samples in-place.
    /// @param buffer Pointer to sample buffer (modified in-place)
    /// @param numSamples Number of samples to process
    void processBlock(float* buffer, size_t numSamples) noexcept {
        if (!prepared_) {
            return;  // Leave buffer unchanged
        }
        filter_.processBlock(buffer, numSamples);
    }

    /// @brief Reset filter state.
    /// Clears internal state without changing cutoff or sample rate.
    void reset() noexcept {
        filter_.reset();
    }

private:
    /// @brief Update biquad coefficients for current cutoff and sample rate.
    void updateCoefficients() noexcept {
        // Configure as Butterworth lowpass (Q = 0.7071 for maximally flat passband)
        filter_.configure(
            FilterType::Lowpass,
            cutoffHz_,
            kButterworthQ,
            0.0f,  // gainDb not used for lowpass
            static_cast<float>(sampleRate_)
        );
    }

    Biquad filter_;               ///< Internal biquad filter
    float cutoffHz_ = 1000.0f;    ///< Current cutoff frequency
    double sampleRate_ = 44100.0; ///< Current sample rate
    bool prepared_ = false;       ///< True after prepare() called
};

} // namespace DSP
} // namespace Krate
