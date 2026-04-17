// ==============================================================================
// Membrum Controller -- Phase 4 per-pad parameter registration + proxy logic
// ==============================================================================

#include "controller.h"
#include "plugin_ids.h"
#include "dsp/pad_config.h"
#include "dsp/exciter_type.h"
#include "dsp/body_model_type.h"
#include "state/state_codec.h"
#include "ui/pad_grid_view.h"
#include "ui/kit_meters_view.h"
#include "ui/polyphony_slider.h"
#include "ui/coupling_matrix_view.h"
#include "ui/pitch_envelope_display.h"  // shared PitchEnvelopeDisplay (Krate::Plugins)
#include "preset/membrum_preset_config.h"

#include "preset/preset_manager.h"
#include "preset/preset_info.h"
#include "ui/preset_browser_view.h"
#include "ui/save_preset_dialog_view.h"

#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "base/source/fstring.h"

#include "vstgui/plugin-bindings/vst3editor.h"
#include "vstgui/uidescription/uiattributes.h"

#include "../ui/outline_button.h"
#include "../ui/preset_inline_browser_view.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/cframe.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace Membrum {

using namespace Steinberg;
using namespace Steinberg::Vst;

// ==============================================================================
// Constructor / Destructor (defined in .cpp so unique_ptr<PresetManager>
// destruction sees the complete type).
// ==============================================================================
Controller::Controller() = default;
Controller::~Controller() = default;

