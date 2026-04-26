#pragma once

// ==============================================================================
// Plugin Identifiers -- Membrum (Synthesized Drum Machine)
// ==============================================================================
// GUIDs uniquely identify the plugin components.
// IMPORTANT: Once published, NEVER change these IDs or hosts will not
// recognize saved projects using your plugin.
// ==============================================================================

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "dsp/pad_config.h"

namespace Membrum {

// Processor Component ID
static const Steinberg::FUID kProcessorUID(0x4D656D62, 0x72756D50, 0x726F6331, 0x00000136);

// Controller Component ID
static const Steinberg::FUID kControllerUID(0x4D656D62, 0x72756D43, 0x74726C31, 0x00000136);

// VST3 subcategories: Instrument|Drum
static constexpr auto kSubCategories = "Instrument|Drum";

// State version for serialization. Single, current format -- the plugin has
// not shipped, so no legacy versions are accepted on read.
constexpr Steinberg::int32 kCurrentStateVersion = 1;

// Number of new globals introduced by Phase 6 (kUiModeId, kOutputBusId).
constexpr int kPhase6GlobalCount = 2;

// ==============================================================================
// Parameter IDs
// ==============================================================================
// Phase 1 parameter range: 100-199 (integer values 100-104 are FROZEN)
// Phase 2 parameter range: 200-249 (Exciter/Body selectors, Tone Shaper,
//                                   Unnatural Zone, Material Morph)
// Phase 3 parameter range: 250-259 (Polyphony, Voice Stealing, Choke Groups)
// ==============================================================================

enum ParameterIds : Steinberg::Vst::ParamID
{
    // ====== Phase 1 (frozen) ======
    kMaterialId       = 100,  // 0.0 woody -- 1.0 metallic
    kSizeId           = 101,  // 0.0 small(500Hz) -- 1.0 large(50Hz)
    kDecayId          = 102,  // 0.0 short -- 1.0 long
    kStrikePositionId = 103,  // 0.0 center -- 1.0 edge
    kLevelId          = 104,  // 0.0 silent -- 1.0 full

    // ====== Phase 2 ======

    // 200-209: Exciter + Body selectors and secondary exciter params
    kExciterTypeId               = 200,  // StringListParameter, 6 choices
    kBodyModelId                 = 201,  // StringListParameter, 6 choices
    kExciterFMRatioId            = 202,  // FM Impulse carrier:mod ratio (1.0..4.0, default 1.4)
    kExciterFeedbackAmountId     = 203,  // Feedback exciter drive amount (0..1)
    kExciterNoiseBurstDurationId = 204,  // Noise Burst duration ms (2..15, default 5)
    kExciterFrictionPressureId   = 205,  // Friction bow pressure (0..1, default 0.3)

    // 210-219: Tone Shaper (filter + drive + fold + pitch env)
    kToneShaperFilterTypeId       = 210,  // StringListParameter: LP/HP/BP
    kToneShaperFilterCutoffId     = 211,  // 20..20000 Hz, default 20000
    kToneShaperFilterResonanceId  = 212,  // 0..1, default 0
    kToneShaperFilterEnvAmountId  = 213,  // -1..1, default 0
    kToneShaperDriveAmountId      = 214,  // 0..1, default 0
    kToneShaperFoldAmountId       = 215,  // 0..1, default 0
    kToneShaperPitchEnvStartId    = 216,  // 20..2000 Hz, default 160
    kToneShaperPitchEnvEndId      = 217,  // 20..2000 Hz, default 50
    kToneShaperPitchEnvTimeId     = 218,  // 0..500 ms, default 0 (disabled)
    kToneShaperPitchEnvCurveId    = 219,  // StringListParameter: Exp/Lin

    // 220-229: Tone Shaper filter envelope sub-parameters
    kToneShaperFilterEnvAttackId  = 220,
    kToneShaperFilterEnvDecayId   = 221,
    kToneShaperFilterEnvSustainId = 222,
    kToneShaperFilterEnvReleaseId = 223,

