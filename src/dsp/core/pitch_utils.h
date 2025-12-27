// Layer 0: Core Utility - Pitch Conversion
// Part of Granular Delay feature (spec 034)
#pragma once

#include <cmath>
#include <cstdint>

namespace Iterum::DSP {

/// Constant for pitch conversion: 2^(1/12)
inline constexpr float kSemitoneRatio = 1.0594630943592953f;

/// Convert semitones to playback rate ratio
/// +12 semitones = 2.0 (octave up), -12 = 0.5 (octave down), 0 = 1.0
/// @param semitones Pitch offset in semitones (-24 to +24 typical range)
/// @return Playback rate ratio (e.g., 2.0 for octave up)
[[nodiscard]] inline float semitonesToRatio(float semitones) noexcept {
    // ratio = 2^(semitones/12)
    return std::pow(2.0f, semitones / 12.0f);
}

/// Convert playback rate ratio to semitones
/// @param ratio Playback rate (e.g., 2.0 for octave up, 0.5 for octave down)
/// @return Pitch offset in semitones
[[nodiscard]] inline float ratioToSemitones(float ratio) noexcept {
    // semitones = 12 * log2(ratio)
    if (ratio <= 0.0f) {
        return 0.0f;  // Invalid ratio, return neutral
    }
    return 12.0f * std::log2(ratio);
}

}  // namespace Iterum::DSP
