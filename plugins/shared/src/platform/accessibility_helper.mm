// ==============================================================================
// AccessibilityHelper - macOS Implementation (Objective-C++)
// ==============================================================================
// FR-025c: macOS high contrast detection using NSWorkspace API
// FR-027: macOS reduced motion detection using NSWorkspace API
//
// Constitution Principle VI: Platform-specific code allowed for accessibility
// detection. This .mm file is only compiled on macOS.
// ==============================================================================

#import <AppKit/AppKit.h>

// C-callable wrapper functions for use from accessibility_helper.cpp

extern "C" {

bool krate_macos_isHighContrastEnabled() {
    @autoreleasepool {
        return [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldIncreaseContrast];
    }
}

bool krate_macos_isReducedMotionPreferred() {
    @autoreleasepool {
        return [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceMotion];
    }
}

void krate_macos_getHighContrastColors(
    uint32_t* foreground,
    uint32_t* background,
    uint32_t* accent,
    uint32_t* border,
    uint32_t* disabled)
{
    @autoreleasepool {
        // Helper: convert NSColor to ARGB uint32_t
        auto colorToARGB = [](NSColor* color) -> uint32_t {
            NSColor* rgbColor = [color colorUsingColorSpace:
                [NSColorSpace sRGBColorSpace]];
            if (!rgbColor) {
                // Fallback if color space conversion fails
                return 0xFFFFFFFF;
            }
            CGFloat r, g, b, a;
            [rgbColor getRed:&r green:&g blue:&b alpha:&a];

            uint8_t ar = static_cast<uint8_t>(a * 255.0);
            uint8_t rr = static_cast<uint8_t>(r * 255.0);
            uint8_t gr = static_cast<uint8_t>(g * 255.0);
            uint8_t br = static_cast<uint8_t>(b * 255.0);

            return (static_cast<uint32_t>(ar) << 24) |
                   (static_cast<uint32_t>(rr) << 16) |
                   (static_cast<uint32_t>(gr) << 8) |
                   static_cast<uint32_t>(br);
        };

        if (foreground) {
            *foreground = colorToARGB([NSColor controlTextColor]);
        }
        if (background) {
            *background = colorToARGB([NSColor controlBackgroundColor]);
        }
        if (accent) {
            *accent = colorToARGB([NSColor selectedControlColor]);
        }
        if (border) {
            *border = colorToARGB([NSColor separatorColor]);
        }
        if (disabled) {
            *disabled = colorToARGB([NSColor disabledControlTextColor]);
        }
    }
}

} // extern "C"