    // 230-239: Unnatural Zone
    kUnnaturalModeStretchId       = 230,  // 0.5..2.0, default 1.0
    kUnnaturalDecaySkewId         = 231,  // -1..1, default 0
    kUnnaturalModeInjectAmountId  = 232,  // 0..1, default 0
    kUnnaturalNonlinearCouplingId = 233,  // 0..1, default 0

    // 240-249: Material Morph
    kMorphEnabledId               = 240,
    kMorphStartId                 = 241,  // 0..1, default 1 (current Material)
    kMorphEndId                   = 242,  // 0..1, default 0
    kMorphDurationMsId            = 243,  // 10..2000 ms, default 200
    kMorphCurveId                 = 244,  // StringListParameter: Lin/Exp

    // ====== Phase 3 ======

    // 250-259: Polyphony / Voice Stealing / Choke Groups
    kMaxPolyphonyId               = 250,  // RangeParameter stepped [4,16], default 8
    kVoiceStealingId              = 251,  // StringListParameter {Oldest,Quietest,Priority}
    kChokeGroupId                 = 252,  // RangeParameter stepped [0,8], default 0

    // ====== Phase 4 ======

    // 260: Selected pad proxy selector
    kSelectedPadId                = 260,  // RangeParameter stepped [0,31], default 0

    // ====== Phase 5 ======

    // 270-279: Cross-Pad Coupling (Sympathetic Resonance)
    kGlobalCouplingId             = 270,  // RangeParameter [0.0, 1.0], default 0.0
    kSnareBuzzId                  = 271,  // RangeParameter [0.0, 1.0], default 0.0
    kTomResonanceId               = 272,  // RangeParameter [0.0, 1.0], default 0.0
    kCouplingDelayId              = 273,  // RangeParameter [0.5, 2.0] ms, default 1.0

    // ====== Phase 6 ======

    // 280: Session-scoped UI parameter (NOT persisted in processor state;
    // persisted in kit preset via codec hasSession flag).
    kUiModeId                     = 280,  // StringListParameter {Acoustic, Extended}

    // FR-065 (spec 141, Phase 8): Output Bus selector for the Selected-Pad Panel.
    // Acts as a Phase 4 selected-pad proxy -- forwarding writes to
    // kPadOutputBus of the currently selected pad. Registered as a 16-entry
    // StringListParameter (Main, Aux 1..Aux 15).
    kOutputBusId                  = 282,

    // ====== Phase 7: parallel noise layer + always-on click transient ======
    // Selected-pad proxies forwarding to kPadNoiseLayer* / kPadClickLayer*
    // for the currently selected pad.
    kNoiseLayerMixId              = 290,
    kNoiseLayerCutoffId           = 291,
    kNoiseLayerResonanceId        = 292,
    kNoiseLayerDecayId            = 293,
    kNoiseLayerColorId            = 294,
    kClickLayerMixId              = 295,
    kClickLayerContactMsId        = 296,
    kClickLayerBrightnessId       = 297,

    // ====== Phase 8A: per-mode damping law ======
    // Selected-pad proxies for the b1/b3 overrides on kPadBodyDampingB1/B3.
    kBodyDampingB1Id              = 300,
    kBodyDampingB3Id              = 301,

    // ====== Phase 8C: air-loading + per-mode scatter ======
    kAirLoadingId                 = 302,
    kModeScatterId                = 303,

    // ====== Phase 8D: head <-> shell coupling ======
    kCouplingStrengthId           = 304,
    kSecondaryEnabledId           = 305,
    kSecondarySizeId              = 306,
    kSecondaryMaterialId          = 307,

    // ====== Phase 8E: nonlinear tension modulation ======
    kTensionModAmtId              = 308,

    // ====== Phase 8F: per-pad enable toggle ======
    // Selected-pad proxy for kPadEnabled (per-pad offset 59). Discrete
    // float-as-bool; default 1.0 (enabled).
    kPadEnabledId                 = 309,

