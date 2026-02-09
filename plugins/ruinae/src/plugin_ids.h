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

namespace Ruinae {

// Processor Component ID
// The audio processing component (runs on audio thread)
static const Steinberg::FUID kProcessorUID(0xA3B7C1D5, 0x2E4F6A8B, 0x9C0D1E2F, 0x3A4B5C6D);

// Controller Component ID
// The edit controller component (runs on UI thread)
static const Steinberg::FUID kControllerUID(0xD6C5B4A3, 0x8B6A4F2E, 0x2F1E0D9C, 0x6D5C4B3A);

// ==============================================================================
// Parameter IDs
// ==============================================================================
// Define all automatable parameters here.
// Constitution Principle V: All parameter values MUST be normalized (0.0 to 1.0)
//
// ID Range Allocation (100-ID gaps for future expansion):
//   0-99:      Global parameters (Mode, Polyphony, Master Gain, Soft Limit)
//   100-199:   OSC A (Type, Waveform, Tune, Detune, Level, Phase, ...)
//   200-299:   OSC B (Type, Waveform, Tune, Detune, Level, Phase, ...)
//   300-399:   Mixer (Mode, Position, SpectralMorphMode)
//   400-499:   Filter (Type, Cutoff, Resonance, EnvAmount, KeyTrack)
//   500-599:   Distortion (Type, Drive, Character, Mix)
//   600-699:   Trance Gate (Enabled, Pattern, Rate, Depth, Attack, Release, ...)
//   700-799:   Amp Envelope (Attack, Decay, Sustain, Release, Curves)
//   800-899:   Filter Envelope (Attack, Decay, Sustain, Release, Curves)
//   900-999:   Mod Envelope (Attack, Decay, Sustain, Release, Curves)
//   1000-1099: LFO 1 (Rate, Shape, Depth, Sync, ...)
//   1100-1199: LFO 2 (Rate, Shape, Depth, Sync, ...)
//   1200-1299: Chaos Mod (Rate, Type, Depth)
//   1300-1399: Modulation Matrix (Source, Dest, Amount x 8 slots)
//   1400-1499: Global Filter (Enabled, Type, Cutoff, Resonance)
//   1500-1599: Freeze Effect (Enabled, Freeze Toggle)
//   1600-1699: Delay Effect (Type, Time, Feedback, Mix, Sync, ...)
//   1700-1799: Reverb (Size, Damping, Width, Mix, PreDelay, ...)
//   1800-1899: Mono Mode (Priority, Legato, Portamento Time, PortaMode)
//   1900-1999: Reserved for expansion
// ==============================================================================

enum ParameterIDs : Steinberg::Vst::ParamID {
    // ==========================================================================
    // Global Parameters (0-99)
    // ==========================================================================
    kMasterGainId = 0,
    kVoiceModeId = 1,          // 0=Poly, 1=Mono
    kPolyphonyId = 2,          // 1-16 voices
    kSoftLimitId = 3,          // on/off
    kGlobalEndId = 99,

    // ==========================================================================
    // OSC A Parameters (100-199)
    // ==========================================================================
    kOscABaseId = 100,
    kOscATypeId = 100,         // OscType enum
    kOscATuneId = 101,         // -24 to +24 semitones
    kOscAFineId = 102,         // -100 to +100 cents
    kOscALevelId = 103,        // 0-1
    kOscAPhaseId = 104,        // 0-1
    // Type-specific params start at 110
    kOscAEndId = 199,

    // ==========================================================================
    // OSC B Parameters (200-299)
    // ==========================================================================
    kOscBBaseId = 200,
    kOscBTypeId = 200,         // OscType enum
    kOscBTuneId = 201,         // -24 to +24 semitones
    kOscBFineId = 202,         // -100 to +100 cents
    kOscBLevelId = 203,        // 0-1
    kOscBPhaseId = 204,        // 0-1
    // Type-specific params start at 210
    kOscBEndId = 299,

    // ==========================================================================
    // Mixer Parameters (300-399)
    // ==========================================================================
    kMixerBaseId = 300,
    kMixerModeId = 300,        // 0=Crossfade, 1=SpectralMorph
    kMixerPositionId = 301,    // 0=A, 1=B
    kMixerEndId = 399,

    // ==========================================================================
    // Filter Parameters (400-499)
    // ==========================================================================
    kFilterBaseId = 400,
    kFilterTypeId = 400,       // SVF/Ladder/Formant/Comb
    kFilterCutoffId = 401,     // 20-20000 Hz
    kFilterResonanceId = 402,  // 0.1-30.0
    kFilterEnvAmountId = 403,  // -48 to +48 semitones
    kFilterKeyTrackId = 404,   // 0-1
    kFilterEndId = 499,

    // ==========================================================================
    // Distortion Parameters (500-599)
    // ==========================================================================
    kDistortionBaseId = 500,
    kDistortionTypeId = 500,   // ChaosWaveshaper/Spectral/Granular/Wavefolder/Tape/Clean
    kDistortionDriveId = 501,  // 0-1
    kDistortionCharacterId = 502, // 0-1
    kDistortionMixId = 503,    // 0-1
    kDistortionEndId = 599,

