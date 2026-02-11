#pragma once

// ==============================================================================
// ModMatrixTypes - Pure Data Types for Modulation Matrix
// ==============================================================================
// Enums, route structs, constants, and parameter ID helpers used by both
// processor (DSP) and controller (UI) sides. No VSTGUI dependency.
//
// Shared across: processor.h, mod_source_colors.h, ModMatrixGrid, ModRingIndicator,
// ModHeatmap, BipolarSlider.
//
// Spec: 049-mod-matrix-grid
// ==============================================================================

#include <array>
#include <cstdint>

namespace Krate::Plugins {

// ==============================================================================
// ModSource Enum
// ==============================================================================
// Per-voice sources (0-6) available in both Voice and Global tabs.
// Global-only sources (7-9) available only in Global tab.
// FR-011: Each source has a unique identity color.

enum class ModSource : uint8_t {
    // Per-Voice sources (0-6)
    Env1 = 0,         // ENV 1 (Amp)
    Env2,              // ENV 2 (Filter)
    Env3,              // ENV 3 (Mod)
    VoiceLFO,          // Voice LFO
    GateOutput,        // Gate Output
    Velocity,          // Velocity
    KeyTrack,          // Key Track
    // Global-only sources (7-9)
    Macros,            // Macros 1-4
    ChaosRungler,      // Chaos/Rungler
    GlobalLFO,         // LFO 1-2 (Global)
    kNumSources = 10
};

/// Number of sources visible in the Voice tab (per-voice only)
inline constexpr int kNumVoiceSources = 7;

/// Number of sources visible in the Global tab (all sources)
inline constexpr int kNumGlobalSources = static_cast<int>(ModSource::kNumSources);

// ==============================================================================
// ModDestination Enum
// ==============================================================================
// Per-voice destinations (0-6) available in both Voice and Global tabs.
// Global-only destinations (7-10) available only in Global tab.
// FR-012, FR-013, FR-014

enum class ModDestination : uint8_t {
    // Per-Voice destinations (0-6)
    FilterCutoff = 0,
    FilterResonance,
    MorphPosition,
    DistortionDrive,
    TranceGateDepth,
    OscAPitch,
    OscBPitch,
    // Global-only destinations (7-10)
    GlobalFilterCutoff,
    GlobalFilterResonance,
    MasterVolume,
    EffectMix,
    kNumDestinations = 11
};

/// Number of destinations visible in the Voice tab (per-voice only)
inline constexpr int kNumVoiceDestinations = 7;

/// Number of destinations visible in the Global tab (all destinations)
inline constexpr int kNumGlobalDestinations = static_cast<int>(ModDestination::kNumDestinations);

// ==============================================================================
// ModRoute Struct
// ==============================================================================
// Represents a single modulation route (used internally by UI components).
// FR-001 to FR-010

struct ModRoute {
    ModSource source = ModSource::Env1;
    ModDestination destination = ModDestination::FilterCutoff;
    float amount = 0.0f;           // [-1.0, +1.0] bipolar
    uint8_t curve = 0;             // 0=Linear, 1=Exponential, 2=Logarithmic, 3=S-Curve
    float smoothMs = 0.0f;         // 0-100ms
    uint8_t scale = 2;             // 0=x0.25, 1=x0.5, 2=x1, 3=x2, 4=x4
    bool bypass = false;
    bool active = false;           // Whether this slot is occupied
};

// ==============================================================================
// VoiceModRoute Struct (IMessage serialization)
// ==============================================================================
// Fixed-size struct for binary IMessage transfer between controller and
// processor. 14 bytes per route, 16 routes max = 224 bytes total.
// FR-046

struct VoiceModRoute {
    uint8_t source = 0;            // ModSource value
    uint8_t destination = 0;       // ModDestination value
    float amount = 0.0f;          // [-1.0, +1.0]
    uint8_t curve = 0;            // 0-3
    float smoothMs = 0.0f;        // 0-100ms
    uint8_t scale = 2;            // 0-4
    uint8_t bypass = 0;           // 0 or 1
    uint8_t active = 0;           // 0 or 1
};

// ==============================================================================
// Constants
// ==============================================================================

inline constexpr int kMaxGlobalRoutes = 8;
inline constexpr int kMaxVoiceRoutes = 16;

// Curve type names for StringListParameter (FR-017)
inline constexpr std::array<const char*, 4> kCurveTypeNames = {{
    "Linear",
    "Exponential",
    "Logarithmic",
    "S-Curve",
}};

// Scale multiplier names for StringListParameter (FR-018)
inline constexpr std::array<const char*, 5> kScaleNames = {{
    "x0.25",
    "x0.5",
    "x1",
    "x2",
    "x4",
}};

// Scale multiplier values (for DSP computation)
inline constexpr std::array<float, 5> kScaleValues = {{
    0.25f,
    0.5f,
    1.0f,
    2.0f,
    4.0f,
}};

// ==============================================================================
// Destination Name Registry (FR-035, FR-036)
// ==============================================================================
// No VSTGUI dependency - pure string data.

struct ModDestInfo {
    const char* fullName;
    const char* voiceAbbr;   // For voice tab heatmap columns
    const char* globalAbbr;  // For global tab heatmap columns
};

inline constexpr std::array<ModDestInfo, 11> kModDestinations = {{
    {"Filter Cutoff",           "FCut", "FCut"},
    {"Filter Resonance",        "FRes", "FRes"},
    {"Morph Position",          "Mrph", "Mrph"},
    {"Distortion Drive",        "Drv",  "Drv"},
    {"TranceGate Depth",        "Gate", "Gate"},
    {"OSC A Pitch",             "OsA",  "OsA"},
    {"OSC B Pitch",             "OsB",  "OsB"},
    {"Global Filter Cutoff",    "",     "GFCt"},
    {"Global Filter Resonance", "",     "GFRs"},
    {"Master Volume",           "",     "Mstr"},
    {"Effect Mix",              "",     "FxMx"},
}};

// ==============================================================================
// Source Name Registry (no color - see mod_source_colors.h for colors)
// ==============================================================================

struct ModSourceName {
    const char* fullName;
    const char* abbreviation;
};

inline constexpr std::array<ModSourceName, 10> kModSourceNames = {{
    {"ENV 1 (Amp)",      "E1"},
    {"ENV 2 (Filter)",    "E2"},
    {"ENV 3 (Mod)",       "E3"},
    {"Voice LFO",         "VLFO"},
    {"Gate Output",       "Gt"},
    {"Velocity",          "Vel"},
    {"Key Track",         "Key"},
    {"Macros 1-4",        "M1-4"},
    {"Chaos/Rungler",     "Chao"},
    {"LFO 1-2 (Global)",  "LF12"},
}};

/// Get the full source name for a given source index.
/// Returns "Unknown" for invalid indices.
[[nodiscard]] inline const char* sourceNameForIndex(int index) {
    if (index >= 0 && index < static_cast<int>(kModSourceNames.size())) {
        return kModSourceNames[static_cast<size_t>(index)].fullName;
    }
    return "Unknown";
}

/// Get the abbreviated source name for a given source index.
/// Returns "?" for invalid indices.
[[nodiscard]] inline const char* sourceAbbrForIndex(int index) {
    if (index >= 0 && index < static_cast<int>(kModSourceNames.size())) {
        return kModSourceNames[static_cast<size_t>(index)].abbreviation;
    }
    return "?";
}

/// Get the full destination name for a given destination index.
/// Returns "Unknown" for invalid indices.
[[nodiscard]] inline const char* destinationNameForIndex(int index) {
    if (index >= 0 && index < static_cast<int>(kModDestinations.size())) {
        return kModDestinations[static_cast<size_t>(index)].fullName;
    }
    return "Unknown";
}

/// Get the abbreviated destination name for a given destination index.
/// @param index Destination index
/// @param globalTab If true, returns the global tab abbreviation
/// Returns "?" for invalid indices.
[[nodiscard]] inline const char* destinationAbbrForIndex(int index, bool globalTab = true) {
    if (index >= 0 && index < static_cast<int>(kModDestinations.size())) {
        return globalTab
            ? kModDestinations[static_cast<size_t>(index)].globalAbbr
            : kModDestinations[static_cast<size_t>(index)].voiceAbbr;
    }
    return "?";
}

// ==============================================================================
// Parameter ID Helpers
// ==============================================================================
// Formulas from spec 049 data model:
//   Source ID      = 1300 + slot * 3
//   Destination ID = 1301 + slot * 3
//   Amount ID      = 1302 + slot * 3
//   Curve ID       = 1324 + slot * 4
//   Smooth ID      = 1325 + slot * 4
//   Scale ID       = 1326 + slot * 4
//   Bypass ID      = 1327 + slot * 4

inline constexpr int kModMatrixBaseParamId = 1300;
inline constexpr int kModMatrixDetailBaseParamId = 1324;

[[nodiscard]] inline constexpr int modSlotSourceId(int slot) {
    return kModMatrixBaseParamId + slot * 3;
}

[[nodiscard]] inline constexpr int modSlotDestinationId(int slot) {
    return kModMatrixBaseParamId + slot * 3 + 1;
}

[[nodiscard]] inline constexpr int modSlotAmountId(int slot) {
    return kModMatrixBaseParamId + slot * 3 + 2;
}

[[nodiscard]] inline constexpr int modSlotCurveId(int slot) {
    return kModMatrixDetailBaseParamId + slot * 4;
}

[[nodiscard]] inline constexpr int modSlotSmoothId(int slot) {
    return kModMatrixDetailBaseParamId + slot * 4 + 1;
}

[[nodiscard]] inline constexpr int modSlotScaleId(int slot) {
    return kModMatrixDetailBaseParamId + slot * 4 + 2;
}

[[nodiscard]] inline constexpr int modSlotBypassId(int slot) {
    return kModMatrixDetailBaseParamId + slot * 4 + 3;
}

} // namespace Krate::Plugins
