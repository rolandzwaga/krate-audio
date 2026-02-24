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
//   1500-1599: FX Enable (Delay, Reverb, Phaser, Harmonizer)
//   1600-1699: Delay Effect (Type, Time, Feedback, Mix, Sync, ...)
//   1700-1799: Reverb (Size, Damping, Width, Mix, PreDelay, ...)
//   1800-1899: Mono Mode (Priority, Legato, Portamento Time, PortaMode)
//   1900-1999: Phaser (Rate, Depth, Feedback, Mix, Stages, ...)
//   2000-2099: Macros (Macro 1-4 values)
//   2100-2199: Rungler (Osc1 Freq, Osc2 Freq, Depth, Filter, Bits, Loop Mode)
//   2200-2299: Settings (Pitch Bend Range, Velocity Curve, Tuning Ref, Alloc Mode, Steal Mode, Gain Comp)
//   2300-2399: Env Follower
//   2400-2499: Sample & Hold
//   2500-2599: Random
//   2600-2699: Pitch Follower
//   2700-2799: Transient Detector
//   2800-2899: Harmonizer (HarmonyMode, Key, Scale, PitchShiftMode,
//              FormantPreserve, NumVoices, DryLevel, WetLevel,
//              Voice1-4 Interval/Level/Pan/Delay/Detune)
//   3000-3099: Arpeggiator (Enabled, Mode, OctaveRange, OctaveMode,
//              TempoSync, NoteValue, FreeRate, GateLength, Swing,
//              LatchMode, Retrigger)
// ==============================================================================

enum ParameterIDs : Steinberg::Vst::ParamID {
    // ==========================================================================
    // ID Range Allocation:
    //   0-99:     Global (Master Gain, Voice Mode, Polyphony, Soft Limit, Width, Spread)
    //   100-199:  OSC A
    //   200-299:  OSC B
    //   300-399:  Mixer
    //   400-499:  Filter
    //   500-599:  Distortion
    //   600-699:  Trance Gate
    //   700-799:  Amp Envelope
    //   800-899:  Filter Envelope
    //   900-999:  Mod Envelope
    //   1000-1099: LFO 1
    //   1100-1199: LFO 2
    //   1200-1299: Chaos Mod
    //   1300-1399: Mod Matrix
    //   1400-1499: Global Filter
    //   1500-1503: FX Enables (Delay, Reverb, Phaser, Harmonizer)
    //   1600-1699: Delay
    //   1700-1799: Reverb
    //   1800-1899: Mono Mode
    //   1900-1999: Phaser
    //   2000-2099: Macros
    //   2100-2199: Rungler
    //   2200-2299: Settings (Pitch Bend Range, Velocity Curve, Tuning Ref, Alloc Mode, Steal Mode, Gain Comp)
    //   2300-2399: Env Follower
    //   2400-2499: Sample & Hold
    //   2500-2599: Random
    //   2600-2699: Pitch Follower
    //   2700-2799: Transient Detector
    //   2800-2899: Harmonizer
    //   3000-3099: Arpeggiator
    // ==========================================================================

    // ==========================================================================
    // Global Parameters (0-99)
    // ==========================================================================
    kMasterGainId = 0,
    kVoiceModeId = 1,          // 0=Poly, 1=Mono
    kPolyphonyId = 2,          // 1-16 voices
    kSoftLimitId = 3,          // on/off
    kWidthId = 4,              // 0-200% stereo width (norm * 2.0)
    kSpreadId = 5,             // 0-100% voice spread
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

    // Type-specific params (110-139) -- 068-osc-type-params
    kOscAWaveformId = 110,     // PolyBLEP waveform (5 values: Sine/Saw/Square/Pulse/Triangle)
    kOscAPulseWidthId = 111,   // PolyBLEP pulse width (0.01-0.99)
    kOscAPhaseModId = 112,     // Phase modulation (shared PolyBLEP/Wavetable) (-1 to +1)
    kOscAFreqModId = 113,      // Frequency modulation (shared PolyBLEP/Wavetable) (-1 to +1)
    kOscAPDWaveformId = 114,   // PD waveform (8 values)
    kOscAPDDistortionId = 115, // PD distortion/DCW (0-1)
    kOscASyncRatioId = 116,    // Sync slave ratio (1.0-8.0)
    kOscASyncWaveformId = 117, // Sync slave waveform (5 values)
    kOscASyncModeId = 118,     // Sync mode (3 values: Hard/Reverse/PhaseAdvance)
    kOscASyncAmountId = 119,   // Sync amount (0-1)
    kOscASyncPulseWidthId = 120, // Sync slave pulse width (0.01-0.99)
    kOscAAdditivePartialsId = 121, // Additive num partials (1-128)
    kOscAAdditiveTiltId = 122, // Additive spectral tilt (-24 to +24 dB/oct)
    kOscAAdditiveInharmId = 123, // Additive inharmonicity (0-1)
    kOscAChaosAttractorId = 124, // Chaos attractor (5 values)
    kOscAChaosAmountId = 125,  // Chaos amount (0-1)
    kOscAChaosCouplingId = 126, // Chaos coupling (0-1)
    kOscAChaosOutputId = 127,  // Chaos output axis (3 values: X/Y/Z)
    kOscAParticleScatterId = 128, // Particle freq scatter (0-12 st)
    kOscAParticleDensityId = 129, // Particle density (1-64)
    kOscAParticleLifetimeId = 130, // Particle lifetime (5-2000 ms)
    kOscAParticleSpawnModeId = 131, // Particle spawn mode (3 values)
    kOscAParticleEnvTypeId = 132, // Particle envelope type (6 values)
    kOscAParticleDriftId = 133, // Particle drift (0-1)
    kOscAFormantVowelId = 134, // Formant vowel (5 values: A/E/I/O/U)
    kOscAFormantMorphId = 135, // Formant morph (0-4)
    kOscASpectralPitchId = 136, // Spectral pitch shift (-24 to +24 st)
    kOscASpectralTiltId = 137, // Spectral tilt (-12 to +12 dB/oct)
    kOscASpectralFormantId = 138, // Spectral formant shift (-12 to +12 st)
    kOscANoiseColorId = 139,   // Noise color (6 values: White/Pink/Brown/Blue/Violet/Grey)

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

