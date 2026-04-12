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

#include <algorithm>
#include <cstdio>

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
    { kMaterialId,                   kPadMaterial },
    { kSizeId,                       kPadSize },
    { kDecayId,                      kPadDecay },
    { kStrikePositionId,             kPadStrikePosition },
    { kLevelId,                      kPadLevel },
    { kExciterTypeId,                kPadExciterType },
    { kBodyModelId,                  kPadBodyModel },
    { kExciterFMRatioId,             kPadFMRatio },
    { kExciterFeedbackAmountId,      kPadFeedbackAmount },
    { kExciterNoiseBurstDurationId,  kPadNoiseBurstDuration },
    { kExciterFrictionPressureId,    kPadFrictionPressure },
    { kToneShaperFilterTypeId,       kPadTSFilterType },
    { kToneShaperFilterCutoffId,     kPadTSFilterCutoff },
    { kToneShaperFilterResonanceId,  kPadTSFilterResonance },
    { kToneShaperFilterEnvAmountId,  kPadTSFilterEnvAmount },
    { kToneShaperDriveAmountId,      kPadTSDriveAmount },
    { kToneShaperFoldAmountId,       kPadTSFoldAmount },
    { kToneShaperPitchEnvStartId,    kPadTSPitchEnvStart },
    { kToneShaperPitchEnvEndId,      kPadTSPitchEnvEnd },
    { kToneShaperPitchEnvTimeId,     kPadTSPitchEnvTime },
    { kToneShaperPitchEnvCurveId,    kPadTSPitchEnvCurve },
    { kToneShaperFilterEnvAttackId,  kPadTSFilterEnvAttack },
    { kToneShaperFilterEnvDecayId,   kPadTSFilterEnvDecay },
    { kToneShaperFilterEnvSustainId, kPadTSFilterEnvSustain },
    { kToneShaperFilterEnvReleaseId, kPadTSFilterEnvRelease },
    { kUnnaturalModeStretchId,       kPadModeStretch },
    { kUnnaturalDecaySkewId,         kPadDecaySkew },
    { kUnnaturalModeInjectAmountId,  kPadModeInjectAmount },
    { kUnnaturalNonlinearCouplingId, kPadNonlinearCoupling },
    { kMorphEnabledId,               kPadMorphEnabled },
    { kMorphStartId,                 kPadMorphStart },
    { kMorphEndId,                   kPadMorphEnd },
    { kMorphDurationMsId,            kPadMorphDuration },
    { kMorphCurveId,                 kPadMorphCurve },
    { kChokeGroupId,                 kPadChokeGroup },
};

constexpr int kProxyMappingCount =
    static_cast<int>(sizeof(kProxyMappings) / sizeof(kProxyMappings[0]));

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
    { kPadExciterType,        "Exciter Type",        true,  5, 0.0 },
    { kPadBodyModel,          "Body Model",          true,  5, 0.0 },
    { kPadMaterial,           "Material",            false, 0, 0.5 },
    { kPadSize,               "Size",                false, 0, 0.5 },
    { kPadDecay,              "Decay",               false, 0, 0.3 },
    { kPadStrikePosition,     "Strike Position",     false, 0, 0.3 },
    { kPadLevel,              "Level",               false, 0, 0.8 },
    { kPadTSFilterType,       "Filter Type",         true,  2, 0.0 },
    { kPadTSFilterCutoff,     "Filter Cutoff",       false, 0, 1.0 },
    { kPadTSFilterResonance,  "Filter Resonance",    false, 0, 0.0 },
    { kPadTSFilterEnvAmount,  "Filter Env Amount",   false, 0, 0.5 },
    { kPadTSDriveAmount,      "Drive Amount",        false, 0, 0.0 },
    { kPadTSFoldAmount,       "Fold Amount",         false, 0, 0.0 },
    { kPadTSPitchEnvStart,    "PitchEnv Start",      false, 0, 0.0 },
    { kPadTSPitchEnvEnd,      "PitchEnv End",        false, 0, 0.0 },
    { kPadTSPitchEnvTime,     "PitchEnv Time",       false, 0, 0.0 },
    { kPadTSPitchEnvCurve,    "PitchEnv Curve",      true,  1, 0.0 },
    { kPadTSFilterEnvAttack,  "Filter Env Atk",      false, 0, 0.0 },
    { kPadTSFilterEnvDecay,   "Filter Env Dec",      false, 0, 0.1 },
    { kPadTSFilterEnvSustain, "Filter Env Sus",      false, 0, 0.0 },
    { kPadTSFilterEnvRelease, "Filter Env Rel",      false, 0, 0.1 },
    { kPadModeStretch,        "Mode Stretch",        false, 0, 0.333333 },
    { kPadDecaySkew,          "Decay Skew",          false, 0, 0.5 },
    { kPadModeInjectAmount,   "Mode Inject",         false, 0, 0.0 },
    { kPadNonlinearCoupling,  "Nonlinear Coupling",  false, 0, 0.0 },
    { kPadMorphEnabled,       "Morph Enabled",       true,  1, 0.0 },
    { kPadMorphStart,         "Morph Start",         false, 0, 1.0 },
    { kPadMorphEnd,           "Morph End",           false, 0, 0.0 },
    { kPadMorphDuration,      "Morph Duration",      false, 0, 0.095477 },
    { kPadMorphCurve,         "Morph Curve",         true,  1, 0.0 },
    { kPadChokeGroup,         "Choke Group",         true,  8, 0.0 },
    { kPadOutputBus,          "Output Bus",          true, 15, 0.0 },
    { kPadFMRatio,            "FM Ratio",            false, 0, 0.5 },
    { kPadFeedbackAmount,     "Feedback Amount",     false, 0, 0.0 },
    { kPadNoiseBurstDuration, "NoiseBurst Duration", false, 0, 0.5 },
    { kPadFrictionPressure,   "Friction Pressure",   false, 0, 0.0 },
};

