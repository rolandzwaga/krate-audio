#pragma once

// ==============================================================================
// Selected-pad proxy table
// ==============================================================================
// Maps each selected-pad proxy parameter ID (a global ID below kPadBaseId that
// the editor binds) to the per-pad offset it stands for. The controller sends a
// gesture on a proxy to the host on the selected pad's per-pad ID and mirrors
// the selected pad's values into the proxies; the editor reachability test
// reads the same table.
// ==============================================================================

#include "plugin_ids.h"
#include "dsp/pad_config.h"

#include "pluginterfaces/vst/vsttypes.h"

namespace Membrum {

struct ProxyMapping
{
    Steinberg::Vst::ParamID globalId;
    int                     padOffset;
};

inline constexpr ProxyMapping kProxyMappings[] = {
    {.globalId = kMaterialId,                   .padOffset = kPadMaterial },
    {.globalId = kSizeId,                       .padOffset = kPadSize },
    {.globalId = kDecayId,                      .padOffset = kPadDecay },
    {.globalId = kStrikePositionId,             .padOffset = kPadStrikePosition },
    {.globalId = kLevelId,                      .padOffset = kPadLevel },
    {.globalId = kExciterTypeId,                .padOffset = kPadExciterType },
    {.globalId = kBodyModelId,                  .padOffset = kPadBodyModel },
    {.globalId = kExciterFMRatioId,             .padOffset = kPadFMRatio },
    {.globalId = kExciterFeedbackAmountId,      .padOffset = kPadFeedbackAmount },
    {.globalId = kExciterNoiseBurstDurationId,  .padOffset = kPadNoiseBurstDuration },
    {.globalId = kExciterFrictionPressureId,    .padOffset = kPadFrictionPressure },
    {.globalId = kToneShaperFilterTypeId,       .padOffset = kPadTSFilterType },
    {.globalId = kToneShaperFilterCutoffId,     .padOffset = kPadTSFilterCutoff },
    {.globalId = kToneShaperFilterResonanceId,  .padOffset = kPadTSFilterResonance },
    {.globalId = kToneShaperFilterEnvAmountId,  .padOffset = kPadTSFilterEnvAmount },
    {.globalId = kToneShaperDriveAmountId,      .padOffset = kPadTSDriveAmount },
    {.globalId = kToneShaperFoldAmountId,       .padOffset = kPadTSFoldAmount },
    {.globalId = kToneShaperPitchEnvStartId,    .padOffset = kPadTSPitchEnvStart },
    {.globalId = kToneShaperPitchEnvEndId,      .padOffset = kPadTSPitchEnvEnd },
    {.globalId = kToneShaperPitchEnvTimeId,     .padOffset = kPadTSPitchEnvTime },
    {.globalId = kToneShaperPitchEnvCurveId,    .padOffset = kPadTSPitchEnvCurve },
    {.globalId = kToneShaperFilterEnvAttackId,  .padOffset = kPadTSFilterEnvAttack },
    {.globalId = kToneShaperFilterEnvDecayId,   .padOffset = kPadTSFilterEnvDecay },
    {.globalId = kToneShaperFilterEnvSustainId, .padOffset = kPadTSFilterEnvSustain },
    {.globalId = kToneShaperFilterEnvReleaseId, .padOffset = kPadTSFilterEnvRelease },
    {.globalId = kUnnaturalModeStretchId,       .padOffset = kPadModeStretch },
    {.globalId = kUnnaturalDecaySkewId,         .padOffset = kPadDecaySkew },
    {.globalId = kUnnaturalModeInjectAmountId,  .padOffset = kPadModeInjectAmount },
    {.globalId = kUnnaturalNonlinearCouplingId, .padOffset = kPadNonlinearCoupling },
    {.globalId = kMorphEnabledId,               .padOffset = kPadMorphEnabled },
    {.globalId = kMorphStartId,                 .padOffset = kPadMorphStart },
    {.globalId = kMorphEndId,                   .padOffset = kPadMorphEnd },
    {.globalId = kMorphDurationMsId,            .padOffset = kPadMorphDuration },
    {.globalId = kMorphCurveId,                 .padOffset = kPadMorphCurve },
    {.globalId = kChokeGroupId,                 .padOffset = kPadChokeGroup },
    // Phase 8 (US7 / FR-065): Output Bus selector proxy.
    {.globalId = kOutputBusId,                  .padOffset = kPadOutputBus },
    // Phase 7: parallel noise layer + always-on click transient
    {.globalId = kNoiseLayerMixId,              .padOffset = kPadNoiseLayerMix },
    {.globalId = kNoiseLayerCutoffId,           .padOffset = kPadNoiseLayerCutoff },
    {.globalId = kNoiseLayerResonanceId,        .padOffset = kPadNoiseLayerResonance },
    {.globalId = kNoiseLayerDecayId,            .padOffset = kPadNoiseLayerDecay },
    {.globalId = kNoiseLayerColorId,            .padOffset = kPadNoiseLayerColor },
    {.globalId = kClickLayerMixId,              .padOffset = kPadClickLayerMix },
    {.globalId = kClickLayerContactMsId,        .padOffset = kPadClickLayerContactMs },
    {.globalId = kClickLayerBrightnessId,       .padOffset = kPadClickLayerBrightness },
    // Phase 8A: per-mode damping law overrides (offsets 50, 51).
    {.globalId = kBodyDampingB1Id,              .padOffset = kPadBodyDampingB1 },
    {.globalId = kBodyDampingB3Id,              .padOffset = kPadBodyDampingB3 },
    // Phase 8C: air-loading + per-mode scatter (offsets 52, 53).
    {.globalId = kAirLoadingId,                 .padOffset = kPadAirLoading },
    {.globalId = kModeScatterId,                .padOffset = kPadModeScatter },
    // Phase 8D: head <-> shell coupling (offsets 54-57).
    {.globalId = kCouplingStrengthId,           .padOffset = kPadCouplingStrength },
    {.globalId = kSecondaryEnabledId,           .padOffset = kPadSecondaryEnabled },
    {.globalId = kSecondarySizeId,              .padOffset = kPadSecondarySize },
    {.globalId = kSecondaryMaterialId,          .padOffset = kPadSecondaryMaterial },
    // Phase 8E: nonlinear tension modulation (offset 58).
    {.globalId = kTensionModAmtId,              .padOffset = kPadTensionModAmt },
    // Phase 8F: per-pad enable toggle (offset 59).
    {.globalId = kPadEnabledId,                 .padOffset = kPadEnabled },
    // Phase 10: three-point pitch envelope extension (offsets 60-63).
    {.globalId = kPitchEnvKneeEnabledId,        .padOffset = kPadTSPitchEnvKneeEnabled },
    {.globalId = kPitchEnvMidPitchId,           .padOffset = kPadTSPitchEnvMidPitch },
    {.globalId = kPitchEnvMidFractionId,        .padOffset = kPadTSPitchEnvMidFraction },
    {.globalId = kPitchEnvCurve2Id,             .padOffset = kPadTSPitchEnvCurve2 },
    // M-9: per-pad pan (offset 64).
    {.globalId = kPadPanId,                     .padOffset = kPadPan },
    // Wire coupling (offset 65).
    {.globalId = kWireCouplingId,               .padOffset = kPadWireCoupling },
    // MB-071: Coupling Amount (offset 36) and the five macros (offsets 37-41).
    {.globalId = kCouplingAmountId,             .padOffset = kPadCouplingAmount },
    {.globalId = kMacroTightnessId,             .padOffset = kPadMacroTightness },
    {.globalId = kMacroBrightnessId,            .padOffset = kPadMacroBrightness },
    {.globalId = kMacroBodySizeId,              .padOffset = kPadMacroBodySize },
    {.globalId = kMacroPunchId,                 .padOffset = kPadMacroPunch },
    {.globalId = kMacroComplexityId,            .padOffset = kPadMacroComplexity },
};

} // namespace Membrum
