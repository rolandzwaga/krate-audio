// ==============================================================================
// Membrum Controller -- Phase 4 per-pad parameter registration + proxy logic
// ==============================================================================

#include "controller.h"
#include "plugin_ids.h"
#include "dsp/pad_config.h"
#include "dsp/exciter_type.h"
#include "dsp/body_model_type.h"

#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/base/ibstream.h"

#include "vstgui/plugin-bindings/vst3editor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

namespace Membrum {

using namespace Steinberg;
using namespace Steinberg::Vst;

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
    // Request current component state from the host/processor
    auto* component = getComponentHandler();
    if (!component)
        return nullptr;

    // Create a memory stream and write the kit preset blob
    // The processor's getState writes 9040 bytes; we need 9036 (no selectedPadIndex).
    // We request the full state via IComponentHandler, then truncate.
    // However, in the controller we don't have direct processor access.
    // The typical VST3 pattern: controller requests state via IComponentHandler2
    // or uses the paired component's getState.

    // For now, produce the state from the controller's own parameter values.
    auto* stream = new MemoryStream();

    int32 version = 4;
    stream->write(&version, sizeof(version), nullptr);

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
    }

    // Kit preset does NOT write selectedPadIndex (9036 bytes total)
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
    if (version != 4)
        return false;

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

IPlugView* PLUGIN_API Controller::createView(const char* name)
{
    // Phase 6 (T028, FR-001): return a VST3Editor backed by editor.uidesc.
    if (name && std::strcmp(name, Steinberg::Vst::ViewType::kEditor) == 0)
    {
        return new VSTGUI::VST3Editor(this, "EditorDefault", "editor.uidesc");
    }
    return nullptr;
}

} // namespace Membrum
