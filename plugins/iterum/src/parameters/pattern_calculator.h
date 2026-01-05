// ==============================================================================
// Pattern Calculator
// ==============================================================================
// Utility for calculating timing pattern ratios in the controller.
// Used by "Copy to Custom" feature (Spec 046 - User Story 4).
//
// This mirrors the DSP pattern calculation logic but outputs normalized
// ratios (0-1) instead of absolute times (ms).
// ==============================================================================

#pragma once

#include <array>
#include <cmath>
#include <algorithm>

namespace Iterum {

/// Maximum number of taps in patterns
static constexpr size_t kPatternMaxTaps = 16;

/// Golden ratio constant (same as DSP)
static constexpr float kGoldenRatio = 1.6180339887498948f;

/// Timing pattern indices (must match DSP TimingPattern enum)
enum class PatternIndex : int {
    // Rhythmic patterns (0-13) - use evenly spaced taps
    WholeNote = 0,
    HalfNote = 1,
    QuarterNote = 2,
    EighthNote = 3,
    SixteenthNote = 4,
    ThirtySecondNote = 5,
    DottedHalf = 6,
    DottedQuarter = 7,
    DottedEighth = 8,
    DottedSixteenth = 9,
    TripletHalf = 10,
    TripletQuarter = 11,
    TripletEighth = 12,
    TripletSixteenth = 13,

    // Mathematical patterns (14-18) - special formulas
    GoldenRatio = 14,
    Fibonacci = 15,
    Exponential = 16,
    PrimeNumbers = 17,
    LinearSpread = 18,

    // Custom pattern (19) - user-defined
    Custom = 19
};

/// Calculate time ratios for a pattern
/// @param patternIndex The pattern type (0-19)
/// @param tapCount Number of taps (2-16)
/// @param outRatios Output array for time ratios (must have at least tapCount elements)
/// @note Ratios are normalized to [0, 1] where 1 = last tap position
inline void calculatePatternRatios(
    int patternIndex,
    size_t tapCount,
    float* outRatios) noexcept {

    if (tapCount == 0 || tapCount > kPatternMaxTaps || !outRatios) return;

    // First, calculate raw times (using arbitrary base unit of 1.0)
    std::array<float, kPatternMaxTaps> times{};

    PatternIndex pattern = static_cast<PatternIndex>(patternIndex);

    switch (pattern) {
        // Mathematical patterns
        case PatternIndex::GoldenRatio: {
            // Each tap = previous * golden ratio
            times[0] = 1.0f;
            for (size_t i = 1; i < tapCount; ++i) {
                times[i] = times[i - 1] * kGoldenRatio;
            }
            break;
        }

        case PatternIndex::Fibonacci: {
            // Fibonacci sequence: 1, 1, 2, 3, 5, 8, 13, 21, 34, 55...
            times[0] = 1.0f;
            if (tapCount > 1) times[1] = 1.0f;
            for (size_t i = 2; i < tapCount; ++i) {
                times[i] = times[i - 1] + times[i - 2];
            }
            break;
        }

        case PatternIndex::Exponential: {
            // Powers of 2: 1, 2, 4, 8, 16, 32...
            for (size_t i = 0; i < tapCount; ++i) {
                times[i] = std::pow(2.0f, static_cast<float>(i));
            }
            break;
        }

        case PatternIndex::PrimeNumbers: {
            // Prime number positions: 2, 3, 5, 7, 11, 13, 17, 19, 23, 29...
            static const int primes[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53};
            for (size_t i = 0; i < tapCount && i < 16; ++i) {
                times[i] = static_cast<float>(primes[i]);
            }
            break;
        }

        case PatternIndex::LinearSpread:
        case PatternIndex::Custom:
        default: {
            // Linear spread: evenly spaced taps (1, 2, 3, 4...)
            // Also used for rhythmic patterns (they all have evenly spaced taps)
            for (size_t i = 0; i < tapCount; ++i) {
                times[i] = static_cast<float>(i + 1);
            }
            break;
        }
    }

    // Normalize to [0, 1] range
    // The last tap is at ratio 1.0, first tap at ratio (1/maxTime)
    float maxTime = times[tapCount - 1];
    if (maxTime <= 0.0f) maxTime = 1.0f;

    for (size_t i = 0; i < tapCount; ++i) {
        outRatios[i] = std::clamp(times[i] / maxTime, 0.0f, 1.0f);
    }
}

/// Get default level for a tap in a pattern
/// @param patternIndex The spatial pattern index (0-6)
/// @param tapIndex The tap index
/// @param tapCount Total number of taps
/// @return Level ratio [0, 1]
inline float getPatternLevel(
    [[maybe_unused]] int patternIndex,
    [[maybe_unused]] size_t tapIndex,
    [[maybe_unused]] size_t tapCount) noexcept {

    // For now, return full level for all taps
    // Spatial patterns affect pan, not level (except Decaying)
    return 1.0f;
}

} // namespace Iterum
