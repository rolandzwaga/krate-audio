// ==============================================================================
// Membrum Controller -- Phase 2 parameter registration (34 parameters)
// ==============================================================================

#include "controller.h"
#include "plugin_ids.h"
#include "dsp/exciter_type.h"
#include "dsp/body_model_type.h"

#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/base/ibstream.h"

#include <algorithm>

namespace Membrum {

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

// The Phase 2 state layout stores continuous floats in this exact order.
// Must match processor.cpp's kPhase2FloatSlots table.
struct Phase2Slot
{
    ParamID id;
    double  defaultValue;
};

constexpr Phase2Slot kPhase2Slots[] = {
    { kExciterFMRatioId,            0.133333 },
    { kExciterFeedbackAmountId,     0.0 },
    { kExciterNoiseBurstDurationId, 0.230769 },
    { kExciterFrictionPressureId,   0.3 },

    { kToneShaperFilterTypeId,       0.0 },
    { kToneShaperFilterCutoffId,     1.0 },
    { kToneShaperFilterResonanceId,  0.0 },
    { kToneShaperFilterEnvAmountId,  0.5 },
    { kToneShaperDriveAmountId,      0.0 },
    { kToneShaperFoldAmountId,       0.0 },
    { kToneShaperPitchEnvStartId,    0.070721 },
    { kToneShaperPitchEnvEndId,      0.0 },
    { kToneShaperPitchEnvTimeId,     0.0 },
    { kToneShaperPitchEnvCurveId,    0.0 },

    { kToneShaperFilterEnvAttackId,  0.0 },
    { kToneShaperFilterEnvDecayId,   0.1 },
    { kToneShaperFilterEnvSustainId, 0.0 },
    { kToneShaperFilterEnvReleaseId, 0.1 },

    { kUnnaturalModeStretchId,       0.333333 },
    { kUnnaturalDecaySkewId,         0.5 },
    { kUnnaturalModeInjectAmountId,  0.0 },
    { kUnnaturalNonlinearCouplingId, 0.0 },

    { kMorphEnabledId,    0.0 },
    { kMorphStartId,      1.0 },
    { kMorphEndId,        0.0 },
    { kMorphDurationMsId, 0.095477 },
    { kMorphCurveId,      0.0 },
};

constexpr int kPhase2SlotCount =
    static_cast<int>(sizeof(kPhase2Slots) / sizeof(kPhase2Slots[0]));

} // namespace

tresult PLUGIN_API Controller::initialize(FUnknown* context)
{
    auto result = EditControllerEx1::initialize(context);
    if (result != kResultOk)
        return result;

    // ---- Phase 1 parameters (unchanged) ----
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

    // ---- Phase 2 selectors (StringListParameter) ----
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

    // ---- Phase 2 continuous parameters ----
    // All registered as RangeParameter on [0, 1]. Denormalization to physical
    // units happens inside the Phase 2 DSP components in later phases.
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

        { kToneShaperFilterEnvAttackId,  "Tone Shaper Filter Atk",  0.0, "ms" },
        { kToneShaperFilterEnvDecayId,   "Tone Shaper Filter Dec",  0.1, "ms" },
        { kToneShaperFilterEnvSustainId, "Tone Shaper Filter Sus",  0.0, nullptr },
        { kToneShaperFilterEnvReleaseId, "Tone Shaper Filter Rel",  0.1, "ms" },

        { kUnnaturalModeStretchId,       "Mode Stretch",          0.333333, nullptr },
        { kUnnaturalDecaySkewId,         "Decay Skew",            0.5,      nullptr },
        { kUnnaturalModeInjectAmountId,  "Mode Inject",           0.0,      nullptr },
        { kUnnaturalNonlinearCouplingId, "Nonlinear Coupling",    0.0,      nullptr },

        { kMorphEnabledId,    "Morph Enabled",   0.0,      nullptr },
        { kMorphStartId,      "Morph Start",     1.0,      nullptr },
        { kMorphEndId,        "Morph End",       0.0,      nullptr },
        { kMorphDurationMsId, "Morph Duration",  0.095477, "ms" },
        { kMorphCurveId,      "Morph Curve",     0.0,      nullptr },
    };

    constexpr int kPhase2SpecCount =
        static_cast<int>(sizeof(kPhase2Specs) / sizeof(kPhase2Specs[0]));
    static_assert(kPhase2SpecCount == 27,
                  "Phase 2 exposes 27 continuous parameters + 2 selectors = 29 total");

    auto toTChar = [](const char* s) {
        // Simple static buffer; StringListParameter / RangeParameter copy their
        // name so a temporary works. For thread safety we use a per-call array.
        static constexpr int kMaxLen = 64;
        // NOTE: this helper relies on the target APIs copying the string data.
        static thread_local Steinberg::Vst::TChar buf[kMaxLen];
        int i = 0;
        for (; i < kMaxLen - 1 && s[i] != '\0'; ++i)
            buf[i] = static_cast<Steinberg::Vst::TChar>(s[i]);
        buf[i] = 0;
        return buf;
    };

    for (const auto& spec : kPhase2Specs)
    {
        Steinberg::Vst::TChar titleBuf[64];
        int i = 0;
        for (; i < 63 && spec.name[i] != '\0'; ++i)
            titleBuf[i] = static_cast<Steinberg::Vst::TChar>(spec.name[i]);
        titleBuf[i] = 0;

        Steinberg::Vst::TChar unitBuf[16] = {0};
        const Steinberg::Vst::TChar* unitPtr = nullptr;
        if (spec.unit != nullptr)
        {
            int u = 0;
            for (; u < 15 && spec.unit[u] != '\0'; ++u)
                unitBuf[u] = static_cast<Steinberg::Vst::TChar>(spec.unit[u]);
            unitBuf[u] = 0;
            unitPtr = unitBuf;
        }

        parameters.addParameter(
            new RangeParameter(titleBuf, spec.id, unitPtr,
                               0.0, 1.0, spec.defaultValue, 0,
                               ParameterInfo::kCanAutomate));
    }

    (void)toTChar;  // helper kept as a reference for any future StringListParameter use

    return kResultOk;
}

