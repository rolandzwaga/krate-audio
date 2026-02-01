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
#include "delay_mode.h"  // DelayMode enum (SDK-independent)

namespace Iterum {

// Processor Component ID
// The audio processing component (runs on audio thread)
static const Steinberg::FUID kProcessorUID(0x12345678, 0x12345678, 0x12345678, 0x12345678);

// Controller Component ID
// The edit controller component (runs on UI thread)
static const Steinberg::FUID kControllerUID(0x87654321, 0x87654321, 0x87654321, 0x87654321);

// ==============================================================================
// Parameter IDs
// ==============================================================================
// Define all automatable parameters here.
// Constitution Principle V: All parameter values MUST be normalized (0.0 to 1.0)
//
// ID Range Allocation (100-ID gaps for future expansion):
//   0-99:      Global parameters
//   100-199:   Granular Delay (spec 034)
//   200-299:   Spectral Delay (spec 033)
//   300-399:   Shimmer Delay (spec 029)
//   400-499:   Tape Delay (spec 024)
//   500-599:   BBD Delay (spec 025)
//   600-699:   Digital Delay (spec 026)
//   700-799:   PingPong Delay (spec 027)
//   800-899:   Reverse Delay (spec 030)
//   900-999:   MultiTap Delay (spec 028)
//   1000-1099: Freeze Mode (spec 031)
// ==============================================================================

// Note: DelayMode enum is defined in delay_mode.h to allow use without VST3 SDK

enum ParameterIDs : Steinberg::Vst::ParamID {
    // ==========================================================================
    // Global Parameters (0-99)
    // ==========================================================================
    // Note: kBypassId removed - DAWs provide their own bypass functionality
    kGainId = 1,
    kModeId = 2,    // 0-9 (DelayMode enum) - selects active delay mode

    // ==========================================================================
    // Granular Delay Parameters (100-199) - spec 034
    // ==========================================================================
    kGranularBaseId = 100,
    kGranularGrainSizeId = 100,      // 10-500ms
    kGranularDensityId = 101,        // 1-100 grains/sec
    kGranularDelayTimeId = 102,      // 0-2000ms
    kGranularPitchId = 103,          // -24 to +24 semitones
    kGranularPitchSprayId = 104,     // 0-1
    kGranularPositionSprayId = 105,  // 0-1
    kGranularPanSprayId = 106,       // 0-1
    kGranularReverseProbId = 107,    // 0-1
    kGranularFreezeId = 108,         // on/off
    kGranularFeedbackId = 109,       // 0-1.2
    kGranularMixId = 110,            // 0-1 (renamed from kGranularDryWetId)
    kGranularEnvelopeTypeId = 112,   // 0-3 (Hann, Trapezoid, Sine, Blackman)
    kGranularTimeModeId = 113,       // 0-1 (Free, Synced) - spec 038
    kGranularNoteValueId = 114,      // 0-9 (note values) - spec 038
    kGranularJitterId = 115,         // 0-1 (timing randomness)
    kGranularPitchQuantId = 116,     // 0-4 (Off, Semitones, Octaves, Fifths, Scale)
    kGranularTextureId = 117,        // 0-1 (ordered to chaotic)
    kGranularStereoWidthId = 118,    // 0-1 (stereo decorrelation)
    kGranularEndId = 199,

    // ==========================================================================
    // Spectral Delay Parameters (200-299) - spec 033
    // ==========================================================================
    kSpectralBaseId = 200,
    kSpectralFFTSizeId = 200,        // 512, 1024, 2048, 4096
    kSpectralBaseDelayId = 201,      // 0-2000ms
    kSpectralSpreadId = 202,         // 0-2000ms
    kSpectralSpreadDirectionId = 203, // 0-2 (LowToHigh, HighToLow, CenterOut)
    kSpectralFeedbackId = 204,       // 0-1.2
    kSpectralFeedbackTiltId = 205,   // -1.0 to +1.0
    kSpectralFreezeId = 206,         // on/off
    kSpectralDiffusionId = 207,      // 0-1
    kSpectralMixId = 208,            // 0-100% (renamed from kSpectralDryWetId)
    kSpectralSpreadCurveId = 209,    // 0-1 (Linear, Logarithmic)
    kSpectralStereoWidthId = 210,    // 0-1 (stereo decorrelation amount)
    kSpectralTimeModeId = 211,       // 0=Free, 1=Synced (spec 041)
    kSpectralNoteValueId = 212,      // 0-9 note value dropdown (spec 041)
    kSpectralEndId = 299,