    // ==========================================================================
    // Trance Gate Parameters (600-699)
    // ==========================================================================
    kTranceGateBaseId = 600,
    kTranceGateEnabledId = 600, // on/off
    kTranceGateNumStepsId = 601, // 2-32 (RangeParameter, stepCount=30)
    kTranceGateRateId = 602,   // Free-run rate Hz
    kTranceGateDepthId = 603,  // 0-1
    kTranceGateAttackId = 604, // 1-20ms
    kTranceGateReleaseId = 605, // 1-50ms
    kTranceGateTempoSyncId = 606, // on/off
    kTranceGateNoteValueId = 607, // step length
    kTranceGateEuclideanEnabledId = 608, // on/off toggle
    kTranceGateEuclideanHitsId = 609,    // 0-32 integer
    kTranceGateEuclideanRotationId = 610, // 0-31 integer
    kTranceGatePhaseOffsetId = 611,       // 0.0-1.0 continuous

    // Step level parameters: contiguous block of 32
    // Usage: kTranceGateStepLevel0Id + stepIndex
    kTranceGateStepLevel0Id = 668,
    kTranceGateStepLevel1Id = 669,
    kTranceGateStepLevel2Id = 670,
    kTranceGateStepLevel3Id = 671,
    kTranceGateStepLevel4Id = 672,
    kTranceGateStepLevel5Id = 673,
    kTranceGateStepLevel6Id = 674,
    kTranceGateStepLevel7Id = 675,
    kTranceGateStepLevel8Id = 676,
    kTranceGateStepLevel9Id = 677,
    kTranceGateStepLevel10Id = 678,
    kTranceGateStepLevel11Id = 679,
    kTranceGateStepLevel12Id = 680,
    kTranceGateStepLevel13Id = 681,
    kTranceGateStepLevel14Id = 682,
    kTranceGateStepLevel15Id = 683,
    kTranceGateStepLevel16Id = 684,
    kTranceGateStepLevel17Id = 685,
    kTranceGateStepLevel18Id = 686,
    kTranceGateStepLevel19Id = 687,
    kTranceGateStepLevel20Id = 688,
    kTranceGateStepLevel21Id = 689,
    kTranceGateStepLevel22Id = 690,
    kTranceGateStepLevel23Id = 691,
    kTranceGateStepLevel24Id = 692,
    kTranceGateStepLevel25Id = 693,
    kTranceGateStepLevel26Id = 694,
    kTranceGateStepLevel27Id = 695,
    kTranceGateStepLevel28Id = 696,
    kTranceGateStepLevel29Id = 697,
    kTranceGateStepLevel30Id = 698,
    kTranceGateStepLevel31Id = 699,
    kTranceGateEndId = 699,

    // ==========================================================================
    // Amp Envelope Parameters (700-799)
    // ==========================================================================
    kAmpEnvBaseId = 700,
    kAmpEnvAttackId = 700,     // 0-10000ms
    kAmpEnvDecayId = 701,      // 0-10000ms
    kAmpEnvSustainId = 702,    // 0-1
    kAmpEnvReleaseId = 703,    // 0-10000ms
    kAmpEnvEndId = 799,

    // ==========================================================================
    // Filter Envelope Parameters (800-899)
    // ==========================================================================
    kFilterEnvBaseId = 800,
    kFilterEnvAttackId = 800,
    kFilterEnvDecayId = 801,
    kFilterEnvSustainId = 802,
    kFilterEnvReleaseId = 803,
    kFilterEnvEndId = 899,

    // ==========================================================================
    // Mod Envelope Parameters (900-999)
    // ==========================================================================
    kModEnvBaseId = 900,
    kModEnvAttackId = 900,
    kModEnvDecayId = 901,
    kModEnvSustainId = 902,
    kModEnvReleaseId = 903,
    kModEnvEndId = 999,

    // ==========================================================================
    // LFO 1 Parameters (1000-1099)
    // ==========================================================================
    kLFO1BaseId = 1000,
    kLFO1RateId = 1000,        // 0.01-50 Hz
    kLFO1ShapeId = 1001,       // Sine/Tri/Saw/Sq/S&H/SmoothRandom
    kLFO1DepthId = 1002,       // 0-1
    kLFO1SyncId = 1003,        // on/off
    kLFO1EndId = 1099,

    // ==========================================================================
    // LFO 2 Parameters (1100-1199)
    // ==========================================================================
    kLFO2BaseId = 1100,
    kLFO2RateId = 1100,
    kLFO2ShapeId = 1101,
    kLFO2DepthId = 1102,
    kLFO2SyncId = 1103,
    kLFO2EndId = 1199,

    // ==========================================================================
    // Chaos Mod Parameters (1200-1299)
    // ==========================================================================
    kChaosModBaseId = 1200,
    kChaosModRateId = 1200,
    kChaosModTypeId = 1201,    // Lorenz/Rossler
    kChaosModDepthId = 1202,
    kChaosModEndId = 1299,

