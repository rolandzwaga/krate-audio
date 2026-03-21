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
    kPartialCountId = 202,         // StringListParameter: "48"/"64"/"80"/"96", default 0 (48)

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

    // Harmonic Memory (304-399) -- M5
    kMemorySlotId = 304,           // StringListParameter: "Slot 1" through "Slot 8"
    kMemoryCaptureId = 305,        // Momentary trigger (stepCount=1)
    kMemoryRecallId = 306,         // Momentary trigger (stepCount=1)

    // Sidechain / Live Analysis (500-599) -- M3
    kInputSourceId = 500,          // 0 = Sample, 1 = Sidechain (StringListParameter)
    kLatencyModeId = 501,          // 0 = Low Latency, 1 = High Precision (StringListParameter)
    kAnalysisModeId = 502,         // 0 = Mono, 1 = Poly, 2 = Auto (StringListParameter)

    // Creative Extensions (600-699) -- M6
    // Cross-Synthesis
    kTimbralBlendId = 600,         // 0.0-1.0, default 1.0
    // Stereo Output
    kStereoSpreadId = 601,         // 0.0-1.0, default 0.0
    // Evolution Engine
    kEvolutionEnableId = 602,      // 0/1, default 0
    kEvolutionSpeedId = 603,       // 0.01-10.0 Hz, default 0.1
    kEvolutionDepthId = 604,       // 0.0-1.0, default 0.5
    kEvolutionModeId = 605,        // 0-2 (Cycle/PingPong/RandomWalk), default 0
    // Modulator 1
    kMod1EnableId = 610,           // 0/1, default 0
    kMod1WaveformId = 611,         // 0-4 (Sine/Triangle/Square/Saw/RandomSH), default 0
    kMod1RateId = 612,             // 0.01-20.0 Hz, default 1.0
    kMod1DepthId = 613,            // 0.0-1.0, default 0.0
    kMod1RangeStartId = 614,       // 1-96, default 1
    kMod1RangeEndId = 615,         // 1-96, default 96
    kMod1TargetId = 616,           // 0-2 (Amplitude/Frequency/Pan), default 0
    // Modulator 2
    kMod2EnableId = 620,           // 0/1, default 0
    kMod2WaveformId = 621,         // 0-4, default 0
    kMod2RateId = 622,             // 0.01-20.0 Hz, default 1.0
    kMod2DepthId = 623,            // 0.0-1.0, default 0.0
    kMod2RangeStartId = 624,       // 1-96, default 1
    kMod2RangeEndId = 625,         // 1-96, default 96
    kMod1RateSyncId = 617,         // Bool (stepCount=1), default 1.0 (synced)
    kMod1NoteValueId = 618,        // StringList (21 entries), default index 10 (1/8)
    kMod2TargetId = 626,           // 0-2, default 0
    kMod2RateSyncId = 627,         // Bool (stepCount=1), default 1.0 (synced)
    kMod2NoteValueId = 628,        // StringList (21 entries), default index 10 (1/8)
    // Detune
    kDetuneSpreadId = 630,         // 0.0-1.0, default 0.0
    // Multi-Source Blend
    kBlendEnableId = 640,          // 0/1, default 0
    kBlendSlotWeight1Id = 641,     // 0.0-1.0, default 0.0
    kBlendSlotWeight2Id = 642,
    kBlendSlotWeight3Id = 643,
    kBlendSlotWeight4Id = 644,
    kBlendSlotWeight5Id = 645,
    kBlendSlotWeight6Id = 646,
    kBlendSlotWeight7Id = 647,
    kBlendSlotWeight8Id = 648,
    kBlendLiveWeightId = 649,      // 0.0-1.0, default 0.0

    // Harmonic Physics (700-703) -- Spec A
    kWarmthId = 700,               // 0.0-1.0, default 0.0
    kCouplingId = 701,             // 0.0-1.0, default 0.0
    kStabilityId = 702,            // 0.0-1.0, default 0.0
    kEntropyId = 703,              // 0.0-1.0, default 0.0

    // Analysis Feedback Loop (710-711) -- Spec B
    kAnalysisFeedbackId = 710,     // 0.0-1.0, default 0.0
    kAnalysisFeedbackDecayId = 711, // 0.0-1.0, default 0.2

    // Voice Mode (730) -- MPE Support
    kVoiceModeId = 730,            // StringListParameter: "Mono"/"4 Voices"/"8 Voices", default 0 (Mono)

    // Physical Modelling (800-899) -- Spec 127
    kPhysModelMixId = 800,             // 0.0-1.0, default 0.0
    kResonanceDecayId = 801,           // 0.01-5.0s, log mapping, default 0.5
    kResonanceBrightnessId = 802,      // 0.0-1.0, default 0.5
    kResonanceStretchId = 803,         // 0.0-1.0, default 0.0
    kResonanceScatterId = 804,         // 0.0-1.0, default 0.0

    // ADSR Envelope (720-728) -- Spec 124
    kAdsrAttackId = 720,           // RangeParameter: 1-5000ms, log mapping, default 10ms
    kAdsrDecayId = 721,            // RangeParameter: 1-5000ms, log mapping, default 100ms
    kAdsrSustainId = 722,          // RangeParameter: 0.0-1.0, linear, default 1.0
    kAdsrReleaseId = 723,          // RangeParameter: 1-5000ms, log mapping, default 100ms
    kAdsrAmountId = 724,           // RangeParameter: 0.0-1.0, linear, default 0.0
    kAdsrTimeScaleId = 725,        // RangeParameter: 0.25-4.0, linear, default 1.0
    kAdsrAttackCurveId = 726,      // RangeParameter: -1.0 to +1.0, linear, default 0.0
    kAdsrDecayCurveId = 727,       // RangeParameter: -1.0 to +1.0, linear, default 0.0
    kAdsrReleaseCurveId = 728,     // RangeParameter: -1.0 to +1.0, linear, default 0.0
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

// EvolutionMode enum is defined in dsp/evolution_engine.h (FR-017)

// ModulatorWaveform and ModulatorTarget enums are defined in dsp/harmonic_modulator.h (FR-024)

} // namespace Innexus