    // ==========================================================================
    // Shimmer Delay Parameters (300-399) - spec 029
    // ==========================================================================
    kShimmerBaseId = 300,
    kShimmerDelayTimeId = 300,        // 10-5000ms
    kShimmerPitchSemitonesId = 301,   // -24 to +24 semitones
    kShimmerPitchCentsId = 302,       // -100 to +100 cents
    kShimmerPitchBlendId = 303,       // 0-100% (renamed from kShimmerShimmerMixId)
    kShimmerFeedbackId = 304,         // 0-120%
    // Note: kShimmerDiffusionAmountId (305) removed - diffusion is always 100%
    kShimmerDiffusionSizeId = 306,    // 0-100%
    kShimmerFilterEnabledId = 307,    // on/off
    kShimmerFilterCutoffId = 308,     // 20-20000Hz
    kShimmerMixId = 309,              // 0-100% (renamed from kShimmerDryWetId)
    kShimmerTimeModeId = 310,         // 0=Free, 1=Synced (spec 043)
    kShimmerNoteValueId = 311,        // 0-9 (note value dropdown) (spec 043)
    kShimmerEndId = 399,

    // ==========================================================================
    // Tape Delay Parameters (400-499) - spec 024
    // ==========================================================================
    kTapeBaseId = 400,
    kTapeMotorSpeedId = 400,       // 20-2000ms (delay time)
    kTapeMotorInertiaId = 401,     // 100-1000ms
    kTapeWearId = 402,             // 0-100% (wow/flutter/hiss)
    kTapeSaturationId = 403,       // 0-100%
    kTapeAgeId = 404,              // 0-100%
    kTapeSpliceEnabledId = 405,    // on/off
    kTapeSpliceIntensityId = 406,  // 0-100%
    kTapeFeedbackId = 407,         // 0-120%
    kTapeMixId = 408,              // 0-100%
    kTapeHead1EnabledId = 410,     // on/off
    kTapeHead2EnabledId = 411,     // on/off
    kTapeHead3EnabledId = 412,     // on/off
    kTapeHead1LevelId = 413,       // -96 to +6 dB
    kTapeHead2LevelId = 414,       // -96 to +6 dB
    kTapeHead3LevelId = 415,       // -96 to +6 dB
    kTapeHead1PanId = 416,         // L100-R100
    kTapeHead2PanId = 417,         // L100-R100
    kTapeHead3PanId = 418,         // L100-R100
    kTapeEndId = 499,

    // ==========================================================================
    // BBD Delay Parameters (500-599) - spec 025
    // ==========================================================================
    kBBDBaseId = 500,
    kBBDDelayTimeId = 500,           // 20-1000ms
    kBBDFeedbackId = 501,            // 0-120%
    kBBDModDepthId = 502,            // 0-100% (renamed from kBBDModulationDepthId)
    kBBDModRateId = 503,             // 0.1-10Hz (renamed from kBBDModulationRateId)
    kBBDAgeId = 504,                 // 0-100%
    kBBDEraId = 505,                 // 0-3 (MN3005, MN3007, MN3205, SAD1024)
    kBBDMixId = 506,                 // 0-100%
    kBBDTimeModeId = 507,            // 0=Free, 1=Synced (spec 043)
    kBBDNoteValueId = 508,           // 0-9 (note value dropdown) (spec 043)
    kBBDEndId = 599,

    // ==========================================================================
    // Digital Delay Parameters (600-699) - spec 026
    // ==========================================================================
    kDigitalBaseId = 600,
    kDigitalDelayTimeId = 600,       // 1-10000ms
    kDigitalTimeModeId = 601,        // 0-1 (Free, Synced)
    kDigitalNoteValueId = 602,       // 0-9 (note values)
    kDigitalFeedbackId = 603,        // 0-120%
    kDigitalLimiterCharacterId = 604, // 0-2 (Soft, Medium, Hard)
    kDigitalEraId = 605,             // 0-2 (Pristine, 80s, LoFi)
    kDigitalAgeId = 606,             // 0-100%
    kDigitalModDepthId = 607,        // 0-100%
    kDigitalModRateId = 608,         // 0.1-10Hz
    kDigitalModWaveformId = 609,     // 0-5 (waveforms)
    kDigitalMixId = 610,             // 0-100%
    kDigitalWavefoldAmountId = 611,  // 0-100% (0 = disabled)
    kDigitalWidthId = 612,           // 0-200% (spec 036)
    kDigitalWavefoldTypeId = 613,    // 0-3 (Simple, Serge, Buchla, Lockhart)
    kDigitalWavefoldSymmetryId = 614, // -100 to +100%
    kDigitalEndId = 699,

