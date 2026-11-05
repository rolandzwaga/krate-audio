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
#include <utility>

namespace Krate::Plugins {

// ==============================================================================
// Source Counts (tab-dependent)
// ==============================================================================
// Global tab sources match DSP ModSource enum (skip None=0): LFO1..Transient
// Voice tab sources match DSP VoiceModSource enum: Env1..Aftertouch

/// Number of sources visible in the Global tab (DSP ModSource 1-14)
/// Must match kGlobalSourceNames.size() — enforced by static_assert below.
inline constexpr int kNumGlobalSources = 14;

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
    kNumDestinations
};

/// Number of destinations visible in the Voice tab (per-voice)
inline constexpr int kNumVoiceDestinations = 8;

/// Number of destinations visible in the Global tab (matching DSP kModDestCount)
inline constexpr int kNumGlobalDestinations = 15;

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
    {.fullName = "Filter Cutoff",      .hostName = "Flt Cutoff",     .abbreviation = "FCut"},
    {.fullName = "Filter Resonance",   .hostName = "Flt Reso",       .abbreviation = "FRes"},
    {.fullName = "Morph Position",     .hostName = "Morph Pos",      .abbreviation = "Mrph"},
    {.fullName = "Distortion Drive",   .hostName = "Dist Drive",     .abbreviation = "Drv"},
    {.fullName = "TranceGate Depth",   .hostName = "Gate Depth",     .abbreviation = "Gate"},
    {.fullName = "OSC A Pitch",        .hostName = "OSC A Pitch",    .abbreviation = "OsA"},
    {.fullName = "OSC B Pitch",        .hostName = "OSC B Pitch",    .abbreviation = "OsB"},
    {.fullName = "Spectral Tilt",      .hostName = "Spectral Tilt",  .abbreviation = "Tilt"},
}};

/// Global tab destinations (indices 0-14): matching DSP RuinaeModDest 64-78
inline constexpr std::array<ModDestInfo, 15> kGlobalDestNames = {{
    {.fullName = "Global Filter Cutoff",    .hostName = "Global Flt Cutoff",     .abbreviation = "GFCt"},
    {.fullName = "Global Filter Resonance", .hostName = "Global Flt Reso",       .abbreviation = "GFRs"},
    {.fullName = "Master Volume",           .hostName = "Master Volume",         .abbreviation = "Mstr"},
    {.fullName = "Effect Mix",              .hostName = "Effect Mix",            .abbreviation = "FxMx"},
    {.fullName = "All Voice Filter Cutoff", .hostName = "All Voice Flt Cutoff",  .abbreviation = "VFCt"},
    {.fullName = "All Voice Morph Pos",     .hostName = "All Voice Morph Pos",   .abbreviation = "VMrp"},
    {.fullName = "All Voice Gate Rate",     .hostName = "All Voice Gate Rate",   .abbreviation = "VGat"},
    {.fullName = "All Voice Spectral Tilt", .hostName = "All Voice Spectral Tilt", .abbreviation = "VTlt"},
    {.fullName = "All Voice Resonance",     .hostName = "All Voice Resonance",   .abbreviation = "VRso"},
    {.fullName = "All Voice Flt Env Amt",   .hostName = "All Voice Flt Env Amt", .abbreviation = "VEnv"},
    // Arpeggiator destinations (078-modulation-integration)
    {.fullName = "Arp Rate",                .hostName = "Arp Rate",              .abbreviation = "ARate"},
    {.fullName = "Arp Gate Length",         .hostName = "Arp Gate",              .abbreviation = "AGat"},
    {.fullName = "Arp Octave Range",        .hostName = "Arp Octave",            .abbreviation = "AOct"},
    {.fullName = "Arp Swing",               .hostName = "Arp Swing",             .abbreviation = "ASwg"},
    {.fullName = "Arp Spice",               .hostName = "Arp Spice",             .abbreviation = "ASpc"},
}};

// ==============================================================================
// Source Name Registry (tab-dependent, no color - see mod_source_colors.h)
// ==============================================================================

struct ModSourceName {
    const char* fullName;
    const char* abbreviation;
};

