// ==============================================================================
// Membrum Controller -- Phase 4 per-pad parameter registration + proxy logic
// ==============================================================================

#include "controller.h"
#include "plugin_ids.h"
#include "dsp/pad_config.h"
#include "dsp/exciter_type.h"
#include "dsp/body_model_type.h"
#include "ui/pad_grid_view.h"
#include "ui/kit_meters_view.h"
#include "ui/coupling_matrix_view.h"
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
#include "vstgui/lib/controls/ctextlabel.h"
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
};

constexpr int kPadParamSpecCount =
    static_cast<int>(sizeof(kPadParamSpecs) / sizeof(kPadParamSpecs[0]));

static_assert(kPadParamSpecCount == kPadActiveParamCountV6,
              "Pad param specs must match active param count (42)");

// Helper: convert narrow string to TChar buffer
void narrowToTChar(const char* src, TChar* dst, int maxLen)
{
    int i = 0;
    for (; i < maxLen - 1 && src[i] != '\0'; ++i)
        dst[i] = static_cast<TChar>(src[i]);
    dst[i] = 0;
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

    // ---- Phase 6 (US1 / T026): session-scoped global UI parameters ----
    // Both are registered as automatable StringListParameters (FR-033) but
    // are NOT serialised in the state blob (enforced by Processor::getState
    // and Controller::setComponentState; see T027).
    {
        auto* uiModeList = new StringListParameter(
            STR16("UI Mode"), kUiModeId, nullptr,
            ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
        uiModeList->appendString(STR16("Acoustic"));
        uiModeList->appendString(STR16("Extended"));
        parameters.addParameter(uiModeList);
    }
    {
        auto* editorSizeList = new StringListParameter(
            STR16("Editor Size"), kEditorSizeId, nullptr,
            ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
        editorSizeList->appendString(STR16("Default"));
        editorSizeList->appendString(STR16("Compact"));
        parameters.addParameter(editorSizeList);
    }

    // ---- Phase 4/6: 1344 per-pad parameters (32 pads x 42 active offsets) ----
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
               const Krate::Plugins::PresetInfo& /*info*/) -> bool {
            const bool ok = kitPresetLoadProvider(stream);
            kitPresetLoadFailed_ = !ok;
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
               const Krate::Plugins::PresetInfo& /*info*/) -> bool {
            // FR-042 / T054: per-pad load applies sound params only;
            // outputBus and couplingAmount are preserved structurally because
            // the per-pad preset blob does not carry them. After load, force
            // a re-application of the affected pad's macros so the underlying
            // params reflect both the just-loaded sound state and the user's
            // current macro positions (calls macroMapper_.apply() in the
            // processor via standard parameter-change events).
            const bool ok = padPresetLoadProvider(stream);
            padPresetLoadFailed_ = !ok;
            if (ok)
                triggerSelectedPadMacroReapply();
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
            return result;
        }
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

    // Phase 6 (T027): session-scoped parameters always reset to their defaults
    // on state load, regardless of blob content. kUiModeId -> Acoustic (0.0);
    // kEditorSizeId -> Default (0.0). Kit presets may re-override kUiModeId via
    // a separate preset-load callback (not through IBStream).
    EditControllerEx1::setParamNormalized(kUiModeId, 0.0);
    EditControllerEx1::setParamNormalized(kEditorSizeId, 0.0);

    int32 version = 0;
    if (state->read(&version, sizeof(version), nullptr) != kResultOk)
        return kResultFalse;

    // For v4 state, read the new format
    if (version == 4)
    {
        int32 maxPoly = 8;
        int32 stealPolicy = 0;
        if (state->read(&maxPoly, sizeof(maxPoly), nullptr) != kResultOk)
            maxPoly = 8;
        if (state->read(&stealPolicy, sizeof(stealPolicy), nullptr) != kResultOk)
            stealPolicy = 0;

        maxPoly = std::clamp(maxPoly, 4, 16);
        stealPolicy = std::clamp(stealPolicy, 0, 2);

        // Sync max polyphony: normalize [4,16] -> [0,1]
        EditControllerEx1::setParamNormalized(kMaxPolyphonyId,
            static_cast<double>(maxPoly - 4) / 12.0);
        // Sync voice stealing: normalize [0,2] -> list normalized
        EditControllerEx1::setParamNormalized(kVoiceStealingId,
            (static_cast<double>(stealPolicy) + 0.5) / 3.0);

        // Read all 32 pad configs and sync per-pad parameters
        for (int pad = 0; pad < kNumPads; ++pad)
        {
            int32 exciterTypeI32 = 0;
            int32 bodyModelI32 = 0;
            if (state->read(&exciterTypeI32, sizeof(exciterTypeI32), nullptr) != kResultOk)
                exciterTypeI32 = 0;
            if (state->read(&bodyModelI32, sizeof(bodyModelI32), nullptr) != kResultOk)
                bodyModelI32 = 0;

            exciterTypeI32 = std::clamp(exciterTypeI32, 0,
                                        static_cast<int>(ExciterType::kCount) - 1);
            bodyModelI32 = std::clamp(bodyModelI32, 0,
                                      static_cast<int>(BodyModelType::kCount) - 1);

            // Sync exciter type
            EditControllerEx1::setParamNormalized(
                static_cast<ParamID>(padParamId(pad, kPadExciterType)),
                (static_cast<double>(exciterTypeI32) + 0.5) / static_cast<double>(ExciterType::kCount));
            // Sync body model
            EditControllerEx1::setParamNormalized(
                static_cast<ParamID>(padParamId(pad, kPadBodyModel)),
                (static_cast<double>(bodyModelI32) + 0.5) / static_cast<double>(BodyModelType::kCount));

            // Read 34 float64 values (offsets 2-35 including chokeGroup/outputBus as float64)
            double vals[34] = {};
            for (auto& val : vals)
            {
                if (state->read(&val, sizeof(val), nullptr) != kResultOk)
                    val = 0.0;
            }

            // Map the 34 float64 values to per-pad param offsets
            // vals[0..27] = offsets 2..29 (material through morphCurve)
            // vals[28..29] = offsets 30..31 (chokeGroup, outputBus as float64)
            // vals[30..33] = offsets 32..35 (secondary exciter params)

            // Offsets 2-29 (continuous sound params)
            const int continuousOffsets[] = {
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
                EditControllerEx1::setParamNormalized(
                    static_cast<ParamID>(padParamId(pad, continuousOffsets[j])),
                    vals[j]);
            }

            // Choke group and output bus (offsets 30-31, vals[28-29])
            // These are stored as float64 representations of integer values
            EditControllerEx1::setParamNormalized(
                static_cast<ParamID>(padParamId(pad, kPadChokeGroup)),
                vals[28] / 8.0);
            EditControllerEx1::setParamNormalized(
                static_cast<ParamID>(padParamId(pad, kPadOutputBus)),
                vals[29] / 15.0);

            // Secondary exciter params (offsets 32-35, vals[30-33])
            const int secOffsets[] = {
                kPadFMRatio, kPadFeedbackAmount, kPadNoiseBurstDuration, kPadFrictionPressure
            };
            for (int j = 0; j < 4; ++j)
            {
                EditControllerEx1::setParamNormalized(
                    static_cast<ParamID>(padParamId(pad, secOffsets[j])),
                    vals[30 + j]);
            }

            // Skip uint8 chokeGroup and outputBus (redundant in the stream)
            std::uint8_t dummy = 0;
            state->read(&dummy, sizeof(dummy), nullptr);
            state->read(&dummy, sizeof(dummy), nullptr);
        }

        // Read selectedPadIndex
        int32 selPad = 0;
        if (state->read(&selPad, sizeof(selPad), nullptr) != kResultOk)
            selPad = 0;
        selPad = std::clamp(selPad, static_cast<int32>(0), static_cast<int32>(kNumPads - 1));
        selectedPadIndex_ = selPad;
        EditControllerEx1::setParamNormalized(kSelectedPadId,
            static_cast<double>(selPad) / 31.0);

        // Sync global proxy params from selected pad
        syncGlobalProxyFromPad(selectedPadIndex_);

        return kResultOk;
    }

    // ------------------------------------------------------------------
    // Legacy v1/v2/v3 state loading
    // ------------------------------------------------------------------

    // Phase 1 params
    const ParamID phase1Ids[] = {
        kMaterialId, kSizeId, kDecayId, kStrikePositionId, kLevelId};
    const double phase1Defaults[] = {0.5, 0.5, 0.3, 0.3, 0.8};

    for (int i = 0; i < 5; ++i)
    {
        double value = phase1Defaults[i];
        if (state->read(&value, sizeof(value), nullptr) != kResultOk)
            value = phase1Defaults[i];
        EditControllerEx1::setParamNormalized(phase1Ids[i], value);
    }

    if (version >= 2)
    {
        int32 exciterTypeI32 = 0;
        int32 bodyModelI32 = 0;
        if (state->read(&exciterTypeI32, sizeof(exciterTypeI32), nullptr) == kResultOk)
        {
            const double norm =
                (std::clamp(exciterTypeI32, 0,
                            static_cast<int>(ExciterType::kCount) - 1) + 0.5) /
                static_cast<double>(ExciterType::kCount);
            EditControllerEx1::setParamNormalized(kExciterTypeId, norm);
        }
        if (state->read(&bodyModelI32, sizeof(bodyModelI32), nullptr) == kResultOk)
        {
            const double norm =
                (std::clamp(bodyModelI32, 0,
                            static_cast<int>(BodyModelType::kCount) - 1) + 0.5) /
                static_cast<double>(BodyModelType::kCount);
            EditControllerEx1::setParamNormalized(kBodyModelId, norm);
        }

        // Phase 2 continuous params
        struct Phase2Slot { ParamID id; double defaultValue; };
        constexpr Phase2Slot kPhase2Slots[] = {
            { .id = kExciterFMRatioId, .defaultValue = 0.133333 },
            { .id = kExciterFeedbackAmountId, .defaultValue = 0.0 },
            { .id = kExciterNoiseBurstDurationId, .defaultValue = 0.230769 },
            { .id = kExciterFrictionPressureId, .defaultValue = 0.3 },
            { .id = kToneShaperFilterTypeId, .defaultValue = 0.0 },
            { .id = kToneShaperFilterCutoffId, .defaultValue = 1.0 },
            { .id = kToneShaperFilterResonanceId, .defaultValue = 0.0 },
            { .id = kToneShaperFilterEnvAmountId, .defaultValue = 0.5 },
            { .id = kToneShaperDriveAmountId, .defaultValue = 0.0 },
            { .id = kToneShaperFoldAmountId, .defaultValue = 0.0 },
            { .id = kToneShaperPitchEnvStartId, .defaultValue = 0.070721 },
            { .id = kToneShaperPitchEnvEndId, .defaultValue = 0.0 },
            { .id = kToneShaperPitchEnvTimeId, .defaultValue = 0.0 },
            { .id = kToneShaperPitchEnvCurveId, .defaultValue = 0.0 },
            { .id = kToneShaperFilterEnvAttackId, .defaultValue = 0.0 },
            { .id = kToneShaperFilterEnvDecayId, .defaultValue = 0.1 },
            { .id = kToneShaperFilterEnvSustainId, .defaultValue = 0.0 },
            { .id = kToneShaperFilterEnvReleaseId, .defaultValue = 0.1 },
            { .id = kUnnaturalModeStretchId, .defaultValue = 0.333333 },
            { .id = kUnnaturalDecaySkewId, .defaultValue = 0.5 },
            { .id = kUnnaturalModeInjectAmountId, .defaultValue = 0.0 },
            { .id = kUnnaturalNonlinearCouplingId, .defaultValue = 0.0 },
            { .id = kMorphEnabledId, .defaultValue = 0.0 },
            { .id = kMorphStartId, .defaultValue = 1.0 },
            { .id = kMorphEndId, .defaultValue = 0.0 },
            { .id = kMorphDurationMsId, .defaultValue = 0.095477 },
            { .id = kMorphCurveId, .defaultValue = 0.0 },
        };

        for (const auto& slot : kPhase2Slots)
        {
            double value = slot.defaultValue;
            if (state->read(&value, sizeof(value), nullptr) != kResultOk)
                value = slot.defaultValue;
            EditControllerEx1::setParamNormalized(slot.id, value);
        }
    }
    else
    {
        EditControllerEx1::setParamNormalized(kExciterTypeId,
            0.5 / static_cast<double>(ExciterType::kCount));
        EditControllerEx1::setParamNormalized(kBodyModelId,
            0.5 / static_cast<double>(BodyModelType::kCount));
    }

    return kResultOk;
}

// ==============================================================================
// Kit Preset StateProvider / LoadProvider (FR-052, T033)
// ==============================================================================

IBStream* Controller::kitPresetStateProvider()
{
    // Build the kit preset blob from the controller's parameter values. The
    // controller mirrors the processor's parameter state (host syncs them
    // on every parameter change), so we do not need IComponentHandler access.
    auto* stream = new MemoryStream();

    // Phase 6 (T055, FR-040..FR-044, FR-070..FR-072): kit preset is now v5.
    // v5 layout extends v4 by:
    //   - one int32 `uiMode` immediately after the global header (so it is
    //     restored before per-pad data is consumed),
    //   - five float64 macros appended to each pad row (offsets 37-41).
    // The v4 reader is preserved for backwards compatibility (no uiMode change
    // and macros default to 0.5).
    int32 version = 5;
    stream->write(&version, sizeof(version), nullptr);

    // FR-030 / Clarification #4: kit preset MAY override the session-scoped
    // kUiModeId. Persist as int32 (0 = Acoustic, 1 = Extended).
    {
        const double uiModeNorm = getParamNormalized(kUiModeId);
        int32 uiMode = (uiModeNorm >= 0.5) ? 1 : 0;
        stream->write(&uiMode, sizeof(uiMode), nullptr);
    }

    // Global settings from controller params
    const double maxPolyNorm = getParamNormalized(kMaxPolyphonyId);
    int32 maxPoly = 4 + static_cast<int32>(maxPolyNorm * 12.0 + 0.5);
    maxPoly = std::clamp(maxPoly, static_cast<int32>(4), static_cast<int32>(16));
    stream->write(&maxPoly, sizeof(maxPoly), nullptr);

    const double stealNorm = getParamNormalized(kVoiceStealingId);
    int32 stealPolicy = std::clamp(static_cast<int32>(stealNorm * 3.0), static_cast<int32>(0), static_cast<int32>(2));
    stream->write(&stealPolicy, sizeof(stealPolicy), nullptr);

    // Write 32 pad configs from controller parameter values
    for (int pad = 0; pad < kNumPads; ++pad)
    {
        // Exciter type
        const double excNorm = getParamNormalized(
            static_cast<ParamID>(padParamId(pad, kPadExciterType)));
        int32 exciterTypeI32 = std::clamp(
            static_cast<int32>(excNorm * static_cast<double>(ExciterType::kCount)),
            static_cast<int32>(0), static_cast<int32>(ExciterType::kCount) - 1);
        stream->write(&exciterTypeI32, sizeof(exciterTypeI32), nullptr);

        // Body model
        const double bodyNorm = getParamNormalized(
            static_cast<ParamID>(padParamId(pad, kPadBodyModel)));
        int32 bodyModelI32 = std::clamp(
            static_cast<int32>(bodyNorm * static_cast<double>(BodyModelType::kCount)),
            static_cast<int32>(0), static_cast<int32>(BodyModelType::kCount) - 1);
        stream->write(&bodyModelI32, sizeof(bodyModelI32), nullptr);

        // Sound params: offsets 2-29 (28 continuous), then 30-31 (choke/bus as float64),
        // then 32-35 (4 secondary exciter)
        const int continuousOffsets[] = {
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
        for (const auto& offset : continuousOffsets)
        {
            double v = getParamNormalized(
                static_cast<ParamID>(padParamId(pad, offset)));
            stream->write(&v, sizeof(v), nullptr);
        }

        // Choke group and output bus as float64
        {
            const double chokeNorm = getParamNormalized(
                static_cast<ParamID>(padParamId(pad, kPadChokeGroup)));
            double cg = chokeNorm * 8.0;
            stream->write(&cg, sizeof(cg), nullptr);

            const double busNorm = getParamNormalized(
                static_cast<ParamID>(padParamId(pad, kPadOutputBus)));
            double ob = busNorm * 15.0;
            stream->write(&ob, sizeof(ob), nullptr);
        }

        // Secondary exciter params
        const int secOffsets[] = {
            kPadFMRatio, kPadFeedbackAmount, kPadNoiseBurstDuration, kPadFrictionPressure
        };
        for (const auto& offset : secOffsets)
        {
            double v = getParamNormalized(
                static_cast<ParamID>(padParamId(pad, offset)));
            stream->write(&v, sizeof(v), nullptr);
        }

        // uint8 chokeGroup and outputBus
        {
            const double chokeNorm = getParamNormalized(
                static_cast<ParamID>(padParamId(pad, kPadChokeGroup)));
            std::uint8_t cg = static_cast<std::uint8_t>(
                std::clamp(static_cast<int>(chokeNorm * 8.0 + 0.5), 0, 8));
            stream->write(&cg, sizeof(cg), nullptr);

            const double busNorm = getParamNormalized(
                static_cast<ParamID>(padParamId(pad, kPadOutputBus)));
            std::uint8_t ob = static_cast<std::uint8_t>(
                std::clamp(static_cast<int>(busNorm * 15.0 + 0.5), 0, 15));
            stream->write(&ob, sizeof(ob), nullptr);
        }

        // Phase 6 (T055): five per-pad macros (offsets 37-41), float64.
        const int macroOffsets[] = {
            kPadMacroTightness, kPadMacroBrightness, kPadMacroBodySize,
            kPadMacroPunch,     kPadMacroComplexity,
        };
        for (const auto& offset : macroOffsets)
        {
            double v = getParamNormalized(
                static_cast<ParamID>(padParamId(pad, offset)));
            stream->write(&v, sizeof(v), nullptr);
        }
    }

    // Kit preset does NOT write selectedPadIndex.
    // v5 size = 4 (version) + 4 (uiMode) + 4 (maxPoly) + 4 (steal) +
    //          32 * (4 + 4 + 34*8 + 1 + 1 + 5*8) = 16 + 32*322 = 10320 bytes.
    stream->seek(0, IBStream::kIBSeekSet, nullptr);
    return stream;
}

bool Controller::kitPresetLoadProvider(IBStream* stream)
{
    if (!stream)
        return false;

    int32 version = 0;
    if (stream->read(&version, sizeof(version), nullptr) != kResultOk)
        return false;
    // Phase 6 (T055): accept both v4 (legacy) and v5 (with uiMode + macros).
    if (version != 4 && version != 5)
        return false;
    const bool isV5 = (version == 5);

    // v5 only: read uiMode (int32, 0=Acoustic, 1=Extended) and apply to the
    // session-scoped kUiModeId (FR-030, Clarification #4). v4 leaves uiMode
    // untouched.
    if (isV5)
    {
        int32 uiMode = 0;
        if (stream->read(&uiMode, sizeof(uiMode), nullptr) != kResultOk)
            return false;
        const double uiNorm = (uiMode >= 1) ? 1.0 : 0.0;
        EditControllerEx1::setParamNormalized(kUiModeId, uiNorm);
    }

    int32 maxPoly = 8;
    int32 stealPolicy = 0;
    if (stream->read(&maxPoly, sizeof(maxPoly), nullptr) != kResultOk)
        return false;
    if (stream->read(&stealPolicy, sizeof(stealPolicy), nullptr) != kResultOk)
        return false;

    maxPoly = std::clamp(maxPoly, static_cast<int32>(4), static_cast<int32>(16));
    stealPolicy = std::clamp(stealPolicy, static_cast<int32>(0), static_cast<int32>(2));

    EditControllerEx1::setParamNormalized(kMaxPolyphonyId,
        static_cast<double>(maxPoly - 4) / 12.0);
    EditControllerEx1::setParamNormalized(kVoiceStealingId,
        (static_cast<double>(stealPolicy) + 0.5) / 3.0);

    for (int pad = 0; pad < kNumPads; ++pad)
    {
        int32 exciterTypeI32 = 0;
        int32 bodyModelI32 = 0;
        if (stream->read(&exciterTypeI32, sizeof(exciterTypeI32), nullptr) != kResultOk)
            return false;
        if (stream->read(&bodyModelI32, sizeof(bodyModelI32), nullptr) != kResultOk)
            return false;

        exciterTypeI32 = std::clamp(exciterTypeI32, static_cast<int32>(0),
                                    static_cast<int32>(ExciterType::kCount) - 1);
        bodyModelI32 = std::clamp(bodyModelI32, static_cast<int32>(0),
                                  static_cast<int32>(BodyModelType::kCount) - 1);

        EditControllerEx1::setParamNormalized(
            static_cast<ParamID>(padParamId(pad, kPadExciterType)),
            (static_cast<double>(exciterTypeI32) + 0.5) / static_cast<double>(ExciterType::kCount));
        EditControllerEx1::setParamNormalized(
            static_cast<ParamID>(padParamId(pad, kPadBodyModel)),
            (static_cast<double>(bodyModelI32) + 0.5) / static_cast<double>(BodyModelType::kCount));

        double vals[34] = {};
        for (auto& val : vals)
        {
            if (stream->read(&val, sizeof(val), nullptr) != kResultOk)
                return false;
        }

        const int continuousOffsets[] = {
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
            EditControllerEx1::setParamNormalized(
                static_cast<ParamID>(padParamId(pad, continuousOffsets[j])),
                vals[j]);
        }

        EditControllerEx1::setParamNormalized(
            static_cast<ParamID>(padParamId(pad, kPadChokeGroup)),
            vals[28] / 8.0);
        EditControllerEx1::setParamNormalized(
            static_cast<ParamID>(padParamId(pad, kPadOutputBus)),
            vals[29] / 15.0);

        const int secOffsets[] = {
            kPadFMRatio, kPadFeedbackAmount, kPadNoiseBurstDuration, kPadFrictionPressure
        };
        for (int j = 0; j < 4; ++j)
        {
            EditControllerEx1::setParamNormalized(
                static_cast<ParamID>(padParamId(pad, secOffsets[j])),
                vals[30 + j]);
        }

        // Skip uint8 chokeGroup and outputBus (redundant in stream)
        std::uint8_t dummy = 0;
        stream->read(&dummy, sizeof(dummy), nullptr);
        stream->read(&dummy, sizeof(dummy), nullptr);

        // Phase 6 (T055): v5 carries 5 macro float64 per pad (offsets 37-41).
        // v4 has none: assign neutral 0.5 to each macro so MacroMapper produces
        // zero delta against registered defaults.
        const int macroOffsets[] = {
            kPadMacroTightness, kPadMacroBrightness, kPadMacroBodySize,
            kPadMacroPunch,     kPadMacroComplexity,
        };
        if (isV5)
        {
            for (const auto& offset : macroOffsets)
            {
                double v = 0.5;
                if (stream->read(&v, sizeof(v), nullptr) != kResultOk)
                    v = 0.5;
                EditControllerEx1::setParamNormalized(
                    static_cast<ParamID>(padParamId(pad, offset)), v);
            }
        }
        else
        {
            for (const auto& offset : macroOffsets)
            {
                EditControllerEx1::setParamNormalized(
                    static_cast<ParamID>(padParamId(pad, offset)), 0.5);
            }
        }
    }

    // Kit preset does NOT read selectedPadIndex -- leave it unchanged
    // Sync global proxy params from selected pad
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

    // Version = 1
    int32 version = 1;
    stream->write(&version, sizeof(version), nullptr);

    // Exciter type as int32
    const double excNorm = getParamNormalized(
        static_cast<ParamID>(padParamId(pad, kPadExciterType)));
    int32 exciterTypeI32 = std::clamp(
        static_cast<int32>(excNorm * static_cast<double>(ExciterType::kCount)),
        static_cast<int32>(0), static_cast<int32>(ExciterType::kCount) - 1);
    stream->write(&exciterTypeI32, sizeof(exciterTypeI32), nullptr);

    // Body model as int32
    const double bodyNorm = getParamNormalized(
        static_cast<ParamID>(padParamId(pad, kPadBodyModel)));
    int32 bodyModelI32 = std::clamp(
        static_cast<int32>(bodyNorm * static_cast<double>(BodyModelType::kCount)),
        static_cast<int32>(0), static_cast<int32>(BodyModelType::kCount) - 1);
    stream->write(&bodyModelI32, sizeof(bodyModelI32), nullptr);

    // 34 float64 sound params (offsets 2-35)
    // Same order as kit preset: 28 continuous (offsets 2-29),
    // then choke/bus as float64 (offsets 30-31), then 4 secondary exciter (offsets 32-35)
    const int continuousOffsets[] = {
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
    for (const auto& offset : continuousOffsets)
    {
        double v = getParamNormalized(
            static_cast<ParamID>(padParamId(pad, offset)));
        stream->write(&v, sizeof(v), nullptr);
    }

    // Choke group and output bus as float64 (positions 28-29 in the 34-value array)
    {
        const double chokeNorm = getParamNormalized(
            static_cast<ParamID>(padParamId(pad, kPadChokeGroup)));
        double cg = chokeNorm * 8.0;
        stream->write(&cg, sizeof(cg), nullptr);

        const double busNorm = getParamNormalized(
            static_cast<ParamID>(padParamId(pad, kPadOutputBus)));
        double ob = busNorm * 15.0;
        stream->write(&ob, sizeof(ob), nullptr);
    }

    // Secondary exciter params (offsets 32-35)
    const int secOffsets[] = {
        kPadFMRatio, kPadFeedbackAmount, kPadNoiseBurstDuration, kPadFrictionPressure
    };
    for (const auto& offset : secOffsets)
    {
        double v = getParamNormalized(
            static_cast<ParamID>(padParamId(pad, offset)));
        stream->write(&v, sizeof(v), nullptr);
    }

    // Total: 4 + 4 + 4 + 34*8 = 284 bytes
    stream->seek(0, IBStream::kIBSeekSet, nullptr);
    return stream;
}

bool Controller::padPresetLoadProvider(IBStream* stream)
{
    if (!stream)
        return false;

    // Helper: read exactly N bytes, fail if fewer
    auto readExact = [&](void* buf, int32 size) -> bool {
        int32 bytesRead = 0;
        if (stream->read(buf, size, &bytesRead) != kResultOk)
            return false;
        return bytesRead == size;
    };

    int32 version = 0;
    if (!readExact(&version, sizeof(version)))
        return false;
    if (version != 1)
        return false;

    int32 exciterTypeI32 = 0;
    int32 bodyModelI32 = 0;
    if (!readExact(&exciterTypeI32, sizeof(exciterTypeI32)))
        return false;
    if (!readExact(&bodyModelI32, sizeof(bodyModelI32)))
        return false;

    double vals[34] = {};
    for (auto& val : vals)
    {
        if (!readExact(&val, sizeof(val)))
            return false;
    }

    // Apply to selected pad only
    const int pad = selectedPadIndex_;

    exciterTypeI32 = std::clamp(exciterTypeI32, static_cast<int32>(0),
                                static_cast<int32>(ExciterType::kCount) - 1);
    bodyModelI32 = std::clamp(bodyModelI32, static_cast<int32>(0),
                              static_cast<int32>(BodyModelType::kCount) - 1);

    EditControllerEx1::setParamNormalized(
        static_cast<ParamID>(padParamId(pad, kPadExciterType)),
        (static_cast<double>(exciterTypeI32) + 0.5) / static_cast<double>(ExciterType::kCount));
    EditControllerEx1::setParamNormalized(
        static_cast<ParamID>(padParamId(pad, kPadBodyModel)),
        (static_cast<double>(bodyModelI32) + 0.5) / static_cast<double>(BodyModelType::kCount));

    // Apply 28 continuous sound params (offsets 2-29)
    const int continuousOffsets[] = {
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
        EditControllerEx1::setParamNormalized(
            static_cast<ParamID>(padParamId(pad, continuousOffsets[j])),
            vals[j]);
    }

    // Skip vals[28] and vals[29] -- choke group and output bus are NOT applied
    // (FR-061: Per-pad presets MUST NOT contain choke group or output routing)

    // Apply 4 secondary exciter params (vals[30..33] = offsets 32-35)
    const int secOffsets[] = {
        kPadFMRatio, kPadFeedbackAmount, kPadNoiseBurstDuration, kPadFrictionPressure
    };
    for (int j = 0; j < 4; ++j)
    {
        EditControllerEx1::setParamNormalized(
            static_cast<ParamID>(padParamId(pad, secOffsets[j])),
            vals[30 + j]);
    }

    // Sync global proxy params for the selected pad
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
    const VSTGUI::UIAttributes& /*attributes*/,
    const VSTGUI::IUIDescription* /*description*/,
    VSTGUI::VST3Editor* /*editor*/)
{
    // Phase 6 (T028): custom view dispatcher. The real views (PadGridView,
    // CouplingMatrixView, PitchEnvelopeDisplay) are constructed by later
    // phases (T042 / Phase 7 / Phase 9). Returning nullptr here lets VSTGUI
    // fall back to a placeholder; the important job of this method today is
    // simply to exist so editor.uidesc references to these custom-view names
    // resolve cleanly instead of triggering parse errors.
    if (!name)
        return nullptr;
    if (std::strcmp(name, "PadGridView") == 0)
    {
        // Phase 6 (T042): construct the real PadGridView. The view's
        // PadGlowPublisher pointer is nullptr here because separate-component
        // mode prevents the Controller from reaching into the Processor --
        // future phases can inject a DataExchange-backed glow mirror if
        // per-cell intensity ends up required at 30 Hz in separate-component
        // hosts. Tests construct the view directly with a real publisher.
        auto* view = new UI::PadGridView(
            VSTGUI::CRect{ 0, 0, 400, 800 },
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
            VSTGUI::CRect{ 0, 0, 320, 320 },
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
    if (std::strcmp(name, "PitchEnvelopeDisplay") == 0)
        return nullptr;
    if (std::strcmp(name, "MetersView") == 0)
    {
        // T046: kit-column peak meter. The size below is a placeholder; the
        // uidesc view frame overrides it.
        auto* view = new UI::KitMetersView(VSTGUI::CRect{ 0, 0, 224, 32 });
        kitMetersView_ = view;
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
    return view;
}

void Controller::didOpen(VSTGUI::VST3Editor* editor)
{
    // Phase 6 (T028): cache the editor pointer and start a 30 Hz poll timer
    // for PadGlowPublisher / MatrixActivityPublisher snapshots. The actual
    // view invalidation lives in later phases; the body here is intentionally
    // minimal but the timer lifecycle is fully correct today so we do not
    // leak a timer between editor instances.
    activeEditor_ = editor;
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
        kitPresetBrowserView_ = new Krate::Plugins::PresetBrowserView(
            frameSize, kitPresetManager_.get(),
            std::vector<std::string>{
                "Factory", "User", "Acoustic", "Electronic",
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
                "Factory", "User", "Kick", "Snare", "Tom",
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
    activeEditor_      = nullptr;
    padGridView_       = nullptr;
    kitMetersView_     = nullptr;
    couplingMatrixView_ = nullptr;
    cpuLabel_          = nullptr;
    activeVoicesLabel_ = nullptr;
    presetStatusLabel_ = nullptr;

    // T053..T054: VSTGUI owns the views; just drop our raw pointers so the
    // 30 Hz poll timer (already cancelled above) and any future code can not
    // dereference dead memory.
    kitPresetBrowserView_ = nullptr;
    padPresetBrowserView_ = nullptr;
    kitSaveDialogView_    = nullptr;
    padSaveDialogView_    = nullptr;
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
        const unsigned int percent =
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
