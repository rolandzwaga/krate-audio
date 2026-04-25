// ==============================================================================
// Membrum Controller -- Phase 4 per-pad parameter registration + proxy logic
// ==============================================================================

#include "controller.h"
#include "plugin_ids.h"
#include "dsp/pad_config.h"
#include "dsp/exciter_type.h"
#include "dsp/body_model_type.h"
#include "state/state_codec.h"
#include "controller_state_codec.h"
#include "ui/pad_grid_view.h"
#include "ui/kit_meters_view.h"
#include "ui/polyphony_slider.h"
#include "ui/coupling_matrix_view.h"
#include "ui/pitch_envelope_display.h"  // shared PitchEnvelopeDisplay (Krate::Plugins)
#include "ui/xy_morph_pad.h"             // shared XYMorphPad (Krate::Plugins)
#include "ui/adsr_display.h"             // shared ADSRDisplay   (Krate::Plugins)
#include "ui/adsr_expanded_overlay.h"    // Membrum::UI::ADSRExpandedOverlayView
#include "ui/outline_button.h"           // Membrum::UI::IconExpandActionButton
#include "preset/membrum_preset_config.h"

#include "preset/preset_manager.h"
#include "preset/preset_info.h"
#include "ui/preset_browser_view.h"
#include "ui/save_preset_dialog_view.h"

#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
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
    // Phase 8F: per-pad enable toggle (offset 59).
    {.globalId = kPadEnabledId,                 .padOffset = kPadEnabled },
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
    // Phase 8F (per-pad enable toggle).
    {.offset = kPadEnabled,              .name = "Pad Enabled",         .isDiscrete = true,  .stepCount = 1, .defaultValue = 1.0 },
};

constexpr int kPadParamSpecCount =
    static_cast<int>(sizeof(kPadParamSpecs) / sizeof(kPadParamSpecs[0]));

static_assert(kPadParamSpecCount == kPadActiveParamCountV8F,
              "Pad param specs must match active param count (60 after Phase 8F)");

// Helper: convert narrow string to TChar buffer
void narrowToTChar(const char* src, TChar* dst, int maxLen)
{
    int i = 0;
    for (; i < maxLen - 1 && src[i] != '\0'; ++i)
        dst[i] = static_cast<TChar>(src[i]);
    dst[i] = 0;
}

