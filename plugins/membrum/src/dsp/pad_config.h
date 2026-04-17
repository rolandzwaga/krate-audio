#pragma once

// ==============================================================================
// PadConfig -- Per-pad configuration for Membrum Phase 4
// Contract: specs/139-membrum-phase4-pads/contracts/pad-config.h
// ==============================================================================

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

    kPadActiveParamCount     = 36,  // offsets 0-35 are active (Phase 4)

    // Phase 5: per-pad coupling participation
    kPadCouplingAmount       = 36,
    kPadActiveParamCountV5   = 37,  // offsets 0-36 are active in Phase 5

    // Phase 6: five per-pad macro controls (Tightness, Brightness, Body Size,
    // Punch, Complexity). Each macro is a normalised [0, 1] proxy that the
    // Processor-side MacroMapper translates into deltas on underlying voice
    // parameters at control-rate. See spec 141 FR-070 / FR-072.
    kPadMacroTightness       = 37,
    kPadMacroBrightness      = 38,
    kPadMacroBodySize        = 39,
    kPadMacroPunch           = 40,
    kPadMacroComplexity      = 41,
    kPadActiveParamCountV6   = 42,  // offsets 0-41 are active in Phase 6

    // Phase 7: parallel always-on noise layer (every voice renders a filtered
    // noise path alongside the modal body -- see noise_layer.h) plus always-on
    // attack "click" transient (2-5 ms raised-cosine filtered-noise burst --
    // see click_layer.h). Both are research-backed realism ingredients (Cook
    // SNT, Serra/Smith SMS, Chromaphone/Microtonic).
    kPadNoiseLayerMix        = 42,
    kPadNoiseLayerCutoff     = 43,
    kPadNoiseLayerResonance  = 44,
    kPadNoiseLayerDecay      = 45,
    kPadNoiseLayerColor      = 46,
    kPadClickLayerMix        = 47,
    kPadClickLayerContactMs  = 48,
    kPadClickLayerBrightness = 49,
    kPadActiveParamCountV7   = 50,  // offsets 0-49 are active in Phase 7

    // Phase 8A: per-mode damping law exposed as first-class params.
    // b1 controls flat damping (s^-1 floor for low-f modes), b3 controls
    // frequency-squared damping (perceived "material": metal vs wood vs
    // plastic -- see Aramaki/KM 2011, Chaigne & Askenfelt 1993).
    // Defaults preserve current behaviour: b1 is derived from the existing
    // decay knob via 1/decayTime, b3 from brightness via (1-brightness)*kMaxB3.
    kPadBodyDampingB1        = 50,
    kPadBodyDampingB3        = 51,
    kPadActiveParamCountV8A  = 52,  // offsets 0-51 are active after Phase 8A

    // Phase 8C: air-loading + per-mode scatter. airLoading depresses the
    // lowest Bessel modes per the tabulated kAirLoadingCurve (real-drum
    // physics, Rossing 1982). modeScatter drives the bank's existing
    // sinusoidal dither for a small amount of "natural imperfection" on
    // top of the physics-correct airLoading curve.
    kPadAirLoading           = 52,
    kPadModeScatter          = 53,
    kPadActiveParamCountV8C  = 54,  // offsets 0-53 are active after Phase 8C

    // Phase 8D: primary <-> secondary body coupling. A second, smaller
    // ModalResonatorBank runs in parallel and exchanges energy with the
    // head via a scalar coupling coefficient. Matches Chromaphone 3's
    // bidirectional-coupling idiom; closes the "no body weight" gap that
    // kicks / shells have without it.
    kPadCouplingStrength     = 54,
    kPadSecondaryEnabled     = 55,
    kPadSecondarySize        = 56,
    kPadSecondaryMaterial    = 57,
    kPadActiveParamCountV8D  = 58,  // offsets 0-57 are active after Phase 8D

    // Phase 8E: nonlinear tension modulation. Energy-dependent pitch glide
    // reproduces the tom-tom "kerthump" -- Avanzini & Rocchesso 2012 /
    // Kirby & Sandler 2021 JASA. Depth is scaled by velocity^2 at noteOn.
    kPadTensionModAmt        = 58,
    kPadActiveParamCountV8E  = 59,  // offsets 0-58 are active after Phase 8E
    // Offsets 59-63 are RESERVED.
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

    // Phase 5: per-pad coupling participation [0.0, 1.0]
    float couplingAmount     = 0.5f;

    // Phase 6: per-pad macros (normalised [0.0, 1.0]). 0.5 is neutral
    // (zero delta against registered defaults).
    float macroTightness     = 0.5f;
    float macroBrightness    = 0.5f;
    float macroBodySize      = 0.5f;
    float macroPunch         = 0.5f;
    float macroComplexity    = 0.5f;

    // Phase 7: parallel noise layer (always-on, independent of body choice).
    float noiseLayerMix        = 0.35f;
    float noiseLayerCutoff     = 0.5f;
    float noiseLayerResonance  = 0.2f;
    float noiseLayerDecay      = 0.3f;
    float noiseLayerColor      = 0.5f;

    // Phase 7: always-on attack "click" transient (2-5 ms raised-cosine
    // filtered-noise burst, fires at noteOn alongside the selected exciter).
    float clickLayerMix        = 0.5f;
    float clickLayerContactMs  = 0.3f;  // normalised: 0 = 2 ms, 1 = 5 ms
    float clickLayerBrightness = 0.6f;

    // Phase 8A: per-mode damping law overrides. Normalised [0, 1];
    // denormalisation happens at the mapper.
    //   bodyDampingB1: 0 -> 0.2 s^-1 (matches legacy 5.0 s decayTime floor),
    //                  1 -> 50 s^-1 (very short t60).
    //   bodyDampingB3: 0 -> 0 (pure flat damping, "metallic"),
    //                  1 -> 8e-5 s * rad^-2 (strong high-mode damping, "wood").
    // Default -1.0f sentinel means "derive from decay/material via mapper"
    // -- preserves Phase 1 bit-identity.
    float bodyDampingB1        = -1.0f;
    float bodyDampingB3        = -1.0f;

    // Phase 8C: air-loading correction + per-mode scatter.
    //   airLoading:  0 -> pure Bessel, 1 -> full Rossing-curve depression.
    //   modeScatter: 0 -> pure ratios, 1 -> 15 % sinusoidal dither on f_k.
    // Default airLoading = 0.6 gives a realistic Membrane "deeper / less
    // whistly" character without sounding detuned. Scatter default 0
    // keeps Phase 1 bit-identity until the user dials it in.
    float airLoading           = 0.6f;
    float modeScatter          = 0.0f;

    // Phase 8D: head <-> shell coupling. Secondary bank is disabled by
    // default so sentinel-rendered audio stays unchanged until a preset
    // or user turns it on.
    //   couplingStrength: 0 -> 0.25 stable range; clamped below an
    //                     eigenvalue-safety ceiling in DrumVoice.
    //   secondaryEnabled: 0 = off, >=0.5 = on (float-as-bool).
    //   secondarySize:    shell size offset from head (0 = head f0,
    //                     0.5 = 0.6 x head f0, 1.0 = 0.25 x head f0).
    //   secondaryMaterial: shell material (same semantic as primary).
    float couplingStrength     = 0.0f;
    float secondaryEnabled     = 0.0f;
    float secondarySize        = 0.5f;
    float secondaryMaterial    = 0.4f;

    // Phase 8E: nonlinear tension modulation depth (norm). 0 -> 0.15
    // effective max at max velocity, which caps pitch shift at ~2
    // semitones (matches JASA 2021 tom-tom measurements).
    float tensionModAmt        = 0.0f;
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
    if (offset >= kPadActiveParamCountV8E)
        return -1;  // reserved range
    return offset;
}

} // namespace Membrum