// ==============================================================================
// setComponentState -- Phase 2 state layout (see processor.cpp)
// ==============================================================================
tresult PLUGIN_API Controller::setComponentState(IBStream* state)
{
    if (!state)
        return kResultFalse;

    int32 version = 0;
    if (state->read(&version, sizeof(version), nullptr) != kResultOk)
        return kResultFalse;

    // Phase 1 params
    const ParamID phase1Ids[] = {
        kMaterialId, kSizeId, kDecayId, kStrikePositionId, kLevelId};
    const double phase1Defaults[] = {0.5, 0.5, 0.3, 0.3, 0.8};

    for (int i = 0; i < 5; ++i)
    {
        double value = phase1Defaults[i];
        if (state->read(&value, sizeof(value), nullptr) != kResultOk)
        {
            setParamNormalized(phase1Ids[i], phase1Defaults[i]);
            continue;
        }
        setParamNormalized(phase1Ids[i], value);
    }

    if (version >= 2)
    {
        // Selectors (int32) → converted to normalized via (idx + 0.5) / kCount
        int32 exciterTypeI32 = 0;
        int32 bodyModelI32 = 0;
        if (state->read(&exciterTypeI32, sizeof(exciterTypeI32), nullptr) == kResultOk)
        {
            const double norm =
                (std::clamp(exciterTypeI32, 0,
                            static_cast<int>(ExciterType::kCount) - 1) + 0.5) /
                static_cast<double>(ExciterType::kCount);
            setParamNormalized(kExciterTypeId, norm);
        }
        if (state->read(&bodyModelI32, sizeof(bodyModelI32), nullptr) == kResultOk)
        {
            const double norm =
                (std::clamp(bodyModelI32, 0,
                            static_cast<int>(BodyModelType::kCount) - 1) + 0.5) /
                static_cast<double>(BodyModelType::kCount);
            setParamNormalized(kBodyModelId, norm);
        }

        for (int i = 0; i < kPhase2SlotCount; ++i)
        {
            double value = kPhase2Slots[i].defaultValue;
            if (state->read(&value, sizeof(value), nullptr) != kResultOk)
            {
                setParamNormalized(kPhase2Slots[i].id, kPhase2Slots[i].defaultValue);
                continue;
            }
            setParamNormalized(kPhase2Slots[i].id, value);
        }
    }
    else
    {
        // Phase 1 blob -- fill Phase 2 with defaults.
        setParamNormalized(kExciterTypeId, 0.5 / static_cast<double>(ExciterType::kCount));
        setParamNormalized(kBodyModelId,   0.5 / static_cast<double>(BodyModelType::kCount));
        for (int i = 0; i < kPhase2SlotCount; ++i)
            setParamNormalized(kPhase2Slots[i].id, kPhase2Slots[i].defaultValue);
    }

    return kResultOk;
}

IPlugView* PLUGIN_API Controller::createView(const char* /*name*/)
{
    return nullptr;
}

} // namespace Membrum
