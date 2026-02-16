#pragma once

// ==============================================================================
// ModMatrixTypes - Pure Data Types for Modulation Matrix
// ==============================================================================
// Enums, route structs, constants, and parameter ID helpers used by both
// processor (DSP) and controller (UI) sides. No VSTGUI dependency.
//
// Source indices are tab-dependent:
//   Global tab: indices 0-12 map to DSP ModSource 1-13 (LFO1..Transient)
//   Voice tab:  indices 0-7  map to DSP VoiceModSource 0-7 (Env1..Aftertouch)
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
// Source Counts (tab-dependent)
// ==============================================================================
// Global tab sources match DSP ModSource enum (skip None=0): LFO1..Transient
// Voice tab sources match DSP VoiceModSource enum: Env1..Aftertouch

/// Number of sources visible in the Global tab (DSP ModSource 1-13)
/// Must match kGlobalSourceNames.size() — enforced by static_assert below.
inline constexpr int kNumGlobalSources = 13;

/// Number of sources visible in the Voice tab (DSP VoiceModSource 0-7)
/// Must match kVoiceSourceNames.size() — enforced by static_assert below.
inline constexpr int kNumVoiceSources = 8;

// ==============================================================================
// ModDestination Enum
// ==============================================================================
// Destination indices are tab-dependent (same pattern as sources):
//   Voice tab:  indices 0-7 → per-voice targets (FilterCutoff..SpectralTilt)
//   Global tab: indices 0-7 → global/all-voice targets matching DSP RuinaeModDest
// FR-012, FR-013, FR-014

enum class ModDestination : uint8_t {
    // Voice tab destinations (0-7). On the Global tab, indices 0-7 have
    // different meanings — see kGlobalDestNames for global tab labels.
    FilterCutoff = 0,
    FilterResonance,
    MorphPosition,
    DistortionDrive,
    TranceGateDepth,
    OscAPitch,
    OscBPitch,
    SpectralTilt,
    kNumDestinations = 8
};

/// Number of destinations visible in the Voice tab (per-voice)
inline constexpr int kNumVoiceDestinations = 8;

/// Number of destinations visible in the Global tab (matching DSP kModDestCount)
inline constexpr int kNumGlobalDestinations = 10;

// ==============================================================================
// ModRoute Struct
// ==============================================================================
// Represents a single modulation route (used internally by UI components).
// The source field is a raw index whose meaning depends on the tab:
//   Global routes: index into kGlobalSourceNames (0-12)
//   Voice routes:  index into kVoiceSourceNames (0-7)
// FR-001 to FR-010

