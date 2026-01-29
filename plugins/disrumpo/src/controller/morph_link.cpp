// ==============================================================================
// Morph Link Mode Implementation
// ==============================================================================
// T158: Link mode mapping functions for morph-sweep integration (US8, FR-032-034)
//
// Reference: specs/006-morph-ui/plan.md "Morph Link Mode Equations"
// ==============================================================================

#include "morph_link.h"

#include <algorithm>
#include <cmath>

namespace Disrumpo {

// =============================================================================
// Constants
// =============================================================================

static constexpr float kMinFrequencyHz = 20.0f;
static constexpr float kMaxFrequencyHz = 20000.0f;

// =============================================================================
// Link Mode Mapping Functions (FR-034a-e)
// =============================================================================

float applyMorphLinkMode(MorphLinkMode mode, float sweepNorm, float manualPosition) {
    // Clamp sweep to valid range
    sweepNorm = std::clamp(sweepNorm, 0.0f, 1.0f);

    switch (mode) {
        case MorphLinkMode::None:
            // No link - return manual position unchanged
            return manualPosition;

        case MorphLinkMode::SweepFreq:
            // FR-034: Linear mapping (low freq = 0, high freq = 1)
            return sweepNorm;

        case MorphLinkMode::InverseSweep:
            // FR-034a: Inverted mapping (high freq = 0, low freq = 1)
            return 1.0f - sweepNorm;

        case MorphLinkMode::EaseIn:
            // FR-034b: Exponential curve emphasizing low frequencies
            // sqrt(x) gives more range in bass (0-0.3 of sweep -> 0-0.55 of morph)
            return std::sqrt(sweepNorm);

        case MorphLinkMode::EaseOut:
            // FR-034c: Exponential curve emphasizing high frequencies
            // x^2 gives more range in highs (0.7-1.0 of sweep -> 0.49-1.0 of morph)
            return sweepNorm * sweepNorm;

        case MorphLinkMode::HoldRise:
            // FR-034d: Hold at 0 until midpoint, then rise linearly to 1
            if (sweepNorm < 0.5f) {
                return 0.0f;
            }
            return (sweepNorm - 0.5f) * 2.0f;

        case MorphLinkMode::Stepped:
            // FR-034e: Quantize to 5 discrete steps (0, 0.25, 0.5, 0.75, 1.0)
            // floor(x * 5) / 4, but clamp the result to ensure it doesn't exceed 1.0
            // Input ranges: [0, 0.2)->0, [0.2, 0.4)->0.25, [0.4, 0.6)->0.5, [0.6, 0.8)->0.75, [0.8, 1.0]->1.0
            return std::min(std::floor(sweepNorm * 5.0f) / 4.0f, 1.0f);

        default:
            return manualPosition;
    }
}

// =============================================================================
// Frequency Conversion Functions
// =============================================================================

float sweepFrequencyToNormalized(float frequencyHz) {
    // Clamp to valid frequency range
    frequencyHz = std::clamp(frequencyHz, kMinFrequencyHz, kMaxFrequencyHz);

    // Convert using log scale
    // Formula: log(freq/minFreq) / log(maxFreq/minFreq)
    float logMin = std::log(kMinFrequencyHz);
    float logMax = std::log(kMaxFrequencyHz);
    float logFreq = std::log(frequencyHz);

    return (logFreq - logMin) / (logMax - logMin);
}

float normalizedToSweepFrequency(float normalized) {
    // Clamp to valid range
    normalized = std::clamp(normalized, 0.0f, 1.0f);

    // Convert from normalized to frequency using log scale
    // Formula: minFreq * exp(normalized * log(maxFreq/minFreq))
    float logMin = std::log(kMinFrequencyHz);
    float logMax = std::log(kMaxFrequencyHz);

    return std::exp(logMin + normalized * (logMax - logMin));
}

} // namespace Disrumpo