    // Type-specific params (210-239) -- 068-osc-type-params (mirrors OSC A +100)
    kOscBWaveformId = 210,
    kOscBPulseWidthId = 211,
    kOscBPhaseModId = 212,
    kOscBFreqModId = 213,
    kOscBPDWaveformId = 214,
    kOscBPDDistortionId = 215,
    kOscBSyncRatioId = 216,
    kOscBSyncWaveformId = 217,
    kOscBSyncModeId = 218,
    kOscBSyncAmountId = 219,
    kOscBSyncPulseWidthId = 220,
    kOscBAdditivePartialsId = 221,
    kOscBAdditiveTiltId = 222,
    kOscBAdditiveInharmId = 223,
    kOscBChaosAttractorId = 224,
    kOscBChaosAmountId = 225,
    kOscBChaosCouplingId = 226,
    kOscBChaosOutputId = 227,
    kOscBParticleScatterId = 228,
    kOscBParticleDensityId = 229,
    kOscBParticleLifetimeId = 230,
    kOscBParticleSpawnModeId = 231,
    kOscBParticleEnvTypeId = 232,
    kOscBParticleDriftId = 233,
    kOscBFormantVowelId = 234,
    kOscBFormantMorphId = 235,
    kOscBSpectralPitchId = 236,
    kOscBSpectralTiltId = 237,
    kOscBSpectralFormantId = 238,
    kOscBNoiseColorId = 239,

    kOscBEndId = 299,

    // ==========================================================================
    // Mixer Parameters (300-399)
    // ==========================================================================
    kMixerBaseId = 300,
    kMixerModeId = 300,        // 0=Crossfade, 1=SpectralMorph
    kMixerPositionId = 301,    // 0=A, 1=B
    kMixerTiltId = 302,        // Spectral tilt [-12, +12] dB/oct
    kMixerShiftId = 303,       // Spectral frequency shift
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
    // Type-specific params (405-409)
    kFilterLadderSlopeId = 405,    // 1-4 poles (6/12/18/24 dB/oct)
    kFilterLadderDriveId = 406,    // 0-24 dB
    kFilterFormantMorphId = 407,   // 0-4 (A=0, E=1, I=2, O=3, U=4)
    kFilterFormantGenderId = 408,  // -1 to +1
    kFilterCombDampingId = 409,    // 0-1
    kFilterSvfSlopeId = 410,       // 12dB or 24dB (cascaded 2-pole)
    kFilterSvfDriveId = 411,       // 0-24 dB post-filter saturation
    kFilterSvfGainId = 412,        // -24 to +24 dB (Peak/LowShelf/HighShelf)
    // Envelope filter (auto-wah) type-specific (413-418)
    kFilterEnvFltSubTypeId = 413,     // 0=LP, 1=BP, 2=HP
    kFilterEnvFltSensitivityId = 414, // -24 to +24 dB
    kFilterEnvFltDepthId = 415,       // 0.0 to 1.0
    kFilterEnvFltAttackId = 416,      // 0.1 to 500 ms
    kFilterEnvFltReleaseId = 417,     // 1 to 5000 ms
    kFilterEnvFltDirectionId = 418,   // 0=Up, 1=Down
    // Self-oscillating filter type-specific (421-424)
    kFilterSelfOscGlideId = 421,   // 0 to 5000 ms
    kFilterSelfOscExtMixId = 422,  // 0.0 to 1.0
    kFilterSelfOscShapeId = 423,   // 0.0 to 1.0
    kFilterSelfOscReleaseId = 424, // 10 to 2000 ms
    kFilterEndId = 499,

    // ==========================================================================
    // Distortion Parameters (500-599)
    // ==========================================================================
    kDistortionBaseId = 500,
    kDistortionTypeId = 500,   // ChaosWaveshaper/Spectral/Granular/Wavefolder/Tape/Clean
    kDistortionDriveId = 501,  // 0-1
    kDistortionCharacterId = 502, // 0-1 (DEAD CODE - reserved for backward compat)
    kDistortionMixId = 503,    // 0-1

    // Chaos Waveshaper type-specific params
    kDistortionChaosModelId = 510,     // 0-3 (Lorenz/Rossler/Chua/Henon)
    kDistortionChaosSpeedId = 511,     // 0-1 mapped to [0.01, 100]
    kDistortionChaosCouplingId = 512,  // 0-1

    // Spectral Distortion type-specific params
    kDistortionSpectralModeId = 520,   // 0-3 (PerBinSaturate/MagnitudeOnly/BinSelective/SpectralBitcrush)
    kDistortionSpectralCurveId = 521,  // 0-8 (9 waveshape types)
    kDistortionSpectralBitsId = 522,   // 0-1 mapped to [1, 16]

    // Granular Distortion type-specific params
    kDistortionGrainSizeId = 530,      // 0-1 mapped to [5, 100] ms
    kDistortionGrainDensityId = 531,   // 0-1 mapped to [1, 8]
    kDistortionGrainVariationId = 532, // 0-1
    kDistortionGrainJitterId = 533,    // 0-1 mapped to [0, 50] ms

    // Wavefolder type-specific params
    kDistortionFoldTypeId = 540,       // 0-2 (Triangle/Sine/Lockhart)

    // Tape Saturator type-specific params
    kDistortionTapeModelId = 550,      // 0-1 (Simple/Hysteresis)
    kDistortionTapeSaturationId = 551, // 0-1
    kDistortionTapeBiasId = 552,       // 0-1 mapped to [-1, +1]

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
    // Curve amounts [-1, +1]
    kAmpEnvAttackCurveId = 704,
    kAmpEnvDecayCurveId = 705,
    kAmpEnvReleaseCurveId = 706,
    // Bezier mode flag (0 or 1)
    kAmpEnvBezierEnabledId = 707,
    // Bezier control points [0, 1] (3 segments x 2 handles x 2 axes = 12)
    kAmpEnvBezierAttackCp1XId = 710,
    kAmpEnvBezierAttackCp1YId = 711,
    kAmpEnvBezierAttackCp2XId = 712,
    kAmpEnvBezierAttackCp2YId = 713,
    kAmpEnvBezierDecayCp1XId = 714,
    kAmpEnvBezierDecayCp1YId = 715,
    kAmpEnvBezierDecayCp2XId = 716,
    kAmpEnvBezierDecayCp2YId = 717,
    kAmpEnvBezierReleaseCp1XId = 718,
    kAmpEnvBezierReleaseCp1YId = 719,
    kAmpEnvBezierReleaseCp2XId = 720,
    kAmpEnvBezierReleaseCp2YId = 721,
    kAmpEnvEndId = 799,

