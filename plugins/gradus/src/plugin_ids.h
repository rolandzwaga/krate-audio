#pragma once

// ==============================================================================
// Plugin Identifiers — Gradus (Standalone Arpeggiator)
// ==============================================================================
// GUIDs uniquely identify the plugin components.
// IMPORTANT: Once published, NEVER change these IDs or hosts will not
// recognize saved projects using your plugin.
//
// Arpeggiator parameter IDs (3000-3372) are intentionally identical to
// Ruinae's arp parameter IDs to enable preset sharing between the two plugins.
// ==============================================================================

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Gradus {

// Processor Component ID
static const Steinberg::FUID kProcessorUID(0x7A1B2C3D, 0x4E5F6A7B, 0x8C9D0E1F, 0x2A3B4C5D);

// Controller Component ID
static const Steinberg::FUID kControllerUID(0x3D2C1B7A, 0x7B6A5F4E, 0x1F0E9D8C, 0x5D4C3B2A);

// VST3 subcategories: Instrument (for universal host compatibility)
static constexpr auto kSubCategories = "Instrument|Synth";

// State version for serialization
constexpr Steinberg::int32 kCurrentStateVersion = 2;

// ==============================================================================
// Parameter IDs
// ==============================================================================
// Arpeggiator IDs (3000-3372): Identical to Ruinae for preset sharing.
// Audition IDs (4000-4003): Gradus-specific built-in sound.
// ==============================================================================

enum ParameterIds : Steinberg::Vst::ParamID
{
    // ==========================================================================
    // Arpeggiator Parameters (3000-3372) — shared with Ruinae
    // ==========================================================================
    kArpBaseId = 3000,
    kArpOperatingModeId = 3000,    // 4-entry list: Off/MIDI/Mod/MIDI+Mod (hidden in Gradus, always MIDI)
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

    // --- Velocity Lane (3020-3052) ---
    kArpVelocityLaneLengthId = 3020,
    kArpVelocityLaneStep0Id  = 3021,
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

    // --- Gate Lane (3060-3092) ---
    kArpGateLaneLengthId = 3060,
    kArpGateLaneStep0Id  = 3061,
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

    // --- Pitch Lane (3100-3132) ---
    kArpPitchLaneLengthId = 3100,
    kArpPitchLaneStep0Id  = 3101,
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

    // --- Modifier Lane (3140-3172) ---
    kArpModifierLaneLengthId = 3140,
    kArpModifierLaneStep0Id  = 3141,
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

    // --- Modifier Configuration (3180-3181) ---
    kArpAccentVelocityId     = 3180,
    kArpSlideTimeId          = 3181,

    // --- Ratchet Lane (3190-3222) ---
    kArpRatchetLaneLengthId  = 3190,
    kArpRatchetLaneStep0Id   = 3191,
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

    // --- Euclidean Timing (3230-3233) ---
    kArpEuclideanEnabledId   = 3230,
    kArpEuclideanHitsId      = 3231,
    kArpEuclideanStepsId     = 3232,
    kArpEuclideanRotationId  = 3233,

    // --- Condition Lane (3240-3272) ---
    kArpConditionLaneLengthId = 3240,
    kArpConditionLaneStep0Id  = 3241,
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

    // --- Fill Toggle (3280) ---
    kArpFillToggleId          = 3280,

    // --- Spice/Dice & Humanize (3290-3293) ---
    kArpSpiceId               = 3290,
    kArpDiceTriggerId         = 3291,
    kArpHumanizeId            = 3292,
    kArpRatchetSwingId        = 3293,

    // --- Playhead Parameters (hidden, not persisted) ---
    kArpVelocityPlayheadId    = 3294,
    kArpGatePlayheadId        = 3295,
    kArpPitchPlayheadId       = 3296,
    kArpRatchetPlayheadId     = 3297,
    kArpModifierPlayheadId    = 3298,
    kArpConditionPlayheadId   = 3299,

    // --- Scale Mode (3300-3303) ---
    kArpScaleTypeId           = 3300,
    kArpRootNoteId            = 3301,
    kArpScaleQuantizeInputId  = 3302,
    kArpMidiOutId             = 3303,    // hidden in Gradus (always on)

    // --- Chord Lane (3304-3336) ---
    kArpChordLaneLengthId     = 3304,
    kArpChordLaneStep0Id      = 3305,
    kArpChordLaneStep1Id      = 3306,
    kArpChordLaneStep2Id      = 3307,
    kArpChordLaneStep3Id      = 3308,
    kArpChordLaneStep4Id      = 3309,
    kArpChordLaneStep5Id      = 3310,
    kArpChordLaneStep6Id      = 3311,
    kArpChordLaneStep7Id      = 3312,
    kArpChordLaneStep8Id      = 3313,
    kArpChordLaneStep9Id      = 3314,
    kArpChordLaneStep10Id     = 3315,
    kArpChordLaneStep11Id     = 3316,
    kArpChordLaneStep12Id     = 3317,
    kArpChordLaneStep13Id     = 3318,
    kArpChordLaneStep14Id     = 3319,
    kArpChordLaneStep15Id     = 3320,
    kArpChordLaneStep16Id     = 3321,
    kArpChordLaneStep17Id     = 3322,
    kArpChordLaneStep18Id     = 3323,
    kArpChordLaneStep19Id     = 3324,
    kArpChordLaneStep20Id     = 3325,
    kArpChordLaneStep21Id     = 3326,
    kArpChordLaneStep22Id     = 3327,
    kArpChordLaneStep23Id     = 3328,
    kArpChordLaneStep24Id     = 3329,
    kArpChordLaneStep25Id     = 3330,
    kArpChordLaneStep26Id     = 3331,
    kArpChordLaneStep27Id     = 3332,
    kArpChordLaneStep28Id     = 3333,
    kArpChordLaneStep29Id     = 3334,
    kArpChordLaneStep30Id     = 3335,
    kArpChordLaneStep31Id     = 3336,

    // --- Inversion Lane (3337-3369) ---
    kArpInversionLaneLengthId = 3337,
    kArpInversionLaneStep0Id  = 3338,
    kArpInversionLaneStep1Id  = 3339,
    kArpInversionLaneStep2Id  = 3340,
    kArpInversionLaneStep3Id  = 3341,
    kArpInversionLaneStep4Id  = 3342,
    kArpInversionLaneStep5Id  = 3343,
    kArpInversionLaneStep6Id  = 3344,
    kArpInversionLaneStep7Id  = 3345,
    kArpInversionLaneStep8Id  = 3346,
    kArpInversionLaneStep9Id  = 3347,
    kArpInversionLaneStep10Id = 3348,
    kArpInversionLaneStep11Id = 3349,
    kArpInversionLaneStep12Id = 3350,
    kArpInversionLaneStep13Id = 3351,
    kArpInversionLaneStep14Id = 3352,
    kArpInversionLaneStep15Id = 3353,
    kArpInversionLaneStep16Id = 3354,
    kArpInversionLaneStep17Id = 3355,
    kArpInversionLaneStep18Id = 3356,
    kArpInversionLaneStep19Id = 3357,
    kArpInversionLaneStep20Id = 3358,
    kArpInversionLaneStep21Id = 3359,
    kArpInversionLaneStep22Id = 3360,
    kArpInversionLaneStep23Id = 3361,
    kArpInversionLaneStep24Id = 3362,
    kArpInversionLaneStep25Id = 3363,
    kArpInversionLaneStep26Id = 3364,
    kArpInversionLaneStep27Id = 3365,
    kArpInversionLaneStep28Id = 3366,
    kArpInversionLaneStep29Id = 3367,
    kArpInversionLaneStep30Id = 3368,
    kArpInversionLaneStep31Id = 3369,

    // --- Voicing & Playheads (3370-3372) ---
    kArpVoicingModeId         = 3370,
    kArpChordPlayheadId       = 3371,
    kArpInversionPlayheadId   = 3372,

    // --- Per-Lane Speed Multipliers (3380-3387) ---
    kArpVelocityLaneSpeedId  = 3380,
    kArpGateLaneSpeedId      = 3381,
    kArpPitchLaneSpeedId     = 3382,
    kArpModifierLaneSpeedId  = 3383,
    kArpRatchetLaneSpeedId   = 3384,
    kArpConditionLaneSpeedId = 3385,
    kArpChordLaneSpeedId     = 3386,
    kArpInversionLaneSpeedId = 3387,

    // --- v1.5 Features: Ratchet Decay, Strum, Per-Lane Swing (3388-3398) ---
    kArpRatchetDecayId       = 3388,  // 0-100% velocity decay per ratchet subdivision
    kArpStrumTimeId          = 3389,  // 0-100ms chord note stagger
    kArpStrumDirectionId     = 3390,  // 0=Up, 1=Down, 2=Random, 3=Alternate

    // Per-lane swing (3391-3398)
    kArpVelocityLaneSwingId  = 3391,
    kArpGateLaneSwingId      = 3392,
    kArpPitchLaneSwingId     = 3393,
    kArpModifierLaneSwingId  = 3394,
    kArpRatchetLaneSwingId   = 3395,
    kArpConditionLaneSwingId = 3396,
    kArpChordLaneSwingId     = 3397,
    kArpInversionLaneSwingId = 3398,

    // --- v1.5 Part 2: Velocity Curve, Transpose, Per-Lane Length Jitter (3399-3409) ---
    kArpVelocityCurveTypeId   = 3399,  // 0=Linear, 1=Exp, 2=Log, 3=S-Curve
    kArpVelocityCurveAmountId = 3400,  // 0-100%
    kArpTransposeId           = 3401,  // -24 to +24 semitones, scale-quantized

    // Per-lane length jitter (3402-3409)
    kArpVelocityLaneJitterId  = 3402,
    kArpGateLaneJitterId      = 3403,
    kArpPitchLaneJitterId     = 3404,
    kArpModifierLaneJitterId  = 3405,
    kArpRatchetLaneJitterId   = 3406,
    kArpConditionLaneJitterId = 3407,
    kArpChordLaneJitterId     = 3408,
    kArpInversionLaneJitterId = 3409,

    // --- v1.5 Part 3: Note Range Mapping (3410-3412) ---
    kArpRangeLowId       = 3410,  // MIDI 0-127, default 0
    kArpRangeHighId      = 3411,  // MIDI 0-127, default 127
    kArpRangeModeId      = 3412,  // 0=Wrap, 1=Clamp, 2=Skip

    // --- v1.5 Part 3: Step Pinning (3413-3445) ---
    kArpPinNoteId        = 3413,  // MIDI 0-127, default 60 (C4) — global pin note
    kArpPinFlagStep0Id   = 3414,  // 0=unpinned, 1=pinned
    // kArpPinFlagStep1Id .. kArpPinFlagStep31Id = 3415..3445
    kArpPinFlagStep31Id  = 3445,

    // --- Markov Chain Mode (3446-3495) ---
    // 1 preset dropdown + 49 matrix cells (7x7 row-major: row * 7 + col).
    // Cell values are 0.0-1.0; rows auto-normalize at sample time.
    kArpMarkovPresetId   = 3446,  // 0=Uniform,1=Jazz,2=Minimal,3=Ambient,4=Classical,5=Custom
    kArpMarkovCell00Id   = 3447,  // row 0, col 0 (I -> I)
    // kArpMarkovCell01Id .. kArpMarkovCell66Id = 3448..3495
    kArpMarkovCell66Id   = 3495,  // row 6, col 6 (vii° -> vii°)

    kArpEndId = 3495,

    // --- Speed Curve Depth (per-lane, 0.0-1.0) ---
    kArpVelocityLaneSpeedCurveDepthId  = 3500,
    kArpGateLaneSpeedCurveDepthId      = 3501,
    kArpPitchLaneSpeedCurveDepthId     = 3502,
    kArpModifierLaneSpeedCurveDepthId  = 3503,
    kArpRatchetLaneSpeedCurveDepthId   = 3504,
    kArpConditionLaneSpeedCurveDepthId = 3505,
    kArpChordLaneSpeedCurveDepthId     = 3506,
    kArpInversionLaneSpeedCurveDepthId = 3507,

    // ==========================================================================
    // Audition Sound Parameters (4000-4003) — Gradus-specific
    // ==========================================================================
    kAuditionEnabledId  = 4000,    // on/off toggle, default on
    kAuditionVolumeId   = 4001,    // 0.0-1.0, default 0.7
    kAuditionWaveformId = 4002,    // 0=Sine, 1=Saw, 2=Square
    kAuditionDecayId    = 4003,    // 10-2000ms, default 200ms

    kNumParameters = 4004,
};

// Operating mode constants are defined in arpeggiator_params.h (ArpOperatingMode enum)

} // namespace Gradus
