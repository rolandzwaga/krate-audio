#pragma once

// ==============================================================================
// ModSourceColors - VSTGUI Color Registry for Modulation Sources
// ==============================================================================
// Tab-dependent source colors indexed to match the name registries in
// mod_matrix_types.h. Include this header in UI components that need colors.
// Processor code should include mod_matrix_types.h instead.
//
// Global tab: 13 sources (DSP ModSource 1-13: LFO1..Transient)
// Voice tab:  8 sources (DSP VoiceModSource 0-7: Env1..Aftertouch)
//
// Names and abbreviations live in mod_matrix_types.h (single source of truth).
// This file only adds color data on top.
//
// Shared across: ModMatrixGrid, ModRingIndicator, ModHeatmap, BipolarSlider.
//
// Spec: 049-mod-matrix-grid
// ==============================================================================

#include "mod_matrix_types.h"
#include "vstgui/lib/ccolor.h"

namespace Krate::Plugins {

// ==============================================================================
// Global Tab Source Colors (13 entries, indexed to match kGlobalSourceNames)
// ==============================================================================

inline constexpr std::array<VSTGUI::CColor, 13> kGlobalSourceColors = {{
    {60, 210, 100, 255},   // 0: LFO 1
    {90, 200, 130, 255},   // 1: LFO 2
    {220, 170, 60, 255},   // 2: Env Follower
    {170, 170, 175, 255},  // 3: Random
    {200, 100, 140, 255},  // 4: Macro 1
    {210, 115, 155, 255},  // 5: Macro 2
    {220, 130, 170, 255},  // 6: Macro 3
    {230, 145, 185, 255},  // 7: Macro 4
    {190, 55, 55, 255},    // 8: Chaos
    {100, 160, 220, 255},  // 9: Rungler
    {80, 200, 200, 255},   // 10: Sample & Hold
    {80, 180, 160, 255},   // 11: Pitch Follower
    {220, 200, 60, 255},   // 12: Transient
}};

// ==============================================================================
// Voice Tab Source Colors (8 entries, indexed to match kVoiceSourceNames)
// ==============================================================================

// Color cross-reference (T009a / FR-048):
//   ENV 1 rgb(80,140,200) matches ADSRDisplay::fillColor_ / strokeColor_
//   ENV 2 rgb(220,170,60) gold accent
//   ENV 3 rgb(160,90,200) purple accent
inline constexpr std::array<VSTGUI::CColor, 8> kVoiceSourceColors = {{
    {80, 140, 200, 255},   // 0: ENV 1 (Amp)
    {220, 170, 60, 255},   // 1: ENV 2 (Filter)
    {160, 90, 200, 255},   // 2: ENV 3 (Mod)
    {90, 200, 130, 255},   // 3: Voice LFO
    {220, 130, 60, 255},   // 4: Gate Output
    {170, 170, 175, 255},  // 5: Velocity
    {80, 200, 200, 255},   // 6: Key Track
    {200, 160, 80, 255},   // 7: Aftertouch
}};

// Compile-time validation: color arrays must match name registries
static_assert(kGlobalSourceColors.size() == kGlobalSourceNames.size(),
    "kGlobalSourceColors must match kGlobalSourceNames size");
static_assert(kVoiceSourceColors.size() == kVoiceSourceNames.size(),
    "kVoiceSourceColors must match kVoiceSourceNames size");

// ==============================================================================
// Tab-Aware Utility Functions (VSTGUI-dependent)
// ==============================================================================

/// Get the source color for a given tab and source index.
/// Returns white for invalid indices.
[[nodiscard]] inline VSTGUI::CColor sourceColorForTab(int tab, int index) {
    if (tab == 0) {
        if (index >= 0 && index < static_cast<int>(kGlobalSourceColors.size()))
            return kGlobalSourceColors[static_cast<size_t>(index)];
    } else {
        if (index >= 0 && index < static_cast<int>(kVoiceSourceColors.size()))
            return kVoiceSourceColors[static_cast<size_t>(index)];
    }
    return VSTGUI::CColor(255, 255, 255, 255);
}

} // namespace Krate::Plugins
