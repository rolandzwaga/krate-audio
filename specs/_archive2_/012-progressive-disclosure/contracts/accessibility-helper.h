// ==============================================================================
// AccessibilityHelper Contract
// ==============================================================================
// Cross-platform accessibility detection for OS-level preferences.
// Placed in plugins/shared/ for reuse by Iterum and future plugins.
//
// FR-024/FR-025: High contrast detection per platform
// FR-027: Reduced motion detection per platform
// FR-025b: Windows - SystemParametersInfo(SPI_GETHIGHCONTRAST)
// FR-025c: macOS - NSWorkspace accessibilityDisplayShouldIncreaseContrast
// FR-025d: Linux - GTK/GSettings best-effort detection
//
// Constitution Principle VI: Platform-specific code allowed for accessibility
// detection with #ifdef guards and graceful fallbacks.
// ==============================================================================

#pragma once

#include <cstdint>

namespace Krate::Plugins {

/// High contrast color palette queried from the operating system
struct HighContrastColors {
    uint32_t foreground = 0xFFFFFFFF;   // Text color (ARGB)
    uint32_t background = 0xFF1E1E1E;   // Background color (ARGB)
    uint32_t accent     = 0xFF3A96DD;   // Accent/highlight color (ARGB)
    uint32_t border     = 0xFFFFFFFF;   // Border color (ARGB)
    uint32_t disabled   = 0xFF6B6B6B;   // Disabled element color (ARGB)
};

/// Accessibility preferences detected from the operating system
struct AccessibilityPreferences {
    bool highContrastEnabled = false;
    bool reducedMotionPreferred = false;
    HighContrastColors colors;
};

/// Query the operating system for accessibility preferences.
/// Call this once per editor open and cache the result.
/// Thread-safe: Can be called from any thread (makes OS API calls).
///
/// @return AccessibilityPreferences with current OS settings
AccessibilityPreferences queryAccessibilityPreferences();

/// Check if high contrast mode is currently enabled.
/// Convenience wrapper around queryAccessibilityPreferences().
/// @return true if the OS has high contrast mode active
bool isHighContrastEnabled();

/// Check if reduced motion is currently preferred.
/// Convenience wrapper around queryAccessibilityPreferences().
/// @return true if the OS indicates reduced motion should be used
bool isReducedMotionPreferred();

} // namespace Krate::Plugins