constexpr int kPadParamSpecCount =
    static_cast<int>(sizeof(kPadParamSpecs) / sizeof(kPadParamSpecs[0]));

static_assert(kPadParamSpecCount == kPadActiveParamCount,
              "Pad param specs must match active param count (36)");

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
        { kExciterFMRatioId,            "Exciter FM Ratio",            0.133333, nullptr },
        { kExciterFeedbackAmountId,     "Exciter Feedback Amount",     0.0,      nullptr },
        { kExciterNoiseBurstDurationId, "Exciter NoiseBurst Duration", 0.230769, "ms" },
        { kExciterFrictionPressureId,   "Exciter Friction Pressure",   0.3,      nullptr },
        { kToneShaperFilterTypeId,      "Tone Shaper Filter Type",     0.0,      nullptr },
        { kToneShaperFilterCutoffId,    "Tone Shaper Filter Cutoff",   1.0,      "Hz" },
        { kToneShaperFilterResonanceId, "Tone Shaper Filter Resonance",0.0,      nullptr },
        { kToneShaperFilterEnvAmountId, "Tone Shaper Filter Env Amt",  0.5,      nullptr },
        { kToneShaperDriveAmountId,     "Tone Shaper Drive",           0.0,      nullptr },
        { kToneShaperFoldAmountId,      "Tone Shaper Fold",            0.0,      nullptr },
        { kToneShaperPitchEnvStartId,   "Tone Shaper PitchEnv Start",  0.070721, "Hz" },
        { kToneShaperPitchEnvEndId,     "Tone Shaper PitchEnv End",    0.0,      "Hz" },
        { kToneShaperPitchEnvTimeId,    "Tone Shaper PitchEnv Time",   0.0,      "ms" },
        { kToneShaperPitchEnvCurveId,   "Tone Shaper PitchEnv Curve",  0.0,      nullptr },
        { kToneShaperFilterEnvAttackId,  "Tone Shaper Filter Atk",     0.0,      "ms" },
        { kToneShaperFilterEnvDecayId,   "Tone Shaper Filter Dec",     0.1,      "ms" },
        { kToneShaperFilterEnvSustainId, "Tone Shaper Filter Sus",     0.0,      nullptr },
        { kToneShaperFilterEnvReleaseId, "Tone Shaper Filter Rel",     0.1,      "ms" },
        { kUnnaturalModeStretchId,       "Mode Stretch",               0.333333, nullptr },
        { kUnnaturalDecaySkewId,         "Decay Skew",                 0.5,      nullptr },
        { kUnnaturalModeInjectAmountId,  "Mode Inject",                0.0,      nullptr },
        { kUnnaturalNonlinearCouplingId, "Nonlinear Coupling",         0.0,      nullptr },
        { kMorphEnabledId,    "Morph Enabled",   0.0,      nullptr },
        { kMorphStartId,      "Morph Start",     1.0,      nullptr },
        { kMorphEndId,        "Morph End",       0.0,      nullptr },
        { kMorphDurationMsId, "Morph Duration",  0.095477, "ms" },
        { kMorphCurveId,      "Morph Curve",     0.0,      nullptr },
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

    // ---- Phase 4: 1152 per-pad parameters (32 pads x 36 active offsets) ----
    for (int pad = 0; pad < kNumPads; ++pad)
    {
        for (int specIdx = 0; specIdx < kPadParamSpecCount; ++specIdx)
        {
            const auto& spec = kPadParamSpecs[specIdx];
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
    for (int i = 0; i < kProxyMappingCount; ++i)
    {
        if (kProxyMappings[i].globalId == tag)
        {
            const auto padId = static_cast<ParamID>(
                padParamId(selectedPadIndex_, kProxyMappings[i].padOffset));
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
// Proxy helpers
// ==============================================================================

void Controller::syncGlobalProxyFromPad(int padIndex)
{
    suppressProxyForward_ = true;
    for (int i = 0; i < kProxyMappingCount; ++i)
    {
        const auto padId = static_cast<ParamID>(
            padParamId(padIndex, kProxyMappings[i].padOffset));
        const double padValue = getParamNormalized(padId);
        EditControllerEx1::setParamNormalized(kProxyMappings[i].globalId, padValue);
    }
    suppressProxyForward_ = false;
}

void Controller::forwardGlobalToPad(ParamID globalId, ParamValue value)
{
    for (int i = 0; i < kProxyMappingCount; ++i)
    {
        if (kProxyMappings[i].globalId == globalId)
        {
            const auto padId = static_cast<ParamID>(
                padParamId(selectedPadIndex_, kProxyMappings[i].padOffset));
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
            for (int j = 0; j < 34; ++j)
            {
                if (state->read(&vals[j], sizeof(vals[j]), nullptr) != kResultOk)
                    vals[j] = 0.0;
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
            { kExciterFMRatioId, 0.133333 },
            { kExciterFeedbackAmountId, 0.0 },
            { kExciterNoiseBurstDurationId, 0.230769 },
            { kExciterFrictionPressureId, 0.3 },
            { kToneShaperFilterTypeId, 0.0 },
            { kToneShaperFilterCutoffId, 1.0 },
            { kToneShaperFilterResonanceId, 0.0 },
            { kToneShaperFilterEnvAmountId, 0.5 },
            { kToneShaperDriveAmountId, 0.0 },
            { kToneShaperFoldAmountId, 0.0 },
            { kToneShaperPitchEnvStartId, 0.070721 },
            { kToneShaperPitchEnvEndId, 0.0 },
            { kToneShaperPitchEnvTimeId, 0.0 },
            { kToneShaperPitchEnvCurveId, 0.0 },
            { kToneShaperFilterEnvAttackId, 0.0 },
            { kToneShaperFilterEnvDecayId, 0.1 },
            { kToneShaperFilterEnvSustainId, 0.0 },
            { kToneShaperFilterEnvReleaseId, 0.1 },
            { kUnnaturalModeStretchId, 0.333333 },
            { kUnnaturalDecaySkewId, 0.5 },
            { kUnnaturalModeInjectAmountId, 0.0 },
            { kUnnaturalNonlinearCouplingId, 0.0 },
            { kMorphEnabledId, 0.0 },
            { kMorphStartId, 1.0 },
            { kMorphEndId, 0.0 },
            { kMorphDurationMsId, 0.095477 },
            { kMorphCurveId, 0.0 },
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
        for (int j = 0; j < 28; ++j)
        {
            double v = getParamNormalized(
                static_cast<ParamID>(padParamId(pad, continuousOffsets[j])));
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
        for (int j = 0; j < 4; ++j)
        {
            double v = getParamNormalized(
                static_cast<ParamID>(padParamId(pad, secOffsets[j])));
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
        for (int j = 0; j < 34; ++j)
        {
            if (stream->read(&vals[j], sizeof(vals[j]), nullptr) != kResultOk)
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

IPlugView* PLUGIN_API Controller::createView(const char* /*name*/)
{
    return nullptr;
}

} // namespace Membrum