// PadSnapshot <-> Controller-param bridging and KitSnapshot encode/decode
// live in `controller_state_codec.{h,cpp}` (Membrum::ControllerState namespace)
// so all three kit-level codec entry points share a single source of truth
// and adding a new per-pad / kit-level field is a one-place edit.

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
        {.id = kToneShaperFilterCutoffId,    .name = "Tone Shaper Filter Cutoff",   .defaultValue = 1.0,      .unit = "Hz" },
        {.id = kToneShaperFilterResonanceId, .name = "Tone Shaper Filter Resonance",.defaultValue = 0.0,      .unit = nullptr },
        {.id = kToneShaperFilterEnvAmountId, .name = "Tone Shaper Filter Env Amt",  .defaultValue = 0.5,      .unit = nullptr },
        {.id = kToneShaperDriveAmountId,     .name = "Tone Shaper Drive",           .defaultValue = 0.0,      .unit = nullptr },
        {.id = kToneShaperFoldAmountId,      .name = "Tone Shaper Fold",            .defaultValue = 0.0,      .unit = nullptr },
        {.id = kToneShaperPitchEnvStartId,   .name = "Tone Shaper PitchEnv Start",  .defaultValue = 0.070721, .unit = "Hz" },
        {.id = kToneShaperPitchEnvEndId,     .name = "Tone Shaper PitchEnv End",    .defaultValue = 0.0,      .unit = "Hz" },
        {.id = kToneShaperPitchEnvTimeId,    .name = "Tone Shaper PitchEnv Time",   .defaultValue = 0.0,      .unit = "ms" },
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

    // Morph Curve selector (binary: Linear / Exponential). Registered as a
    // StringListParameter so the Advanced template's COptionMenu can populate
    // its dropdown entries. The DSP side treats the normalised value as a
    // threshold at 0.5 (see processor.cpp / material_morph.h setCurve()).
    {
        auto* curveList = new StringListParameter(
            STR16("Morph Curve"), kMorphCurveId, nullptr,
            ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
        curveList->appendString(STR16("Linear"));
        curveList->appendString(STR16("Exponential"));
        parameters.addParameter(curveList);
    }

    // Tone Shaper Filter Type selector (LP / HP / BP). Same story as Morph
    // Curve: the UI binds a COptionMenu to this tag, so it must be a
    // StringListParameter. The processor decodes the normalised value via
    // std::clamp(static_cast<int>(v * 3.0f), 0, 2), which matches the
    // StringListParameter's index -> normalised mapping (0->0.0, 1->0.5, 2->1.0).
    {
        auto* filterTypeList = new StringListParameter(
            STR16("Tone Shaper Filter Type"), kToneShaperFilterTypeId, nullptr,
            ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
        filterTypeList->appendString(STR16("Lowpass"));
        filterTypeList->appendString(STR16("Highpass"));
        filterTypeList->appendString(STR16("Bandpass"));
        parameters.addParameter(filterTypeList);
    }

    // Tone Shaper Pitch Envelope Curve selector (Exponential / Linear).
    // Entry order must match the ToneShaperCurve enum (Exponential=0,
    // Linear=1) so the StringListParameter's index -> normalised mapping
    // (0->0.0, 1->1.0) lines up with the processor's
    // std::clamp(static_cast<int>(v * 2.0f), 0, 1) decode.
    {
        auto* pitchCurveList = new StringListParameter(
            STR16("Tone Shaper PitchEnv Curve"), kToneShaperPitchEnvCurveId, nullptr,
            ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
        pitchCurveList->appendString(STR16("Exponential"));
        pitchCurveList->appendString(STR16("Linear"));
        parameters.addParameter(pitchCurveList);
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
        uiModeList->appendString(STR16("Simple"));
        uiModeList->appendString(STR16("Advanced"));
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

    // ---- Phase 8F global proxy: per-pad enable toggle ----
    // Discrete float-as-bool. Default 1.0 = enabled.
    parameters.addParameter(
        new RangeParameter(STR16("Pad Enabled"), kPadEnabledId, nullptr,
                           0.0, 1.0, 1.0, /*stepCount=*/1, ParameterInfo::kCanAutomate));

    // ---- Phase 4/7/8A..8F: 1920 per-pad parameters (32 pads x 60 offsets) ----
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

    // Power toggle for the Material Morph section: whenever kMorphEnabledId
    // changes (user toggle, automation, preset load, or pad-switch sync via
    // syncGlobalProxyFromPad -> EditControllerEx1::setParamNormalized), cascade
    // the new state across the cached section views.
    if (tag == static_cast<ParamID>(kMorphEnabledId))
        updateMorphControlsEnabled();

    // Tone Shaper filter-envelope display: repaint the curve whenever any of
    // the four A/D/S/R parameters moves, regardless of source (knob edit,
    // automation, preset load, or the direct-setter pad-switch sync path).
    if (tag == static_cast<ParamID>(kToneShaperFilterEnvAttackId)
        || tag == static_cast<ParamID>(kToneShaperFilterEnvDecayId)
        || tag == static_cast<ParamID>(kToneShaperFilterEnvSustainId)
        || tag == static_cast<ParamID>(kToneShaperFilterEnvReleaseId))
    {
        updateFilterEnvDisplay();
    }

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

    // Phase 8F: per-pad enable toggle. Push the new value into the grid
    // view so the cell dim / power glyph state tracks every source of
    // change (user click on the glyph, host automation, preset load).
    if (padOffset == kPadEnabled && padGridView_ != nullptr)
    {
        const int padIdx = padIndexFromParamId(static_cast<int>(tag));
        if (padIdx >= 0)
            padGridView_->setPadEnabled(padIdx, value >= 0.5);
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
    // The sync loop above calls the base class setter directly, which bypasses
    // the derived setParamNormalized() override and its kMorphEnabledId hook.
    // Reflect the freshly-synced state onto the Material Morph views so the
    // dim/enable visuals track pad selection.
    updateMorphControlsEnabled();
    // Same reasoning for the filter-envelope display: direct base-class writes
    // skip the setParamNormalized hook, so refresh the display once the sync
    // loop has settled all four A/D/S/R globals to the new pad's values.
    updateFilterEnvDisplay();
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

    Membrum::State::KitSnapshot kit;
    if (Membrum::State::readKitBlob(state, kit) != kResultOk)
        return kResultFalse;

    // Phase 6 (T027): session-scoped kUiModeId always resets to Acoustic
    // (0.0) on state load. Kit presets may re-override it via a separate
    // preset-load callback (not through IBStream). uiMode lives outside
    // the snapshot helper because each load path has its own semantics
    // (host load forces 0; preset load reads from `kit.hasSession`).
    EditControllerEx1::setParamNormalized(kUiModeId, 0.0);

    // Host-driven path: the processor has already consumed its own state via
    // IComponent::setState, so we just mirror the values into the
    // controller's parameter objects -- no performEdit chain.
    Membrum::ControllerState::ParamSetter setDirect =
        [this](Steinberg::Vst::ParamID id, double v) {
            Steinberg::Vst::EditControllerEx1::setParamNormalized(id, v);
        };
    Membrum::ControllerState::applySnapshot(
        kit, setDirect, {.applySelectedPad = true});

    selectedPadIndex_ = kit.selectedPadIndex;
    pushKitEnabledToGrid(kit);
    syncGlobalProxyFromPad(selectedPadIndex_);

    return kResultOk;
}

// ==============================================================================
// Kit Preset StateProvider / LoadProvider (FR-052, T033)
// ==============================================================================

IBStream* Controller::kitPresetStateProvider()
{
    // Build the kit snapshot via the shared encoder, then layer on the
    // session-scoped uiMode that only kit-presets persist (FR-030 / FR-072).
    auto* stream = new MemoryStream();

    auto kit = Membrum::ControllerState::buildSnapshot(*this, selectedPadIndex_);
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
    Membrum::ControllerState::ParamSetter setAndNotify =
        [this](Steinberg::Vst::ParamID id, double v) {
            const double clamped = std::clamp(v, 0.0, 1.0);
            Steinberg::Vst::EditControllerEx1::setParamNormalized(id, clamped);
            beginEdit(id);
            performEdit(id, clamped);
            endEdit(id);
        };
    Membrum::ControllerState::applySnapshot(
        kit, setAndNotify, {.applySelectedPad = false});

    // Session-scoped uiMode: kit presets restore it when present, but the
    // controller-side write is direct (no performEdit) because the processor
    // never consumes uiMode.
    if (kit.hasSession)
    {
        EditControllerEx1::setParamNormalized(kUiModeId,
            kit.uiMode >= 1 ? 1.0 : 0.0);
    }

    pushKitEnabledToGrid(kit);

    // Kit preset deliberately preserves the user's current selectedPadIndex_
    // (FR-052), so the global proxy sync uses the EXISTING value.
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
    const auto full =
        Membrum::ControllerState::buildPadSnapshotFromParams(*this, pad);

    // Project the per-pad snapshot down to the narrower per-pad preset slice.
    // PadPresetSnapshot::sound is intentionally one slot shorter than
    // PadSnapshot::sound (the Phase 8F enable-toggle slot at index 51 is
    // kit-level only and never persisted in per-pad presets).
    Membrum::State::PadPresetSnapshot preset;
    preset.exciterType = full.exciterType;
    preset.bodyModel   = full.bodyModel;
    std::copy_n(full.sound.begin(), preset.sound.size(), preset.sound.begin());

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

    // Per-pad preset load bypasses the host, so we need begin/perform/end
    // around every write so the processor sees the new values.
    Membrum::ControllerState::ParamSetter setAndNotify =
        [this](Steinberg::Vst::ParamID id, double v) {
            const double clamped = std::clamp(v, 0.0, 1.0);
            Steinberg::Vst::EditControllerEx1::setParamNormalized(id, clamped);
            beginEdit(id);
            performEdit(id, clamped);
            endEdit(id);
        };

    Membrum::ControllerState::applyPadPresetSnapshotToParams(
        selectedPadIndex_, preset, setAndNotify);

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
        // The view's PadGlowPublisher pointer is the controller-side
        // `padGlowMirror_`, not the real processor-side publisher (which lives
        // on the audio thread across the separate-component boundary). The
        // MetersBlock DataExchange stream carries a snapshot of the real
        // publisher each audio block; onDataExchangeBlocksReceived() re-applies
        // those buckets to `padGlowMirror_` so the view's existing 30 Hz
        // snapshot()-based polling path lights up pads in sync with audio.
        auto* view = new UI::PadGridView(
            viewRect,
            /*glowPublisher*/ &padGlowMirror_,
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

        // Phase 8F: power-glyph click toggles the per-pad enable parameter
        // directly (no proxy detour). The toggle goes through the standard
        // beginEdit/performEdit/endEdit gesture so DAW automation lanes
        // record correctly, and the per-pad ID writes the per-pad config
        // even when the affected pad is not the currently selected one.
        view->setEnableToggleCallback([this](int padIndex, bool newState) {
            if (padIndex < 0 || padIndex >= kNumPads) return;
            const auto pid = static_cast<ParamID>(
                padParamId(padIndex, kPadEnabled));
            const ParamValue norm = newState ? 1.0 : 0.0;
            beginEdit(pid);
            performEdit(pid, norm);
            setParamNormalized(pid, norm);
            endEdit(pid);
            // If the toggled pad is the currently selected one, also keep
            // the global proxy in sync so any future UI bound to it sees
            // the same value.
            if (padIndex == selectedPadIndex_)
            {
                EditControllerEx1::setParamNormalized(
                    static_cast<ParamID>(kPadEnabledId), norm);
            }
        });

        // Push the initial enabled state into the view so disabled pads
        // render correctly the moment the editor opens.
        for (int i = 0; i < kNumPads; ++i)
        {
            const auto norm = getParamNormalized(static_cast<ParamID>(
                padParamId(i, kPadEnabled)));
            view->setPadEnabled(i, norm >= 0.5);
        }

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
        // Label shows the target state, not the current one: when the Simple
        // view is active the button reads "Advanced" (where the click will
        // take you), and vice versa.
        auto* view = new UI::OutlineActionButton(
            viewRect,
            getParamNormalized(kUiModeId) >= 0.5 ? "Simple" : "Advanced");
        view->setAction([this, view]() {
            const auto cur = getParamNormalized(kUiModeId);
            const auto next = cur >= 0.5 ? 0.0 : 1.0;
            beginEdit(kUiModeId);
            performEdit(kUiModeId, next);
            setParamNormalized(kUiModeId, next);
            endEdit(kUiModeId);
            view->setTitle(next >= 0.5 ? "Simple" : "Advanced");
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
    // Expand button for the Tone Shaper filter envelope. Clicking opens the
    // modal ADSRExpandedOverlayView created in didOpen(); the overlay can be
    // re-opened any number of times. The action is a no-op until didOpen()
    // has added the overlay to the frame.
    if (std::strcmp(name, "FilterEnvExpandButton") == 0)
    {
        auto* btn = new UI::IconExpandActionButton(viewRect);
        btn->setAction([this]() {
            if (filterEnvOverlay_ != nullptr && !filterEnvOverlay_->isOpen())
                filterEnvOverlay_->open();
        });
        return btn;
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
            ctrl->registerViewListener(this);
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
        pev->registerViewListener(this);
    }

    // MaterialMorph XY pad (Advanced template). The shared XYMorphPad drives
    // its X-axis via the CControl tag (MaterialMorph -> kMorphStartId) and its
    // Y-axis through a direct performEdit() on this edit controller. The
    // secondary-tag="MorphEnd" attribute wires Y to kMorphEndId via the
    // ViewCreator; the controller pointer must be wired here.
    // Track whether any Material Morph view was just cached so we can apply
    // the current MorphEnabled state immediately -- verifyView() fires as the
    // template is (re)built (including lazy rebuilds by UIViewSwitchContainer
    // after didOpen has already run), so didOpen's one-shot state sync is not
    // enough on its own.
    bool morphViewCached = false;

    if (auto* xyPad = dynamic_cast<Krate::Plugins::XYMorphPad*>(view))
    {
        xyPad->setController(this);
        xyPad->setSecondaryParamId(kMorphEndId);

        const auto startNorm = getParamNormalized(kMorphStartId);
        const auto endNorm   = getParamNormalized(kMorphEndId);
        xyPad->setMorphPosition(static_cast<float>(startNorm),
                                static_cast<float>(endNorm));
        xyMorphPad_ = xyPad;
        xyPad->registerViewListener(this);
        morphViewCached = true;
    }

    // Material Morph knob + menu: discover by control-tag so updateMorphControlsEnabled()
    // can cascade the MorphEnabled (power) toggle state across the whole section.
    if (auto* ctrl = dynamic_cast<VSTGUI::CControl*>(view))
    {
        const auto tag = ctrl->getTag();
        if (tag == static_cast<int32>(kMorphDurationMsId))
        {
            morphDurationView_ = ctrl;
            ctrl->registerViewListener(this);
            morphViewCached = true;
        }
        else if (tag == static_cast<int32>(kMorphCurveId))
        {
            morphCurveView_ = ctrl;
            ctrl->registerViewListener(this);
            morphViewCached = true;
        }
    }

    // "Dur" label sits next to the MorphDuration knob; dim it alongside the
    // controls when the power toggle is off. It is the only CTextLabel in the
    // uidesc with this title, so a title match is unambiguous.
    if (auto* label = dynamic_cast<VSTGUI::CTextLabel*>(view))
    {
        VSTGUI::UTF8StringPtr t = label->getText();
        if (t != nullptr && std::strcmp(t, "Dur") == 0)
        {
            morphDurLabel_ = label;
            label->registerViewListener(this);
            morphViewCached = true;
        }
    }

    if (morphViewCached)
        updateMorphControlsEnabled();

    // Tone Shaper Filter ADSR display (Advanced template). Membrum's filter
    // envelope uses asymmetric linear ranges: attack x500 ms, decay/release
    // x2000 ms (see processor.cpp:72-75). The shared ADSRDisplay exposes
    // per-segment max-time setters so its cubic drag->callback encoding lines
    // up with these ranges; we configure them here, wire the param IDs, and
    // forward edits back to the host via performEdit().
    if (auto* adsr = dynamic_cast<Krate::Plugins::ADSRDisplay*>(view))
    {
        adsr->setAttackMaxMs(500.0f);
        adsr->setDecayMaxMs(2000.0f);
        adsr->setReleaseMaxMs(2000.0f);
        adsr->setAdsrBaseParamId(kToneShaperFilterEnvAttackId);
        adsr->setParameterCallback(
            [this](uint32_t paramId, float normalizedValue) {
                const auto tag = static_cast<Steinberg::Vst::ParamID>(paramId);
                const auto v   = static_cast<Steinberg::Vst::ParamValue>(normalizedValue);
                performEdit(tag, v);
                setParamNormalized(tag, v);
            });
        adsr->setBeginEditCallback(
            [this](uint32_t paramId) {
                beginEdit(static_cast<Steinberg::Vst::ParamID>(paramId));
            });
        adsr->setEndEditCallback(
            [this](uint32_t paramId) {
                endEdit(static_cast<Steinberg::Vst::ParamID>(paramId));
            });
        filterEnvDisplay_ = adsr;
        adsr->registerViewListener(this);
        updateFilterEnvDisplay();
    }

    return view;
}

// ------------------------------------------------------------------------------
// IViewListener::viewWillDelete
//
// Fired by VSTGUI just before destroying a view we registered as a listener
// on. UIViewSwitchContainer destroys the outgoing template's view tree on
// every Simple<->Advanced toggle, so without this hook the cached raw
// pointers in the controller go dangling between destruction and the
// rebuild's verifyView() reassignment -- and quick toggling crashes when a
// parameter write hits one of those stale pointers.
// ------------------------------------------------------------------------------
void Controller::viewWillDelete(VSTGUI::CView* view)
{
    if (view == nullptr)
        return;

    if (view == outputBusSelView_)         outputBusSelView_         = nullptr;
    if (view == pitchEnvelopeDisplay_)     pitchEnvelopeDisplay_     = nullptr;
    if (view == xyMorphPad_)               xyMorphPad_               = nullptr;
    if (view == morphDurationView_)        morphDurationView_        = nullptr;
    if (view == morphCurveView_)           morphCurveView_           = nullptr;
    if (view == morphDurLabel_)            morphDurLabel_            = nullptr;
    if (view == filterEnvDisplay_)         filterEnvDisplay_         = nullptr;

    view->unregisterViewListener(this);
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

// ------------------------------------------------------------------------------
// Reflect the MorphEnabled (power toggle) state onto the Material Morph
// section's controls: dim to 0.35 alpha and block mouse input when off,
// restore to 1.0 and re-enable when on. Tolerant of any null view pointer,
// so it is safe to call before verifyView caches them or after willClose
// zeros them.
// ------------------------------------------------------------------------------
void Controller::updateMorphControlsEnabled() noexcept
{
    const bool enabled =
        getParamNormalized(static_cast<ParamID>(kMorphEnabledId)) >= 0.5;
    const float alpha = enabled ? 1.0f : 0.35f;

    // Guard against redundant setAlphaValue()/invalid() churn: verifyView()
    // invokes this helper for every Material Morph view as they are built, so
    // most calls end up asking for the alpha a previously-cached view already
    // has. Comparing to getAlphaValue() short-circuits those no-ops.
    auto apply = [enabled, alpha](VSTGUI::CView* v) {
        if (v == nullptr) return;
        if (v->getAlphaValue() == alpha
            && v->getMouseEnabled() == enabled)
            return;
        v->setAlphaValue(alpha);
        v->setMouseEnabled(enabled);
        v->invalid();
    };

    apply(xyMorphPad_);
    apply(morphDurationView_);
    apply(morphCurveView_);
    apply(morphDurLabel_);
}

// ------------------------------------------------------------------------------
// Push the four Tone Shaper filter-envelope normalized values into the cached
// ADSRDisplay, converting attack/decay/release to their true DSP millisecond
// ranges (x500 for attack, x2000 for decay and release -- see
// processor.cpp:72-75). Sustain is a pure [0,1] level. Tolerant of a null
// display pointer, so this is safe to call before verifyView populates the
// pointer or after willClose zeros it.
// ------------------------------------------------------------------------------
void Controller::updateFilterEnvDisplay() noexcept
{
    if (filterEnvDisplay_ == nullptr && filterEnvOverlayDisplay_ == nullptr)
        return;

    const auto attackNorm  = getParamNormalized(
        static_cast<ParamID>(kToneShaperFilterEnvAttackId));
    const auto decayNorm   = getParamNormalized(
        static_cast<ParamID>(kToneShaperFilterEnvDecayId));
    const auto sustainNorm = getParamNormalized(
        static_cast<ParamID>(kToneShaperFilterEnvSustainId));
    const auto releaseNorm = getParamNormalized(
        static_cast<ParamID>(kToneShaperFilterEnvReleaseId));

    const float attackMs  = static_cast<float>(attackNorm) * 500.0f;
    const float decayMs   = static_cast<float>(decayNorm)  * 2000.0f;
    const float sustain   = static_cast<float>(sustainNorm);
    const float releaseMs = static_cast<float>(releaseNorm) * 2000.0f;

    auto pushTo = [&](Krate::Plugins::ADSRDisplay* display) {
        if (display == nullptr) return;
        display->setAttackMs(attackMs);
        display->setDecayMs(decayMs);
        display->setSustainLevel(sustain);
        display->setReleaseMs(releaseMs);
    };
    pushTo(filterEnvDisplay_);
    pushTo(filterEnvOverlayDisplay_);
}

void Controller::pushKitEnabledToGrid(
    const Membrum::State::KitSnapshot& kit) noexcept
{
    if (padGridView_ == nullptr)
        return;
    for (int pad = 0; pad < kNumPads; ++pad)
    {
        // Slot 51 of PadSnapshot::sound is the per-pad enable flag (Phase 8F).
        padGridView_->setPadEnabled(
            pad, kit.pads[static_cast<std::size_t>(pad)].sound[51] >= 0.5);
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

    // Expanded filter-envelope overlay. Opened by the FilterEnvExpandButton
    // (see createCustomView). Wire its internal ADSRDisplay with the same
    // per-segment max times, base param ID, and edit callbacks as the inline
    // display so edits in either view reach the same parameters and the
    // shape stays synchronised.
    if (filterEnvOverlay_ == nullptr)
    {
        filterEnvOverlay_ = new Membrum::UI::ADSRExpandedOverlayView(frameSize);
        filterEnvOverlayDisplay_ = filterEnvOverlay_->getDisplay();
        if (filterEnvOverlayDisplay_ != nullptr)
        {
            filterEnvOverlayDisplay_->setAttackMaxMs(500.0f);
            filterEnvOverlayDisplay_->setDecayMaxMs(2000.0f);
            filterEnvOverlayDisplay_->setReleaseMaxMs(2000.0f);
            filterEnvOverlayDisplay_->setAdsrBaseParamId(kToneShaperFilterEnvAttackId);
            filterEnvOverlayDisplay_->setParameterCallback(
                [this](uint32_t paramId, float normalizedValue) {
                    const auto tag = static_cast<Steinberg::Vst::ParamID>(paramId);
                    const auto v   = static_cast<Steinberg::Vst::ParamValue>(normalizedValue);
                    performEdit(tag, v);
                    setParamNormalized(tag, v);
                });
            filterEnvOverlayDisplay_->setBeginEditCallback(
                [this](uint32_t paramId) {
                    beginEdit(static_cast<Steinberg::Vst::ParamID>(paramId));
                });
            filterEnvOverlayDisplay_->setEndEditCallback(
                [this](uint32_t paramId) {
                    endEdit(static_cast<Steinberg::Vst::ParamID>(paramId));
                });
        }
        frame->addView(filterEnvOverlay_);
    }

    // Reflect the current power-toggle state onto the Material Morph controls
    // now that verifyView() has populated the cached view pointers for the
    // freshly-built template.
    updateMorphControlsEnabled();
    // Push current filter-envelope values into both displays -- the overlay
    // was just built and needs its initial shape / labels.
    updateFilterEnvDisplay();
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
    xyMorphPad_              = nullptr;
    morphDurationView_       = nullptr;
    morphCurveView_          = nullptr;
    morphDurLabel_           = nullptr;
    filterEnvDisplay_        = nullptr;
    filterEnvOverlay_        = nullptr;
    filterEnvOverlayDisplay_ = nullptr;

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

    // Mirror the processor's pad-glow buckets into our controller-side
    // publisher so PadGridView's polling path picks them up. The publish API
    // wants a float amplitude; bucket/31 round-trips back to the same bucket
    // on the next snapshot().
    for (int pad = 0; pad < kNumPads; ++pad)
    {
        const std::uint8_t bucket = cachedMeters_.padGlowBuckets[pad];
        const float amp = (bucket == 0)
                        ? 0.0f
                        : static_cast<float>(bucket) / 31.0f;
        padGlowMirror_.publish(pad, amp);
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

// ==============================================================================
// getParamStringByValue -- human-readable ArcKnob value popups (Acoustic view)
// ==============================================================================
// Formats the normalised [0, 1] parameter value for the ten ArcKnobs on the
// "Acoustic" template: five macro knobs (bipolar %), Material (Wood <-> Metal),
// Size (Hz, log-mapped), Decay (%), Strike Position (Center <-> Edge), Level
// (dB). Per-pad macro / physics params route through here for any pad slot
// because we dispatch on the offset within the 64-param stride. Anything else
// falls back to the SDK default formatter.
//
// The numeric mappings MUST stay in sync with:
//   drum_voice.h::updateModalParameters (Size -> 500 * 0.1^norm Hz)
//   macro_mapper.cpp                    (macros are centred at 0.5)
// ==============================================================================
namespace {

inline void writeString128(Steinberg::Vst::String128 dst, const char* src)
{
    Steinberg::UString(dst, 128).fromAscii(src);
}

void formatPercent(Steinberg::Vst::ParamValue norm, Steinberg::Vst::String128 out)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.0f%%", norm * 100.0);
    writeString128(out, text);
}

void formatBipolarPercent(Steinberg::Vst::ParamValue norm, Steinberg::Vst::String128 out)
{
    const double pct = (norm * 2.0 - 1.0) * 100.0;
    char text[32];
    if (pct > 0.5)
        std::snprintf(text, sizeof(text), "+%.0f%%", pct);
    else if (pct < -0.5)
        std::snprintf(text, sizeof(text), "%.0f%%", pct);
    else
        std::snprintf(text, sizeof(text), "0%%");
    writeString128(out, text);
}

// min..max linear multiplier ("0.50x" .. "2.00x" style).
void formatMultiplier(Steinberg::Vst::ParamValue norm,
                      float minX, float maxX,
                      Steinberg::Vst::String128 out)
{
    const float x = minX + static_cast<float>(norm) * (maxX - minX);
    char text[32];
    std::snprintf(text, sizeof(text), "%.2fx", x);
    writeString128(out, text);
}

// Linear ms mapping; auto-switches to seconds past 1000 ms.
void formatLinearMs(Steinberg::Vst::ParamValue norm,
                    float minMs, float maxMs,
                    Steinberg::Vst::String128 out)
{
    const float ms = minMs + static_cast<float>(norm) * (maxMs - minMs);
    char text[32];
    if (ms >= 1000.0f)
        std::snprintf(text, sizeof(text), "%.2f s", ms / 1000.0f);
    else if (ms >= 100.0f)
        std::snprintf(text, sizeof(text), "%.0f ms", ms);
    else if (ms >= 10.0f)
        std::snprintf(text, sizeof(text), "%.1f ms", ms);
    else
        std::snprintf(text, sizeof(text), "%.2f ms", ms);
    writeString128(out, text);
}

// Log ms mapping: ms = minMs * (maxMs/minMs)^norm.
void formatLogMs(Steinberg::Vst::ParamValue norm,
                 float minMs, float maxMs,
                 Steinberg::Vst::String128 out)
{
    const float ms = minMs * std::pow(maxMs / minMs, static_cast<float>(norm));
    char text[32];
    if (ms >= 1000.0f)
        std::snprintf(text, sizeof(text), "%.2f s", ms / 1000.0f);
    else if (ms >= 100.0f)
        std::snprintf(text, sizeof(text), "%.0f ms", ms);
    else if (ms >= 10.0f)
        std::snprintf(text, sizeof(text), "%.1f ms", ms);
    else
        std::snprintf(text, sizeof(text), "%.2f ms", ms);
    writeString128(out, text);
}

// Log Hz mapping: hz = minHz * (maxHz/minHz)^norm.
void formatLogHz(Steinberg::Vst::ParamValue norm,
                 float minHz, float maxHz,
                 Steinberg::Vst::String128 out)
{
    const float hz = minHz * std::pow(maxHz / minHz, static_cast<float>(norm));
    char text[32];
    if (hz >= 1000.0f)
        std::snprintf(text, sizeof(text), "%.2f kHz", hz / 1000.0f);
    else if (hz >= 100.0f)
        std::snprintf(text, sizeof(text), "%.0f Hz", hz);
    else
        std::snprintf(text, sizeof(text), "%.1f Hz", hz);
    writeString128(out, text);
}

// Linear Q factor ("Q 0.30" .. "Q 5.00").
void formatQ(Steinberg::Vst::ParamValue norm,
             float minQ, float maxQ,
             Steinberg::Vst::String128 out)
{
    const float q = minQ + static_cast<float>(norm) * (maxQ - minQ);
    char text[32];
    std::snprintf(text, sizeof(text), "Q %.2f", q);
    writeString128(out, text);
}

// Noise color discretisation (matches noise_layer.h::denormColor thresholds).
void formatNoiseColor(Steinberg::Vst::ParamValue norm, Steinberg::Vst::String128 out)
{
    const float v = static_cast<float>(norm);
    const char* name;
    if (v < 0.25f)      name = "Brown";
    else if (v < 0.55f) name = "Pink";
    else if (v < 0.80f) name = "White";
    else                name = "Violet";
    writeString128(out, name);
}

void formatOnOff(Steinberg::Vst::ParamValue norm, Steinberg::Vst::String128 out)
{
    writeString128(out, norm >= 0.5 ? "On" : "Off");
}

void formatMaterial(Steinberg::Vst::ParamValue norm, Steinberg::Vst::String128 out)
{
    // 0.0 = pure Wood, 1.0 = pure Metal. Show the dominant material on either
    // side of the 50/50 midpoint so the user sees "70% Wood" at low values
    // instead of "30% Metal".
    const float v = static_cast<float>(norm);
    char text[32];
    if (v < 0.02f)
        std::snprintf(text, sizeof(text), "Wood");
    else if (v > 0.98f)
        std::snprintf(text, sizeof(text), "Metal");
    else if (v < 0.48f)
        std::snprintf(text, sizeof(text), "%.0f%% Wood", (1.0f - v) * 100.0f);
    else if (v > 0.52f)
        std::snprintf(text, sizeof(text), "%.0f%% Metal", v * 100.0f);
    else
        std::snprintf(text, sizeof(text), "Wood / Metal");
    writeString128(out, text);
}

void formatStrikePosition(Steinberg::Vst::ParamValue norm, Steinberg::Vst::String128 out)
{
    const float v = static_cast<float>(norm);
    char text[32];
    if (v < 0.02f)
        std::snprintf(text, sizeof(text), "Center");
    else if (v > 0.98f)
        std::snprintf(text, sizeof(text), "Edge");
    else
        std::snprintf(text, sizeof(text), "%.0f%% Edge", v * 100.0f);
    writeString128(out, text);
}

void formatSizeHz(Steinberg::Vst::ParamValue norm, Steinberg::Vst::String128 out)
{
    // Size reads in the knob's direction (0% smallest -> 100% largest) with the
    // actual body fundamental appended as a hint. Hz formula matches
    // drum_voice.h: naturalFundamentalHz = 500 * 0.1^size.
    const float hz = 500.0f * std::pow(0.1f, static_cast<float>(norm));
    const int pct = static_cast<int>(norm * 100.0 + 0.5);
    char text[32];
    if (hz >= 100.0f)
        std::snprintf(text, sizeof(text), "%d%% (%.0f Hz)", pct, hz);
    else
        std::snprintf(text, sizeof(text), "%d%% (%.1f Hz)", pct, hz);
    writeString128(out, text);
}

void formatLevelDb(Steinberg::Vst::ParamValue norm, Steinberg::Vst::String128 out)
{
    const float gain = static_cast<float>(norm);
    char text[32];
    if (gain < 0.0005f)
        std::snprintf(text, sizeof(text), "-inf dB");
    else
    {
        const float dB = 20.0f * std::log10(gain);
        std::snprintf(text, sizeof(text), "%+.1f dB", dB);
    }
    writeString128(out, text);
}

// Returns true (and fills `out`) if the pad-relative offset corresponds to a
// Simple- or Advanced-view ArcKnob parameter; false otherwise so the caller
// can fall through to the SDK default.
bool formatByPadOffset(int offset,
                       Steinberg::Vst::ParamValue norm,
                       Steinberg::Vst::String128 out)
{
    switch (offset)
    {
    // --- Simple view primaries ------------------------------------------
    case kPadMaterial:         formatMaterial(norm, out);        return true;
    case kPadSize:             formatSizeHz(norm, out);          return true;
    case kPadDecay:            formatPercent(norm, out);         return true;
    case kPadStrikePosition:   formatStrikePosition(norm, out);  return true;
    case kPadLevel:            formatLevelDb(norm, out);         return true;
    case kPadMacroTightness:
    case kPadMacroBrightness:
    case kPadMacroBodySize:
    case kPadMacroPunch:
    case kPadMacroComplexity:
        formatBipolarPercent(norm, out);
        return true;

    // --- Tone Shaper (Advanced) -----------------------------------------
    case kPadTSFilterCutoff:   formatLogHz(norm, 20.0f, 20000.0f, out);  return true;
    case kPadTSFilterResonance: formatPercent(norm, out);                return true;
    case kPadTSFilterEnvAmount: formatBipolarPercent(norm, out);         return true;
    case kPadTSDriveAmount:    formatPercent(norm, out);                 return true;
    case kPadTSFoldAmount:     formatPercent(norm, out);                 return true;
    case kPadTSFilterEnvAttack:  formatLinearMs(norm, 0.0f, 500.0f, out);  return true;
    case kPadTSFilterEnvDecay:   formatLinearMs(norm, 0.0f, 2000.0f, out); return true;
    case kPadTSFilterEnvSustain: formatPercent(norm, out);                 return true;
    case kPadTSFilterEnvRelease: formatLinearMs(norm, 0.0f, 2000.0f, out); return true;

    // --- Unnatural Zone / Morph -----------------------------------------
    case kPadModeStretch:      formatMultiplier(norm, 0.5f, 2.0f, out);  return true;
    case kPadDecaySkew:        formatBipolarPercent(norm, out);          return true;
    case kPadModeInjectAmount: formatPercent(norm, out);                 return true;
    case kPadNonlinearCoupling: formatPercent(norm, out);                return true;
    case kPadMorphDuration:    formatLinearMs(norm, 10.0f, 2000.0f, out); return true;

    // --- Exciter secondary params ---------------------------------------
    case kPadFMRatio:              formatMultiplier(norm, 1.0f, 4.0f, out);  return true;
    case kPadFeedbackAmount:       formatPercent(norm, out);                 return true;
    case kPadNoiseBurstDuration:   formatLinearMs(norm, 2.0f, 15.0f, out);   return true;
    case kPadFrictionPressure:     formatPercent(norm, out);                 return true;

    // --- Phase 5 per-pad coupling ---------------------------------------
    case kPadCouplingAmount:       formatPercent(norm, out);                 return true;

    // --- Phase 7 parallel layers ----------------------------------------
    case kPadNoiseLayerMix:        formatPercent(norm, out);                 return true;
    case kPadNoiseLayerCutoff:     formatLogHz(norm, 40.0f, 18000.0f, out);  return true;
    case kPadNoiseLayerResonance:  formatQ(norm, 0.3f, 5.0f, out);           return true;
    case kPadNoiseLayerDecay:      formatLogMs(norm, 20.0f, 2000.0f, out);   return true;
    case kPadNoiseLayerColor:      formatNoiseColor(norm, out);              return true;
    case kPadClickLayerMix:        formatPercent(norm, out);                 return true;
    case kPadClickLayerContactMs:  formatLinearMs(norm, 2.0f, 5.0f, out);    return true;
    case kPadClickLayerBrightness: formatLogHz(norm, 200.0f, 12000.0f, out); return true;

    // --- Phase 8A damping -----------------------------------------------
    case kPadBodyDampingB1:        formatPercent(norm, out);                 return true;
    case kPadBodyDampingB3:        formatPercent(norm, out);                 return true;

    // --- Phase 8C air / scatter -----------------------------------------
    case kPadAirLoading:           formatPercent(norm, out);                 return true;
    case kPadModeScatter:          formatPercent(norm, out);                 return true;

    // --- Phase 8D shell coupling ----------------------------------------
    case kPadCouplingStrength:     formatPercent(norm, out);                 return true;
    case kPadSecondaryEnabled:     formatOnOff(norm, out);                   return true;
    case kPadSecondarySize:        formatPercent(norm, out);                 return true;
    case kPadSecondaryMaterial:    formatMaterial(norm, out);                return true;

    // --- Phase 8E tension -----------------------------------------------
    case kPadTensionModAmt:        formatPercent(norm, out);                 return true;

    // --- Phase 8F per-pad enable toggle ---------------------------------
    case kPadEnabled:              formatOnOff(norm, out);                   return true;

    default:
        return false;
    }
}

} // anonymous namespace

Steinberg::tresult PLUGIN_API Controller::getParamStringByValue(
    Steinberg::Vst::ParamID tag,
    Steinberg::Vst::ParamValue valueNormalized,
    Steinberg::Vst::String128 string)
{
    // Global proxies share formatters with their corresponding per-pad offsets.
    switch (tag)
    {
    // Phase 1 primaries
    case kMaterialId:        formatMaterial(valueNormalized, string);       return kResultOk;
    case kSizeId:            formatSizeHz(valueNormalized, string);         return kResultOk;
    case kDecayId:           formatPercent(valueNormalized, string);        return kResultOk;
    case kStrikePositionId:  formatStrikePosition(valueNormalized, string); return kResultOk;
    case kLevelId:           formatLevelDb(valueNormalized, string);        return kResultOk;

    // Exciter secondary params
    case kExciterFMRatioId:            formatMultiplier(valueNormalized, 1.0f, 4.0f, string);  return kResultOk;
    case kExciterFeedbackAmountId:     formatPercent(valueNormalized, string);                 return kResultOk;
    case kExciterNoiseBurstDurationId: formatLinearMs(valueNormalized, 2.0f, 15.0f, string);   return kResultOk;
    case kExciterFrictionPressureId:   formatPercent(valueNormalized, string);                 return kResultOk;

    // Tone Shaper
    case kToneShaperFilterCutoffId:     formatLogHz(valueNormalized, 20.0f, 20000.0f, string);  return kResultOk;
    case kToneShaperFilterResonanceId:  formatPercent(valueNormalized, string);                 return kResultOk;
    case kToneShaperFilterEnvAmountId:  formatBipolarPercent(valueNormalized, string);          return kResultOk;
    case kToneShaperDriveAmountId:      formatPercent(valueNormalized, string);                 return kResultOk;
    case kToneShaperFoldAmountId:       formatPercent(valueNormalized, string);                 return kResultOk;
    case kToneShaperFilterEnvAttackId:  formatLinearMs(valueNormalized, 0.0f, 500.0f,  string); return kResultOk;
    case kToneShaperFilterEnvDecayId:   formatLinearMs(valueNormalized, 0.0f, 2000.0f, string); return kResultOk;
    case kToneShaperFilterEnvSustainId: formatPercent(valueNormalized, string);                 return kResultOk;
    case kToneShaperFilterEnvReleaseId: formatLinearMs(valueNormalized, 0.0f, 2000.0f, string); return kResultOk;

    // Unnatural Zone / Material Morph
    case kUnnaturalModeStretchId:       formatMultiplier(valueNormalized, 0.5f, 2.0f, string);   return kResultOk;
    case kUnnaturalDecaySkewId:         formatBipolarPercent(valueNormalized, string);           return kResultOk;
    case kUnnaturalModeInjectAmountId:  formatPercent(valueNormalized, string);                  return kResultOk;
    case kUnnaturalNonlinearCouplingId: formatPercent(valueNormalized, string);                  return kResultOk;
    case kMorphDurationMsId:            formatLinearMs(valueNormalized, 10.0f, 2000.0f, string); return kResultOk;

    // Phase 7 parallel layers
    case kNoiseLayerMixId:        formatPercent(valueNormalized, string);                  return kResultOk;
    case kNoiseLayerCutoffId:     formatLogHz(valueNormalized, 40.0f, 18000.0f, string);   return kResultOk;
    case kNoiseLayerResonanceId:  formatQ(valueNormalized, 0.3f, 5.0f, string);            return kResultOk;
    case kNoiseLayerDecayId:      formatLogMs(valueNormalized, 20.0f, 2000.0f, string);    return kResultOk;
    case kNoiseLayerColorId:      formatNoiseColor(valueNormalized, string);               return kResultOk;
    case kClickLayerMixId:        formatPercent(valueNormalized, string);                  return kResultOk;
    case kClickLayerContactMsId:  formatLinearMs(valueNormalized, 2.0f, 5.0f, string);     return kResultOk;
    case kClickLayerBrightnessId: formatLogHz(valueNormalized, 200.0f, 12000.0f, string);  return kResultOk;

    // Phase 8 physics detail
    case kBodyDampingB1Id:    formatPercent(valueNormalized, string);       return kResultOk;
    case kBodyDampingB3Id:    formatPercent(valueNormalized, string);       return kResultOk;
    case kAirLoadingId:       formatPercent(valueNormalized, string);       return kResultOk;
    case kModeScatterId:      formatPercent(valueNormalized, string);       return kResultOk;
    case kCouplingStrengthId: formatPercent(valueNormalized, string);       return kResultOk;
    case kSecondaryEnabledId: formatOnOff(valueNormalized, string);         return kResultOk;
    case kSecondarySizeId:    formatPercent(valueNormalized, string);       return kResultOk;
    case kSecondaryMaterialId: formatMaterial(valueNormalized, string);     return kResultOk;
    case kTensionModAmtId:    formatPercent(valueNormalized, string);       return kResultOk;
    case kPadEnabledId:       formatOnOff(valueNormalized, string);         return kResultOk;

    default:
        break;
    }

    // Per-pad parameter space (tag >= 1000). Strip the pad index and dispatch
    // on the offset. Selected-pad proxies are the pad-0 slot.
    if (tag >= static_cast<ParamID>(kPadBaseId))
    {
        const int offset =
            (static_cast<int>(tag) - kPadBaseId) % kPadParamStride;
        if (formatByPadOffset(offset, valueNormalized, string))
            return kResultOk;
    }

    return EditControllerEx1::getParamStringByValue(tag, valueNormalized, string);
}

} // namespace Membrum