    // ====== Phase 9: global master output gain ======
    // RangeParameter [-24, +12] dB, default -6 dB. Applied to the main output
    // bus (L/R) only -- aux buses are pre-master sends.
    kMasterGainId                 = 320,
};

// Compile-time collision guard: Phase 1 IDs (100-104) must not overlap Phase 2
// IDs (200-244). This static_assert keeps future editors honest.
static_assert(kLevelId < kExciterTypeId,
              "Phase 1 and Phase 2 parameter ID ranges must not overlap");
static_assert(kCurrentStateVersion >= 1,
              "State version must be >= 1");

// Phase 3 collision guards (FR-151). kMorphCurveId (244) is the last Phase 2
// ID; kMaxPolyphonyId (250) opens the Phase 3 range. The gap 245..249 is
// reserved for Phase 2 follow-ups.
static_assert(kMorphCurveId < kMaxPolyphonyId,
              "Phase 2 and Phase 3 parameter ID ranges must not overlap");

// ==============================================================================
// Phase 4: Per-Pad Parameter Layout
// ==============================================================================
// Per-pad parameters live at kPadBaseId + padIndex * kPadParamStride + offset.
// Pad 0: 1000-1063, Pad 1: 1064-1127, ..., Pad 31: 2984-3047.
// Offsets 0-35 are active (kPadActiveParamCount); 36-63 reserved for Phase 5+.
// ==============================================================================

// Phase 4 per-pad constants (kPadBaseId, kPadParamStride, kNumPads,
// kMaxOutputBuses, PadConfig, PadParamOffset, helper functions) are
// defined in dsp/pad_config.h, included at the top of this file.

// Phase 4 collision guards. kSelectedPadId (260) is the last Phase 4 global param;
// kPadBaseId (1000) opens the per-pad range. Gap 274..999 is reserved.
static_assert(kSelectedPadId < kGlobalCouplingId,
              "Phase 4 and Phase 5 parameter ID ranges must not overlap (FR-062)");
static_assert(kCouplingDelayId < kPadBaseId,
              "Phase 5 global and per-pad parameter ID ranges must not overlap");

// Phase 6 collision guards (FR-071).
static_assert(kCouplingDelayId < kUiModeId,
              "Phase 5 and Phase 6 parameter ID ranges must not overlap");
static_assert(kUiModeId + kPhase6GlobalCount <= kPadBaseId,
              "Phase 6 global parameters must not collide with per-pad range");
static_assert(kCurrentStateVersion == 1,
              "Pre-release codec is pinned at state version 1");

// Phase 7 collision guards: proxy IDs 290..297 must sit below the per-pad base.
static_assert(kClickLayerBrightnessId < kPadBaseId,
              "Phase 7 global proxy IDs must not collide with per-pad range");

// Phase 8A collision guards: proxy IDs 300..301 must sit below the per-pad base.
static_assert(kBodyDampingB3Id < kPadBaseId,
              "Phase 8A global proxy IDs must not collide with per-pad range");

// Phase 8C collision guards: proxy IDs 302..303 must sit below the per-pad base.
static_assert(kModeScatterId < kPadBaseId,
              "Phase 8C global proxy IDs must not collide with per-pad range");

// Phase 8D collision guards: proxy IDs 304..307 must sit below the per-pad base.
static_assert(kSecondaryMaterialId < kPadBaseId,
              "Phase 8D global proxy IDs must not collide with per-pad range");

// Phase 8E collision guard.
static_assert(kTensionModAmtId < kPadBaseId,
              "Phase 8E global proxy IDs must not collide with per-pad range");

// Phase 8F collision guard.
static_assert(kPadEnabledId < kPadBaseId,
              "Phase 8F global proxy ID must not collide with per-pad range");

// Phase 9 collision guard.
static_assert(kMasterGainId < kPadBaseId,
              "Phase 9 master gain ID must not collide with per-pad range");

} // namespace Membrum