    // ==========================================================================
    // PingPong Delay Parameters (700-799) - spec 027
    // ==========================================================================
    kPingPongBaseId = 700,
    kPingPongDelayTimeId = 700,      // 1-10000ms
    kPingPongTimeModeId = 701,       // 0-1 (Free, Synced)
    kPingPongNoteValueId = 702,      // 0-9 (note values)
    kPingPongLRRatioId = 703,        // 0-6 (ratio presets)
    kPingPongFeedbackId = 704,       // 0-120%
    kPingPongCrossFeedbackId = 705,  // 0-100%
    kPingPongWidthId = 706,          // 0-200%
    kPingPongModDepthId = 707,       // 0-100%
    kPingPongModRateId = 708,        // 0.1-10Hz
    kPingPongMixId = 709,            // 0-100%
    kPingPongEndId = 799,

    // ==========================================================================
    // Reverse Delay Parameters (800-899) - spec 030
    // ==========================================================================
    kReverseBaseId = 800,
    kReverseChunkSizeId = 800,       // 10-2000ms
    kReverseCrossfadeId = 801,       // 0-100%
    kReversePlaybackModeId = 802,    // 0-2 (FullReverse, Alternating, Random)
    kReverseFeedbackId = 803,        // 0-120%
    kReverseFilterEnabledId = 804,   // on/off
    kReverseFilterCutoffId = 805,    // 20-20000Hz
    kReverseFilterTypeId = 806,      // 0-2 (LowPass, HighPass, BandPass)
    kReverseMixId = 807,             // 0-100% (renamed from kReverseDryWetId)
    kReverseTimeModeId = 808,        // 0=Free, 1=Synced (spec 043)
    kReverseNoteValueId = 809,       // 0-9 (note value dropdown) (spec 043)
    kReverseEndId = 899,

    // ==========================================================================
    // MultiTap Delay Parameters (900-999) - spec 028
    // Simplified design: No TimeMode toggle, no Base Time slider, no Internal Tempo
    // - Rhythmic patterns (0-13): Use host tempo. Pattern name defines note value.
    // - Mathematical patterns (14-19): Use Note Value + host tempo for baseTimeMs.
    // ==========================================================================
    kMultiTapBaseId = 900,
    kMultiTapTimingPatternId = 900,  // 0-19 (pattern presets)
    kMultiTapSpatialPatternId = 901, // 0-6 (spatial presets)
    kMultiTapTapCountId = 902,       // 2-16 taps
    kMultiTapFeedbackId = 905,       // 0-110%
    kMultiTapFeedbackLPCutoffId = 906, // 20-20000Hz
    kMultiTapFeedbackHPCutoffId = 907, // 20-20000Hz
    kMultiTapMorphTimeId = 908,      // 50-2000ms
    kMultiTapMixId = 909,            // 0-100%
    kMultiTapNoteValueId = 911,      // 0-9 (note value) - for mathematical patterns only
    kMultiTapNoteModifierId = 912,   // 0-2 (none, triplet, dotted) - for mathematical patterns only
    kMultiTapSnapDivisionId = 913,   // 0-21 (off + 21 note values) - spec 046

    // Custom Pattern Editor UI Tags (920-929) - spec 046
    // Used for visibility control (not parameters, just UI control tags)
    kMultiTapPatternEditorTagId = 920,      // TapPatternEditor visibility tag
    kMultiTapCopyPatternButtonTagId = 921,  // CopyPatternButton visibility tag
    kMultiTapPatternEditorLabelTagId = 922, // Label visibility tag
    kMultiTapResetPatternButtonTagId = 923, // ResetPatternButton visibility tag

    // Custom Pattern Time Ratios (950-965) - spec 046
    kMultiTapCustomTime0Id = 950,
    kMultiTapCustomTime1Id = 951,
    kMultiTapCustomTime2Id = 952,
    kMultiTapCustomTime3Id = 953,
    kMultiTapCustomTime4Id = 954,
    kMultiTapCustomTime5Id = 955,
    kMultiTapCustomTime6Id = 956,
    kMultiTapCustomTime7Id = 957,
    kMultiTapCustomTime8Id = 958,
    kMultiTapCustomTime9Id = 959,
    kMultiTapCustomTime10Id = 960,
    kMultiTapCustomTime11Id = 961,
    kMultiTapCustomTime12Id = 962,
    kMultiTapCustomTime13Id = 963,
    kMultiTapCustomTime14Id = 964,
    kMultiTapCustomTime15Id = 965,