// ==============================================================================
// Global proxy parameter mapping table
// Maps each global parameter ID to its corresponding PadParamOffset.
// ==============================================================================
namespace {

struct ProxyMapping
{
    ParamID globalId;
    int     padOffset;
};

constexpr ProxyMapping kProxyMappings[] = {
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
};

// Per-pad parameter name table
struct PadParamSpec
{
    int         offset;
    const char* name;
    bool        isDiscrete;
    int         stepCount;  // 0 for continuous
    double      defaultValue;
};

const PadParamSpec kPadParamSpecs[] = {
    {.offset = kPadExciterType,        .name = "Exciter Type",        .isDiscrete = true,  .stepCount = 5, .defaultValue = 0.0 },
    {.offset = kPadBodyModel,          .name = "Body Model",          .isDiscrete = true,  .stepCount = 5, .defaultValue = 0.0 },
    {.offset = kPadMaterial,           .name = "Material",            .isDiscrete = false, .stepCount = 0, .defaultValue = 0.5 },
    {.offset = kPadSize,               .name = "Size",                .isDiscrete = false, .stepCount = 0, .defaultValue = 0.5 },
    {.offset = kPadDecay,              .name = "Decay",               .isDiscrete = false, .stepCount = 0, .defaultValue = 0.3 },
    {.offset = kPadStrikePosition,     .name = "Strike Position",     .isDiscrete = false, .stepCount = 0, .defaultValue = 0.3 },
    {.offset = kPadLevel,              .name = "Level",               .isDiscrete = false, .stepCount = 0, .defaultValue = 0.8 },
    {.offset = kPadTSFilterType,       .name = "Filter Type",         .isDiscrete = true,  .stepCount = 2, .defaultValue = 0.0 },
    {.offset = kPadTSFilterCutoff,     .name = "Filter Cutoff",       .isDiscrete = false, .stepCount = 0, .defaultValue = 1.0 },
    {.offset = kPadTSFilterResonance,  .name = "Filter Resonance",    .isDiscrete = false, .stepCount = 0, .defaultValue = 0.0 },
    {.offset = kPadTSFilterEnvAmount,  .name = "Filter Env Amount",   .isDiscrete = false, .stepCount = 0, .defaultValue = 0.5 },
    {.offset = kPadTSDriveAmount,      .name = "Drive Amount",        .isDiscrete = false, .stepCount = 0, .defaultValue = 0.0 },
    {.offset = kPadTSFoldAmount,       .name = "Fold Amount",         .isDiscrete = false, .stepCount = 0, .defaultValue = 0.0 },
    {.offset = kPadTSPitchEnvStart,    .name = "PitchEnv Start",      .isDiscrete = false, .stepCount = 0, .defaultValue = 0.0 },
    {.offset = kPadTSPitchEnvEnd,      .name = "PitchEnv End",        .isDiscrete = false, .stepCount = 0, .defaultValue = 0.0 },
    {.offset = kPadTSPitchEnvTime,     .name = "PitchEnv Time",       .isDiscrete = false, .stepCount = 0, .defaultValue = 0.0 },
    {.offset = kPadTSPitchEnvCurve,    .name = "PitchEnv Curve",      .isDiscrete = true,  .stepCount = 1, .defaultValue = 0.0 },
    {.offset = kPadTSFilterEnvAttack,  .name = "Filter Env Atk",      .isDiscrete = false, .stepCount = 0, .defaultValue = 0.0 },
    {.offset = kPadTSFilterEnvDecay,   .name = "Filter Env Dec",      .isDiscrete = false, .stepCount = 0, .defaultValue = 0.1 },
    {.offset = kPadTSFilterEnvSustain, .name = "Filter Env Sus",      .isDiscrete = false, .stepCount = 0, .defaultValue = 0.0 },
    {.offset = kPadTSFilterEnvRelease, .name = "Filter Env Rel",      .isDiscrete = false, .stepCount = 0, .defaultValue = 0.1 },
    {.offset = kPadModeStretch,        .name = "Mode Stretch",        .isDiscrete = false, .stepCount = 0, .defaultValue = 0.333333 },
    {.offset = kPadDecaySkew,          .name = "Decay Skew",          .isDiscrete = false, .stepCount = 0, .defaultValue = 0.5 },
    {.offset = kPadModeInjectAmount,   .name = "Mode Inject",         .isDiscrete = false, .stepCount = 0, .defaultValue = 0.0 },
    {.offset = kPadNonlinearCoupling,  .name = "Nonlinear Coupling",  .isDiscrete = false, .stepCount = 0, .defaultValue = 0.0 },
    {.offset = kPadMorphEnabled,       .name = "Morph Enabled",       .isDiscrete = true,  .stepCount = 1, .defaultValue = 0.0 },
    {.offset = kPadMorphStart,         .name = "Morph Start",         .isDiscrete = false, .stepCount = 0, .defaultValue = 1.0 },
    {.offset = kPadMorphEnd,           .name = "Morph End",           .isDiscrete = false, .stepCount = 0, .defaultValue = 0.0 },
    {.offset = kPadMorphDuration,      .name = "Morph Duration",      .isDiscrete = false, .stepCount = 0, .defaultValue = 0.095477 },
    {.offset = kPadMorphCurve,         .name = "Morph Curve",         .isDiscrete = true,  .stepCount = 1, .defaultValue = 0.0 },
    {.offset = kPadChokeGroup,         .name = "Choke Group",         .isDiscrete = true,  .stepCount = 8, .defaultValue = 0.0 },
    {.offset = kPadOutputBus,          .name = "Output Bus",          .isDiscrete = true,  .stepCount = 15, .defaultValue = 0.0 },
    {.offset = kPadFMRatio,            .name = "FM Ratio",            .isDiscrete = false, .stepCount = 0, .defaultValue = 0.5 },
    {.offset = kPadFeedbackAmount,     .name = "Feedback Amount",     .isDiscrete = false, .stepCount = 0, .defaultValue = 0.0 },
    {.offset = kPadNoiseBurstDuration, .name = "NoiseBurst Duration", .isDiscrete = false, .stepCount = 0, .defaultValue = 0.5 },
    {.offset = kPadFrictionPressure,   .name = "Friction Pressure",   .isDiscrete = false, .stepCount = 0, .defaultValue = 0.0 },
    // Phase 6 (US4 / T044): per-pad coupling amount (offset 36)
    {.offset = kPadCouplingAmount,     .name = "Coupling Amount",     .isDiscrete = false, .stepCount = 0, .defaultValue = 0.5 },
    // Phase 6 (US1 / T026): per-pad macros (offsets 37-41), FR-072 naming
    {.offset = kPadMacroTightness,     .name = "Tightness",           .isDiscrete = false, .stepCount = 0, .defaultValue = 0.5 },
    {.offset = kPadMacroBrightness,    .name = "Brightness",          .isDiscrete = false, .stepCount = 0, .defaultValue = 0.5 },
    {.offset = kPadMacroBodySize,      .name = "Body Size",           .isDiscrete = false, .stepCount = 0, .defaultValue = 0.5 },
    {.offset = kPadMacroPunch,         .name = "Punch",               .isDiscrete = false, .stepCount = 0, .defaultValue = 0.5 },
    {.offset = kPadMacroComplexity,    .name = "Complexity",          .isDiscrete = false, .stepCount = 0, .defaultValue = 0.5 },
    // Phase 7 (parallel noise layer + always-on click transient)
    {.offset = kPadNoiseLayerMix,        .name = "Noise Mix",           .isDiscrete = false, .stepCount = 0, .defaultValue = 0.35 },
    {.offset = kPadNoiseLayerCutoff,     .name = "Noise Cutoff",        .isDiscrete = false, .stepCount = 0, .defaultValue = 0.5 },
    {.offset = kPadNoiseLayerResonance,  .name = "Noise Resonance",     .isDiscrete = false, .stepCount = 0, .defaultValue = 0.2 },
    {.offset = kPadNoiseLayerDecay,      .name = "Noise Decay",         .isDiscrete = false, .stepCount = 0, .defaultValue = 0.3 },
    {.offset = kPadNoiseLayerColor,      .name = "Noise Color",         .isDiscrete = false, .stepCount = 0, .defaultValue = 0.5 },
    {.offset = kPadClickLayerMix,        .name = "Click Mix",           .isDiscrete = false, .stepCount = 0, .defaultValue = 0.5 },
    {.offset = kPadClickLayerContactMs,  .name = "Click Contact",       .isDiscrete = false, .stepCount = 0, .defaultValue = 0.3 },
    {.offset = kPadClickLayerBrightness, .name = "Click Brightness",    .isDiscrete = false, .stepCount = 0, .defaultValue = 0.6 },
    // Phase 8A: per-mode damping law. Default 0.5 = neutral override:
    //   b1 norm 0.5 -> 25 s^-1 (mid-tight)
    //   b3 norm 0.5 -> 4e-5 s*rad^-2 (equivalent to legacy brightness=0.5)
    // The effective override only kicks in once the host/user actively
    // writes a value; DrumVoice stores -1.0f sentinel by default, preserving
    // Phase 1 bit-identity for untouched pads (see DrumVoice::bodyDampingB1_).
    {.offset = kPadBodyDampingB1,        .name = "Body Damping b1",     .isDiscrete = false, .stepCount = 0, .defaultValue = 0.5 },
    {.offset = kPadBodyDampingB3,        .name = "Body Damping b3",     .isDiscrete = false, .stepCount = 0, .defaultValue = 0.5 },
    // Phase 8C (air-loading + per-mode scatter).
    {.offset = kPadAirLoading,           .name = "Air Loading",         .isDiscrete = false, .stepCount = 0, .defaultValue = 0.6 },
    {.offset = kPadModeScatter,          .name = "Mode Scatter",        .isDiscrete = false, .stepCount = 0, .defaultValue = 0.0 },
    // Phase 8D (head <-> shell coupling).
    {.offset = kPadCouplingStrength,     .name = "Coupling Strength",   .isDiscrete = false, .stepCount = 0, .defaultValue = 0.0 },
    {.offset = kPadSecondaryEnabled,     .name = "Secondary Enabled",   .isDiscrete = true,  .stepCount = 1, .defaultValue = 0.0 },
    {.offset = kPadSecondarySize,        .name = "Secondary Size",      .isDiscrete = false, .stepCount = 0, .defaultValue = 0.5 },
    {.offset = kPadSecondaryMaterial,    .name = "Secondary Material",  .isDiscrete = false, .stepCount = 0, .defaultValue = 0.4 },
    // Phase 8E (nonlinear tension modulation / pitch glide).
    {.offset = kPadTensionModAmt,        .name = "Tension Mod",         .isDiscrete = false, .stepCount = 0, .defaultValue = 0.0 },
};

constexpr int kPadParamSpecCount =
    static_cast<int>(sizeof(kPadParamSpecs) / sizeof(kPadParamSpecs[0]));

static_assert(kPadParamSpecCount == kPadActiveParamCountV8E,
              "Pad param specs must match active param count (59 after Phase 8E)");

// Helper: convert narrow string to TChar buffer
void narrowToTChar(const char* src, TChar* dst, int maxLen)
{
    int i = 0;
    for (; i < maxLen - 1 && src[i] != '\0'; ++i)
        dst[i] = static_cast<TChar>(src[i]);
    dst[i] = 0;
}

// ==============================================================================
// PadSnapshot <-> Controller param bridging. Used by setComponentState,
// kitPresetStateProvider, and kitPresetLoadProvider.
// ==============================================================================

// Read pad params from a Controller and produce a PadSnapshot. `includeMacros`
// controls whether the macro (offsets 37-41) fields are captured -- kit preset
// always writes them, and so does kit state.
[[nodiscard]] Membrum::State::PadSnapshot
buildPadSnapshotFromParams(const Steinberg::Vst::EditControllerEx1& ctrl,
                           int pad) noexcept
{
    // Local helper: getParamNormalized wrapper (const_cast because the VST3
    // API is not const-correct for read access).
    auto getNorm = [&](ParamID id) -> double {
        return const_cast<Steinberg::Vst::EditControllerEx1&>(ctrl)
                   .getParamNormalized(id);
    };

    Membrum::State::PadSnapshot snap;

    const double excNorm = getNorm(static_cast<ParamID>(padParamId(pad, kPadExciterType)));
    const int excI = std::clamp(
        static_cast<int>(excNorm * static_cast<double>(ExciterType::kCount)),
        0, static_cast<int>(ExciterType::kCount) - 1);
    snap.exciterType = static_cast<ExciterType>(excI);

    const double bodyNorm = getNorm(static_cast<ParamID>(padParamId(pad, kPadBodyModel)));
    const int bodyI = std::clamp(
        static_cast<int>(bodyNorm * static_cast<double>(BodyModelType::kCount)),
        0, static_cast<int>(BodyModelType::kCount) - 1);
    snap.bodyModel = static_cast<BodyModelType>(bodyI);

    // Sound indices 0-27 -> PadParamOffsets 2-29.
    constexpr int kContinuousOffsets[28] = {
        kPadMaterial, kPadSize, kPadDecay, kPadStrikePosition, kPadLevel,
        kPadTSFilterType, kPadTSFilterCutoff, kPadTSFilterResonance,
        kPadTSFilterEnvAmount, kPadTSDriveAmount, kPadTSFoldAmount,
        kPadTSPitchEnvStart, kPadTSPitchEnvEnd, kPadTSPitchEnvTime,
        kPadTSPitchEnvCurve,
        kPadTSFilterEnvAttack, kPadTSFilterEnvDecay,
        kPadTSFilterEnvSustain, kPadTSFilterEnvRelease,
        kPadModeStretch, kPadDecaySkew, kPadModeInjectAmount,
        kPadNonlinearCoupling,
        kPadMorphEnabled, kPadMorphStart, kPadMorphEnd,
        kPadMorphDuration, kPadMorphCurve,
    };
    for (int j = 0; j < 28; ++j)
    {
        snap.sound[static_cast<std::size_t>(j)] =
            getNorm(static_cast<ParamID>(padParamId(pad, kContinuousOffsets[j])));
    }

    // Sound indices 28-29: chokeGroup / outputBus float64 mirrors.
    const double chokeNorm = getNorm(static_cast<ParamID>(padParamId(pad, kPadChokeGroup)));
    const double busNorm   = getNorm(static_cast<ParamID>(padParamId(pad, kPadOutputBus)));
    snap.sound[28] = chokeNorm * 8.0;
    snap.sound[29] = busNorm   * 15.0;

    // Sound indices 30-33 -> offsets 32-35.
    constexpr int kSecOffsets[4] = {
        kPadFMRatio, kPadFeedbackAmount, kPadNoiseBurstDuration, kPadFrictionPressure
    };
    for (int j = 0; j < 4; ++j)
    {
        snap.sound[static_cast<std::size_t>(30 + j)] =
            getNorm(static_cast<ParamID>(padParamId(pad, kSecOffsets[j])));
    }

    // Authoritative uint8 choke/bus (quantised from normalised).
    snap.chokeGroup = static_cast<std::uint8_t>(
        std::clamp(static_cast<int>(chokeNorm * 8.0 + 0.5), 0, 8));
    snap.outputBus = static_cast<std::uint8_t>(
        std::clamp(static_cast<int>(busNorm * 15.0 + 0.5), 0, 15));

    snap.couplingAmount =
        getNorm(static_cast<ParamID>(padParamId(pad, kPadCouplingAmount)));

    snap.macros[0] = getNorm(static_cast<ParamID>(padParamId(pad, kPadMacroTightness)));
    snap.macros[1] = getNorm(static_cast<ParamID>(padParamId(pad, kPadMacroBrightness)));
    snap.macros[2] = getNorm(static_cast<ParamID>(padParamId(pad, kPadMacroBodySize)));
    snap.macros[3] = getNorm(static_cast<ParamID>(padParamId(pad, kPadMacroPunch)));
    snap.macros[4] = getNorm(static_cast<ParamID>(padParamId(pad, kPadMacroComplexity)));
    return snap;
}

// Apply a PadSnapshot to a Controller's per-pad parameter values. Used both
// from setComponentState and kitPresetLoadProvider. The caller supplies the
// parameter-writer: setComponentState uses EditControllerEx1::setParamNormalized
// directly (host has already updated the processor), while the preset-load
// providers supply a notifying setter that also runs beginEdit/performEdit/
// endEdit so the host pushes the new values through to the processor.
//
// The setter MUST NOT re-enter the derived Controller::setParamNormalized
// override to avoid triggering proxy-forward logic during bulk loads.
template <class Setter>
void applyPadSnapshotToParams(int pad,
                              const Membrum::State::PadSnapshot& snap,
                              Setter& setNorm) noexcept
{
    setNorm(static_cast<ParamID>(padParamId(pad, kPadExciterType)),
            (static_cast<double>(snap.exciterType) + 0.5) /
                static_cast<double>(ExciterType::kCount));
    setNorm(static_cast<ParamID>(padParamId(pad, kPadBodyModel)),
            (static_cast<double>(snap.bodyModel) + 0.5) /
                static_cast<double>(BodyModelType::kCount));

    constexpr int kContinuousOffsets[28] = {
        kPadMaterial, kPadSize, kPadDecay, kPadStrikePosition, kPadLevel,
        kPadTSFilterType, kPadTSFilterCutoff, kPadTSFilterResonance,
        kPadTSFilterEnvAmount, kPadTSDriveAmount, kPadTSFoldAmount,
        kPadTSPitchEnvStart, kPadTSPitchEnvEnd, kPadTSPitchEnvTime,
        kPadTSPitchEnvCurve,
        kPadTSFilterEnvAttack, kPadTSFilterEnvDecay,
        kPadTSFilterEnvSustain, kPadTSFilterEnvRelease,
        kPadModeStretch, kPadDecaySkew, kPadModeInjectAmount,
        kPadNonlinearCoupling,
        kPadMorphEnabled, kPadMorphStart, kPadMorphEnd,
        kPadMorphDuration, kPadMorphCurve,
    };
    for (int j = 0; j < 28; ++j)
    {
        setNorm(static_cast<ParamID>(padParamId(pad, kContinuousOffsets[j])),
                snap.sound[static_cast<std::size_t>(j)]);
    }

    // Choke group / output bus: prefer the authoritative uint8 values when
    // provided (the float64 mirror at snap.sound[28-29] is for on-wire
    // redundancy only).
    setNorm(static_cast<ParamID>(padParamId(pad, kPadChokeGroup)),
            static_cast<double>(snap.chokeGroup) / 8.0);
    setNorm(static_cast<ParamID>(padParamId(pad, kPadOutputBus)),
            static_cast<double>(snap.outputBus) / 15.0);

    constexpr int kSecOffsets[4] = {
        kPadFMRatio, kPadFeedbackAmount, kPadNoiseBurstDuration, kPadFrictionPressure
    };
    for (int j = 0; j < 4; ++j)
    {
        setNorm(static_cast<ParamID>(padParamId(pad, kSecOffsets[j])),
                snap.sound[static_cast<std::size_t>(30 + j)]);
    }

    setNorm(static_cast<ParamID>(padParamId(pad, kPadCouplingAmount)),
            snap.couplingAmount);
    setNorm(static_cast<ParamID>(padParamId(pad, kPadMacroTightness)),  snap.macros[0]);
    setNorm(static_cast<ParamID>(padParamId(pad, kPadMacroBrightness)), snap.macros[1]);
    setNorm(static_cast<ParamID>(padParamId(pad, kPadMacroBodySize)),   snap.macros[2]);
    setNorm(static_cast<ParamID>(padParamId(pad, kPadMacroPunch)),      snap.macros[3]);
    setNorm(static_cast<ParamID>(padParamId(pad, kPadMacroComplexity)), snap.macros[4]);
}

} // namespace