    // ==========================================================================
    // Filter Envelope Parameters (800-899)
    // ==========================================================================
    kFilterEnvBaseId = 800,
    kFilterEnvAttackId = 800,
    kFilterEnvDecayId = 801,
    kFilterEnvSustainId = 802,
    kFilterEnvReleaseId = 803,
    // Curve amounts [-1, +1]
    kFilterEnvAttackCurveId = 804,
    kFilterEnvDecayCurveId = 805,
    kFilterEnvReleaseCurveId = 806,
    // Bezier mode flag (0 or 1)
    kFilterEnvBezierEnabledId = 807,
    // Bezier control points [0, 1] (3 segments x 2 handles x 2 axes = 12)
    kFilterEnvBezierAttackCp1XId = 810,
    kFilterEnvBezierAttackCp1YId = 811,
    kFilterEnvBezierAttackCp2XId = 812,
    kFilterEnvBezierAttackCp2YId = 813,
    kFilterEnvBezierDecayCp1XId = 814,
    kFilterEnvBezierDecayCp1YId = 815,
    kFilterEnvBezierDecayCp2XId = 816,
    kFilterEnvBezierDecayCp2YId = 817,
    kFilterEnvBezierReleaseCp1XId = 818,
    kFilterEnvBezierReleaseCp1YId = 819,
    kFilterEnvBezierReleaseCp2XId = 820,
    kFilterEnvBezierReleaseCp2YId = 821,
    kFilterEnvEndId = 899,

    // ==========================================================================
    // Mod Envelope Parameters (900-999)
    // ==========================================================================
    kModEnvBaseId = 900,
    kModEnvAttackId = 900,
    kModEnvDecayId = 901,
    kModEnvSustainId = 902,
    kModEnvReleaseId = 903,
    // Curve amounts [-1, +1]
    kModEnvAttackCurveId = 904,
    kModEnvDecayCurveId = 905,
    kModEnvReleaseCurveId = 906,
    // Bezier mode flag (0 or 1)
    kModEnvBezierEnabledId = 907,
    // Bezier control points [0, 1] (3 segments x 2 handles x 2 axes = 12)
    kModEnvBezierAttackCp1XId = 910,
    kModEnvBezierAttackCp1YId = 911,
    kModEnvBezierAttackCp2XId = 912,
    kModEnvBezierAttackCp2YId = 913,
    kModEnvBezierDecayCp1XId = 914,
    kModEnvBezierDecayCp1YId = 915,
    kModEnvBezierDecayCp2XId = 916,
    kModEnvBezierDecayCp2YId = 917,
    kModEnvBezierReleaseCp1XId = 918,
    kModEnvBezierReleaseCp1YId = 919,
    kModEnvBezierReleaseCp2XId = 920,
    kModEnvBezierReleaseCp2YId = 921,
    kModEnvEndId = 999,

    // ==========================================================================
    // LFO 1 Parameters (1000-1099)
    // ==========================================================================
    kLFO1BaseId = 1000,
    kLFO1RateId = 1000,        // 0.01-50 Hz
    kLFO1ShapeId = 1001,       // Sine/Tri/Saw/Sq/S&H/SmoothRandom
    kLFO1DepthId = 1002,       // 0-1
    kLFO1SyncId = 1003,        // on/off
    kLFO1PhaseOffsetId = 1004, // 0-360 degrees
    kLFO1RetriggerId = 1005,   // on/off
    kLFO1NoteValueId = 1006,   // note value dropdown (21 entries)
    kLFO1UnipolarId = 1007,    // bipolar/unipolar toggle
    kLFO1FadeInId = 1008,      // 0-5000 ms fade-in time
    kLFO1SymmetryId = 1009,    // 0-100% waveform skew
    kLFO1QuantizeId = 1010,    // 0=off, 2-16 steps
    kLFO1EndId = 1099,

    // ==========================================================================
    // LFO 2 Parameters (1100-1199)
    // ==========================================================================
    kLFO2BaseId = 1100,
    kLFO2RateId = 1100,
    kLFO2ShapeId = 1101,
    kLFO2DepthId = 1102,
    kLFO2SyncId = 1103,
    kLFO2PhaseOffsetId = 1104,
    kLFO2RetriggerId = 1105,
    kLFO2NoteValueId = 1106,
    kLFO2UnipolarId = 1107,
    kLFO2FadeInId = 1108,
    kLFO2SymmetryId = 1109,
    kLFO2QuantizeId = 1110,
    kLFO2EndId = 1199,

    // ==========================================================================
    // Chaos Mod Parameters (1200-1299)
    // ==========================================================================
    kChaosModBaseId = 1200,
    kChaosModRateId = 1200,
    kChaosModTypeId = 1201,    // Lorenz/Rossler
    kChaosModDepthId = 1202,
    kChaosModSyncId = 1203,    // Tempo sync on/off
    kChaosModNoteValueId = 1204, // Note value dropdown (21 entries)
    kChaosModEndId = 1299,

    // ==========================================================================
    // Modulation Matrix Parameters (1300-1399)
    // Base: 8 slots x (Source + Dest + Amount) = 24 IDs (1300-1323)
    // Detail: 8 slots x (Curve + Smooth + Scale + Bypass) = 32 IDs (1324-1355)
    // Total: 56 parameters (spec 049)
    // ==========================================================================
    kModMatrixBaseId = 1300,

    // --- Base Parameters: Source, Destination, Amount (3 per slot) ---
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

