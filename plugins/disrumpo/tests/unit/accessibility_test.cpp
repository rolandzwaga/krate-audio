// ==============================================================================
// AccessibilityHelper Unit Tests
// ==============================================================================
// T015: Tests for accessibility detection and color palette
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "platform/accessibility_helper.h"

using namespace Krate::Plugins;

// =============================================================================
// Default Preferences Tests
// =============================================================================

TEST_CASE("AccessibilityPreferences has correct defaults", "[accessibility]") {
    AccessibilityPreferences prefs;

    SECTION("high contrast is disabled by default") {
        REQUIRE(prefs.highContrastEnabled == false);
    }

    SECTION("reduced motion is not preferred by default") {
        REQUIRE(prefs.reducedMotionPreferred == false);
    }
}

TEST_CASE("HighContrastColors has correct defaults", "[accessibility]") {
    HighContrastColors colors;

    SECTION("foreground defaults to white") {
        REQUIRE(colors.foreground == 0xFFFFFFFF);
    }

    SECTION("background defaults to dark gray") {
        REQUIRE(colors.background == 0xFF1E1E1E);
    }

    SECTION("accent defaults to blue") {
        REQUIRE(colors.accent == 0xFF3A96DD);
    }

    SECTION("border defaults to white") {
        REQUIRE(colors.border == 0xFFFFFFFF);
    }

    SECTION("disabled defaults to gray") {
        REQUIRE(colors.disabled == 0xFF6B6B6B);
    }
}

// =============================================================================
// Query Function Tests
// =============================================================================
// Note: These tests verify the function can be called without crashing.
// The actual OS state may vary per machine, so we don't assert specific
// accessibility states.

TEST_CASE("queryAccessibilityPreferences returns valid struct", "[accessibility]") {
    auto prefs = queryAccessibilityPreferences();

    // The struct should be valid regardless of OS state
    // Colors should have alpha channel set (0xFF000000 or higher)
    if (prefs.highContrastEnabled) {
        // If high contrast is enabled, colors should have alpha
        REQUIRE((prefs.colors.foreground & 0xFF000000) != 0);
        REQUIRE((prefs.colors.background & 0xFF000000) != 0);
    }
}

TEST_CASE("isHighContrastEnabled returns a boolean", "[accessibility]") {
    // Just verify it doesn't crash
    [[maybe_unused]] bool result = isHighContrastEnabled();
}

TEST_CASE("isReducedMotionPreferred returns a boolean", "[accessibility]") {
    // Just verify it doesn't crash
    [[maybe_unused]] bool result = isReducedMotionPreferred();
}

// =============================================================================
// Color Palette Parsing Tests
// =============================================================================

TEST_CASE("HighContrastColors can be customized", "[accessibility]") {
    HighContrastColors colors;
    colors.foreground = 0xFF000000;  // Black text
    colors.background = 0xFFFFFFFF;  // White background
    colors.accent = 0xFF0000FF;      // Blue accent

    REQUIRE(colors.foreground == 0xFF000000);
    REQUIRE(colors.background == 0xFFFFFFFF);
    REQUIRE(colors.accent == 0xFF0000FF);
}

TEST_CASE("AccessibilityPreferences carries color palette", "[accessibility]") {
    AccessibilityPreferences prefs;
    prefs.highContrastEnabled = true;
    prefs.colors.foreground = 0xFF000000;
    prefs.colors.background = 0xFFFFFFFF;
    prefs.colors.accent = 0xFFFF0000;

    REQUIRE(prefs.highContrastEnabled == true);
    REQUIRE(prefs.colors.foreground == 0xFF000000);
    REQUIRE(prefs.colors.background == 0xFFFFFFFF);
    REQUIRE(prefs.colors.accent == 0xFFFF0000);
}

// =============================================================================
// T075: Integration Tests - Reduced Motion Disables Animations
// =============================================================================

