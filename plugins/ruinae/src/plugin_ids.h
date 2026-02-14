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
//   1500-1599: FX Enable (Delay, Reverb, Phaser)
//   1600-1699: Delay Effect (Type, Time, Feedback, Mix, Sync, ...)
//   1700-1799: Reverb (Size, Damping, Width, Mix, PreDelay, ...)
//   1800-1899: Mono Mode (Priority, Legato, Portamento Time, PortaMode)
//   1900-1999: Phaser (Rate, Depth, Feedback, Mix, Stages, ...)
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
    // FX Enable Parameters (1500-1502)
    // ==========================================================================
    kDelayEnabledId = 1500,    // on/off (default: on)
    kReverbEnabledId = 1501,   // on/off (default: on)
    kPhaserEnabledId = 1502,   // on/off (default: on)

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
    kNumParameters = 2000,

    // ==========================================================================
    // UI Action Button Tags (NOT VST parameters - UI-only triggers)
    // ==========================================================================
    kActionTransformInvertTag = 10006,
    kActionTransformShiftRightTag = 10007,
    kActionTransformShiftLeftTag = 10008,
    kActionEuclideanRegenTag = 10009,

    // FX Detail Panel Expand/Collapse Chevrons (UI-only, not VST parameters)
    kActionFxExpandDelayTag = 10011,
    kActionFxExpandReverbTag = 10012,

    // Envelope Expand/Collapse Chevrons (UI-only, not VST parameters)
    kActionEnvExpandAmpTag = 10013,
    kActionEnvExpandFilterTag = 10014,
    kActionEnvExpandModTag = 10015,

    // Filter View Mode Tab (UI-only, ephemeral - not saved with state)
    kFilterViewModeTag = 10016,

    // Distortion View Mode Tab (UI-only, ephemeral - not saved with state)
    kDistortionViewModeTag = 10017,

    // Phaser FX Detail Panel Expand/Collapse Chevron (UI-only)
    kActionFxExpandPhaserTag = 10018,

    // Modulation Source View Mode Tab (UI-only, ephemeral - not saved with state)
    kModSourceViewModeTag = 10019,
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