    // --- Detail Parameters: Curve, Smooth, Scale, Bypass (4 per slot) ---
    // Formula: Curve = 1324 + slot*4, Smooth = 1325 + slot*4,
    //          Scale = 1326 + slot*4, Bypass = 1327 + slot*4
    kModMatrixDetailBaseId = 1324,
    kModMatrixSlot0CurveId = 1324,
    kModMatrixSlot0SmoothId = 1325,
    kModMatrixSlot0ScaleId = 1326,
    kModMatrixSlot0BypassId = 1327,
    kModMatrixSlot1CurveId = 1328,
    kModMatrixSlot1SmoothId = 1329,
    kModMatrixSlot1ScaleId = 1330,
    kModMatrixSlot1BypassId = 1331,
    kModMatrixSlot2CurveId = 1332,
    kModMatrixSlot2SmoothId = 1333,
    kModMatrixSlot2ScaleId = 1334,
    kModMatrixSlot2BypassId = 1335,
    kModMatrixSlot3CurveId = 1336,
    kModMatrixSlot3SmoothId = 1337,
    kModMatrixSlot3ScaleId = 1338,
    kModMatrixSlot3BypassId = 1339,
    kModMatrixSlot4CurveId = 1340,
    kModMatrixSlot4SmoothId = 1341,
    kModMatrixSlot4ScaleId = 1342,
    kModMatrixSlot4BypassId = 1343,
    kModMatrixSlot5CurveId = 1344,
    kModMatrixSlot5SmoothId = 1345,
    kModMatrixSlot5ScaleId = 1346,
    kModMatrixSlot5BypassId = 1347,
    kModMatrixSlot6CurveId = 1348,
    kModMatrixSlot6SmoothId = 1349,
    kModMatrixSlot6ScaleId = 1350,
    kModMatrixSlot6BypassId = 1351,
    kModMatrixSlot7CurveId = 1352,
    kModMatrixSlot7SmoothId = 1353,
    kModMatrixSlot7ScaleId = 1354,
    kModMatrixSlot7BypassId = 1355,
    kModMatrixDetailEndId = 1355,

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
    // FX Enable Parameters (1500-1503)
    // ==========================================================================
    kDelayEnabledId = 1500,    // on/off (default: on)
    kReverbEnabledId = 1501,   // on/off (default: on)
    kPhaserEnabledId = 1502,   // on/off (default: on)
    kHarmonizerEnabledId = 1503, // on/off (default: off)

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

    // Digital delay type-specific (1606-1615)
    kDelayDigitalEraId = 1606,            // DigitalEra (0-2)
    kDelayDigitalAgeId = 1607,            // 0-1 (0-100%)
    kDelayDigitalLimiterId = 1608,        // LimiterCharacter (0-2)
    kDelayDigitalModDepthId = 1609,       // 0-1
    kDelayDigitalModRateId = 1610,        // 0.1-10 Hz
    kDelayDigitalModWaveformId = 1611,    // Waveform (0-5)
    kDelayDigitalWidthId = 1612,          // 0-200%
    kDelayDigitalWavefoldAmountId = 1613, // 0-100%
    kDelayDigitalWavefoldModelId = 1614,  // WavefolderModel (0-3)
    kDelayDigitalWavefoldSymmetryId = 1615, // -1 to +1

    // Tape delay type-specific (1626-1640)
    kDelayTapeMotorInertiaId = 1626,      // 100-1000 ms
    kDelayTapeWearId = 1627,              // 0-1
    kDelayTapeSaturationId = 1628,        // 0-1
    kDelayTapeAgeId = 1629,              // 0-1
    kDelayTapeSpliceEnabledId = 1630,     // on/off
    kDelayTapeSpliceIntensityId = 1631,   // 0-1
    kDelayTapeHead1EnabledId = 1632,      // on/off
    kDelayTapeHead1LevelId = 1633,        // -96 to +6 dB
    kDelayTapeHead1PanId = 1634,          // -100 to +100
    kDelayTapeHead2EnabledId = 1635,      // on/off
    kDelayTapeHead2LevelId = 1636,        // -96 to +6 dB
    kDelayTapeHead2PanId = 1637,          // -100 to +100
    kDelayTapeHead3EnabledId = 1638,      // on/off
    kDelayTapeHead3LevelId = 1639,        // -96 to +6 dB
    kDelayTapeHead3PanId = 1640,          // -100 to +100

    // Granular delay type-specific (1646-1658)
    kDelayGranularSizeId = 1646,          // 10-500 ms
    kDelayGranularDensityId = 1647,       // 1-100 grains/s
    kDelayGranularPitchId = 1648,         // -24 to +24 st
    kDelayGranularPitchSprayId = 1649,    // 0-1
    kDelayGranularPitchQuantId = 1650,    // PitchQuantMode (0-4)
    kDelayGranularPositionSprayId = 1651, // 0-1
    kDelayGranularReverseProbId = 1652,   // 0-1
    kDelayGranularPanSprayId = 1653,      // 0-1
    kDelayGranularJitterId = 1654,        // 0-1
    kDelayGranularTextureId = 1655,       // 0-1
    kDelayGranularWidthId = 1656,         // 0-1
    kDelayGranularEnvelopeId = 1657,      // GrainEnvelopeType (0-5)
    kDelayGranularFreezeId = 1658,        // on/off

    // Spectral delay type-specific (1666-1673)
    kDelaySpectralFFTSizeId = 1666,       // 512/1024/2048/4096
    kDelaySpectralSpreadId = 1667,        // 0-2000 ms
    kDelaySpectralDirectionId = 1668,     // SpreadDirection (0-2)
    kDelaySpectralCurveId = 1669,         // SpreadCurve (0-1)
    kDelaySpectralTiltId = 1670,          // -1 to +1
    kDelaySpectralDiffusionId = 1671,     // 0-1
    kDelaySpectralWidthId = 1672,         // 0-1
    kDelaySpectralFreezeId = 1673,        // on/off

    // PingPong delay type-specific (1686-1690)
    kDelayPingPongRatioId = 1686,         // LRRatio (0-6)
    kDelayPingPongCrossFeedId = 1687,     // 0-1
    kDelayPingPongWidthId = 1688,         // 0-200%
    kDelayPingPongModDepthId = 1689,      // 0-1
    kDelayPingPongModRateId = 1690,       // 0.1-10 Hz

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
    // Phaser Parameters (1900-1999)
    // ==========================================================================
    kPhaserBaseId = 1900,
    kPhaserRateId = 1900,           // 0.01-20 Hz
    kPhaserDepthId = 1901,          // 0-1
    kPhaserFeedbackId = 1902,       // -1 to +1
    kPhaserMixId = 1903,            // 0-1
    kPhaserStagesId = 1904,         // 2/4/6/8/10/12 (dropdown)
    kPhaserCenterFreqId = 1905,     // 100-10000 Hz
    kPhaserStereoSpreadId = 1906,   // 0-360 degrees
    kPhaserWaveformId = 1907,       // Sine/Triangle/Square/Sawtooth (dropdown)
    kPhaserSyncId = 1908,           // on/off
    kPhaserNoteValueId = 1909,      // Note value (dropdown)
    kPhaserEndId = 1999,