    // Custom Pattern Levels (966-981) - spec 046
    kMultiTapCustomLevel0Id = 966,
    kMultiTapCustomLevel1Id = 967,
    kMultiTapCustomLevel2Id = 968,
    kMultiTapCustomLevel3Id = 969,
    kMultiTapCustomLevel4Id = 970,
    kMultiTapCustomLevel5Id = 971,
    kMultiTapCustomLevel6Id = 972,
    kMultiTapCustomLevel7Id = 973,
    kMultiTapCustomLevel8Id = 974,
    kMultiTapCustomLevel9Id = 975,
    kMultiTapCustomLevel10Id = 976,
    kMultiTapCustomLevel11Id = 977,
    kMultiTapCustomLevel12Id = 978,
    kMultiTapCustomLevel13Id = 979,
    kMultiTapCustomLevel14Id = 980,
    kMultiTapCustomLevel15Id = 981,

    kMultiTapEndId = 999,

    // ==========================================================================
    // Freeze Mode Parameters (1000-1099) - spec 069 (Pattern Freeze)
    // Legacy shimmer/diffusion parameters (1001-1011, 1013-1014) removed in v0.12
    // ==========================================================================
    kFreezeBaseId = 1000,
    kFreezeMixId = 1012,              // 0-100% dry/wet mix

    // --------------------------------------------------------------------------
    // Pattern Freeze Parameters (1015-1060) - spec 069
    // --------------------------------------------------------------------------
    // Pattern Type & Core
    kFreezePatternTypeId = 1015,      // 0-3 (PatternType enum: Euclidean, GranularScatter, HarmonicDrones, NoiseBursts)
    kFreezeSliceLengthId = 1016,      // 10-2000ms
    kFreezeSliceModeId = 1017,        // 0-1 (SliceMode enum: Fixed, Variable)

    // Euclidean Parameters
    kFreezeEuclideanStepsId = 1020,   // 2-32
    kFreezeEuclideanHitsId = 1021,    // 1-steps
    kFreezeEuclideanRotationId = 1022, // 0-(steps-1)
    kFreezePatternRateId = 1023,      // 0-20 (NoteValue dropdown)

    // Granular Scatter Parameters
    kFreezeGranularDensityId = 1030,       // 1-50 Hz
    kFreezeGranularPositionJitterId = 1031, // 0-100%
    kFreezeGranularSizeJitterId = 1032,    // 0-100%
    kFreezeGranularGrainSizeId = 1033,     // 10-500ms

    // Harmonic Drones Parameters
    kFreezeDroneVoiceCountId = 1040,  // 1-4
    kFreezeDroneIntervalId = 1041,    // 0-5 (PitchInterval enum)
    kFreezeDroneDriftId = 1042,       // 0-100%
    kFreezeDroneDriftRateId = 1043,   // 0.1-2.0 Hz

    // Noise Bursts Parameters
    kFreezeNoiseColorId = 1050,       // 0-2 (NoiseColor enum: White, Pink, Brown)
    kFreezeNoiseBurstRateId = 1051,   // 0-20 (NoteValue dropdown)
    kFreezeNoiseFilterTypeId = 1052,  // 0-2 (LowPass, HighPass, BandPass)
    kFreezeNoiseFilterCutoffId = 1053, // 20-20000 Hz
    kFreezeNoiseFilterSweepId = 1054, // 0-100%

    // Envelope Parameters (shared across patterns)
    kFreezeEnvelopeAttackId = 1060,   // 0-500ms
    kFreezeEnvelopeReleaseId = 1061,  // 0-2000ms
    kFreezeEnvelopeShapeId = 1062,    // 0-1 (EnvelopeShape enum: Linear, Exponential)

    kFreezeEndId = 1099,

    // ==========================================================================
    kNumParameters = 1100
};

// ==============================================================================
// Plugin Metadata
// ==============================================================================
// Note: Vendor info (company name, URL, email, copyright) is defined in
// version.h.in which CMake uses to generate version.h
// ==============================================================================

// VST3 Sub-categories (see VST3 SDK documentation for full list)
// Examples: "Fx", "Instrument", "Analyzer", "Delay", "Reverb", etc.
constexpr const char* kSubCategories = "Delay";

} // namespace Iterum