/// Global tab sources (indices 0-13, matching DSP ModSource 1-14)
inline constexpr std::array<ModSourceName, 14> kGlobalSourceNames = {{
    {.fullName = "LFO 1",           .abbreviation = "LF1"},
    {.fullName = "LFO 2",           .abbreviation = "LF2"},
    {.fullName = "Env Follower",    .abbreviation = "EnvF"},
    {.fullName = "Random",          .abbreviation = "Rnd"},
    {.fullName = "Macro 1",         .abbreviation = "M1"},
    {.fullName = "Macro 2",         .abbreviation = "M2"},
    {.fullName = "Macro 3",         .abbreviation = "M3"},
    {.fullName = "Macro 4",         .abbreviation = "M4"},
    {.fullName = "Chaos",           .abbreviation = "Chao"},
    {.fullName = "Rungler",         .abbreviation = "Rung"},
    {.fullName = "Sample & Hold",   .abbreviation = "S&H"},
    {.fullName = "Pitch Follower",  .abbreviation = "PFol"},
    {.fullName = "Transient",       .abbreviation = "Tran"},
    {.fullName = "Arp Pitch",       .abbreviation = "ArpP"},
}};

/// Voice tab sources (indices 0-7, matching DSP VoiceModSource 0-7)
inline constexpr std::array<ModSourceName, 8> kVoiceSourceNames = {{
    {.fullName = "ENV 1 (Amp)",     .abbreviation = "E1"},
    {.fullName = "ENV 2 (Filter)",  .abbreviation = "E2"},
    {.fullName = "ENV 3 (Mod)",     .abbreviation = "E3"},
    {.fullName = "Voice LFO",       .abbreviation = "VLFO"},
    {.fullName = "Gate Output",     .abbreviation = "Gt"},
    {.fullName = "Velocity",        .abbreviation = "Vel"},
    {.fullName = "Key Track",       .abbreviation = "Key"},
    {.fullName = "Aftertouch",      .abbreviation = "AT"},
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
        if (index >= 0 && std::cmp_less(index, kGlobalSourceNames.size()))
            return kGlobalSourceNames[static_cast<size_t>(index)].fullName;
    } else {
        if (index >= 0 && std::cmp_less(index, kVoiceSourceNames.size()))
            return kVoiceSourceNames[static_cast<size_t>(index)].fullName;
    }
    return "Unknown";
}

/// Get the abbreviated source name for a given tab and source index.
[[nodiscard]] inline const char* sourceAbbrForTab(int tab, int index) {
    if (tab == 0) {
        if (index >= 0 && std::cmp_less(index, kGlobalSourceNames.size()))
            return kGlobalSourceNames[static_cast<size_t>(index)].abbreviation;
    } else {
        if (index >= 0 && std::cmp_less(index, kVoiceSourceNames.size()))
            return kVoiceSourceNames[static_cast<size_t>(index)].abbreviation;
    }
    return "?";
}

/// Get the full destination name for a given tab and destination index.
[[nodiscard]] inline const char* destinationNameForTab(int tab, int index) {
    if (tab == 0) {
        if (index >= 0 && std::cmp_less(index, kGlobalDestNames.size()))
            return kGlobalDestNames[static_cast<size_t>(index)].fullName;
    } else {
        if (index >= 0 && std::cmp_less(index, kVoiceDestNames.size()))
            return kVoiceDestNames[static_cast<size_t>(index)].fullName;
    }
    return "Unknown";
}

/// Get the abbreviated destination name for a given tab and destination index.
[[nodiscard]] inline const char* destinationAbbrForTab(int tab, int index) {
    if (tab == 0) {
        if (index >= 0 && std::cmp_less(index, kGlobalDestNames.size()))
            return kGlobalDestNames[static_cast<size_t>(index)].abbreviation;
    } else {
        if (index >= 0 && std::cmp_less(index, kVoiceDestNames.size()))
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

[[nodiscard]] constexpr int modSlotSourceId(int slot) {
    return kModMatrixBaseParamId + slot * 3;
}

[[nodiscard]] constexpr int modSlotDestinationId(int slot) {
    return kModMatrixBaseParamId + slot * 3 + 1;
}

[[nodiscard]] constexpr int modSlotAmountId(int slot) {
    return kModMatrixBaseParamId + slot * 3 + 2;
}

[[nodiscard]] constexpr int modSlotCurveId(int slot) {
    return kModMatrixDetailBaseParamId + slot * 4;
}

[[nodiscard]] constexpr int modSlotSmoothId(int slot) {
    return kModMatrixDetailBaseParamId + slot * 4 + 1;
}

[[nodiscard]] constexpr int modSlotScaleId(int slot) {
    return kModMatrixDetailBaseParamId + slot * 4 + 2;
}

[[nodiscard]] constexpr int modSlotBypassId(int slot) {
    return kModMatrixDetailBaseParamId + slot * 4 + 3;
}

} // namespace Krate::Plugins