tresult PLUGIN_API Controller::initialize(FUnknown* context)
{
    auto result = EditControllerEx1::initialize(context);
    if (result != kResultOk)
        return result;

    // ---- Phase 1 parameters (global proxy, unchanged) ----
    parameters.addParameter(
        new RangeParameter(STR16("Material"), kMaterialId, nullptr,
                           0.0, 1.0, 0.5, 0, ParameterInfo::kCanAutomate));
    parameters.addParameter(
        new RangeParameter(STR16("Size"), kSizeId, nullptr,
                           0.0, 1.0, 0.5, 0, ParameterInfo::kCanAutomate));
    parameters.addParameter(
        new RangeParameter(STR16("Resonance"), kDecayId, nullptr,
                           0.0, 1.0, 0.3, 0, ParameterInfo::kCanAutomate));
    parameters.addParameter(
        new RangeParameter(STR16("Strike Point"), kStrikePositionId, nullptr,
                           0.0, 1.0, 0.3, 0, ParameterInfo::kCanAutomate));
    parameters.addParameter(
        new RangeParameter(STR16("Level"), kLevelId, STR16("dB"),
                           0.0, 1.0, 0.8, 0, ParameterInfo::kCanAutomate));

    // ---- Phase 2 selectors (global proxy) ----
    {
        auto* excList = new StringListParameter(
            STR16("Exciter Type"), kExciterTypeId, nullptr,
            ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
        excList->appendString(STR16("Impulse"));
        excList->appendString(STR16("Mallet"));
        excList->appendString(STR16("NoiseBurst"));
        excList->appendString(STR16("Friction"));
        excList->appendString(STR16("FMImpulse"));
        excList->appendString(STR16("Feedback"));
        parameters.addParameter(excList);
    }
    {
        auto* bodyList = new StringListParameter(
            STR16("Body Model"), kBodyModelId, nullptr,
            ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
        bodyList->appendString(STR16("Membrane"));
        bodyList->appendString(STR16("Plate"));
        bodyList->appendString(STR16("Shell"));
        bodyList->appendString(STR16("String"));
        bodyList->appendString(STR16("Bell"));
        bodyList->appendString(STR16("NoiseBody"));
        parameters.addParameter(bodyList);
    }

    // ---- Phase 2 continuous parameters (global proxy) ----
    struct Phase2ParamSpec
    {
        ParamID     id;
        const char* name;
        double      defaultValue;
        const char* unit;
    };

    static const Phase2ParamSpec kPhase2Specs[] = {
        {.id = kExciterFMRatioId,            .name = "Exciter FM Ratio",            .defaultValue = 0.133333, .unit = nullptr },
        {.id = kExciterFeedbackAmountId,     .name = "Exciter Feedback Amount",     .defaultValue = 0.0,      .unit = nullptr },
        {.id = kExciterNoiseBurstDurationId, .name = "Exciter NoiseBurst Duration", .defaultValue = 0.230769, .unit = "ms" },
        {.id = kExciterFrictionPressureId,   .name = "Exciter Friction Pressure",   .defaultValue = 0.3,      .unit = nullptr },
        {.id = kToneShaperFilterTypeId,      .name = "Tone Shaper Filter Type",     .defaultValue = 0.0,      .unit = nullptr },
        {.id = kToneShaperFilterCutoffId,    .name = "Tone Shaper Filter Cutoff",   .defaultValue = 1.0,      .unit = "Hz" },
        {.id = kToneShaperFilterResonanceId, .name = "Tone Shaper Filter Resonance",.defaultValue = 0.0,      .unit = nullptr },
        {.id = kToneShaperFilterEnvAmountId, .name = "Tone Shaper Filter Env Amt",  .defaultValue = 0.5,      .unit = nullptr },
        {.id = kToneShaperDriveAmountId,     .name = "Tone Shaper Drive",           .defaultValue = 0.0,      .unit = nullptr },
        {.id = kToneShaperFoldAmountId,      .name = "Tone Shaper Fold",            .defaultValue = 0.0,      .unit = nullptr },
        {.id = kToneShaperPitchEnvStartId,   .name = "Tone Shaper PitchEnv Start",  .defaultValue = 0.070721, .unit = "Hz" },
        {.id = kToneShaperPitchEnvEndId,     .name = "Tone Shaper PitchEnv End",    .defaultValue = 0.0,      .unit = "Hz" },
        {.id = kToneShaperPitchEnvTimeId,    .name = "Tone Shaper PitchEnv Time",   .defaultValue = 0.0,      .unit = "ms" },
        {.id = kToneShaperPitchEnvCurveId,   .name = "Tone Shaper PitchEnv Curve",  .defaultValue = 0.0,      .unit = nullptr },
        {.id = kToneShaperFilterEnvAttackId,  .name = "Tone Shaper Filter Atk",     .defaultValue = 0.0,      .unit = "ms" },
        {.id = kToneShaperFilterEnvDecayId,   .name = "Tone Shaper Filter Dec",     .defaultValue = 0.1,      .unit = "ms" },
        {.id = kToneShaperFilterEnvSustainId, .name = "Tone Shaper Filter Sus",     .defaultValue = 0.0,      .unit = nullptr },
        {.id = kToneShaperFilterEnvReleaseId, .name = "Tone Shaper Filter Rel",     .defaultValue = 0.1,      .unit = "ms" },
        {.id = kUnnaturalModeStretchId,       .name = "Mode Stretch",               .defaultValue = 0.333333, .unit = nullptr },
        {.id = kUnnaturalDecaySkewId,         .name = "Decay Skew",                 .defaultValue = 0.5,      .unit = nullptr },
        {.id = kUnnaturalModeInjectAmountId,  .name = "Mode Inject",                .defaultValue = 0.0,      .unit = nullptr },
        {.id = kUnnaturalNonlinearCouplingId, .name = "Nonlinear Coupling",         .defaultValue = 0.0,      .unit = nullptr },
        {.id = kMorphEnabledId,    .name = "Morph Enabled",   .defaultValue = 0.0,      .unit = nullptr },
        {.id = kMorphStartId,      .name = "Morph Start",     .defaultValue = 1.0,      .unit = nullptr },
        {.id = kMorphEndId,        .name = "Morph End",       .defaultValue = 0.0,      .unit = nullptr },
        {.id = kMorphDurationMsId, .name = "Morph Duration",  .defaultValue = 0.095477, .unit = "ms" },
        {.id = kMorphCurveId,      .name = "Morph Curve",     .defaultValue = 0.0,      .unit = nullptr },
    };

    for (const auto& spec : kPhase2Specs)
    {
        TChar titleBuf[64];
        narrowToTChar(spec.name, titleBuf, 64);

        TChar unitBuf[16] = {0};
        const TChar* unitPtr = nullptr;
        if (spec.unit != nullptr)
        {
            narrowToTChar(spec.unit, unitBuf, 16);
            unitPtr = unitBuf;
        }
        parameters.addParameter(
            new RangeParameter(titleBuf, spec.id, unitPtr,
                               0.0, 1.0, spec.defaultValue, 0,
                               ParameterInfo::kCanAutomate));
    }

    // ---- Phase 3 parameters (polyphony / stealing / choke) ----
    parameters.addParameter(
        new RangeParameter(STR16("Max Polyphony"), kMaxPolyphonyId, STR16("voices"),
                           4.0, 16.0, 8.0, /*stepCount=*/12,
                           ParameterInfo::kCanAutomate));
    {
        auto* stealList = new StringListParameter(
            STR16("Voice Stealing"), kVoiceStealingId, nullptr,
            ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
        stealList->appendString(STR16("Oldest"));
        stealList->appendString(STR16("Quietest"));
        stealList->appendString(STR16("Priority"));
        parameters.addParameter(stealList);
    }
    parameters.addParameter(
        new RangeParameter(STR16("Choke Group"), kChokeGroupId, STR16("group"),
                           0.0, 8.0, 0.0, /*stepCount=*/8,
                           ParameterInfo::kCanAutomate));

    // ---- Phase 4: kSelectedPadId ----
    parameters.addParameter(
        new RangeParameter(STR16("Selected Pad"), kSelectedPadId, nullptr,
                           0.0, 31.0, 0.0, /*stepCount=*/31,
                           ParameterInfo::kCanAutomate));

    // ---- Phase 5: Cross-Pad Coupling parameters ----
    parameters.addParameter(
        new RangeParameter(STR16("Global Coupling"), kGlobalCouplingId, nullptr,
                           0.0, 1.0, 0.0, 0, ParameterInfo::kCanAutomate));
    parameters.addParameter(
        new RangeParameter(STR16("Snare Buzz"), kSnareBuzzId, nullptr,
                           0.0, 1.0, 0.0, 0, ParameterInfo::kCanAutomate));
    parameters.addParameter(
        new RangeParameter(STR16("Tom Resonance"), kTomResonanceId, nullptr,
                           0.0, 1.0, 0.0, 0, ParameterInfo::kCanAutomate));
    parameters.addParameter(
        new RangeParameter(STR16("Coupling Delay"), kCouplingDelayId, STR16("ms"),
                           0.0, 1.0, 0.333333, 0, ParameterInfo::kCanAutomate));

    // ---- Phase 6 (US1 / T026): session-scoped UI Mode parameter ----
    // Registered as an automatable StringListParameter. Carried in kit
    // presets via the state codec's hasSession flag (so a kit author can
    // specify whether the kit opens in Acoustic or Extended mode).
    {
        auto* uiModeList = new StringListParameter(
            STR16("UI Mode"), kUiModeId, nullptr,
            ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
        uiModeList->appendString(STR16("Acoustic"));
        uiModeList->appendString(STR16("Extended"));
        parameters.addParameter(uiModeList);
    }

    // ---- Phase 6 (US7 / T074): Output Bus selector proxy (FR-065) ----
    // Global Phase 4 selected-pad proxy: forwards to kPadOutputBus of the
    // currently selected pad. Registered as a 16-entry StringListParameter
    // (Main, Aux 1..Aux 15) so the host and the COptionMenu populate entries
    // automatically.
    {
        auto* outputBusList = new StringListParameter(
            STR16("Output Bus"), kOutputBusId, nullptr,
            ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
        outputBusList->appendString(STR16("Main"));
        for (int i = 1; i < kMaxOutputBuses; ++i)
        {
            char nameBuf[16];
            std::snprintf(nameBuf, sizeof(nameBuf), "Aux %d", i);
            TChar titleBuf[16];
            narrowToTChar(nameBuf, titleBuf, 16);
            outputBusList->appendString(titleBuf);
        }
        parameters.addParameter(outputBusList);
    }

    // ---- Phase 7 global proxies: parallel noise layer + click transient ----
    parameters.addParameter(
        new RangeParameter(STR16("Noise Mix"), kNoiseLayerMixId, nullptr,
                           0.0, 1.0, 0.35, 0, ParameterInfo::kCanAutomate));
    parameters.addParameter(
        new RangeParameter(STR16("Noise Cutoff"), kNoiseLayerCutoffId, STR16("Hz"),
                           0.0, 1.0, 0.5, 0, ParameterInfo::kCanAutomate));
    parameters.addParameter(
        new RangeParameter(STR16("Noise Resonance"), kNoiseLayerResonanceId, nullptr,
                           0.0, 1.0, 0.2, 0, ParameterInfo::kCanAutomate));
    parameters.addParameter(
        new RangeParameter(STR16("Noise Decay"), kNoiseLayerDecayId, STR16("ms"),
                           0.0, 1.0, 0.3, 0, ParameterInfo::kCanAutomate));
    parameters.addParameter(
        new RangeParameter(STR16("Noise Color"), kNoiseLayerColorId, nullptr,
                           0.0, 1.0, 0.5, 0, ParameterInfo::kCanAutomate));
    parameters.addParameter(
        new RangeParameter(STR16("Click Mix"), kClickLayerMixId, nullptr,
                           0.0, 1.0, 0.5, 0, ParameterInfo::kCanAutomate));
    parameters.addParameter(
        new RangeParameter(STR16("Click Contact"), kClickLayerContactMsId, STR16("ms"),
                           0.0, 1.0, 0.3, 0, ParameterInfo::kCanAutomate));
    parameters.addParameter(
        new RangeParameter(STR16("Click Brightness"), kClickLayerBrightnessId, nullptr,
                           0.0, 1.0, 0.6, 0, ParameterInfo::kCanAutomate));

    // ---- Phase 8A global proxies: per-mode damping law ----
    parameters.addParameter(
        new RangeParameter(STR16("Body Damping b1"), kBodyDampingB1Id, nullptr,
                           0.0, 1.0, 0.5, 0, ParameterInfo::kCanAutomate));
    parameters.addParameter(
        new RangeParameter(STR16("Body Damping b3"), kBodyDampingB3Id, nullptr,
                           0.0, 1.0, 0.5, 0, ParameterInfo::kCanAutomate));

    // ---- Phase 8C global proxies: air-loading + per-mode scatter ----
    parameters.addParameter(
        new RangeParameter(STR16("Air Loading"), kAirLoadingId, nullptr,
                           0.0, 1.0, 0.6, 0, ParameterInfo::kCanAutomate));
    parameters.addParameter(
        new RangeParameter(STR16("Mode Scatter"), kModeScatterId, nullptr,
                           0.0, 1.0, 0.0, 0, ParameterInfo::kCanAutomate));

    // ---- Phase 8D global proxies: head <-> shell coupling ----
    parameters.addParameter(
        new RangeParameter(STR16("Coupling Strength"), kCouplingStrengthId, nullptr,
                           0.0, 1.0, 0.0, 0, ParameterInfo::kCanAutomate));
    parameters.addParameter(
        new RangeParameter(STR16("Secondary Enabled"), kSecondaryEnabledId, nullptr,
                           0.0, 1.0, 0.0, 1, ParameterInfo::kCanAutomate));
    parameters.addParameter(
        new RangeParameter(STR16("Secondary Size"), kSecondarySizeId, nullptr,
                           0.0, 1.0, 0.5, 0, ParameterInfo::kCanAutomate));
    parameters.addParameter(
        new RangeParameter(STR16("Secondary Material"), kSecondaryMaterialId, nullptr,
                           0.0, 1.0, 0.4, 0, ParameterInfo::kCanAutomate));

    // ---- Phase 8E global proxy: nonlinear tension modulation ----
    parameters.addParameter(
        new RangeParameter(STR16("Tension Mod"), kTensionModAmtId, nullptr,
                           0.0, 1.0, 0.0, 0, ParameterInfo::kCanAutomate));

    // ---- Phase 4/7/8A..8E: 1888 per-pad parameters (32 pads x 59 offsets) ----
    for (int pad = 0; pad < kNumPads; ++pad)
    {
        for (const auto& spec : kPadParamSpecs)
        {
            const auto paramId = static_cast<ParamID>(padParamId(pad, spec.offset));

            // Build parameter name: "Pad NN ParamName"
            char nameBuf[64];
            std::snprintf(nameBuf, sizeof(nameBuf), "Pad %02d %s", pad + 1, spec.name);
            TChar titleBuf[64];
            narrowToTChar(nameBuf, titleBuf, 64);

            if (spec.isDiscrete && spec.offset == kPadExciterType)
            {
                auto* list = new StringListParameter(
                    titleBuf, paramId, nullptr, ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
                list->appendString(STR16("Impulse"));
                list->appendString(STR16("Mallet"));
                list->appendString(STR16("NoiseBurst"));
                list->appendString(STR16("Friction"));
                list->appendString(STR16("FMImpulse"));
                list->appendString(STR16("Feedback"));
                parameters.addParameter(list);
            }
            else if (spec.isDiscrete && spec.offset == kPadBodyModel)
            {
                auto* list = new StringListParameter(
                    titleBuf, paramId, nullptr, ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
                list->appendString(STR16("Membrane"));
                list->appendString(STR16("Plate"));
                list->appendString(STR16("Shell"));
                list->appendString(STR16("String"));
                list->appendString(STR16("Bell"));
                list->appendString(STR16("NoiseBody"));
                parameters.addParameter(list);
            }
            else if (spec.isDiscrete)
            {
                parameters.addParameter(
                    new RangeParameter(titleBuf, paramId, nullptr,
                                       0.0, static_cast<double>(spec.stepCount), spec.defaultValue,
                                       spec.stepCount, ParameterInfo::kCanAutomate));
            }
            else
            {
                parameters.addParameter(
                    new RangeParameter(titleBuf, paramId, nullptr,
                                       0.0, 1.0, spec.defaultValue, 0,
                                       ParameterInfo::kCanAutomate));
            }
        }
    }

    // ==========================================================================
    // Phase 6 (T052..T056): create kit + per-pad PresetManager instances.
    // Two managers, two roots, two browsers. Both use the controller as their
    // VST3 sync target; the processor pointer is left null because we operate
    // entirely through controller params (kit/pad presets are flattened to
    // parameter writes via state providers below).
    // ==========================================================================
    kitPresetManager_ = std::make_unique<Krate::Plugins::PresetManager>(
        kitPresetConfig(),
        /*processor*/   nullptr,
        /*controller*/  this);
    kitPresetManager_->setStateProvider([this]() -> Steinberg::IBStream* {
        return kitPresetStateProvider();
    });
    kitPresetManager_->setLoadProvider(
        [this](Steinberg::IBStream* stream,
               const Krate::Plugins::PresetInfo& info) -> bool {
            const bool ok = kitPresetLoadProvider(stream);
            kitPresetLoadFailed_ = !ok;
            if (ok) {
                lastKitPresetName_ = info.name;
                if (kitInlineBrowser_)
                    kitInlineBrowser_->setCurrentPresetName(info.name);
            }
            return ok;
        });

    padPresetManager_ = std::make_unique<Krate::Plugins::PresetManager>(
        padPresetConfig(),
        /*processor*/   nullptr,
        /*controller*/  this);
    padPresetManager_->setStateProvider([this]() -> Steinberg::IBStream* {
        return padPresetStateProvider();
    });
    padPresetManager_->setLoadProvider(
        [this](Steinberg::IBStream* stream,
               const Krate::Plugins::PresetInfo& info) -> bool {
            // FR-042 / T054: per-pad load applies sound params only;
            // outputBus and couplingAmount are preserved structurally because
            // the per-pad preset blob does not carry them. After load, force
            // a re-application of the affected pad's macros so the underlying
            // params reflect both the just-loaded sound state and the user's
            // current macro positions (calls macroMapper_.apply() in the
            // processor via standard parameter-change events).
            const bool ok = padPresetLoadProvider(stream);
            padPresetLoadFailed_ = !ok;
            if (ok) {
                triggerSelectedPadMacroReapply();
                lastPadPresetName_ = info.name;
                if (padInlineBrowser_)
                    padInlineBrowser_->setCurrentPresetName(info.name);
            }
            return ok;
        });

    return kResultOk;
}

// ==============================================================================
// setParamNormalized override -- Phase 4 proxy logic
// ==============================================================================

tresult PLUGIN_API Controller::setParamNormalized(ParamID tag, ParamValue value)
{
    // First, let the base class update the parameter value.
    auto result = EditControllerEx1::setParamNormalized(tag, value);

    // If we're in the middle of a pad switch, don't recurse.
    if (suppressProxyForward_)
        return result;

    // Handle kSelectedPadId change: sync global proxies to new pad's values
    if (tag == kSelectedPadId)
    {
        // Convert normalized [0,1] to pad index [0,31]
        selectedPadIndex_ = std::clamp(
            static_cast<int>(value * 31.0 + 0.5), 0, 31);
        syncGlobalProxyFromPad(selectedPadIndex_);
        return result;
    }

    // Handle global proxy param change: forward to selected pad's per-pad param
    for (const auto& mapping : kProxyMappings)
    {
        if (mapping.globalId == tag)
        {
            const auto padId = static_cast<ParamID>(
                padParamId(selectedPadIndex_, mapping.padOffset));
            // Update the per-pad param to match the global proxy
            suppressProxyForward_ = true;
            EditControllerEx1::setParamNormalized(padId, value);
            suppressProxyForward_ = false;

            // FR-065 (spec 141, Phase 8 / US7): when the Output Bus proxy
            // changes, refresh the pad grid so its "BUS{N}" indicator picks
            // up the new value live via IDependent-style invalidation.
            // Same treatment for Choke Group keeps the "CG{N}" indicator in
            // sync without a per-pad tag scan.
            if ((mapping.globalId == kOutputBusId
                 || mapping.globalId == kChokeGroupId)
                && padGridView_ != nullptr)
            {
                padGridView_->notifyMetaChanged(selectedPadIndex_);
            }
            // FR-066: when the Output Bus proxy changes, refresh the warning
            // tooltip on the Output Bus selector in case the new selection
            // targets an inactive aux bus.
            if (mapping.globalId == kOutputBusId)
                updateOutputBusTooltip();
            return result;
        }
    }

    // Direct per-pad Output Bus / Choke Group edit (e.g. from automation
    // targeting a specific pad's per-pad parameter, not the global proxy):
    // refresh the grid's BUS / CG indicator for that pad.
    const int padOffset = padOffsetFromParamId(static_cast<int>(tag));
    if (padOffset >= 0
        && (padOffset == kPadOutputBus || padOffset == kPadChokeGroup)
        && padGridView_ != nullptr)
    {
        const int padIdx = padIndexFromParamId(static_cast<int>(tag));
        if (padIdx >= 0)
            padGridView_->notifyMetaChanged(padIdx);
    }

    return result;
}

// ==============================================================================
// activateBus -- FR-043: track bus activation for Output Bus param validity
// ==============================================================================

void Controller::notifyBusActivation(int32 busIndex, bool active)
{
    if (busIndex < 0 || std::cmp_greater_equal(busIndex, busActive_.size()))
        return;
    busActive_[static_cast<std::size_t>(busIndex)] = active;
    // FR-043: bus 0 is always active
    busActive_[0] = true;

    // FR-043: When a bus is deactivated, reset any pads currently assigned to
    // that bus back to bus 0 (main). This ensures the Output Bus parameter
    // only holds values corresponding to active buses.
    if (!active && busIndex > 0)
    {
        const double deactivatedNorm =
            static_cast<double>(busIndex) / static_cast<double>(kMaxOutputBuses - 1);
        for (int pad = 0; pad < kNumPads; ++pad)
        {
            const auto outputBusParamId =
                static_cast<ParamID>(padParamId(pad, kPadOutputBus));
            const double currentValue = getParamNormalized(outputBusParamId);
            // Compare with tolerance for floating-point normalization
            if (std::abs(currentValue - deactivatedNorm) < 0.001)
            {
                EditControllerEx1::setParamNormalized(outputBusParamId, 0.0);
            }
        }
    }

    // FR-066: bus activation state changed -- refresh the Output Bus
    // selector tooltip so any "Host must activate Aux N bus" warning
    // disappears or appears as appropriate.
    updateOutputBusTooltip();
}

// ==============================================================================
// Proxy helpers
// ==============================================================================

void Controller::syncGlobalProxyFromPad(int padIndex)
{
    suppressProxyForward_ = true;
    for (const auto& mapping : kProxyMappings)
    {
        const auto padId = static_cast<ParamID>(
            padParamId(padIndex, mapping.padOffset));
        const double padValue = getParamNormalized(padId);
        EditControllerEx1::setParamNormalized(mapping.globalId, padValue);
    }
    suppressProxyForward_ = false;
    // FR-066: after syncing the proxies to a newly selected pad, refresh the
    // Output Bus tooltip so it reflects that pad's assigned bus.
    updateOutputBusTooltip();
}

void Controller::forwardGlobalToPad(ParamID globalId, ParamValue value)
{
    for (const auto& mapping : kProxyMappings)
    {
        if (mapping.globalId == globalId)
        {
            const auto padId = static_cast<ParamID>(
                padParamId(selectedPadIndex_, mapping.padOffset));
            suppressProxyForward_ = true;
            EditControllerEx1::setParamNormalized(padId, value);
            suppressProxyForward_ = false;
            return;
        }
    }
}

// ==============================================================================
// setComponentState -- Phase 4: read state and sync all parameter values
// ==============================================================================

tresult PLUGIN_API Controller::setComponentState(IBStream* state)
{
    if (!state)
        return kResultFalse;

    // Phase 6 (T027): session-scoped kUiModeId always resets to Acoustic
    // (0.0) on state load. Kit presets may re-override it via a separate
    // preset-load callback (not through IBStream).
    EditControllerEx1::setParamNormalized(kUiModeId, 0.0);

    Membrum::State::KitSnapshot kit;
    if (Membrum::State::readKitBlob(state, kit) != kResultOk)
        return kResultFalse;

    // Polyphony + stealing policy.
    EditControllerEx1::setParamNormalized(kMaxPolyphonyId,
        static_cast<double>(kit.maxPolyphony - 4) / 12.0);
    EditControllerEx1::setParamNormalized(kVoiceStealingId,
        (static_cast<double>(kit.voiceStealingPolicy) + 0.5) / 3.0);

    // Per-pad parameters. setComponentState is the host-driven path: the
    // processor has already consumed its own state via IComponent::setState
    // before this method runs, so we just mirror the values into the
    // controller's parameter objects (UI only -- no performEdit needed).
    auto setDirect = [this](ParamID id, double v) {
        Steinberg::Vst::EditControllerEx1::setParamNormalized(id, v);
    };
    for (int pad = 0; pad < kNumPads; ++pad)
        applyPadSnapshotToParams(pad, kit.pads[static_cast<std::size_t>(pad)], setDirect);

    // Selected pad.
    selectedPadIndex_ = kit.selectedPadIndex;
    EditControllerEx1::setParamNormalized(kSelectedPadId,
        static_cast<double>(selectedPadIndex_) / 31.0);

    // Sync global proxy params from selected pad.
    syncGlobalProxyFromPad(selectedPadIndex_);

    return kResultOk;
}

// ==============================================================================
// Kit Preset StateProvider / LoadProvider (FR-052, T033)
// ==============================================================================

IBStream* Controller::kitPresetStateProvider()
{
    // Build a KitSnapshot from controller params and delegate to the shared
    // codec. Kit preset carries uiMode via the hasSession flag (FR-030,
    // FR-072). selectedPadIndex is intentionally zero on kit preset save --
    // the codec's selectedPadIndex field is a no-op for preset-load paths,
    // which never apply the value.
    auto* stream = new MemoryStream();

    Membrum::State::KitSnapshot kit;

    // Polyphony / stealing from controller params.
    const double maxPolyNorm = getParamNormalized(kMaxPolyphonyId);
    kit.maxPolyphony = std::clamp(
        4 + static_cast<int>(maxPolyNorm * 12.0 + 0.5), 4, 16);
    const double stealNorm = getParamNormalized(kVoiceStealingId);
    kit.voiceStealingPolicy =
        std::clamp(static_cast<int>(stealNorm * 3.0), 0, 2);

    // Phase 5 globals.
    kit.globalCoupling  = getParamNormalized(kGlobalCouplingId);
    kit.snareBuzz       = getParamNormalized(kSnareBuzzId);
    kit.tomResonance    = getParamNormalized(kTomResonanceId);
    // Coupling delay: param is [0.5, 2.0] ms -- controller stores the
    // normalized [0,1] value, so denormalize for the wire (the codec clamps
    // to the original range on read).
    {
        const double cdNorm = getParamNormalized(kCouplingDelayId);
        kit.couplingDelayMs = std::clamp(0.5 + cdNorm * 1.5, 0.5, 2.0);
    }

    kit.selectedPadIndex = selectedPadIndex_;

    for (int pad = 0; pad < kNumPads; ++pad)
    {
        kit.pads[static_cast<std::size_t>(pad)] =
            buildPadSnapshotFromParams(*this, pad);
    }

    // uiMode persists in kit presets (FR-030 / FR-072).
    kit.hasSession = true;
    kit.uiMode     = (getParamNormalized(kUiModeId) >= 0.5) ? 1 : 0;

    Membrum::State::writeKitBlob(stream, kit);
    stream->seek(0, IBStream::kIBSeekSet, nullptr);
    return stream;
}

bool Controller::kitPresetLoadProvider(IBStream* stream)
{
    if (!stream)
        return false;

    Membrum::State::KitSnapshot kit;
    if (Membrum::State::readKitBlob(stream, kit) != kResultOk)
        return false;

    // Kit preset load bypasses the host's preset-load mechanism, so the
    // processor never sees the new values unless we notify the host through
    // beginEdit / performEdit / endEdit. Plain setParamNormalized only
    // updates the controller-side Parameter objects (UI).
    auto setAndNotify = [this](ParamID id, double v) {
        const double clamped = std::clamp(v, 0.0, 1.0);
        Steinberg::Vst::EditControllerEx1::setParamNormalized(id, clamped);
        beginEdit(id);
        performEdit(id, clamped);
        endEdit(id);
    };

    // Global settings.
    setAndNotify(kMaxPolyphonyId,
        static_cast<double>(kit.maxPolyphony - 4) / 12.0);
    setAndNotify(kVoiceStealingId,
        (static_cast<double>(kit.voiceStealingPolicy) + 0.5) / 3.0);
    setAndNotify(kGlobalCouplingId, kit.globalCoupling);
    setAndNotify(kSnareBuzzId,      kit.snareBuzz);
    setAndNotify(kTomResonanceId,   kit.tomResonance);
    setAndNotify(kCouplingDelayId,
        std::clamp((kit.couplingDelayMs - 0.5) / 1.5, 0.0, 1.0));

    // Per-pad parameters.
    for (int pad = 0; pad < kNumPads; ++pad)
        applyPadSnapshotToParams(pad, kit.pads[static_cast<std::size_t>(pad)],
                                 setAndNotify);

    // Session field (uiMode) restored if present. uiMode is a session-scoped
    // UI-only param; controller-side update is enough (no performEdit needed
    // because the processor does not consume it).
    if (kit.hasSession)
    {
        EditControllerEx1::setParamNormalized(kUiModeId,
            kit.uiMode >= 1 ? 1.0 : 0.0);
    }

    // Kit preset does NOT restore selectedPadIndex -- leave it unchanged.
    syncGlobalProxyFromPad(selectedPadIndex_);

    return true;
}

// ==============================================================================
// Pad Preset StateProvider / LoadProvider (FR-060 through FR-063, T040)
// ==============================================================================

IBStream* Controller::padPresetStateProvider()
{
    auto* stream = new MemoryStream();

    const int pad = selectedPadIndex_;
    const auto full = buildPadSnapshotFromParams(*this, pad);

    // Project the per-pad snapshot down to the narrower per-pad preset slice.
    Membrum::State::PadPresetSnapshot preset;
    preset.exciterType = full.exciterType;
    preset.bodyModel   = full.bodyModel;
    preset.sound       = full.sound;

    Membrum::State::writePadPresetBlob(stream, preset);
    stream->seek(0, IBStream::kIBSeekSet, nullptr);
    return stream;
}

bool Controller::padPresetLoadProvider(IBStream* stream)
{
    if (!stream)
        return false;

    Membrum::State::PadPresetSnapshot preset;
    if (Membrum::State::readPadPresetBlob(stream, preset) != kResultOk)
        return false;

    // Apply to the selected pad. Per FR-061 we intentionally write only the
    // fields carried by the per-pad preset slice (exciterType, bodyModel, the
    // 28 continuous sound fields, and the 4 secondary fields) -- chokeGroup,
    // outputBus, couplingAmount, and macros are left untouched so the user's
    // kit-level routing and macro positions survive per-pad preset browsing.
    //
    // Like the kit-preset path, this bypasses the host's preset-load
    // mechanism; without begin/perform/endEdit the processor never sees the
    // new values and the pad sounds identical to before the load.
    const int pad = selectedPadIndex_;

    auto setAndNotify = [this](ParamID id, double v) {
        const double clamped = std::clamp(v, 0.0, 1.0);
        Steinberg::Vst::EditControllerEx1::setParamNormalized(id, clamped);
        beginEdit(id);
        performEdit(id, clamped);
        endEdit(id);
    };

    setAndNotify(static_cast<ParamID>(padParamId(pad, kPadExciterType)),
        (static_cast<double>(preset.exciterType) + 0.5) /
            static_cast<double>(ExciterType::kCount));
    setAndNotify(static_cast<ParamID>(padParamId(pad, kPadBodyModel)),
        (static_cast<double>(preset.bodyModel) + 0.5) /
            static_cast<double>(BodyModelType::kCount));

    constexpr int kContinuousOffsets[28] = {
        kPadMaterial, kPadSize, kPadDecay, kPadStrikePosition, kPadLevel,
        kPadTSFilterType, kPadTSFilterCutoff, kPadTSFilterResonance,
        kPadTSFilterEnvAmount, kPadTSDriveAmount, kPadTSFoldAmount,
        kPadTSPitchEnvStart, kPadTSPitchEnvEnd, kPadTSPitchEnvTime,
        kPadTSPitchEnvCurve,
        kPadTSFilterEnvAttack, kPadTSFilterEnvDecay,
        kPadTSFilterEnvSustain, kPadTSFilterEnvRelease,
        kPadModeStretch, kPadDecaySkew, kPadModeInjectAmount,
        kPadNonlinearCoupling,
        kPadMorphEnabled, kPadMorphStart, kPadMorphEnd,
        kPadMorphDuration, kPadMorphCurve,
    };
    for (int j = 0; j < 28; ++j)
    {
        setAndNotify(
            static_cast<ParamID>(padParamId(pad, kContinuousOffsets[j])),
            preset.sound[static_cast<std::size_t>(j)]);
    }

    // sound[28]/[29] (chokeGroup/outputBus float64 mirrors) intentionally
    // skipped per FR-061.

    constexpr int kSecOffsets[4] = {
        kPadFMRatio, kPadFeedbackAmount, kPadNoiseBurstDuration, kPadFrictionPressure
    };
    for (int j = 0; j < 4; ++j)
    {
        setAndNotify(
            static_cast<ParamID>(padParamId(pad, kSecOffsets[j])),
            preset.sound[static_cast<std::size_t>(30 + j)]);
    }

    // Sync global proxy params for the selected pad.
    syncGlobalProxyFromPad(selectedPadIndex_);

    return true;
}

void Controller::triggerSelectedPadMacroReapply()
{
    // T054: re-apply current macros to the selected pad after a per-pad
    // preset load. Use beginEdit/performEdit/endEdit on each macro param so
    // the processor sees a parameter-change event and invokes its MacroMapper
    // for the selected pad. Setting to the same value still propagates the
    // change through processParameterChanges() because VST3 host-mediated
    // changes are not value-deduplicated.
    const int pad = selectedPadIndex_;
    const int macroOffsets[] = {
        kPadMacroTightness, kPadMacroBrightness, kPadMacroBodySize,
        kPadMacroPunch,     kPadMacroComplexity,
    };
    for (const auto& offset : macroOffsets)
    {
        const auto pid = static_cast<ParamID>(padParamId(pad, offset));
        const ParamValue current = getParamNormalized(pid);
        beginEdit(pid);
        performEdit(pid, current);
        endEdit(pid);
    }
}

IPlugView* PLUGIN_API Controller::createView(const char* name)
{
    // Phase 6 (T028, FR-001): return a VST3Editor backed by editor.uidesc.
    if (name && std::strcmp(name, Steinberg::Vst::ViewType::kEditor) == 0)
    {
        return new VSTGUI::VST3Editor(this, "EditorDefault", "editor.uidesc");
    }
    return nullptr;
}

// ==============================================================================
// IVST3EditorDelegate implementations (T028)
// ==============================================================================

VSTGUI::CView* Controller::createCustomView(
    VSTGUI::UTF8StringPtr name,
    const VSTGUI::UIAttributes& attributes,
    const VSTGUI::IUIDescription* /*description*/,
    VSTGUI::VST3Editor* /*editor*/)
{
    if (!name)
        return nullptr;

    VSTGUI::CPoint origin(0, 0);
    VSTGUI::CPoint size(100, 100);
    attributes.getPointAttribute("origin", origin);
    attributes.getPointAttribute("size", size);
    const VSTGUI::CRect viewRect(
        origin.x, origin.y,
        origin.x + size.x, origin.y + size.y);

    if (std::strcmp(name, "PadGridView") == 0)
    {
        // Phase 6 (T042): construct the real PadGridView. The view's
        // PadGlowPublisher pointer is nullptr here because separate-component
        // mode prevents the Controller from reaching into the Processor --
        // future phases can inject a DataExchange-backed glow mirror if
        // per-cell intensity ends up required at 30 Hz in separate-component
        // hosts. Tests construct the view directly with a real publisher.
        auto* view = new UI::PadGridView(
            viewRect,
            /*glowPublisher*/ nullptr,
            /*metaProvider*/ [](int) -> const PadConfig* { return nullptr; });

        // FR-012: clicking a pad drives kSelectedPadId through the standard
        // beginEdit/performEdit/endEdit sequence.
        view->setSelectCallback([this](int padIndex) {
            const auto normalised =
                static_cast<Steinberg::Vst::ParamValue>(padIndex) /
                static_cast<Steinberg::Vst::ParamValue>(kNumPads - 1);
            beginEdit(kSelectedPadId);
            performEdit(kSelectedPadId, normalised);
            setParamNormalized(kSelectedPadId, normalised);
            endEdit(kSelectedPadId);
        });

        // Audition: click or shift/right-click sends "AuditionPad" IMessage
        // to the processor so the drum sound plays on every pad interaction.
        // Velocity arrives normalized [0,1] from PadGridView; the processor
        // side stores a 7-bit MIDI value, so scale back to 0-127.
        view->setAuditionCallback([this](int padIndex, float velocityNormalized) {
            if (padIndex < 0 || padIndex >= kNumPads) return;
            const int midi = 36 + padIndex;
            int velocity7 = static_cast<int>(
                std::clamp(velocityNormalized, 0.0f, 1.0f) * 127.0f + 0.5f);
            if (velocity7 <= 0) velocity7 = 100; // guard against zero -> noteOff
            auto* msg = allocateMessage();
            if (msg == nullptr) return;
            Steinberg::IPtr<Steinberg::Vst::IMessage> owned(msg, false);
            owned->setMessageID("AuditionPad");
            auto* attrs = owned->getAttributes();
            if (attrs == nullptr) return;
            attrs->setInt("midi",     static_cast<Steinberg::int64>(midi));
            attrs->setInt("velocity", static_cast<Steinberg::int64>(velocity7));
            sendMessage(owned);
        });

        padGridView_ = view;
        return view;
    }
    if (std::strcmp(name, "CouplingMatrixView") == 0)
    {
        // T068 (Spec 141): construct the CouplingMatrixView wired to the
        // controller-side uiCouplingMatrix_ mirror. The authoritative matrix
        // lives in the processor (audio thread) and cannot be shared across
        // the VST3 component boundary; instead we keep a local mirror that is
        // synced via "CouplingMatrixSnapshot" IMessages and push user edits
        // back to the processor via "CouplingMatrixEdit" IMessages
        // (see sendCouplingMatrixEdit()).
        //
        // The MatrixActivityPublisher pointer remains nullptr: the view
        // tolerates it (pollActivity early-returns on null); 30 Hz activity
        // highlighting is deferred to a later phase that can piggyback on
        // DataExchange. Override/solo/heat-map editing, Tier 2 persistence,
        // and Reset are fully functional with only the matrix mirror wired.
        auto* view = new UI::CouplingMatrixView(
            viewRect,
            /*matrix*/ &uiCouplingMatrix_,
            /*activityPublisher*/ nullptr);
        view->setEditCallback(
            [this](int op, int src, int dst, float value) noexcept {
                sendCouplingMatrixEdit(op, src, dst, value);
            });
        couplingMatrixView_ = view;

        // Request a fresh snapshot from the processor so the newly-opened
        // view reflects the current authoritative override map even if
        // setComponentState was called before the editor was open.
        requestCouplingMatrixSnapshot();
        return view;
    }
    // Phase 9 (T080, Spec 141 US8): PitchEnvelopeDisplay is now a registered
    // shared VSTGUI class (Krate::Plugins::PitchEnvelopeDisplay); the uidesc
    // factory constructs it directly. Parameter-edit callbacks are wired in
    // verifyView() below.
    if (std::strcmp(name, "MetersView") == 0)
    {
        // T046: kit-column peak meter. The size below is a placeholder; the
        // uidesc view frame overrides it.
        auto* view = new UI::KitMetersView(viewRect);
        kitMetersView_ = view;
        return view;
    }
    if (std::strcmp(name, "PolyphonySlider") == 0)
    {
        // Horizontal integer slider for kMaxPolyphonyId. Tagged so VST3Editor
        // routes valueChanged() through the standard parameter pipeline.
        auto* view = new UI::PolyphonySliderView(viewRect, kMaxPolyphonyId);
        view->setValueNormalized(
            static_cast<float>(getParamNormalized(kMaxPolyphonyId)));
        return view;
    }
    if (std::strcmp(name, "ModeToggleButton") == 0)
    {
        auto* view = new UI::OutlineActionButton(
            viewRect,
            getParamNormalized(kUiModeId) >= 0.5 ? "Extended" : "Acoustic");
        view->setAction([this, view]() {
            const auto cur = getParamNormalized(kUiModeId);
            const auto next = cur >= 0.5 ? 0.0 : 1.0;
            beginEdit(kUiModeId);
            performEdit(kUiModeId, next);
            setParamNormalized(kUiModeId, next);
            endEdit(kUiModeId);
            view->setTitle(next >= 0.5 ? "Extended" : "Acoustic");
        });
        modeToggleButton_ = view;
        return view;
    }
    if (std::strcmp(name, "MatrixSoloButton") == 0)
    {
        return new UI::OutlineActionButton(
            viewRect, "Solo",
            [this]() {
                if (couplingMatrixView_)
                    couplingMatrixView_->clearSolo();
            });
    }
    if (std::strcmp(name, "MatrixResetButton") == 0)
    {
        return new UI::OutlineActionButton(
            viewRect, "Reset",
            [this]() { sendCouplingMatrixEdit(4, 0, 0, 0.0f); });
    }
    // Inline kit-column browser widgets (Spec 141 US4): composite of a
    // current-preset-name label + Prev / Next / Browse buttons. Browse opens
    // the modal PresetBrowserView overlay that is added to the frame in
    // didOpen(); prev/next cycle through scanPresets() directly through the
    // PresetManager. The load providers push the loaded name back into the
    // label so modal loads and prev/next both stay in sync.
    if (std::strcmp(name, "KitBrowserView") == 0)
    {
        auto* view = new UI::InlinePresetBrowserView(
            viewRect,
            kitPresetManager_.get(),
            [this]() {
                if (kitPresetBrowserView_ && !kitPresetBrowserView_->isOpen())
                    kitPresetBrowserView_->open();
            });
        view->setCurrentPresetName(lastKitPresetName_);
        kitInlineBrowser_ = view;
        return view;
    }
    if (std::strcmp(name, "PadBrowserView") == 0)
    {
        auto* view = new UI::InlinePresetBrowserView(
            viewRect,
            padPresetManager_.get(),
            [this]() {
                if (padPresetBrowserView_ && !padPresetBrowserView_->isOpen())
                    padPresetBrowserView_->open();
            });
        view->setCurrentPresetName(lastPadPresetName_);
        padInlineBrowser_ = view;
        return view;
    }
    return nullptr;
}

// ------------------------------------------------------------------------------
// verifyView (T046): discover the CPU text label by its title prefix "CPU".
// Called for every view the editor builds from uidesc.
// ------------------------------------------------------------------------------
VSTGUI::CView* Controller::verifyView(VSTGUI::CView* view,
                                      const VSTGUI::UIAttributes& /*attributes*/,
                                      const VSTGUI::IUIDescription* /*description*/,
                                      VSTGUI::VST3Editor* /*editor*/)
{
    if (auto* label = dynamic_cast<VSTGUI::CTextLabel*>(view))
    {
        VSTGUI::UTF8StringPtr title = label->getText();
        if (title != nullptr && std::strncmp(title, "CPU", 3) == 0)
        {
            cpuLabel_ = label;
        }
        // T060/T062 (Phase 6 / US5): active-voices readout label. Template
        // authors mark the label with a title prefix of "ActiveVoices" so it
        // is discovered here without relying on a control-tag.
        else if (title != nullptr
                 && std::strncmp(title, "ActiveVoices", 12) == 0)
        {
            activeVoicesLabel_ = label;
        }
        // T056: status label for preset load failures. Template authors mark
        // the label with a title prefix of "PresetStatus" so it is discovered
        // here without relying on a control-tag. Initial text is cleared.
        else if (title != nullptr
                 && std::strncmp(title, "PresetStatus", 12) == 0)
        {
            presetStatusLabel_ = label;
            presetStatusLabel_->setText("");
        }
    }
    // Phase 8 (T074 / US7 / FR-066): cache the Output Bus selector control so
    // setParamNormalized() can push a warning tooltip when the selected aux
    // bus is not host-activated. The Acoustic and Extended templates both
    // contain this control with control-tag == kOutputBusId; verifyView()
    // fires for each one as the editor builds the view tree. The most
    // recently-built template's control wins -- correct because
    // UIViewSwitchContainer only shows one at a time.
    if (auto* ctrl = dynamic_cast<VSTGUI::CControl*>(view))
    {
        if (ctrl->getTag() == static_cast<int32>(kOutputBusId))
        {
            outputBusSelView_ = ctrl;
            updateOutputBusTooltip();
        }
    }

    // Phase 9 (T080 / US8): PitchEnvelopeDisplay is a registered shared view
    // class. When the uidesc factory hands us back an instance, attach the
    // parameter callbacks that bridge to beginEdit / performEdit / endEdit on
    // the tone-shaper pitch-envelope param IDs. Both the Acoustic and Extended
    // templates contain one; the most-recently-built template's view wins
    // (correct because UIViewSwitchContainer only shows one at a time).
    if (auto* pev = dynamic_cast<Krate::Plugins::PitchEnvelopeDisplay*>(view))
    {
        pev->setParameterCallback(
            [this](uint32_t paramId, float value) {
                const auto tag = static_cast<Steinberg::Vst::ParamID>(paramId);
                const auto v   = static_cast<Steinberg::Vst::ParamValue>(value);
                performEdit(tag, v);
                setParamNormalized(tag, v);
            });
        pev->setBeginEditCallback(
            [this](uint32_t paramId) {
                beginEdit(static_cast<Steinberg::Vst::ParamID>(paramId));
            });
        pev->setEndEditCallback(
            [this](uint32_t paramId) {
                endEdit(static_cast<Steinberg::Vst::ParamID>(paramId));
            });
        pitchEnvelopeDisplay_ = pev;
    }
    return view;
}

// ------------------------------------------------------------------------------
// Phase 8 (T074 / US7 / FR-066): push a warning tooltip onto the Output Bus
// selector when the currently-selected aux bus is inactive. FR-066 requires
// the message to read "Host must activate Aux {N} bus". Clears the tooltip
// when the bus is active or when Main (bus 0) is selected. Tolerant of a
// missing view -- safe to call before or after the editor opens.
// ------------------------------------------------------------------------------
void Controller::updateOutputBusTooltip() noexcept
{
    if (outputBusSelView_ == nullptr)
        return;

    // Resolve the currently-selected bus index from the global proxy value.
    // The parameter is a 16-entry StringListParameter so its normalised value
    // maps to [0, kMaxOutputBuses - 1] via round-to-nearest.
    const auto norm = getParamNormalized(static_cast<ParamID>(kOutputBusId));
    const int busIndex = std::clamp(
        static_cast<int>(std::lround(norm * (kMaxOutputBuses - 1))),
        0, kMaxOutputBuses - 1);

    if (busIndex >= 1 && !isBusActive(busIndex))
    {
        char buf[64] = {};
        std::snprintf(buf, sizeof(buf),
                      "Host must activate Aux %d bus", busIndex);
        outputBusSelView_->setTooltipText(buf);
    }
    else
    {
        // Clear any stale warning: VSTGUI's CView::setTooltipText with an
        // empty string removes the tooltip.
        outputBusSelView_->setTooltipText("");
    }
}

void Controller::didOpen(VSTGUI::VST3Editor* editor)
{
    // Phase 6 (T028): cache the editor pointer and start a 30 Hz poll timer
    // for PadGlowPublisher / MatrixActivityPublisher snapshots. The actual
    // view invalidation lives in later phases; the body here is intentionally
    // minimal but the timer lifecycle is fully correct today so we do not
    // leak a timer between editor instances.
    activeEditor_ = editor;

    // Phase 6: instantiate the editor sub-controller that listens to
    // kUiModeId and drives the Acoustic/Extended UIViewSwitchContainer.
    // Without this the mode toggle only flips its own label.
    editorSubController_ = Steinberg::owned(
        new Membrum::UI::MembrumEditorController(editor, this));

    pollTimer_ = VSTGUI::owned(new VSTGUI::CVSTGUITimer(
        [this](VSTGUI::CVSTGUITimer* /*timer*/) {
            // T046: read the last cached MetersBlock and push its values
            // into the kit-column meter / CPU views. PadGridView drives its
            // own 30 Hz poll against PadGlowPublisher.
            if (activeEditor_ == nullptr)
                return;
            updateMeterViews(cachedMeters_);
        },
        33 /* ~30 Hz */,
        true /* start immediately */));

    // Phase 6 (T053..T054): create the two PresetBrowserView instances (kit +
    // per-pad scope) and the two SavePresetDialogView overlays, parented to
    // the editor's frame. VSTGUI takes ownership; willClose() zeros the raw
    // pointers before the frame tears the views down.
    auto* frame = editor->getFrame();
    if (frame == nullptr)
        return;

    const auto frameSize = frame->getViewSize();

    if (kitPresetManager_ && kitPresetBrowserView_ == nullptr)
    {
        // Shared PresetBrowserView convention: tabLabels_[0] = "All",
        // remaining entries must match PresetManager::subcategoryNames (see
        // plugins/shared/src/ui/preset_browser_view.cpp parsing + filtering).
        kitPresetBrowserView_ = new Krate::Plugins::PresetBrowserView(
            frameSize, kitPresetManager_.get(),
            std::vector<std::string>{
                "All", "Acoustic", "Electronic",
                "Percussive", "Unnatural"});
        frame->addView(kitPresetBrowserView_);
    }
    if (kitPresetManager_ && kitSaveDialogView_ == nullptr)
    {
        kitSaveDialogView_ = new Krate::Plugins::SavePresetDialogView(
            frameSize, kitPresetManager_.get(),
            kitPresetConfig().subcategoryNames);
        frame->addView(kitSaveDialogView_);
    }

    if (padPresetManager_ && padPresetBrowserView_ == nullptr)
    {
        padPresetBrowserView_ = new Krate::Plugins::PresetBrowserView(
            frameSize, padPresetManager_.get(),
            std::vector<std::string>{
                "All", "Kick", "Snare", "Tom",
                "Hat", "Cymbal", "Perc", "Tonal", "FX"});
        frame->addView(padPresetBrowserView_);
    }
    if (padPresetManager_ && padSaveDialogView_ == nullptr)
    {
        padSaveDialogView_ = new Krate::Plugins::SavePresetDialogView(
            frameSize, padPresetManager_.get(),
            padPresetConfig().subcategoryNames);
        frame->addView(padSaveDialogView_);
    }
}

void Controller::willClose(VSTGUI::VST3Editor* /*editor*/)
{
    // Phase 6 (T028, SC-014): cancel the poll timer and zero the raw view
    // pointer BEFORE the editor tears down the view tree, so no background
    // tick can dereference a dead view.
    if (pollTimer_)
    {
        pollTimer_->stop();
        pollTimer_ = nullptr;
    }
    // Release the sub-controller before zeroing activeEditor_: its destructor
    // deregisters IDependent subscriptions and cancels its deferred
    // exchangeView timer, which references editor_.
    editorSubController_ = nullptr;

    activeEditor_      = nullptr;
    padGridView_       = nullptr;
    kitMetersView_     = nullptr;
    couplingMatrixView_ = nullptr;
    pitchEnvelopeDisplay_ = nullptr;
    cpuLabel_          = nullptr;
    activeVoicesLabel_ = nullptr;
    presetStatusLabel_ = nullptr;
    outputBusSelView_  = nullptr;

    // T053..T054: VSTGUI owns the views; just drop our raw pointers so the
    // 30 Hz poll timer (already cancelled above) and any future code can not
    // dereference dead memory.
    kitPresetBrowserView_ = nullptr;
    padPresetBrowserView_ = nullptr;
    kitSaveDialogView_    = nullptr;
    padSaveDialogView_    = nullptr;
    kitInlineBrowser_     = nullptr;
    padInlineBrowser_     = nullptr;
}

// ------------------------------------------------------------------------------
// updateMeterViews (T046): push MetersBlock values to cached views.
// Tolerant of missing views -- safe when editor is not open.
// ------------------------------------------------------------------------------
void Controller::updateMeterViews(const MetersBlock& meters) noexcept
{
    if (kitMetersView_ != nullptr)
    {
        kitMetersView_->setPeaks(meters.peakL, meters.peakR);
    }
    if (cpuLabel_ != nullptr)
    {
        // cpuPermille is 0..1000 (per-mille). Display as whole percent.
        const auto percent =
            static_cast<unsigned int>((meters.cpuPermille + 5) / 10);
        char buf[32] = {};
        std::snprintf(buf, sizeof(buf), "CPU: %u%%", percent);
        cpuLabel_->setText(buf);
    }

    // T060/T062 (Phase 6 / US5): push MetersBlock.activeVoices into the
    // Kit Column readout. The label title prefix marker ("ActiveVoices") is
    // discovered in verifyView() so we never collide with the CPU label.
    if (activeVoicesLabel_ != nullptr)
    {
        char buf[32] = {};
        std::snprintf(buf, sizeof(buf), "ActiveVoices: %u",
                      static_cast<unsigned int>(meters.activeVoices));
        activeVoicesLabel_->setText(buf);
    }

    // T056: surface preset load failures on the status label. A fresh failure
    // arms a ~3 second countdown (90 ticks at 30 Hz); when it elapses both
    // the label and the latched flags are cleared. Tolerant of a missing
    // label: the flags still clear on timeout so state does not accumulate.
    constexpr int kPresetStatusDurationTicks = 90; // ~3 s at 30 Hz
    if (kitPresetLoadFailed_ || padPresetLoadFailed_)
    {
        if (presetStatusClearTicks_ == 0)
        {
            const char* text = kitPresetLoadFailed_
                                   ? "Kit preset load failed"
                                   : "Pad preset load failed";
            if (presetStatusLabel_ != nullptr)
                presetStatusLabel_->setText(text);
            presetStatusClearTicks_ = kPresetStatusDurationTicks;
        }
    }
    if (presetStatusClearTicks_ > 0)
    {
        if (--presetStatusClearTicks_ == 0)
        {
            if (presetStatusLabel_ != nullptr)
                presetStatusLabel_->setText("");
            kitPresetLoadFailed_ = false;
            padPresetLoadFailed_ = false;
        }
    }
}

// ==============================================================================
// IDataExchangeReceiver -- T046 (following Innexus controller.cpp:1703-1740)
// ==============================================================================
void PLUGIN_API Controller::queueOpened(
    Steinberg::Vst::DataExchangeUserContextID /*userContextID*/,
    Steinberg::uint32 /*blockSize*/,
    Steinberg::TBool& dispatchOnBackgroundThread)
{
    // Dispatch on the UI thread so we can update cachedMeters_ without a
    // mutex (the poll timer also runs on the UI thread).
    dispatchOnBackgroundThread = static_cast<Steinberg::TBool>(false);
}

void PLUGIN_API Controller::queueClosed(
    Steinberg::Vst::DataExchangeUserContextID /*userContextID*/)
{
    // Nothing to clean up -- cachedMeters_ is a POD value.
}

void PLUGIN_API Controller::onDataExchangeBlocksReceived(
    Steinberg::Vst::DataExchangeUserContextID /*userContextID*/,
    Steinberg::uint32 numBlocks,
    Steinberg::Vst::DataExchangeBlock* blocks,
    Steinberg::TBool /*onBackgroundThread*/)
{
    // Use the most recent block (Innexus pattern); older blocks are stale.
    for (Steinberg::uint32 i = 0; i < numBlocks; ++i)
    {
        if (blocks[i].data != nullptr
            && blocks[i].size >= sizeof(MetersBlock))
        {
            std::memcpy(&cachedMeters_, blocks[i].data, sizeof(MetersBlock));
        }
    }
}

// ==============================================================================
// T068 (Spec 141, retry): IMessage bridge for the 32x32 coupling matrix.
// ==============================================================================
//
// The processor owns the authoritative CouplingMatrix (audio thread). The
// controller maintains uiCouplingMatrix_ as a mirror used by the UI view.
//
// Wire format:
//   "CouplingMatrixEdit"   controller -> processor
//       attrs: "op"  (int64)  0=setOverride, 1=clearOverride,
//                             2=setSolo,     3=clearSolo
//              "src" (int64)  source pad index [0, 31]
//              "dst" (int64)  destination pad index [0, 31]
//              "val" (int64)  float32 bits re-interpreted (only for op=0)
//   "CouplingMatrixSnapshotRequest"   controller -> processor
//       attrs: (none)
//   "CouplingMatrixSnapshot"          processor -> controller
//       attrs: "overrides" (binary) -- N records of
//              {uint8 src; uint8 dst; float32 coeff}. N = blobSize / 6.
// ==============================================================================

namespace {

constexpr char kCouplingEditMsgId[]     = "CouplingMatrixEdit";
constexpr char kCouplingReqMsgId[]      = "CouplingMatrixSnapshotRequest";
constexpr char kCouplingSnapshotMsgId[] = "CouplingMatrixSnapshot";

} // anonymous namespace

void Controller::sendCouplingMatrixEdit(int op, int src, int dst,
                                        float value) noexcept
{
    auto* msg = allocateMessage();
    if (msg == nullptr) return;
    Steinberg::IPtr<Steinberg::Vst::IMessage> owned(msg, /*addRef*/ false);

    owned->setMessageID(kCouplingEditMsgId);
    auto* attrs = owned->getAttributes();
    if (attrs == nullptr) return;

    attrs->setInt("op",  static_cast<Steinberg::int64>(op));
    attrs->setInt("src", static_cast<Steinberg::int64>(src));
    attrs->setInt("dst", static_cast<Steinberg::int64>(dst));

    Steinberg::int64 bits = 0;
    std::memcpy(&bits, &value, sizeof(float));
    attrs->setInt("val", bits);

    sendMessage(owned);
}

void Controller::requestCouplingMatrixSnapshot() noexcept
{
    auto* msg = allocateMessage();
    if (msg == nullptr) return;
    Steinberg::IPtr<Steinberg::Vst::IMessage> owned(msg, /*addRef*/ false);

    owned->setMessageID(kCouplingReqMsgId);
    sendMessage(owned);
}

tresult PLUGIN_API Controller::notify(Steinberg::Vst::IMessage* message)
{
    if (message == nullptr)
        return EditControllerEx1::notify(message);

    // DataExchange IMessage fallback: hosts without the DataExchange API send
    // blocks as IMessage payloads. The SDK helper decodes them and invokes
    // onDataExchangeBlocksReceived(). Returns true iff the message was a
    // DataExchange fallback payload.
    if (dataExchangeReceiver_.onMessage(message))
        return kResultOk;

    const auto* id = message->getMessageID();
    if (id != nullptr && std::strcmp(id, kCouplingSnapshotMsgId) == 0)
    {
        auto* attrs = message->getAttributes();
        if (attrs == nullptr) return kResultOk;

        // Reset the mirror, then re-apply every override from the payload.
        uiCouplingMatrix_.clearAll();

        const void*      data = nullptr;
        Steinberg::uint32 size = 0;
        if (attrs->getBinary("overrides", data, size) == kResultOk
            && data != nullptr)
        {
            constexpr Steinberg::uint32 kRecordSize =
                sizeof(std::uint8_t) + sizeof(std::uint8_t) + sizeof(float);
            const Steinberg::uint32 count = size / kRecordSize;
            const auto* bytes = static_cast<const unsigned char*>(data);
            for (Steinberg::uint32 i = 0; i < count; ++i)
            {
                const Steinberg::uint32 off = i * kRecordSize;
                const std::uint8_t s = bytes[off];
                const std::uint8_t d = bytes[off + 1];
                float coeff = 0.0f;
                std::memcpy(&coeff, bytes + off + 2, sizeof(float));
                if (s < kNumPads && d < kNumPads)
                {
                    uiCouplingMatrix_.setOverride(
                        static_cast<int>(s),
                        static_cast<int>(d),
                        std::clamp(coeff, 0.0f, CouplingMatrix::kMaxCoefficient));
                }
            }
        }

        if (couplingMatrixView_ != nullptr)
            couplingMatrixView_->invalid();
        return kResultOk;
    }

    return EditControllerEx1::notify(message);
}

} // namespace Membrum