    // ==========================================================================
    // Macro Parameters (2000-2099)
    // ==========================================================================
    kMacroBaseId = 2000,
    kMacro1ValueId = 2000,     // Macro 1 knob value [0, 1] (default 0)
    kMacro2ValueId = 2001,     // Macro 2 knob value [0, 1] (default 0)
    kMacro3ValueId = 2002,     // Macro 3 knob value [0, 1] (default 0)
    kMacro4ValueId = 2003,     // Macro 4 knob value [0, 1] (default 0)
    kMacroEndId = 2099,

    // ==========================================================================
    // Rungler Parameters (2100-2199)
    // ==========================================================================
    kRunglerBaseId = 2100,
    kRunglerOsc1FreqId = 2100, // Osc1 frequency [0.1, 100] Hz (default 2.0 Hz)
    kRunglerOsc2FreqId = 2101, // Osc2 frequency [0.1, 100] Hz (default 3.0 Hz)
    kRunglerDepthId = 2102,    // Cross-mod depth [0, 1] (default 0)
    kRunglerFilterId = 2103,   // CV filter amount [0, 1] (default 0)
    kRunglerBitsId = 2104,     // Shift register bits [4, 16] (default 8)
    kRunglerLoopModeId = 2105, // Loop mode on/off (default off = chaos)
    kRunglerEndId = 2199,

    // ==========================================================================
    // Settings Parameters (2200-2299)
    // ==========================================================================
    kSettingsBaseId = 2200,
    kSettingsPitchBendRangeId = 2200,  // Pitch bend range [0, 24] semitones (default 2)
    kSettingsVelocityCurveId = 2201,   // Velocity curve (4 options: Linear/Soft/Hard/Fixed, default 0 = Linear)
    kSettingsTuningReferenceId = 2202, // A4 tuning reference [400, 480] Hz (default 440)
    kSettingsVoiceAllocModeId = 2203,  // Voice allocation (4 options: RR/Oldest/LowVel/HighNote, default 1 = Oldest)
    kSettingsVoiceStealModeId = 2204,  // Voice steal (2 options: Hard/Soft, default 0 = Hard)
    kSettingsGainCompensationId = 2205, // Gain compensation on/off (default 1 = enabled for new presets)
    kSettingsEndId = 2299,

    // ==========================================================================
    // Env Follower Parameters (2300-2399)
    // ==========================================================================
    kEnvFollowerBaseId = 2300,
    kEnvFollowerSensitivityId = 2300, // Sensitivity [0, 1] (default 0.5)
    kEnvFollowerAttackId = 2301,      // Attack time [0.1, 500] ms (default 10 ms)
    kEnvFollowerReleaseId = 2302,     // Release time [1, 5000] ms (default 100 ms)
    kEnvFollowerEndId = 2399,

    // ==========================================================================
    // Sample & Hold Parameters (2400-2499)
    // ==========================================================================
    kSampleHoldBaseId = 2400,
    kSampleHoldRateId = 2400,         // Rate [0.1, 50] Hz (default 4 Hz)
    kSampleHoldSyncId = 2401,         // Tempo sync on/off (default off)
    kSampleHoldNoteValueId = 2402,    // Note value dropdown (default 1/8)
    kSampleHoldSlewId = 2403,         // Slew time [0, 500] ms (default 0 ms)
    kSampleHoldEndId = 2499,

    // ==========================================================================
    // Random Parameters (2500-2599)
    // ==========================================================================
    kRandomBaseId = 2500,
    kRandomRateId = 2500,             // Rate [0.1, 50] Hz (default 4 Hz)
    kRandomSyncId = 2501,             // Tempo sync on/off (default off)
    kRandomNoteValueId = 2502,        // Note value dropdown (default 1/8)
    kRandomSmoothnessId = 2503,       // Smoothness [0, 1] (default 0)
    kRandomEndId = 2599,

    // ==========================================================================
    // Pitch Follower Parameters (2600-2699)
    // ==========================================================================
    kPitchFollowerBaseId = 2600,
    kPitchFollowerMinHzId = 2600,     // Min frequency [20, 500] Hz (default 80 Hz)
    kPitchFollowerMaxHzId = 2601,     // Max frequency [200, 5000] Hz (default 2000 Hz)
    kPitchFollowerConfidenceId = 2602, // Confidence [0, 1] (default 0.5)
    kPitchFollowerSpeedId = 2603,     // Tracking speed [10, 300] ms (default 50 ms)
    kPitchFollowerEndId = 2699,

    // ==========================================================================
    // Transient Detector Parameters (2700-2799)
    // ==========================================================================
    kTransientBaseId = 2700,
    kTransientSensitivityId = 2700,   // Sensitivity [0, 1] (default 0.5)
    kTransientAttackId = 2701,        // Attack time [0.5, 10] ms (default 2 ms)
    kTransientDecayId = 2702,         // Decay time [20, 200] ms (default 50 ms)
    kTransientEndId = 2799,

    // ==========================================================================
    // Harmonizer Parameters (2800-2899)
    // ==========================================================================
    kHarmonizerBaseId = 2800,

    // Global harmonizer params (2800-2807)
    kHarmonizerHarmonyModeId = 2800,    // Chromatic/Scalic (dropdown, 2 entries)
    kHarmonizerKeyId = 2801,             // C through B (dropdown, 12 entries)
    kHarmonizerScaleId = 2802,           // 9 scale types (dropdown, 9 entries)
    kHarmonizerPitchShiftModeId = 2803,  // Simple/Granular/PhaseVocoder/PitchSync (dropdown, 4 entries)
    kHarmonizerFormantPreserveId = 2804, // on/off toggle
    kHarmonizerNumVoicesId = 2805,       // 1-4 (dropdown, 4 entries)
    kHarmonizerDryLevelId = 2806,        // -60 to +6 dB (default 0 dB = norm ~0.909)
    kHarmonizerWetLevelId = 2807,        // -60 to +6 dB (default -6 dB = norm ~0.818)

