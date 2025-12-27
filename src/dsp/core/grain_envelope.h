// Layer 0: Core Utility - Grain Envelope Tables
// Part of Granular Delay feature (spec 034)
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Iterum::DSP {

/// Grain envelope types for granular synthesis
enum class GrainEnvelopeType : uint8_t {
    Hann,       ///< Raised cosine (smooth, general purpose)
    Trapezoid,  ///< Attack-sustain-decay (preserves transients)
    Sine,       ///< Half-cosine (better for pitch shifting)
    Blackman    ///< Low sidelobe (less coloration)
};

namespace GrainEnvelope {

/// Mathematical constant
inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kTwoPi = 6.28318530718f;

/// Pre-compute envelope lookup table (call in prepare, not process)
/// @param output Pre-allocated buffer for envelope values
/// @param size Number of samples in the envelope
/// @param type Envelope shape to generate
/// @param attackRatio Attack time as ratio of total length (for Trapezoid)
/// @param releaseRatio Release time as ratio of total length (for Trapezoid)
inline void generate(float* output, size_t size, GrainEnvelopeType type,
                     float attackRatio = 0.1f, float releaseRatio = 0.1f) noexcept {
    if (output == nullptr || size == 0) {
        return;
    }

    // Use (size - 1) as denominator to ensure phase goes from 0 to 1 exactly
    // This ensures symmetry and that first/last samples are at endpoints
    const auto sizeM1 = static_cast<float>(size - 1);
    const auto sizeFloat = static_cast<float>(size);

    switch (type) {
        case GrainEnvelopeType::Hann:
            // Raised cosine: 0.5 * (1 - cos(2*pi*n/(N-1)))
            for (size_t i = 0; i < size; ++i) {
                const float phase = static_cast<float>(i) / sizeM1;
                output[i] = 0.5f * (1.0f - std::cos(kTwoPi * phase));
            }
            break;

        case GrainEnvelopeType::Trapezoid: {
            // Attack-sustain-decay envelope
            const size_t attackSamples =
                static_cast<size_t>(sizeFloat * std::clamp(attackRatio, 0.0f, 0.5f));
            const size_t releaseSamples =
                static_cast<size_t>(sizeFloat * std::clamp(releaseRatio, 0.0f, 0.5f));
            const size_t sustainEnd = size - releaseSamples;

            for (size_t i = 0; i < size; ++i) {
                if (i < attackSamples && attackSamples > 0) {
                    // Attack ramp
                    output[i] = static_cast<float>(i) / static_cast<float>(attackSamples);
                } else if (i >= sustainEnd && releaseSamples > 0) {
                    // Release ramp
                    output[i] = static_cast<float>(size - 1 - i) /
                                static_cast<float>(releaseSamples);
                } else {
                    // Sustain
                    output[i] = 1.0f;
                }
            }
            break;
        }

        case GrainEnvelopeType::Sine:
            // Half-sine (better for pitch shifting)
            for (size_t i = 0; i < size; ++i) {
                const float phase = static_cast<float>(i) / sizeM1;
                output[i] = std::sin(kPi * phase);
            }
            break;

        case GrainEnvelopeType::Blackman:
            // Blackman window: low sidelobe, less coloration
            // 0.42 - 0.5*cos(2*pi*n/(N-1)) + 0.08*cos(4*pi*n/(N-1))
            for (size_t i = 0; i < size; ++i) {
                const float phase = static_cast<float>(i) / sizeM1;
                float value = 0.42f - 0.5f * std::cos(kTwoPi * phase) +
                              0.08f * std::cos(2.0f * kTwoPi * phase);
                // Clamp small negative values to zero (floating point precision)
                output[i] = std::max(0.0f, value);
            }
            break;
    }
}

/// Get envelope value at normalized phase [0, 1] with linear interpolation
/// @param table Pre-computed envelope lookup table
/// @param tableSize Size of the lookup table
/// @param phase Normalized position in envelope [0.0, 1.0]
/// @return Interpolated envelope amplitude
[[nodiscard]] inline float lookup(const float* table, size_t tableSize,
                                  float phase) noexcept {
    if (table == nullptr || tableSize == 0) {
        return 0.0f;
    }

    // Clamp phase to valid range
    phase = std::clamp(phase, 0.0f, 1.0f);

    // Calculate fractional index
    const float indexFloat = phase * static_cast<float>(tableSize - 1);
    const auto index0 = static_cast<size_t>(indexFloat);
    const size_t index1 = std::min(index0 + 1, tableSize - 1);
    const float frac = indexFloat - static_cast<float>(index0);

    // Linear interpolation
    return table[index0] + frac * (table[index1] - table[index0]);
}

}  // namespace GrainEnvelope

}  // namespace Iterum::DSP
