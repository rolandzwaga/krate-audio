// ==============================================================================
// AccessibilityHelper Implementation
// ==============================================================================
// Cross-platform accessibility detection for OS-level preferences.
//
// Constitution Principle VI: Platform-specific code allowed for accessibility
// detection with #ifdef guards and graceful fallbacks.
//
// Platform implementations:
//   Windows: SystemParametersInfo API (SPI_GETHIGHCONTRAST, SPI_GETCLIENTAREAANIMATION)
//   macOS:   NSWorkspace API via Objective-C++ wrapper (accessibility_helper.mm)
//   Linux:   GTK_THEME env variable + gsettings subprocess (best-effort)
// ==============================================================================

#include "platform/accessibility_helper.h"

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __APPLE__
// C-callable wrappers defined in accessibility_helper.mm (Objective-C++)
extern "C" {
bool krate_macos_isHighContrastEnabled();
bool krate_macos_isReducedMotionPreferred();
void krate_macos_getHighContrastColors(
    uint32_t* foreground,
    uint32_t* background,
    uint32_t* accent,
    uint32_t* border,
    uint32_t* disabled);
}
#endif

#if !defined(_WIN32) && !defined(__APPLE__)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#endif

namespace Krate::Plugins {

AccessibilityPreferences queryAccessibilityPreferences() {
    AccessibilityPreferences prefs;

#ifdef _WIN32
    // FR-025b: Windows high contrast detection
    HIGHCONTRAST hc{};
    hc.cbSize = sizeof(HIGHCONTRAST);
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(HIGHCONTRAST), &hc, 0)) {
        prefs.highContrastEnabled = (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
        if (prefs.highContrastEnabled) {
            // Query system colors for high contrast palette
            prefs.colors.foreground = 0xFF000000 | static_cast<uint32_t>(GetSysColor(COLOR_WINDOWTEXT));
            prefs.colors.background = 0xFF000000 | static_cast<uint32_t>(GetSysColor(COLOR_WINDOW));
            prefs.colors.accent     = 0xFF000000 | static_cast<uint32_t>(GetSysColor(COLOR_HIGHLIGHT));
            prefs.colors.border     = 0xFF000000 | static_cast<uint32_t>(GetSysColor(COLOR_WINDOWFRAME));
            prefs.colors.disabled   = 0xFF000000 | static_cast<uint32_t>(GetSysColor(COLOR_GRAYTEXT));
        }
    }

    // FR-027: Windows reduced motion detection
    BOOL animEnabled = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animEnabled, 0)) {
        prefs.reducedMotionPreferred = (animEnabled == FALSE);
    }

#elif defined(__APPLE__)
    // FR-025c: macOS high contrast detection via NSWorkspace API
    prefs.highContrastEnabled = krate_macos_isHighContrastEnabled();

    if (prefs.highContrastEnabled) {
        // Query system colors for high contrast palette
        krate_macos_getHighContrastColors(
            &prefs.colors.foreground,
            &prefs.colors.background,
            &prefs.colors.accent,
            &prefs.colors.border,
            &prefs.colors.disabled);
    }

    // FR-027: macOS reduced motion detection via NSWorkspace API
    prefs.reducedMotionPreferred = krate_macos_isReducedMotionPreferred();

#else
    // FR-025d: Linux best-effort detection
    // Check GTK_THEME environment variable for high contrast
    const char* gtkTheme = std::getenv("GTK_THEME");
    if (gtkTheme) {
        std::string theme(gtkTheme);
        if (theme.find("HighContrast") != std::string::npos ||
            theme.find("high-contrast") != std::string::npos) {
            prefs.highContrastEnabled = true;
        }
    }

    // FR-027: Linux reduced motion detection via gsettings (best-effort)
    // Query GNOME accessibility setting without linking GLib.
    // Uses popen() to call gsettings as a subprocess. If gsettings is not
    // available (non-GNOME desktop), defaults to false (animations enabled).
    {
        bool reducedMotion = false;
        FILE* pipe = popen(
            "gsettings get org.gnome.desktop.interface enable-animations 2>/dev/null",
            "r");
        if (pipe) {
            char buffer[64];
            if (std::fgets(buffer, sizeof(buffer), pipe)) {
                // gsettings returns "true" or "false"
                // If enable-animations is "false", user prefers reduced motion
                if (std::strstr(buffer, "false") != nullptr) {
                    reducedMotion = true;
                }
            }
            pclose(pipe);
        }
        prefs.reducedMotionPreferred = reducedMotion;
    }
#endif

    return prefs;
}

bool isHighContrastEnabled() {
    return queryAccessibilityPreferences().highContrastEnabled;
}

bool isReducedMotionPreferred() {
    return queryAccessibilityPreferences().reducedMotionPreferred;
}

} // namespace Krate::Plugins