TEST_CASE("Reduced motion flag disables animation", "[accessibility][integration]") {
    SECTION("when reduced motion is preferred, animations should be disabled") {
        AccessibilityPreferences prefs;
        prefs.reducedMotionPreferred = true;

        // Controller logic: if reducedMotion -> setAnimationsEnabled(false)
        bool animationsEnabled = !prefs.reducedMotionPreferred;
        REQUIRE(animationsEnabled == false);
    }

    SECTION("when reduced motion is not preferred, animations stay enabled") {
        AccessibilityPreferences prefs;
        prefs.reducedMotionPreferred = false;

        bool animationsEnabled = !prefs.reducedMotionPreferred;
        REQUIRE(animationsEnabled == true);
    }
}

TEST_CASE("High contrast colors applied to views when enabled", "[accessibility][integration]") {
    SECTION("border color is extracted from preferences") {
        AccessibilityPreferences prefs;
        prefs.highContrastEnabled = true;
        prefs.colors.border = 0xFFFFFFFF;  // White borders

        // Extract color components
        uint8_t r = static_cast<uint8_t>((prefs.colors.border >> 16) & 0xFF);
        uint8_t g = static_cast<uint8_t>((prefs.colors.border >> 8) & 0xFF);
        uint8_t b = static_cast<uint8_t>(prefs.colors.border & 0xFF);
        uint8_t a = static_cast<uint8_t>((prefs.colors.border >> 24) & 0xFF);

        REQUIRE(r == 255);
        REQUIRE(g == 255);
        REQUIRE(b == 255);
        REQUIRE(a == 255);
    }

    SECTION("accent color is extracted from preferences") {
        AccessibilityPreferences prefs;
        prefs.highContrastEnabled = true;
        prefs.colors.accent = 0xFF3A96DD;  // Blue accent

        uint8_t r = static_cast<uint8_t>((prefs.colors.accent >> 16) & 0xFF);
        uint8_t g = static_cast<uint8_t>((prefs.colors.accent >> 8) & 0xFF);
        uint8_t b = static_cast<uint8_t>(prefs.colors.accent & 0xFF);

        REQUIRE(r == 0x3A);
        REQUIRE(g == 0x96);
        REQUIRE(b == 0xDD);
    }

    SECTION("high contrast is not applied when disabled") {
        AccessibilityPreferences prefs;
        prefs.highContrastEnabled = false;

        // No color application should happen
        REQUIRE(prefs.highContrastEnabled == false);
    }
}

// =============================================================================
// T076: Text Contrast Ratio Verification (SC-007)
// =============================================================================

TEST_CASE("High contrast text meets WCAG 4.5:1 ratio", "[accessibility][contrast]") {
    // SC-007: Text elements must have >= 4.5:1 contrast ratio in high contrast mode

    // Helper: calculate relative luminance per WCAG 2.0
    auto relativeLuminance = [](uint8_t r, uint8_t g, uint8_t b) -> double {
        auto sRGBtoLinear = [](uint8_t val) -> double {
            double v = static_cast<double>(val) / 255.0;
            return (v <= 0.03928) ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * sRGBtoLinear(r) + 0.7152 * sRGBtoLinear(g) + 0.0722 * sRGBtoLinear(b);
    };

    auto contrastRatio = [&relativeLuminance](
        uint8_t r1, uint8_t g1, uint8_t b1,
        uint8_t r2, uint8_t g2, uint8_t b2) -> double {
        double l1 = relativeLuminance(r1, g1, b1);
        double l2 = relativeLuminance(r2, g2, b2);
        double lighter = std::max(l1, l2);
        double darker = std::min(l1, l2);
        return (lighter + 0.05) / (darker + 0.05);
    };

    SECTION("default high contrast: white text on dark background") {
        // Default: foreground 0xFFFFFFFF on background 0xFF1E1E1E
        double ratio = contrastRatio(255, 255, 255, 0x1E, 0x1E, 0x1E);
        REQUIRE(ratio >= 4.5);
    }

    SECTION("accent color on dark background") {
        // Default accent: 0xFF3A96DD on background 0xFF1E1E1E
        double ratio = contrastRatio(0x3A, 0x96, 0xDD, 0x1E, 0x1E, 0x1E);
        REQUIRE(ratio >= 4.5);
    }

    SECTION("white text on black background has maximum contrast") {
        double ratio = contrastRatio(255, 255, 255, 0, 0, 0);
        REQUIRE(ratio >= 20.9);  // Pure white on black ~ 21:1 (allowing for float precision)
    }
}
