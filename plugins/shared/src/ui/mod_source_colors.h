#pragma once

// ==============================================================================
// ModSourceColors - VSTGUI Color Registry for Modulation Sources
// ==============================================================================
// Tab-dependent source colors on top of the pure data types in
// mod_matrix_types.h. Include this header in UI components that need colors.
// Processor code should include mod_matrix_types.h instead.
//
// Global tab: 13 sources (DSP ModSource 1-13: LFO1..Transient)
// Voice tab:  8 sources (DSP VoiceModSource 0-7: Env1..Aftertouch)
//
// Shared across: ModMatrixGrid, ModRingIndicator, ModHeatmap, BipolarSlider.
//
// Spec: 049-mod-matrix-grid
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

// ==============================================================================
// Global Tab Sources (13 entries, matching DSP ModSource 1-13)
// ==============================================================================

inline constexpr std::array<ModSourceInfo, 13> kGlobalSourceInfos = {{
    {{60, 210, 100, 255},  "LFO 1",           "LF1"},    // 0 -> DSP 1
    {{90, 200, 130, 255},  "LFO 2",           "LF2"},    // 1 -> DSP 2
    {{220, 170, 60, 255},  "Env Follower",    "EnvF"},   // 2 -> DSP 3
    {{170, 170, 175, 255}, "Random",          "Rnd"},    // 3 -> DSP 4
    {{200, 100, 140, 255}, "Macro 1",         "M1"},     // 4 -> DSP 5
    {{210, 115, 155, 255}, "Macro 2",         "M2"},     // 5 -> DSP 6
    {{220, 130, 170, 255}, "Macro 3",         "M3"},     // 6 -> DSP 7
    {{230, 145, 185, 255}, "Macro 4",         "M4"},     // 7 -> DSP 8
    {{190, 55, 55, 255},   "Chaos",           "Chao"},   // 8 -> DSP 9
    {{100, 160, 220, 255}, "Rungler",         "Rung"},   // 9 -> DSP 10
    {{80, 200, 200, 255},  "Sample & Hold",   "S&H"},    // 10 -> DSP 11
    {{80, 180, 160, 255},  "Pitch Follower",  "PFol"},   // 11 -> DSP 12
    {{220, 200, 60, 255},  "Transient",       "Tran"},   // 12 -> DSP 13
}};

// ==============================================================================
// Voice Tab Sources (8 entries, matching DSP VoiceModSource 0-7)
// ==============================================================================

// Color cross-reference (T009a / FR-048):
//   ENV 1 rgb(80,140,200) matches ADSRDisplay::fillColor_ / strokeColor_
//   ENV 2 rgb(220,170,60) gold accent
//   ENV 3 rgb(160,90,200) purple accent
inline constexpr std::array<ModSourceInfo, 8> kVoiceSourceInfos = {{
    {{80, 140, 200, 255},  "ENV 1 (Amp)",     "E1"},     // 0
    {{220, 170, 60, 255},  "ENV 2 (Filter)",  "E2"},     // 1
    {{160, 90, 200, 255},  "ENV 3 (Mod)",     "E3"},     // 2
    {{90, 200, 130, 255},  "Voice LFO",       "VLFO"},   // 3
    {{220, 130, 60, 255},  "Gate Output",     "Gt"},     // 4
    {{170, 170, 175, 255}, "Velocity",        "Vel"},    // 5
    {{80, 200, 200, 255},  "Key Track",       "Key"},    // 6
    {{200, 160, 80, 255},  "Aftertouch",      "AT"},     // 7
}};

// ==============================================================================
// Tab-Aware Utility Functions (VSTGUI-dependent)
// ==============================================================================

/// Get the source color for a given tab and source index.
/// Returns white for invalid indices.
[[nodiscard]] inline VSTGUI::CColor sourceColorForTab(int tab, int index) {
    if (tab == 0) {
        if (index >= 0 && index < static_cast<int>(kGlobalSourceInfos.size()))
            return kGlobalSourceInfos[static_cast<size_t>(index)].color;
    } else {
        if (index >= 0 && index < static_cast<int>(kVoiceSourceInfos.size()))
            return kVoiceSourceInfos[static_cast<size_t>(index)].color;
    }
    return VSTGUI::CColor(255, 255, 255, 255);
}

} // namespace Krate::Plugins
