// ==============================================================================
// Layer 1: DSP Primitive - Pink Noise Filter
// ==============================================================================
// Paul Kellet's pink noise filter for converting white noise to pink noise.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, value semantics)
// - Principle IX: Layer 1 (depends only on Layer 0)
// - Principle XII: Test-First Development
//
// Reference: https://www.firstpr.com.au/dsp/pink-noise/
// Spec: specs/023-noise-oscillator/spec.md (RF-001, RF-002)
// ==============================================================================

#pragma once

namespace Krate {
namespace DSP {

/// @brief Paul Kellet's pink noise filter.
///
/// Converts white noise to pink noise (-3dB/octave spectral rolloff).
/// Uses a 7-state recursive filter for excellent accuracy with minimal CPU.
///
/// @par Algorithm
/// Filter coefficients from Paul Kellet's "pink noise generation" article.
/// Accuracy: +/- 0.05dB from 9.2Hz to Nyquist at 44.1kHz.
/// The recursive filter structure means coefficients work across all sample
/// rates in the audible range (44.1kHz-192kHz).
///
/// @par Reference
/// https://www.firstpr.com.au/dsp/pink-noise/
///
/// @par Layer
/// Layer 1 (primitives/) - depends only on Layer 0
///
/// @par Real-Time Safety
/// process() is fully real-time safe (noexcept, no allocation)
///
/// @par Usage
/// @code
/// PinkNoiseFilter filter;
/// Xorshift32 rng(12345);
///
/// for (size_t i = 0; i < numSamples; ++i) {
///     float white = rng.nextFloat();  // White noise in [-1, 1]
///     float pink = filter.process(white);  // Pink noise in [-1, 1]
///     output[i] = pink;
/// }
/// @endcode
class PinkNoiseFilter {
public:
    /// @brief Process one white noise sample through the filter.
    ///
    /// Applies Paul Kellet's 7-stage recursive filter to convert white noise
    /// to pink noise (-3dB/octave slope).
    ///
    /// @param white Input white noise sample (typically [-1, 1])
    /// @return Pink noise sample (bounded to [-1, 1])
    ///
    /// @note RF-002: Exact Paul Kellet coefficients preserved
    [[nodiscard]] float process(float white) noexcept {
        // Paul Kellet's filter coefficients (RF-002: exact coefficients preserved)
        b0_ = 0.99886f * b0_ + white * 0.0555179f;
        b1_ = 0.99332f * b1_ + white * 0.0750759f;
        b2_ = 0.96900f * b2_ + white * 0.1538520f;
        b3_ = 0.86650f * b3_ + white * 0.3104856f;
        b4_ = 0.55000f * b4_ + white * 0.5329522f;
        b5_ = -0.7616f * b5_ - white * 0.0168980f;

        float pink = b0_ + b1_ + b2_ + b3_ + b4_ + b5_ + b6_ + white * 0.5362f;
        b6_ = white * 0.115926f;

        // Normalize output to stay within [-1, 1] range
        // The filter has peak gain of approximately 5.0, so we use a conservative factor
        // and clamp to ensure we never exceed the range
        // (RF-002: exact normalization factor 0.2f preserved)
        float normalized = pink * 0.2f;
        return (normalized > 1.0f) ? 1.0f : ((normalized < -1.0f) ? -1.0f : normalized);
    }

    /// @brief Reset filter state to zero.
    ///
    /// Clears all internal state variables, causing the filter to restart
    /// from a clean state. Useful when starting a new noise sequence or
    /// when switching noise colors.
    void reset() noexcept {
        b0_ = b1_ = b2_ = b3_ = b4_ = b5_ = b6_ = 0.0f;
    }

private:
    float b0_ = 0.0f;
    float b1_ = 0.0f;
    float b2_ = 0.0f;
    float b3_ = 0.0f;
    float b4_ = 0.0f;
    float b5_ = 0.0f;
    float b6_ = 0.0f;
};

} // namespace DSP
} // namespace Krate
