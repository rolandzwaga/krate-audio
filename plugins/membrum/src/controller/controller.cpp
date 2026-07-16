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
#include "ui/pitch_envelope_display.h"  // shared PitchEnvelopeDisplay (Krate::Plugins)
#include "ui/xy_morph_pad.h"             // shared XYMorphPad (Krate::Plugins)
#include "ui/adsr_display.h"             // shared ADSRDisplay   (Krate::Plugins)
#include "ui/adsr_expanded_overlay.h"    // Membrum::UI::ADSRExpandedOverlayView
#include "ui/membrum_buttons.h"          // Membrum::UI::IconExpandActionButton + shared OutlineActionButton
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

#include "../ui/membrum_buttons.h"
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
// Controller: init, proxy/pad sync, view creation + local helper data, editor lifecycle, messaging
// ==============================================================================

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
    // Phase 10: three-point pitch envelope extension (offsets 60-63).
    {.globalId = kPitchEnvKneeEnabledId,        .padOffset = kPadTSPitchEnvKneeEnabled },
    {.globalId = kPitchEnvMidPitchId,           .padOffset = kPadTSPitchEnvMidPitch },
    {.globalId = kPitchEnvMidFractionId,        .padOffset = kPadTSPitchEnvMidFraction },
    {.globalId = kPitchEnvCurve2Id,             .padOffset = kPadTSPitchEnvCurve2 },
    // M-9: per-pad pan (offset 64).
    {.globalId = kPadPanId,                     .padOffset = kPadPan },
    // Wire coupling (offset 65).
    {.globalId = kWireCouplingId,               .padOffset = kPadWireCoupling },
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
    {.offset = kPadTSPitchEnvCurve,    .name = "PitchEnv Curve",      .isDiscrete = false, .stepCount = 0, .defaultValue = 0.5 },
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
    {.offset = kPadFMRatio,            .name = "FM Ratio",            .isDiscrete = false, .stepCount = 0, .defaultValue = 0.133333 },
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
    //   b3 norm 0.5 -> 4e-5 s (Hz convention; equivalent to legacy brightness=0.5)
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
    // Phase 10 (three-point pitch envelope extension).
    {.offset = kPadTSPitchEnvKneeEnabled,.name = "PitchEnv Knee",       .isDiscrete = true,  .stepCount = 1, .defaultValue = 0.0 },
    {.offset = kPadTSPitchEnvMidPitch,   .name = "PitchEnv Mid",        .isDiscrete = false, .stepCount = 0, .defaultValue = 0.5 },
    {.offset = kPadTSPitchEnvMidFraction,.name = "PitchEnv Mid Frac",   .isDiscrete = false, .stepCount = 0, .defaultValue = 0.5 },
    {.offset = kPadTSPitchEnvCurve2,     .name = "PitchEnv Curve2",     .isDiscrete = false, .stepCount = 0, .defaultValue = 0.5 },
    // M-9: per-pad pan (offset 64). 0.5 = center.
    {.offset = kPadPan,                  .name = "Pan",                 .isDiscrete = false, .stepCount = 0, .defaultValue = 0.5 },
    // Wire coupling (offset 65). 0 = legacy independent buzz.
    {.offset = kPadWireCoupling,         .name = "Wire Coupling",       .isDiscrete = false, .stepCount = 0, .defaultValue = 0.0 },
};

constexpr int kPadParamSpecCount =
    static_cast<int>(sizeof(kPadParamSpecs) / sizeof(kPadParamSpecs[0]));

