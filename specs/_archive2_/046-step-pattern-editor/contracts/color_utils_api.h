// ==============================================================================
// ColorUtils API Contract (046-step-pattern-editor)
// ==============================================================================
// Shared color utility functions extracted from ArcKnob and FieldsetContainer.
// This file documents the public API. NOT compiled.
// ==============================================================================

#pragma once

#include "vstgui/lib/ccolor.h"
#include <algorithm>
#include <cstdint>

namespace Krate::Plugins {

/// @brief Linearly interpolate between two colors.
/// @param a Start color (t=0)
/// @param b End color (t=1)
/// @param t Interpolation factor [0.0, 1.0]
/// @return Interpolated color
[[nodiscard]] inline VSTGUI::CColor lerpColor(
    const VSTGUI::CColor& a, const VSTGUI::CColor& b, float t) {
    return VSTGUI::CColor(
        static_cast<uint8_t>(a.red + (b.red - a.red) * t),
        static_cast<uint8_t>(a.green + (b.green - a.green) * t),
        static_cast<uint8_t>(a.blue + (b.blue - a.blue) * t),
        static_cast<uint8_t>(a.alpha + (b.alpha - a.alpha) * t));
}

/// @brief Darken a color by a factor.
/// @param color Input color
/// @param factor Darkening factor (0 = black, 1 = unchanged)
/// @return Darkened color
[[nodiscard]] inline VSTGUI::CColor darkenColor(
    const VSTGUI::CColor& color, float factor) {
    return VSTGUI::CColor(
        static_cast<uint8_t>(color.red * factor),
        static_cast<uint8_t>(color.green * factor),
        static_cast<uint8_t>(color.blue * factor),
        color.alpha);
}

/// @brief Brighten a color by a factor.
/// @param color Input color
/// @param factor Brightening factor (1 = unchanged, >1 = brighter)
/// @return Brightened color (clamped to 255)
[[nodiscard]] inline VSTGUI::CColor brightenColor(
    const VSTGUI::CColor& color, float factor) {
    return VSTGUI::CColor(
        static_cast<uint8_t>(std::min(255.0f, color.red * factor)),
        static_cast<uint8_t>(std::min(255.0f, color.green * factor)),
        static_cast<uint8_t>(std::min(255.0f, color.blue * factor)),
        color.alpha);
}

} // namespace Krate::Plugins