struct ModRoute {
    uint8_t source = 0;
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
    uint8_t source = 0;            // VoiceModSource value
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
// Tab-dependent, matching the source pattern. No VSTGUI dependency.

struct ModDestInfo {
    const char* fullName;       ///< Full name for our UI dropdown (e.g. "Global Filter Cutoff")
    const char* hostName;       ///< Shorter name for VST host parameter display (e.g. "Global Flt Cutoff")
    const char* abbreviation;   ///< Compact abbreviation for grid cells (e.g. "GFCt")
};

/// Voice tab destinations (indices 0-7): per-voice targets
inline constexpr std::array<ModDestInfo, 8> kVoiceDestNames = {{
    {"Filter Cutoff",      "Flt Cutoff",     "FCut"},
    {"Filter Resonance",   "Flt Reso",       "FRes"},
    {"Morph Position",     "Morph Pos",      "Mrph"},
    {"Distortion Drive",   "Dist Drive",     "Drv"},
    {"TranceGate Depth",   "Gate Depth",     "Gate"},
    {"OSC A Pitch",        "OSC A Pitch",    "OsA"},
    {"OSC B Pitch",        "OSC B Pitch",    "OsB"},
    {"Spectral Tilt",      "Spectral Tilt",  "Tilt"},
}};

/// Global tab destinations (indices 0-9): matching DSP RuinaeModDest 64-73
inline constexpr std::array<ModDestInfo, 10> kGlobalDestNames = {{
    {"Global Filter Cutoff",    "Global Flt Cutoff",     "GFCt"},
    {"Global Filter Resonance", "Global Flt Reso",       "GFRs"},
    {"Master Volume",           "Master Volume",         "Mstr"},
    {"Effect Mix",              "Effect Mix",            "FxMx"},
    {"All Voice Filter Cutoff", "All Voice Flt Cutoff",  "VFCt"},
    {"All Voice Morph Pos",     "All Voice Morph Pos",   "VMrp"},
    {"All Voice Gate Rate",     "All Voice Gate Rate",   "VGat"},
    {"All Voice Spectral Tilt", "All Voice Spectral Tilt", "VTlt"},
    {"All Voice Resonance",     "All Voice Resonance",   "VRso"},
    {"All Voice Flt Env Amt",   "All Voice Flt Env Amt", "VEnv"},
}};

// ==============================================================================
// Source Name Registry (tab-dependent, no color - see mod_source_colors.h)
// ==============================================================================

struct ModSourceName {
    const char* fullName;
    const char* abbreviation;
};

/// Global tab sources (indices 0-12, matching DSP ModSource 1-13)
inline constexpr std::array<ModSourceName, 13> kGlobalSourceNames = {{
    {"LFO 1",           "LF1"},
    {"LFO 2",           "LF2"},
    {"Env Follower",    "EnvF"},
    {"Random",          "Rnd"},
    {"Macro 1",         "M1"},
    {"Macro 2",         "M2"},
    {"Macro 3",         "M3"},
    {"Macro 4",         "M4"},
    {"Chaos",           "Chao"},
    {"Rungler",         "Rung"},
    {"Sample & Hold",   "S&H"},
    {"Pitch Follower",  "PFol"},
    {"Transient",       "Tran"},
}};

/// Voice tab sources (indices 0-7, matching DSP VoiceModSource 0-7)
inline constexpr std::array<ModSourceName, 8> kVoiceSourceNames = {{
    {"ENV 1 (Amp)",     "E1"},
    {"ENV 2 (Filter)",  "E2"},
    {"ENV 3 (Mod)",     "E3"},
    {"Voice LFO",       "VLFO"},
    {"Gate Output",     "Gt"},
    {"Velocity",        "Vel"},
    {"Key Track",       "Key"},
    {"Aftertouch",      "AT"},
}};

// ==============================================================================
// Registry Size Validation
// ==============================================================================
// Compile-time checks that count constants match their corresponding arrays.
// Adding an entry to an array without updating the count (or vice versa) will
// fail the build here, preventing silent data mismatches.

static_assert(kGlobalSourceNames.size() == static_cast<size_t>(kNumGlobalSources),
    "kNumGlobalSources must match kGlobalSourceNames array size");
static_assert(kVoiceSourceNames.size() == static_cast<size_t>(kNumVoiceSources),
    "kNumVoiceSources must match kVoiceSourceNames array size");
static_assert(kGlobalDestNames.size() == static_cast<size_t>(kNumGlobalDestinations),
    "kNumGlobalDestinations must match kGlobalDestNames array size");
static_assert(kVoiceDestNames.size() == static_cast<size_t>(kNumVoiceDestinations),
    "kNumVoiceDestinations must match kVoiceDestNames array size");

/// Get the full source name for a given tab and source index.
[[nodiscard]] inline const char* sourceNameForTab(int tab, int index) {
    if (tab == 0) {
        if (index >= 0 && index < static_cast<int>(kGlobalSourceNames.size()))
            return kGlobalSourceNames[static_cast<size_t>(index)].fullName;
    } else {
        if (index >= 0 && index < static_cast<int>(kVoiceSourceNames.size()))
            return kVoiceSourceNames[static_cast<size_t>(index)].fullName;
    }
    return "Unknown";
}

/// Get the abbreviated source name for a given tab and source index.
[[nodiscard]] inline const char* sourceAbbrForTab(int tab, int index) {
    if (tab == 0) {
        if (index >= 0 && index < static_cast<int>(kGlobalSourceNames.size()))
            return kGlobalSourceNames[static_cast<size_t>(index)].abbreviation;
    } else {
        if (index >= 0 && index < static_cast<int>(kVoiceSourceNames.size()))
            return kVoiceSourceNames[static_cast<size_t>(index)].abbreviation;
    }
    return "?";
}

/// Get the full destination name for a given tab and destination index.
[[nodiscard]] inline const char* destinationNameForTab(int tab, int index) {
    if (tab == 0) {
        if (index >= 0 && index < static_cast<int>(kGlobalDestNames.size()))
            return kGlobalDestNames[static_cast<size_t>(index)].fullName;
    } else {
        if (index >= 0 && index < static_cast<int>(kVoiceDestNames.size()))
            return kVoiceDestNames[static_cast<size_t>(index)].fullName;
    }
    return "Unknown";
}

/// Get the abbreviated destination name for a given tab and destination index.
[[nodiscard]] inline const char* destinationAbbrForTab(int tab, int index) {
    if (tab == 0) {
        if (index >= 0 && index < static_cast<int>(kGlobalDestNames.size()))
            return kGlobalDestNames[static_cast<size_t>(index)].abbreviation;
    } else {
        if (index >= 0 && index < static_cast<int>(kVoiceDestNames.size()))
            return kVoiceDestNames[static_cast<size_t>(index)].abbreviation;
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