    // Per-voice params: Voice 1 (2810-2814)
    kHarmonizerVoice1IntervalId = 2810,  // -24 to +24 diatonic steps (default 0)
    kHarmonizerVoice1LevelId = 2811,     // -60 to +6 dB (default 0 dB)
    kHarmonizerVoice1PanId = 2812,       // -1 to +1 (default 0 = center)
    kHarmonizerVoice1DelayId = 2813,     // 0 to 50 ms (default 0)
    kHarmonizerVoice1DetuneId = 2814,    // -50 to +50 cents (default 0)

    // Per-voice params: Voice 2 (2820-2824)
    kHarmonizerVoice2IntervalId = 2820,
    kHarmonizerVoice2LevelId = 2821,
    kHarmonizerVoice2PanId = 2822,
    kHarmonizerVoice2DelayId = 2823,
    kHarmonizerVoice2DetuneId = 2824,

    // Per-voice params: Voice 3 (2830-2834)
    kHarmonizerVoice3IntervalId = 2830,
    kHarmonizerVoice3LevelId = 2831,
    kHarmonizerVoice3PanId = 2832,
    kHarmonizerVoice3DelayId = 2833,
    kHarmonizerVoice3DetuneId = 2834,

    // Per-voice params: Voice 4 (2840-2844)
    kHarmonizerVoice4IntervalId = 2840,
    kHarmonizerVoice4LevelId = 2841,
    kHarmonizerVoice4PanId = 2842,
    kHarmonizerVoice4DelayId = 2843,
    kHarmonizerVoice4DetuneId = 2844,

    kHarmonizerEndId = 2899,

    // ==========================================================================
    // Arpeggiator Parameters (3000-3099)
    // ==========================================================================
    kArpBaseId = 3000,
    kArpEnabledId = 3000,          // on/off toggle
    kArpModeId = 3001,             // 10-entry list: Up/Down/UpDown/DownUp/Converge/Diverge/Random/Walk/AsPlayed/Chord
    kArpOctaveRangeId = 3002,      // 1-4 integer range
    kArpOctaveModeId = 3003,       // 2-entry list: Sequential/Interleaved
    kArpTempoSyncId = 3004,        // on/off toggle
    kArpNoteValueId = 3005,        // 21-entry dropdown (same as TG/LFO)
    kArpFreeRateId = 3006,         // 0.5-50 Hz continuous
    kArpGateLengthId = 3007,       // 1-200% continuous
    kArpSwingId = 3008,            // 0-75% continuous
    kArpLatchModeId = 3009,        // 3-entry list: Off/Hold/Add
    kArpRetriggerId = 3010,        // 3-entry list: Off/Note/Beat
    // 3011-3019: reserved for future base arp params

    // --- Velocity Lane (3020-3052) ---
    kArpVelocityLaneLengthId = 3020, // discrete: 1-32
    kArpVelocityLaneStep0Id  = 3021, // continuous: 0.0-1.0
    kArpVelocityLaneStep1Id  = 3022,
    kArpVelocityLaneStep2Id  = 3023,
    kArpVelocityLaneStep3Id  = 3024,
    kArpVelocityLaneStep4Id  = 3025,
    kArpVelocityLaneStep5Id  = 3026,
    kArpVelocityLaneStep6Id  = 3027,
    kArpVelocityLaneStep7Id  = 3028,
    kArpVelocityLaneStep8Id  = 3029,
    kArpVelocityLaneStep9Id  = 3030,
    kArpVelocityLaneStep10Id = 3031,
    kArpVelocityLaneStep11Id = 3032,
    kArpVelocityLaneStep12Id = 3033,
    kArpVelocityLaneStep13Id = 3034,
    kArpVelocityLaneStep14Id = 3035,
    kArpVelocityLaneStep15Id = 3036,
    kArpVelocityLaneStep16Id = 3037,
    kArpVelocityLaneStep17Id = 3038,
    kArpVelocityLaneStep18Id = 3039,
    kArpVelocityLaneStep19Id = 3040,
    kArpVelocityLaneStep20Id = 3041,
    kArpVelocityLaneStep21Id = 3042,
    kArpVelocityLaneStep22Id = 3043,
    kArpVelocityLaneStep23Id = 3044,
    kArpVelocityLaneStep24Id = 3045,
    kArpVelocityLaneStep25Id = 3046,
    kArpVelocityLaneStep26Id = 3047,
    kArpVelocityLaneStep27Id = 3048,
    kArpVelocityLaneStep28Id = 3049,
    kArpVelocityLaneStep29Id = 3050,
    kArpVelocityLaneStep30Id = 3051,
    kArpVelocityLaneStep31Id = 3052,
    // 3053-3059: reserved for velocity lane metadata

    // --- Gate Lane (3060-3092) ---
    kArpGateLaneLengthId = 3060,     // discrete: 1-32
    kArpGateLaneStep0Id  = 3061,     // continuous: 0.01-2.0
    kArpGateLaneStep1Id  = 3062,
    kArpGateLaneStep2Id  = 3063,
    kArpGateLaneStep3Id  = 3064,
    kArpGateLaneStep4Id  = 3065,
    kArpGateLaneStep5Id  = 3066,
    kArpGateLaneStep6Id  = 3067,
    kArpGateLaneStep7Id  = 3068,
    kArpGateLaneStep8Id  = 3069,
    kArpGateLaneStep9Id  = 3070,
    kArpGateLaneStep10Id = 3071,
    kArpGateLaneStep11Id = 3072,
    kArpGateLaneStep12Id = 3073,
    kArpGateLaneStep13Id = 3074,
    kArpGateLaneStep14Id = 3075,
    kArpGateLaneStep15Id = 3076,
    kArpGateLaneStep16Id = 3077,
    kArpGateLaneStep17Id = 3078,
    kArpGateLaneStep18Id = 3079,
    kArpGateLaneStep19Id = 3080,
    kArpGateLaneStep20Id = 3081,
    kArpGateLaneStep21Id = 3082,
    kArpGateLaneStep22Id = 3083,
    kArpGateLaneStep23Id = 3084,
    kArpGateLaneStep24Id = 3085,
    kArpGateLaneStep25Id = 3086,
    kArpGateLaneStep26Id = 3087,
    kArpGateLaneStep27Id = 3088,
    kArpGateLaneStep28Id = 3089,
    kArpGateLaneStep29Id = 3090,
    kArpGateLaneStep30Id = 3091,
    kArpGateLaneStep31Id = 3092,
    // 3093-3099: reserved for gate lane metadata

