#pragma once

// ==============================================================================
// ModSourceColors - VSTGUI Color Registry for Modulation Sources
// ==============================================================================
// Adds CColor-based source colors on top of the pure data types in
// mod_matrix_types.h. Include this header in UI components that need colors.
// Processor code should include mod_matrix_types.h instead.
//
// Shared across: ModMatrixGrid, ModRingIndicator, ModHeatmap, BipolarSlider.
// Future consumers: ADSRDisplay (ENV 1-3 identity colors), XYMorphPad (trail).
//
// Spec: 049-mod-matrix-grid
//
// Color cross-reference (T009a):
//   ENV 1 color rgb(80,140,200) matches ADSRDisplay fillColor_/strokeColor_
//   (adsr_display.h line 1814: CColor(80, 140, 200, 77/255))
// ==============================================================================

#include "mod_matrix_types.h"
#include "vstgui/lib/ccolor.h"

namespace Krate::Plugins {

// ==============================================================================
// ModSourceInfo - Color and Name Registry (FR-011)
// ==============================================================================

struct ModSourceInfo {
    VSTGUI::CColor color;
    const char* fullName;
    const char* abbreviation;
};

// Color cross-reference (T009a / FR-048):
//   ENV 1 rgb(80,140,200)  matches ADSRDisplay::fillColor_ / strokeColor_
//   ENV 2 rgb(220,170,60)  gold accent
//   ENV 3 rgb(160,90,200)  purple accent
inline constexpr std::array<ModSourceInfo, 10> kModSources = {{
    {{80, 140, 200, 255},  "ENV 1 (Amp)",      "E1"},
    {{220, 170, 60, 255},  "ENV 2 (Filter)",    "E2"},
    {{160, 90, 200, 255},  "ENV 3 (Mod)",       "E3"},
    {{90, 200, 130, 255},  "Voice LFO",         "VLFO"},
    {{220, 130, 60, 255},  "Gate Output",       "Gt"},
    {{170, 170, 175, 255}, "Velocity",          "Vel"},
    {{80, 200, 200, 255},  "Key Track",         "Key"},
    {{200, 100, 140, 255}, "Macros 1-4",        "M1-4"},
    {{190, 55, 55, 255},   "Chaos/Rungler",     "Chao"},
    {{60, 210, 100, 255},  "LFO 1-2 (Global)",  "LF12"},
}};

// ==============================================================================
// Utility Functions (VSTGUI-dependent)
// ==============================================================================

/// Get the source color for a given source index.
/// Returns white for invalid indices.
[[nodiscard]] inline VSTGUI::CColor sourceColorForIndex(int index) {
    if (index >= 0 && index < static_cast<int>(kModSources.size())) {
        return kModSources[static_cast<size_t>(index)].color;
    }
    return VSTGUI::CColor(255, 255, 255, 255);
}

} // namespace Krate::Plugins