    // ==========================================================================
    // Modulation Matrix Parameters (1300-1399)
    // 8 slots x (Source + Dest + Amount) = 24 IDs
    // ==========================================================================
    kModMatrixBaseId = 1300,
    kModMatrixSlot0SourceId = 1300,
    kModMatrixSlot0DestId = 1301,
    kModMatrixSlot0AmountId = 1302,
    kModMatrixSlot1SourceId = 1303,
    kModMatrixSlot1DestId = 1304,
    kModMatrixSlot1AmountId = 1305,
    kModMatrixSlot2SourceId = 1306,
    kModMatrixSlot2DestId = 1307,
    kModMatrixSlot2AmountId = 1308,
    kModMatrixSlot3SourceId = 1309,
    kModMatrixSlot3DestId = 1310,
    kModMatrixSlot3AmountId = 1311,
    kModMatrixSlot4SourceId = 1312,
    kModMatrixSlot4DestId = 1313,
    kModMatrixSlot4AmountId = 1314,
    kModMatrixSlot5SourceId = 1315,
    kModMatrixSlot5DestId = 1316,
    kModMatrixSlot5AmountId = 1317,
    kModMatrixSlot6SourceId = 1318,
    kModMatrixSlot6DestId = 1319,
    kModMatrixSlot6AmountId = 1320,
    kModMatrixSlot7SourceId = 1321,
    kModMatrixSlot7DestId = 1322,
    kModMatrixSlot7AmountId = 1323,
    kModMatrixEndId = 1399,

    // ==========================================================================
    // Global Filter Parameters (1400-1499)
    // ==========================================================================
    kGlobalFilterBaseId = 1400,
    kGlobalFilterEnabledId = 1400,
    kGlobalFilterTypeId = 1401,
    kGlobalFilterCutoffId = 1402,
    kGlobalFilterResonanceId = 1403,
    kGlobalFilterEndId = 1499,

    // ==========================================================================
    // Freeze Effect Parameters (1500-1599)
    // ==========================================================================
    kFreezeBaseId = 1500,
    kFreezeEnabledId = 1500,
    kFreezeToggleId = 1501,
    kFreezeEndId = 1599,

    // ==========================================================================
    // Delay Effect Parameters (1600-1699)
    // ==========================================================================
    kDelayBaseId = 1600,
    kDelayTypeId = 1600,       // Digital/Tape/PingPong/Granular/Spectral
    kDelayTimeId = 1601,       // ms
    kDelayFeedbackId = 1602,   // 0-1.2
    kDelayMixId = 1603,        // 0-1
    kDelaySyncId = 1604,       // on/off
    kDelayNoteValueId = 1605,
    kDelayEndId = 1699,

    // ==========================================================================
    // Reverb Parameters (1700-1799)
    // ==========================================================================
    kReverbBaseId = 1700,
    kReverbSizeId = 1700,      // 0-1
    kReverbDampingId = 1701,   // 0-1
    kReverbWidthId = 1702,     // 0-1
    kReverbMixId = 1703,       // 0-1
    kReverbPreDelayId = 1704,  // 0-100ms
    kReverbDiffusionId = 1705, // 0-1
    kReverbFreezeId = 1706,    // on/off
    kReverbModRateId = 1707,   // 0-2 Hz
    kReverbModDepthId = 1708,  // 0-1
    kReverbEndId = 1799,

    // ==========================================================================
    // Mono Mode Parameters (1800-1899)
    // ==========================================================================
    kMonoBaseId = 1800,
    kMonoPriorityId = 1800,    // Last/High/Low
    kMonoLegatoId = 1801,      // on/off
    kMonoPortamentoTimeId = 1802, // 0-5000ms
    kMonoPortaModeId = 1803,   // Always/Legato
    kMonoEndId = 1899,

    // ==========================================================================
    kNumParameters = 2000,

    // ==========================================================================
    // UI Action Button Tags (NOT VST parameters - UI-only triggers)
    // ==========================================================================
    kActionPresetAllTag = 10000,
    kActionPresetOffTag = 10001,
    kActionPresetAlternateTag = 10002,
    kActionPresetRampUpTag = 10003,
    kActionPresetRampDownTag = 10004,
    kActionPresetRandomTag = 10005,
    kActionTransformInvertTag = 10006,
    kActionTransformShiftRightTag = 10007,
    kActionTransformShiftLeftTag = 10008,
    kActionEuclideanRegenTag = 10009,
};

// ==============================================================================
// Plugin Metadata
// ==============================================================================
// Note: Vendor info (company name, URL, email, copyright) is defined in
// version.h.in which CMake uses to generate version.h
// ==============================================================================

// VST3 Sub-categories (see VST3 SDK documentation for full list)
// Ruinae is an instrument (synthesizer)
constexpr const char* kSubCategories = "Instrument|Synth";

} // namespace Ruinae
