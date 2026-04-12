// Contract: PadConfig structure for Membrum Phase 4
// This file defines the API contract for the PadConfig structure.
// Implementation will be at: plugins/membrum/src/dsp/pad_config.h

#pragma once

#include "exciter_type.h"
#include "body_model_type.h"
#include <array>
#include <cstdint>

namespace Membrum {

/// Number of pads in the kit (GM drum map MIDI 36-67).
constexpr int kNumPads = 32;

/// Base parameter ID for per-pad parameters.
constexpr int kPadBaseId = 1000;

/// Stride between consecutive pads' parameter ID ranges.
/// Offsets 0-35 are active; 36-63 reserved for future phases.
constexpr int kPadParamStride = 64;

/// Maximum number of output buses (1 main + 15 aux).
constexpr int kMaxOutputBuses = 16;

/// First MIDI note in the GM drum map range.
constexpr std::uint8_t kFirstDrumNote = 36;

/// Last MIDI note in the GM drum map range (inclusive).
constexpr std::uint8_t kLastDrumNote = 67;

/// Per-pad parameter offsets within each pad's stride.
enum PadParamOffset : int
{
    kPadExciterType          = 0,
    kPadBodyModel            = 1,
    kPadMaterial             = 2,
    kPadSize                 = 3,
    kPadDecay                = 4,
    kPadStrikePosition       = 5,
    kPadLevel                = 6,
    kPadTSFilterType         = 7,
    kPadTSFilterCutoff       = 8,
    kPadTSFilterResonance    = 9,
    kPadTSFilterEnvAmount    = 10,
    kPadTSDriveAmount        = 11,
    kPadTSFoldAmount         = 12,
    kPadTSPitchEnvStart      = 13,
    kPadTSPitchEnvEnd        = 14,
    kPadTSPitchEnvTime       = 15,
    kPadTSPitchEnvCurve      = 16,
    kPadTSFilterEnvAttack    = 17,
    kPadTSFilterEnvDecay     = 18,
    kPadTSFilterEnvSustain   = 19,
    kPadTSFilterEnvRelease   = 20,
    kPadModeStretch          = 21,
    kPadDecaySkew            = 22,
    kPadModeInjectAmount     = 23,
    kPadNonlinearCoupling    = 24,
    kPadMorphEnabled         = 25,
    kPadMorphStart           = 26,
    kPadMorphEnd             = 27,
    kPadMorphDuration        = 28,
    kPadMorphCurve           = 29,
    kPadChokeGroup           = 30,
    kPadOutputBus            = 31,

    // Exciter secondary params (per-pad, Phase 4)
    kPadFMRatio              = 32,
    kPadFeedbackAmount       = 33,
    kPadNoiseBurstDuration   = 34,
    kPadFrictionPressure     = 35,

    kPadActiveParamCount     = 36,  // offsets 0-35 are active
    // Offsets 36-63 are RESERVED for Phase 5+ (e.g., per-pad coupling)
};

/// Complete configuration for one drum pad. Pre-allocated, no dynamic memory.
/// All float values are NORMALIZED [0.0, 1.0] -- denormalization happens
/// at the point of use (voice noteOn, tone shaper configuration, etc.).
struct PadConfig
{
    // Selectors (discrete)
    ExciterType   exciterType = ExciterType::Impulse;
    BodyModelType bodyModel   = BodyModelType::Membrane;

    // Core sound params (normalized)
    float material       = 0.5f;
    float size           = 0.5f;
    float decay          = 0.3f;
    float strikePosition = 0.3f;
    float level          = 0.8f;

    // Tone Shaper (normalized)
    float tsFilterType       = 0.0f;  // discrete: 0=LP, 0.5=HP, 1.0=BP
    float tsFilterCutoff     = 1.0f;  // normalized (log scale in denorm)
    float tsFilterResonance  = 0.0f;
    float tsFilterEnvAmount  = 0.5f;  // normalized (bipolar in denorm)
    float tsDriveAmount      = 0.0f;
    float tsFoldAmount       = 0.0f;
    float tsPitchEnvStart    = 0.0f;  // normalized (log scale)
    float tsPitchEnvEnd      = 0.0f;
    float tsPitchEnvTime     = 0.0f;
    float tsPitchEnvCurve    = 0.0f;  // discrete: 0=Exp, 1=Lin
    float tsFilterEnvAttack  = 0.0f;
    float tsFilterEnvDecay   = 0.1f;
    float tsFilterEnvSustain = 0.0f;
    float tsFilterEnvRelease = 0.1f;

    // Unnatural Zone (normalized)
    float modeStretch       = 0.333333f;  // norm of [0.5, 2.0], default 1.0
    float decaySkew         = 0.5f;       // norm of [-1, +1], default 0.0
    float modeInjectAmount  = 0.0f;
    float nonlinearCoupling = 0.0f;

    // Material Morph (normalized)
    float morphEnabled  = 0.0f;  // discrete: 0=off, 1=on
    float morphStart    = 1.0f;
    float morphEnd      = 0.0f;
    float morphDuration = 0.095477f;  // norm of [10, 2000] ms, default 200
    float morphCurve    = 0.0f;       // discrete: 0=Lin, 1=Exp

    // Kit-level per-pad settings
    std::uint8_t chokeGroup = 0;  // [0, 8], 0 = no choke
    std::uint8_t outputBus  = 0;  // [0, 15], 0 = main only

    // Exciter secondary params (per-pad, Phase 4)
    float fmRatio            = 0.5f;   // normalized FM ratio
    float feedbackAmount     = 0.0f;   // FM feedback amount
    float noiseBurstDuration = 0.5f;   // normalized burst duration
    float frictionPressure   = 0.0f;   // friction exciter pressure
};

/// Compute the VST3 parameter ID for a specific pad and offset.
[[nodiscard]] constexpr int padParamId(int padIndex, int offset) noexcept
{
    return kPadBaseId + padIndex * kPadParamStride + offset;
}

/// Extract pad index from a per-pad parameter ID. Returns -1 if not a pad param.
[[nodiscard]] constexpr int padIndexFromParamId(int paramId) noexcept
{
    if (paramId < kPadBaseId)
        return -1;
    const int relative = paramId - kPadBaseId;
    const int padIdx = relative / kPadParamStride;
    if (padIdx >= kNumPads)
        return -1;
    return padIdx;
}

/// Extract parameter offset from a per-pad parameter ID. Returns -1 if invalid.
[[nodiscard]] constexpr int padOffsetFromParamId(int paramId) noexcept
{
    if (paramId < kPadBaseId)
        return -1;
    const int relative = paramId - kPadBaseId;
    const int padIdx = relative / kPadParamStride;
    if (padIdx >= kNumPads)
        return -1;
    const int offset = relative % kPadParamStride;
    if (offset >= kPadActiveParamCount)
        return -1;  // reserved range
    return offset;
}

} // namespace Membrum