    // --- Pitch Lane (3100-3132) ---
    // NOTE: 3100 was formerly kNumParameters; sentinel is now 3200
    kArpPitchLaneLengthId = 3100,    // discrete: 1-32
    kArpPitchLaneStep0Id  = 3101,    // discrete: -24 to +24
    kArpPitchLaneStep1Id  = 3102,
    kArpPitchLaneStep2Id  = 3103,
    kArpPitchLaneStep3Id  = 3104,
    kArpPitchLaneStep4Id  = 3105,
    kArpPitchLaneStep5Id  = 3106,
    kArpPitchLaneStep6Id  = 3107,
    kArpPitchLaneStep7Id  = 3108,
    kArpPitchLaneStep8Id  = 3109,
    kArpPitchLaneStep9Id  = 3110,
    kArpPitchLaneStep10Id = 3111,
    kArpPitchLaneStep11Id = 3112,
    kArpPitchLaneStep12Id = 3113,
    kArpPitchLaneStep13Id = 3114,
    kArpPitchLaneStep14Id = 3115,
    kArpPitchLaneStep15Id = 3116,
    kArpPitchLaneStep16Id = 3117,
    kArpPitchLaneStep17Id = 3118,
    kArpPitchLaneStep18Id = 3119,
    kArpPitchLaneStep19Id = 3120,
    kArpPitchLaneStep20Id = 3121,
    kArpPitchLaneStep21Id = 3122,
    kArpPitchLaneStep22Id = 3123,
    kArpPitchLaneStep23Id = 3124,
    kArpPitchLaneStep24Id = 3125,
    kArpPitchLaneStep25Id = 3126,
    kArpPitchLaneStep26Id = 3127,
    kArpPitchLaneStep27Id = 3128,
    kArpPitchLaneStep28Id = 3129,
    kArpPitchLaneStep29Id = 3130,
    kArpPitchLaneStep30Id = 3131,
    kArpPitchLaneStep31Id = 3132,
    // 3133-3139: reserved

    // --- Modifier Lane (073-per-step-mods, 3140-3172) ---
    kArpModifierLaneLengthId = 3140,    // discrete: 1-32 (RangeParameter, stepCount=31)
    kArpModifierLaneStep0Id  = 3141,    // discrete: 0-255 (RangeParameter, stepCount=255)
    kArpModifierLaneStep1Id  = 3142,
    kArpModifierLaneStep2Id  = 3143,
    kArpModifierLaneStep3Id  = 3144,
    kArpModifierLaneStep4Id  = 3145,
    kArpModifierLaneStep5Id  = 3146,
    kArpModifierLaneStep6Id  = 3147,
    kArpModifierLaneStep7Id  = 3148,
    kArpModifierLaneStep8Id  = 3149,
    kArpModifierLaneStep9Id  = 3150,
    kArpModifierLaneStep10Id = 3151,
    kArpModifierLaneStep11Id = 3152,
    kArpModifierLaneStep12Id = 3153,
    kArpModifierLaneStep13Id = 3154,
    kArpModifierLaneStep14Id = 3155,
    kArpModifierLaneStep15Id = 3156,
    kArpModifierLaneStep16Id = 3157,
    kArpModifierLaneStep17Id = 3158,
    kArpModifierLaneStep18Id = 3159,
    kArpModifierLaneStep19Id = 3160,
    kArpModifierLaneStep20Id = 3161,
    kArpModifierLaneStep21Id = 3162,
    kArpModifierLaneStep22Id = 3163,
    kArpModifierLaneStep23Id = 3164,
    kArpModifierLaneStep24Id = 3165,
    kArpModifierLaneStep25Id = 3166,
    kArpModifierLaneStep26Id = 3167,
    kArpModifierLaneStep27Id = 3168,
    kArpModifierLaneStep28Id = 3169,
    kArpModifierLaneStep29Id = 3170,
    kArpModifierLaneStep30Id = 3171,
    kArpModifierLaneStep31Id = 3172,
    // 3173-3179: reserved

    // --- Modifier Configuration (073-per-step-mods, 3180-3181) ---
    kArpAccentVelocityId     = 3180,    // discrete: 0-127 (RangeParameter, stepCount=127)
    kArpSlideTimeId          = 3181,    // continuous: 0-500ms (Parameter, default 60ms)
    // 3182-3189: reserved

    // --- Ratchet Lane (074-ratcheting, 3190-3222) ---
    kArpRatchetLaneLengthId  = 3190,    // discrete: 1-32 (RangeParameter, stepCount=31)
    kArpRatchetLaneStep0Id   = 3191,    // discrete: 1-4 (RangeParameter, stepCount=3)
    kArpRatchetLaneStep1Id   = 3192,
    kArpRatchetLaneStep2Id   = 3193,
    kArpRatchetLaneStep3Id   = 3194,
    kArpRatchetLaneStep4Id   = 3195,
    kArpRatchetLaneStep5Id   = 3196,
    kArpRatchetLaneStep6Id   = 3197,
    kArpRatchetLaneStep7Id   = 3198,
    kArpRatchetLaneStep8Id   = 3199,
    kArpRatchetLaneStep9Id   = 3200,
    kArpRatchetLaneStep10Id  = 3201,
    kArpRatchetLaneStep11Id  = 3202,
    kArpRatchetLaneStep12Id  = 3203,
    kArpRatchetLaneStep13Id  = 3204,
    kArpRatchetLaneStep14Id  = 3205,
    kArpRatchetLaneStep15Id  = 3206,
    kArpRatchetLaneStep16Id  = 3207,
    kArpRatchetLaneStep17Id  = 3208,
    kArpRatchetLaneStep18Id  = 3209,
    kArpRatchetLaneStep19Id  = 3210,
    kArpRatchetLaneStep20Id  = 3211,
    kArpRatchetLaneStep21Id  = 3212,
    kArpRatchetLaneStep22Id  = 3213,
    kArpRatchetLaneStep23Id  = 3214,
    kArpRatchetLaneStep24Id  = 3215,
    kArpRatchetLaneStep25Id  = 3216,
    kArpRatchetLaneStep26Id  = 3217,
    kArpRatchetLaneStep27Id  = 3218,
    kArpRatchetLaneStep28Id  = 3219,
    kArpRatchetLaneStep29Id  = 3220,
    kArpRatchetLaneStep30Id  = 3221,
    kArpRatchetLaneStep31Id  = 3222,
    // --- Euclidean Timing (075-euclidean-timing, 3230-3233) ---
    kArpEuclideanEnabledId   = 3230,    // discrete: 0-1 (on/off toggle)
    kArpEuclideanHitsId      = 3231,    // discrete: 0-32
    kArpEuclideanStepsId     = 3232,    // discrete: 2-32
    kArpEuclideanRotationId  = 3233,    // discrete: 0-31
    // 3234-3239: reserved (gap before condition lane; reserved for use before Phase 9)

