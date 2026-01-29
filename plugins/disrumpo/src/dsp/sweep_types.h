// ==============================================================================
// Sweep Types
// ==============================================================================
// Enumeration types for the Sweep System.
//
// Reference: specs/007-sweep-system/spec.md
// ==============================================================================

#pragma once

#include <cstdint>

namespace Disrumpo {

/// @brief Defines the intensity falloff shape for sweep processing.
///
/// Sharp mode uses linear falloff with exactly 0 at the edge.
/// Smooth mode uses Gaussian (normal distribution) falloff.
enum class SweepFalloff : uint8_t {
    Sharp = 0,   ///< Linear falloff, exactly 0 at edge
    Smooth = 1   ///< Gaussian falloff
};

/// @brief Number of sweep falloff modes.
constexpr int kSweepFalloffCount = 2;

/// @brief Get display name for a sweep falloff mode.
/// @param mode The sweep falloff mode
/// @return C-string display name
constexpr const char* getSweepFalloffName(SweepFalloff mode) noexcept {
    switch (mode) {
        case SweepFalloff::Sharp:  return "Sharp";
        case SweepFalloff::Smooth: return "Smooth";
        default:                   return "Unknown";
    }
}

} // namespace Disrumpo