static_assert(kPadParamSpecCount == kPadActiveParamCountV12,
              "Pad param specs must match active param count (66 after wire-coupling)");

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
        excList->appendString(STR16("Clap"));
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
        // Audit finding 10: proxy default aligned to the per-pad default (0.0,
        // pad_config.h:216) so a fresh instance displays the value the DSP
        // actually uses. Friction Pressure amount=0 reproduces legacy
        // velocity-only behaviour, so this changes initial display only.
        {.id = kExciterFrictionPressureId,   .name = "Exciter Friction Pressure",   .defaultValue = 0.0,      .unit = nullptr },
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
        // Audit finding 11: kMorphEnabledId is a boolean toggle and is
        // registered separately below with stepCount=1 (not in this
        // stepCount=0 continuous batch).
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

    // Audit finding 11: Morph Enabled is a boolean toggle, so it must expose
    // a single step (stepCount=1) to the host instead of a continuous range,
    // matching its per-pad target (kPadMorphEnabled, controller.cpp:179).
    // Kept as a RangeParameter (not switched to StringListParameter) so the
    // registered VST3 parameter type is unchanged for hosts that cache param
    // metadata; the ToggleButton snaps to 0/1 and DSP thresholds at 0.5.
    parameters.addParameter(
        new RangeParameter(STR16("Morph Enabled"), kMorphEnabledId, nullptr,
                           0.0, 1.0, 0.0, /*stepCount=*/1,
                           ParameterInfo::kCanAutomate));

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

    // Tone Shaper Pitch Envelope segment-1 curve amount.
    // Phase 10: promoted from a 2-option StringList (Exp/Lin) to a continuous
    // RangeParameter. Norm 0.5 = linear (curveAmount 0); norm 0 = fast initial
    // drop (curveAmount -1, classic 808 exponential decay shape); norm 1 = slow
    // start / fast end (curveAmount +1). Drives the editor's drag-the-segment
    // handle for segment 1; also serves as the single-segment curve when the
    // knee toggle is OFF.
    parameters.addParameter(
        new RangeParameter(STR16("Tone Shaper PitchEnv Curve"),
                           kToneShaperPitchEnvCurveId, nullptr,
                           0.0, 1.0, 0.5, 0,
                           ParameterInfo::kCanAutomate));

    // ---- Phase 10: three-point pitch envelope ----
    // Knee toggle: float-as-bool. >= 0.5 = ON enables the middle breakpoint,
    // < 0.5 = OFF preserves the legacy 1-segment Start -> End shape.
    {
        auto* kneeList = new StringListParameter(
            STR16("PitchEnv Knee"), kPitchEnvKneeEnabledId, nullptr,
            ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
        kneeList->appendString(STR16("Off"));
        kneeList->appendString(STR16("On"));
        parameters.addParameter(kneeList);
    }
    // Mid pitch: same 0..1 -> 20..2000 Hz log mapping as Start/End.
    parameters.addParameter(
        new RangeParameter(STR16("PitchEnv Mid"), kPitchEnvMidPitchId, STR16("Hz"),
                           0.0, 1.0, 0.5, 0,
                           ParameterInfo::kCanAutomate));
    // Mid fraction: portion of total time spent in segment 1.
    parameters.addParameter(
        new RangeParameter(STR16("PitchEnv Mid Fraction"), kPitchEnvMidFractionId, nullptr,
                           0.0, 1.0, 0.5, 0,
                           ParameterInfo::kCanAutomate));
    // Segment-2 curve amount (same encoding as the Phase-2 curve param).
    parameters.addParameter(
        new RangeParameter(STR16("PitchEnv Curve 2"), kPitchEnvCurve2Id, nullptr,
                           0.0, 1.0, 0.5, 0,
                           ParameterInfo::kCanAutomate));

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
    // Audit finding 17: register as a StringListParameter so the bound
    // COptionMenu (ChokeGroupSel, tag 252) shows "None"/"CG1".."CG8" instead
    // of raw "0".."8", matching the sibling OutputBus selector. The 9 entries
    // yield stepCount=8 and the SDK ToNormalized/FromNormalized mapping is
    // bit-identical to the previous RangeParameter(0..8, stepCount=8), so the
    // normalized wire value and the per-pad proxy sync are unchanged.
    {
        auto* chokeList = new StringListParameter(
            STR16("Choke Group"), kChokeGroupId, nullptr,
            ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
        chokeList->appendString(STR16("None"));
        chokeList->appendString(STR16("CG1"));
        chokeList->appendString(STR16("CG2"));
        chokeList->appendString(STR16("CG3"));
        chokeList->appendString(STR16("CG4"));
        chokeList->appendString(STR16("CG5"));
        chokeList->appendString(STR16("CG6"));
        chokeList->appendString(STR16("CG7"));
        chokeList->appendString(STR16("CG8"));
        parameters.addParameter(chokeList);
    }

    // ---- Phase 4: kSelectedPadId ----
    parameters.addParameter(
        new RangeParameter(STR16("Selected Pad"), kSelectedPadId, nullptr,
                           0.0, 31.0, 0.0, /*stepCount=*/31,
                           ParameterInfo::kCanAutomate));

    // ---- Phase 5: Cross-Pad Coupling parameters ----
    // Coupling sends: register with [0, 100] + "%" so the ArcKnob popup and
    // any CParamDisplay show "37 %" instead of raw "0.37". The wire format
    // is still normalized [0..1] (VST3 contract), so processor code that
    // reads fValue stays correct.
    {
        auto* p = new RangeParameter(STR16("Global Coupling"), kGlobalCouplingId, STR16("%"),
                                     0.0, 100.0, 0.0, 0, ParameterInfo::kCanAutomate);
        p->setPrecision(0);
        parameters.addParameter(p);
    }
    {
        auto* p = new RangeParameter(STR16("Snare Buzz"), kSnareBuzzId, STR16("%"),
                                     0.0, 100.0, 0.0, 0, ParameterInfo::kCanAutomate);
        p->setPrecision(0);
        parameters.addParameter(p);
    }
    {
        auto* p = new RangeParameter(STR16("Tom Resonance"), kTomResonanceId, STR16("%"),
                                     0.0, 100.0, 0.0, 0, ParameterInfo::kCanAutomate);
        p->setPrecision(0);
        parameters.addParameter(p);
    }
    // Coupling delay: register with the real ms range [0.5, 2.0] so the
    // popup shows "1.00 ms". Processor still receives normalized [0..1] and
    // denormalizes via 0.5 + norm * 1.5.
    {
        auto* p = new RangeParameter(STR16("Coupling Delay"), kCouplingDelayId, STR16("ms"),
                                     0.5, 2.0, 1.0, 0, ParameterInfo::kCanAutomate);
        p->setPrecision(2);
        parameters.addParameter(p);
    }

    // ---- Phase 9: global master output gain ----
    // Range [-24, +12] dB, default -6 dB. Templates run hot at 0 dB master, so
    // -6 dB gives kits a sensible headroom margin out-of-the-box.
    {
        auto* p = new RangeParameter(STR16("Master Gain"), kMasterGainId, STR16("dB"),
                                     -24.0, 12.0, -6.0, 0, ParameterInfo::kCanAutomate);
        p->setPrecision(1);
        parameters.addParameter(p);
    }

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
    //
    // Audit finding 19: this proxy is a StringListParameter while the per-pad
    // target (kPadOutputBus) is a stepped RangeParameter(stepCount=15). The
    // asymmetry is INTENTIONAL and functionally safe: both ends share
    // stepCount=15/min=0 and the SDK ToNormalized/FromNormalized mapping is
    // bit-identical, so the normalized wire value round-trips exactly. The
    // proxy is a StringList purely so the bound COptionMenu shows friendly
    // labels (the same reason ChokeGroup was converted in finding 17); the
    // per-pad target stays a RangeParameter because swapping the type on 32
    // already-registered per-pad ParamIDs would disturb DAWs that cache
    // parameter metadata, for zero functional gain. Do not "unify" the types.
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

    // ---- M-9 global proxy: per-pad pan ----
    // Continuous [0, 1], default 0.5 = center.
    parameters.addParameter(
        new RangeParameter(STR16("Pan"), kPadPanId, nullptr,
                           0.0, 1.0, 0.5, 0, ParameterInfo::kCanAutomate));

    // ---- Wire-coupling global proxy: buzz-follows-body depth ----
    // Continuous [0, 1], default 0.0 = independent buzz (legacy).
    parameters.addParameter(
        new RangeParameter(STR16("Wire Coupling"), kWireCouplingId, nullptr,
                           0.0, 1.0, 0.0, 0, ParameterInfo::kCanAutomate));

    // ---- Phase 4/7/8A..8F/M-9/wire: 2112 per-pad parameters (32 pads x 66 offsets) ----
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
                list->appendString(STR16("Clap"));
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
        requestViewRefresh(kRefreshMorphControls);

    // Body model gate for the pitch-envelope section: the envelope only
    // retargets f0 on Membrane bodies (drum_voice.h:486), so dim/disable the
    // controls when any other body type is selected. Fires on the global proxy
    // tag whether the change came from the user, automation, preset load, or
    // syncGlobalProxyFromPad's pad-switch sync.
    if (tag == static_cast<ParamID>(kBodyModelId))
    {
        // Same gate hides the Material Morph power toggle on non-Membrane
        // bodies (drum_voice.h:1238): the morph's body-mapper refresh is
        // Membrane-only, so the toggle would enable an inert section.
        requestViewRefresh(kRefreshPitchEnvControls | kRefreshMorphToggleVis);
    }

    // Tone Shaper filter-envelope display: repaint the curve whenever any of
    // the four A/D/S/R parameters moves, regardless of source (knob edit,
    // automation, preset load, or the direct-setter pad-switch sync path).
    if (tag == static_cast<ParamID>(kToneShaperFilterEnvAttackId)
        || tag == static_cast<ParamID>(kToneShaperFilterEnvDecayId)
        || tag == static_cast<ParamID>(kToneShaperFilterEnvSustainId)
        || tag == static_cast<ParamID>(kToneShaperFilterEnvReleaseId))
    {
        requestViewRefresh(kRefreshFilterEnvDisplay);
    }

    // Tone Shaper pitch-envelope display: same treatment for the original four
    // start/end/time/curve params plus the Phase 10 knee/mid/curve2 fields.
    // The display holds its values independently of CControl's single-tag
    // value_, so we push the new value through by hand on every source of
    // change (knob/drag edit, automation, preset load, pad-switch sync).
    if (tag == static_cast<ParamID>(kToneShaperPitchEnvStartId)
        || tag == static_cast<ParamID>(kToneShaperPitchEnvEndId)
        || tag == static_cast<ParamID>(kToneShaperPitchEnvTimeId)
        || tag == static_cast<ParamID>(kToneShaperPitchEnvCurveId)
        || tag == static_cast<ParamID>(kPitchEnvKneeEnabledId)
        || tag == static_cast<ParamID>(kPitchEnvMidPitchId)
        || tag == static_cast<ParamID>(kPitchEnvMidFractionId)
        || tag == static_cast<ParamID>(kPitchEnvCurve2Id))
    {
        requestViewRefresh(kRefreshPitchEnvDisplay);
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
        // Audit finding 6: the grid highlight is otherwise only moved by a
        // mouse-down, so host automation / a remote selection change would
        // leave it stale. Push the new index into the grid (null-guarded;
        // setSelectedPadIndex bounds-checks and no-ops when unchanged).
        if (padGridView_ != nullptr)
            padGridView_->setSelectedPadIndex(selectedPadIndex_);
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
    // the derived setParamNormalized() override and its per-tag hooks. Reflect
    // the freshly-synced state onto every gated section/display so the visuals
    // track pad selection: Material Morph dim/enable + toggle visibility, the
    // filter- and pitch-envelope displays, and the pitch-env Body gate. Routed
    // through requestViewRefresh so a non-compliant host calling
    // setComponentState off the UI thread defers the mutation (finding 12).
    requestViewRefresh(kRefreshMorphControls | kRefreshFilterEnvDisplay
                       | kRefreshPitchEnvDisplay | kRefreshPitchEnvControls
                       | kRefreshMorphToggleVis);
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

        // Mouse-up after an audition: send an AuditionPadOff message so the
        // processor releases the held voice. Lets the user sustain a pad
        // for as long as the mouse button is down (essential for hearing
        // long morph / pitch-envelope trajectories).
        view->setAuditionOffCallback([this](int padIndex) {
            if (padIndex < 0 || padIndex >= kNumPads) return;
            const int midi = 36 + padIndex;
            auto* msg = allocateMessage();
            if (msg == nullptr) return;
            Steinberg::IPtr<Steinberg::Vst::IMessage> owned(msg, false);
            owned->setMessageID("AuditionPadOff");
            auto* attrs = owned->getAttributes();
            if (attrs == nullptr) return;
            attrs->setInt("midi", static_cast<Steinberg::int64>(midi));
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

        // Audit finding 6: seed the highlight from the persisted selection so
        // a re-opened editor (after a project load) shows the correct pad.
        view->setSelectedPadIndex(selectedPadIndex_);

        padGridView_ = view;
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
        // Push current parameter state into the freshly-built display so a
        // Simple<->Advanced template switch (or any other rebuild) does not
        // strand it at its hard-coded constructor defaults.
        updatePitchEnvelopeDisplay();
        // Apply the current Body gate so the freshly-built display starts in
        // the right dim/enabled state (e.g. when the selected pad's body is
        // not Membrane).
        updatePitchEnvControlsEnabled();
    }

    // Pitch-envelope Knee menu (Advanced template only). Cached by control-tag
    // so updatePitchEnvControlsEnabled() can cascade the Body gate across the
    // whole section. Tagged here separately from the Material Morph block to
    // keep the conditional simple.
    if (auto* ctrl = dynamic_cast<VSTGUI::CControl*>(view))
    {
        if (ctrl->getTag() == static_cast<int32>(kPitchEnvKneeEnabledId))
        {
            pitchEnvKneeView_ = ctrl;
            ctrl->registerViewListener(this);
            updatePitchEnvControlsEnabled();
        }
    }

    // "Knee" label sits next to the PitchEnvKneeEnabled menu in the Advanced
    // template; dim it alongside the rest of the pitch-env section. It is the
    // only CTextLabel in the uidesc with this title, so a title match is
    // unambiguous.
    if (auto* label = dynamic_cast<VSTGUI::CTextLabel*>(view))
    {
        VSTGUI::UTF8StringPtr t = label->getText();
        if (t != nullptr && std::strcmp(t, "Knee") == 0)
        {
            pitchEnvKneeLabel_ = label;
            label->registerViewListener(this);
            updatePitchEnvControlsEnabled();
        }
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
        else if (tag == static_cast<int32>(kMorphEnabledId))
        {
            morphEnabledToggleView_ = ctrl;
            ctrl->registerViewListener(this);
            updateMorphEnabledToggleVisibility();
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
    if (view == pitchEnvKneeView_)         pitchEnvKneeView_         = nullptr;
    if (view == pitchEnvKneeLabel_)        pitchEnvKneeLabel_        = nullptr;
    if (view == xyMorphPad_)               xyMorphPad_               = nullptr;
    if (view == morphDurationView_)        morphDurationView_        = nullptr;
    if (view == morphCurveView_)           morphCurveView_           = nullptr;
    if (view == morphDurLabel_)            morphDurLabel_            = nullptr;
    if (view == morphEnabledToggleView_)   morphEnabledToggleView_   = nullptr;
    if (view == filterEnvDisplay_)         filterEnvDisplay_         = nullptr;

    view->unregisterViewListener(this);
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

    // Audit finding 12: didOpen() is a guaranteed UI-thread callback. Record
    // the UI thread id so requestViewRefresh() can tell an off-thread caller
    // (non-compliant host) from the normal in-thread case.
    uiThreadId_ = std::this_thread::get_id();

    // The Acoustic/Extended view swap is driven entirely by the uidesc:
    // UIViewSwitchContainer's template-switch-control="UiMode" follows a hidden
    // CParamDisplay proxy bound to kUiModeId (see editor.uidesc). No C++
    // sub-controller is needed for it.
    pollTimer_ = VSTGUI::owned(new VSTGUI::CVSTGUITimer(
        [this](VSTGUI::CVSTGUITimer* /*timer*/) {
            // T046: read the last cached MetersBlock and push its values
            // into the kit-column meter / CPU views. PadGridView drives its
            // own 30 Hz poll against PadGlowPublisher.
            if (activeEditor_ == nullptr)
                return;
            // Audit finding 12: drain any view refreshes queued by an
            // off-UI-thread setParamNormalized()/setComponentState() and apply
            // them here on the UI thread.
            const auto pending =
                pendingViewRefresh_.exchange(0, std::memory_order_relaxed);
            if (pending != 0)
                applyViewRefresh(pending);
            updateMeterViews(cachedMeters_);
            // Belt-and-braces refresh of the Material Morph power-toggle
            // visibility. UIViewSwitchContainer's animated template swap can
            // strand the freshly-built toggle in the wrong visibility state
            // even though verifyView already called the helper -- some hosts
            // run the swap after our delegate hook returns. The poll tick
            // catches that within ~33 ms; the helper is a no-op when the
            // cached pointer is null or the toggle state already matches.
            updateMorphEnabledToggleVisibility();
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
    // Same for the pitch-envelope display in whichever template was just
    // built (Simple or Advanced).
    updatePitchEnvelopeDisplay();
    // Apply the Body gate to the freshly-built pitch-env section so it opens
    // in the correct dim/enabled state for the selected pad's body type.
    updatePitchEnvControlsEnabled();
    // And hide the Material Morph toggle if the selected pad is currently on
    // a non-Membrane body.
    updateMorphEnabledToggleVisibility();
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
    activeEditor_      = nullptr;
    // Audit finding 12: drop any queued view refreshes and forget the UI thread
    // id so a refresh queued against this (now torn-down) editor cannot leak
    // into a freshly-opened one. didOpen() re-applies all sections directly.
    pendingViewRefresh_.store(0, std::memory_order_relaxed);
    uiThreadId_ = std::thread::id{};
    padGridView_       = nullptr;
    kitMetersView_     = nullptr;
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
// IMessage notify pass-through (DataExchange IMessage fallback for hosts that
// don't implement the native DataExchange API).
// ==============================================================================

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

    return EditControllerEx1::notify(message);
}
} // namespace Membrum