    // --- Condition Lane (076-conditional-trigs, 3240-3272) ---
    kArpConditionLaneLengthId = 3240,   // discrete: 1-32 (RangeParameter, stepCount=31)
    kArpConditionLaneStep0Id  = 3241,   // discrete: 0-17 (RangeParameter, stepCount=17)
    kArpConditionLaneStep1Id  = 3242,
    kArpConditionLaneStep2Id  = 3243,
    kArpConditionLaneStep3Id  = 3244,
    kArpConditionLaneStep4Id  = 3245,
    kArpConditionLaneStep5Id  = 3246,
    kArpConditionLaneStep6Id  = 3247,
    kArpConditionLaneStep7Id  = 3248,
    kArpConditionLaneStep8Id  = 3249,
    kArpConditionLaneStep9Id  = 3250,
    kArpConditionLaneStep10Id = 3251,
    kArpConditionLaneStep11Id = 3252,
    kArpConditionLaneStep12Id = 3253,
    kArpConditionLaneStep13Id = 3254,
    kArpConditionLaneStep14Id = 3255,
    kArpConditionLaneStep15Id = 3256,
    kArpConditionLaneStep16Id = 3257,
    kArpConditionLaneStep17Id = 3258,
    kArpConditionLaneStep18Id = 3259,
    kArpConditionLaneStep19Id = 3260,
    kArpConditionLaneStep20Id = 3261,
    kArpConditionLaneStep21Id = 3262,
    kArpConditionLaneStep22Id = 3263,
    kArpConditionLaneStep23Id = 3264,
    kArpConditionLaneStep24Id = 3265,
    kArpConditionLaneStep25Id = 3266,
    kArpConditionLaneStep26Id = 3267,
    kArpConditionLaneStep27Id = 3268,
    kArpConditionLaneStep28Id = 3269,
    kArpConditionLaneStep29Id = 3270,
    kArpConditionLaneStep30Id = 3271,
    kArpConditionLaneStep31Id = 3272,
    // 3273-3279: reserved (gap between condition step IDs and fill toggle; reserved for future condition-lane extensions)

    // --- Fill Toggle (076-conditional-trigs, 3280) ---
    kArpFillToggleId          = 3280,   // discrete: 0-1 (latching toggle)

    // --- Spice/Dice & Humanize (077-spice-dice-humanize) ---
    kArpSpiceId               = 3290,   // continuous: 0.0-1.0 (displayed as 0-100%)
    kArpDiceTriggerId         = 3291,   // discrete: 0-1 (momentary trigger, edge-detected)
    kArpHumanizeId            = 3292,   // continuous: 0.0-1.0 (displayed as 0-100%)
    kArpRatchetSwingId        = 3293,   // continuous: 0.0-1.0 (displayed as 50-75%)

    // --- Playhead Parameters (079-layout-framework + 080-specialized-lane-types) ---
    kArpVelocityPlayheadId    = 3294,   // hidden: 0.0-1.0 (step/32 encoding, not persisted)
    kArpGatePlayheadId        = 3295,   // hidden: 0.0-1.0 (step/32 encoding, not persisted)
    kArpPitchPlayheadId       = 3296,   // hidden: 0.0-1.0 (step/32 encoding, not persisted)
    kArpRatchetPlayheadId     = 3297,   // hidden: 0.0-1.0 (step/32 encoding, not persisted)
    kArpModifierPlayheadId    = 3298,   // hidden: 0.0-1.0 (step/32 encoding, not persisted)
    kArpConditionPlayheadId   = 3299,   // hidden: 0.0-1.0 (step/32 encoding, not persisted)

    kArpEndId = 3299,

    // ==========================================================================
    kNumParameters = 3300,

    // ==========================================================================
    // UI Action Button Tags (NOT VST parameters - UI-only triggers)
    // ==========================================================================
    kActionTransformInvertTag = 10006,
    kActionTransformShiftRightTag = 10007,
    kActionTransformShiftLeftTag = 10008,
    kActionEuclideanRegenTag = 10009,

    // (FX expand tags 10011, 10012 removed — panels always visible in Tab_Fx)

    // (Env expand tags 10013-10015 removed — envelopes always visible in persistent strip)

    // Filter View Mode Tab (UI-only, ephemeral - not saved with state)
    kFilterViewModeTag = 10016,

    // Distortion View Mode Tab (UI-only, ephemeral - not saved with state)
    kDistortionViewModeTag = 10017,

    // (FX expand tag 10018 removed — phaser always visible in Tab_Fx)

    // (FX expand tag 10022 removed — harmonizer always visible in Tab_Fx)

    // Modulation Source View Mode Tab (UI-only, ephemeral - not saved with state)
    kModSourceViewModeTag = 10019,

    // Settings Drawer Toggle (UI-only, gear icon click)
    kActionSettingsToggleTag = 10020,

    // Settings Drawer Click-Outside Overlay (UI-only, dismiss gesture)
    kActionSettingsOverlayTag = 10021,

    // Main Tab Selector (UI-only, ephemeral - not saved with state)
    // Drives UIViewSwitchContainer: SOUND=0, MOD=1, FX=2, SEQ=3
    kMainTabTag = 10023,
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
