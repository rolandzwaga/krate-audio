#pragma once

// ==============================================================================
// Plugin Identifiers
// ==============================================================================
// These GUIDs uniquely identify the plugin components.
// Generate new GUIDs for your plugin at: https://www.guidgenerator.com/
//
// IMPORTANT: Once published, NEVER change these IDs or hosts will not
// recognize saved projects using your plugin.
// ==============================================================================

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Innexus {

// Processor Component ID
// The audio processing component (runs on audio thread)
static const Steinberg::FUID kProcessorUID(0xE1F2A3B4, 0x5C6D7E8F, 0x9A0B1C2D, 0x3E4F5A6B);

// Controller Component ID
// The edit controller component (runs on UI thread)
static const Steinberg::FUID kControllerUID(0xB4A3F2E1, 0x8F7E6D5C, 0x2D1C0B9A, 0x6B5A4F3E);

// VST3 subcategories: Instrument with sidechain capability
static constexpr auto kSubCategories = "Instrument|Synth";

// ==============================================================================
// Parameter IDs
// ==============================================================================
// Define all automatable parameters here.
// All parameter values MUST be normalized (0.0 to 1.0)
//
// ID Range Allocation (100-ID gaps for future expansion):
//   0-99:      Global parameters (Master Gain, etc.)
//   100-199:   Analysis parameters (Source mode, window size, etc.)
//   200-299:   Oscillator bank (Inharmonicity, partial count, etc.)
//   300-399:   Musical control (Freeze, morph, harmonic filter, etc.)
//   400-499:   Residual model (Mix, brightness, transient emphasis)
//   500-599:   Sidechain / Live Analysis (M3)
//   600-699:   Modulators (LFO targets, evolution engine)
//   700-799:   Output (Stereo spread, voice management)
// ==============================================================================

enum ParameterIds : Steinberg::Vst::ParamID
{
    // Global
    kBypassId = 0,
    kMasterGainId = 1,

    // Oscillator Bank (200-299)
    kReleaseTimeId = 200,          // 20-5000ms, default 100ms
    kInharmonicityAmountId = 201,  // 0-100%, default 100%

    // Residual Model (400-499) -- M2
    kHarmonicLevelId = 400,        // plain 0.0-2.0, normalized 0.0-1.0, default plain 1.0 (normalized 0.5)
    kResidualLevelId = 401,        // plain 0.0-2.0, normalized 0.0-1.0, default plain 1.0 (normalized 0.5)
    kResidualBrightnessId = 402,   // plain -1.0 to +1.0, normalized 0.0-1.0, default plain 0.0 (normalized 0.5)
    kTransientEmphasisId = 403,    // plain 0.0-2.0, normalized 0.0-1.0, default plain 0.0 (normalized 0.0)

    // Musical Control (300-399) -- M4
    kFreezeId = 300,               // 0.0 = off, 1.0 = on (toggle)
    kMorphPositionId = 301,        // 0.0 to 1.0, default 0.0
    kHarmonicFilterTypeId = 302,   // StringListParameter: 5 presets
    kResponsivenessId = 303,       // 0.0 to 1.0, default 0.5

    // Sidechain / Live Analysis (500-599) -- M3
    kInputSourceId = 500,          // 0 = Sample, 1 = Sidechain (StringListParameter)
    kLatencyModeId = 501,          // 0 = Low Latency, 1 = High Precision (StringListParameter)
};

// ==============================================================================
// Input Source Enum (M3: Live Sidechain Mode)
// ==============================================================================
enum class InputSource : int
{
    Sample = 0,
    Sidechain = 1
};

// ==============================================================================
// Latency Mode Enum (M3: Live Sidechain Mode)
// ==============================================================================
enum class LatencyMode : int
{
    LowLatency = 0,
    HighPrecision = 1
};

// ==============================================================================
// Harmonic Filter Type Enum (M4: Musical Control Layer)
// ==============================================================================
enum class HarmonicFilterType : int
{
    AllPass = 0,
    OddOnly = 1,
    EvenOnly = 2,
    LowHarmonics = 3,
    HighHarmonics = 4
};

} // namespace Innexus
