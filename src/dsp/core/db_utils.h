// ==============================================================================
// Layer 0: Core Utilities
// db_utils.h - dB/Linear Conversion Functions
// ==============================================================================
// Constitution Principle II: Real-Time Audio Thread Safety
// - No allocation, no locks, no exceptions, no I/O
//
// Constitution Principle III: Modern C++ Standards
// - constexpr, const, value semantics
//
// Constitution Principle IX: Layered DSP Architecture
// - Layer 0: NO dependencies on higher layers
// ==============================================================================

#pragma once

#include <cmath>

namespace Iterum {
namespace DSP {

// ==============================================================================
// Constants
// ==============================================================================

/// Floor value for silence/zero gain in decibels.
/// Represents approximately 24-bit dynamic range (6.02 dB/bit * 24 = ~144 dB).
/// Used as the return value when gain is zero, negative, or NaN.
constexpr float kSilenceFloorDb = -144.0f;

// ==============================================================================
// Functions
// ==============================================================================

/// Convert decibels to linear gain.
///
/// @param dB  Decibel value (any finite float)
/// @return    Linear gain multiplier (>= 0)
///
/// @formula   gain = 10^(dB/20)
///
/// @note      Real-time safe: no allocation, no exceptions
/// @note      Constexpr: usable at compile time (C++20)
/// @note      NaN input returns 0.0f
///
/// @example   dbToGain(0.0f)    -> 1.0f     (unity gain)
/// @example   dbToGain(-6.02f)  -> ~0.5f    (half amplitude)
/// @example   dbToGain(-20.0f)  -> 0.1f     (-20 dB)
/// @example   dbToGain(+20.0f)  -> 10.0f    (+20 dB)
///
[[nodiscard]] constexpr float dbToGain(float dB) noexcept {
    // NaN check: NaN != NaN (works in constexpr context unlike std::isnan)
    if (dB != dB) {
        return 0.0f;
    }
    return std::pow(10.0f, dB / 20.0f);
}

/// Convert linear gain to decibels.
///
/// @param gain  Linear gain value
/// @return      Decibel value (clamped to kSilenceFloorDb minimum)
///
/// @formula     dB = 20 * log10(gain), clamped to floor for invalid inputs
///
/// @note        Real-time safe: no allocation, no exceptions
/// @note        Constexpr: usable at compile time (C++20)
/// @note        Zero/negative/NaN input returns kSilenceFloorDb (-144 dB)
///
/// @example     gainToDb(1.0f)   -> 0.0f      (unity = 0 dB)
/// @example     gainToDb(0.5f)   -> ~-6.02f   (half amplitude)
/// @example     gainToDb(0.0f)   -> -144.0f   (silence floor)
/// @example     gainToDb(-1.0f)  -> -144.0f   (invalid -> floor)
///
[[nodiscard]] constexpr float gainToDb(float gain) noexcept {
    // NaN or non-positive check
    if (gain != gain || gain <= 0.0f) {
        return kSilenceFloorDb;
    }
    float result = 20.0f * std::log10(gain);
    return (result < kSilenceFloorDb) ? kSilenceFloorDb : result;
}

} // namespace DSP
} // namespace Iterum
