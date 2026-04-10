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
    { .id = kExciterFMRatioId,            .defaultValue = 0.133333 },
    { .id = kExciterFeedbackAmountId,     .defaultValue = 0.0 },
    { .id = kExciterNoiseBurstDurationId, .defaultValue = 0.230769 },
    { .id = kExciterFrictionPressureId,   .defaultValue = 0.3 },

    { .id = kToneShaperFilterTypeId,       .defaultValue = 0.0 },
    { .id = kToneShaperFilterCutoffId,     .defaultValue = 1.0 },
    { .id = kToneShaperFilterResonanceId,  .defaultValue = 0.0 },
    { .id = kToneShaperFilterEnvAmountId,  .defaultValue = 0.5 },
    { .id = kToneShaperDriveAmountId,      .defaultValue = 0.0 },
    { .id = kToneShaperFoldAmountId,       .defaultValue = 0.0 },
    { .id = kToneShaperPitchEnvStartId,    .defaultValue = 0.070721 },
    { .id = kToneShaperPitchEnvEndId,      .defaultValue = 0.0 },
    { .id = kToneShaperPitchEnvTimeId,     .defaultValue = 0.0 },
    { .id = kToneShaperPitchEnvCurveId,    .defaultValue = 0.0 },

    { .id = kToneShaperFilterEnvAttackId,  .defaultValue = 0.0 },
    { .id = kToneShaperFilterEnvDecayId,   .defaultValue = 0.1 },
    { .id = kToneShaperFilterEnvSustainId, .defaultValue = 0.0 },
    { .id = kToneShaperFilterEnvReleaseId, .defaultValue = 0.1 },

    { .id = kUnnaturalModeStretchId,       .defaultValue = 0.333333 },
    { .id = kUnnaturalDecaySkewId,         .defaultValue = 0.5 },
    { .id = kUnnaturalModeInjectAmountId,  .defaultValue = 0.0 },
    { .id = kUnnaturalNonlinearCouplingId, .defaultValue = 0.0 },

    { .id = kMorphEnabledId,    .defaultValue = 0.0 },
    { .id = kMorphStartId,      .defaultValue = 1.0 },
    { .id = kMorphEndId,        .defaultValue = 0.0 },
    { .id = kMorphDurationMsId, .defaultValue = 0.095477 },
    { .id = kMorphCurveId,      .defaultValue = 0.0 },
};

static_assert(sizeof(kPhase2Slots) / sizeof(kPhase2Slots[0]) == 27,
              "Phase 2 is expected to expose 27 continuous parameters "
              "(29 total minus the 2 integer selectors).");

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
        { .id = kExciterFMRatioId,            .name = "Exciter FM Ratio",            .defaultValue = 0.133333, .unit = nullptr },
        { .id = kExciterFeedbackAmountId,     .name = "Exciter Feedback Amount",     .defaultValue = 0.0,      .unit = nullptr },
        { .id = kExciterNoiseBurstDurationId, .name = "Exciter NoiseBurst Duration", .defaultValue = 0.230769, .unit = "ms" },
        { .id = kExciterFrictionPressureId,   .name = "Exciter Friction Pressure",   .defaultValue = 0.3,      .unit = nullptr },

        { .id = kToneShaperFilterTypeId,      .name = "Tone Shaper Filter Type",     .defaultValue = 0.0,      .unit = nullptr },
        { .id = kToneShaperFilterCutoffId,    .name = "Tone Shaper Filter Cutoff",   .defaultValue = 1.0,      .unit = "Hz" },
        { .id = kToneShaperFilterResonanceId, .name = "Tone Shaper Filter Resonance",.defaultValue = 0.0,      .unit = nullptr },
        { .id = kToneShaperFilterEnvAmountId, .name = "Tone Shaper Filter Env Amt",  .defaultValue = 0.5,      .unit = nullptr },
        { .id = kToneShaperDriveAmountId,     .name = "Tone Shaper Drive",           .defaultValue = 0.0,      .unit = nullptr },
        { .id = kToneShaperFoldAmountId,      .name = "Tone Shaper Fold",            .defaultValue = 0.0,      .unit = nullptr },
        { .id = kToneShaperPitchEnvStartId,   .name = "Tone Shaper PitchEnv Start",  .defaultValue = 0.070721, .unit = "Hz" },
        { .id = kToneShaperPitchEnvEndId,     .name = "Tone Shaper PitchEnv End",    .defaultValue = 0.0,      .unit = "Hz" },
        { .id = kToneShaperPitchEnvTimeId,    .name = "Tone Shaper PitchEnv Time",   .defaultValue = 0.0,      .unit = "ms" },
        { .id = kToneShaperPitchEnvCurveId,   .name = "Tone Shaper PitchEnv Curve",  .defaultValue = 0.0,      .unit = nullptr },

        { .id = kToneShaperFilterEnvAttackId,  .name = "Tone Shaper Filter Atk",  .defaultValue = 0.0, .unit = "ms" },
        { .id = kToneShaperFilterEnvDecayId,   .name = "Tone Shaper Filter Dec",  .defaultValue = 0.1, .unit = "ms" },
        { .id = kToneShaperFilterEnvSustainId, .name = "Tone Shaper Filter Sus",  .defaultValue = 0.0, .unit = nullptr },
        { .id = kToneShaperFilterEnvReleaseId, .name = "Tone Shaper Filter Rel",  .defaultValue = 0.1, .unit = "ms" },

        { .id = kUnnaturalModeStretchId,       .name = "Mode Stretch",          .defaultValue = 0.333333, .unit = nullptr },
        { .id = kUnnaturalDecaySkewId,         .name = "Decay Skew",            .defaultValue = 0.5,      .unit = nullptr },
        { .id = kUnnaturalModeInjectAmountId,  .name = "Mode Inject",           .defaultValue = 0.0,      .unit = nullptr },
        { .id = kUnnaturalNonlinearCouplingId, .name = "Nonlinear Coupling",    .defaultValue = 0.0,      .unit = nullptr },

        { .id = kMorphEnabledId,    .name = "Morph Enabled",   .defaultValue = 0.0,      .unit = nullptr },
        { .id = kMorphStartId,      .name = "Morph Start",     .defaultValue = 1.0,      .unit = nullptr },
        { .id = kMorphEndId,        .name = "Morph End",       .defaultValue = 0.0,      .unit = nullptr },
        { .id = kMorphDurationMsId, .name = "Morph Duration",  .defaultValue = 0.095477, .unit = "ms" },
        { .id = kMorphCurveId,      .name = "Morph Curve",     .defaultValue = 0.0,      .unit = nullptr },
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

        for (const auto& slot : kPhase2Slots)
        {
            double value = slot.defaultValue;
            if (state->read(&value, sizeof(value), nullptr) != kResultOk)
            {
                setParamNormalized(slot.id, slot.defaultValue);
                continue;
            }
            setParamNormalized(slot.id, value);
        }
    }
    else
    {
        // Phase 1 blob -- fill Phase 2 with defaults.
        setParamNormalized(kExciterTypeId, 0.5 / static_cast<double>(ExciterType::kCount));
        setParamNormalized(kBodyModelId,   0.5 / static_cast<double>(BodyModelType::kCount));
        for (const auto& slot : kPhase2Slots)
            setParamNormalized(slot.id, slot.defaultValue);
    }

    return kResultOk;
}

IPlugView* PLUGIN_API Controller::createView(const char* /*name*/)
{
    return nullptr;
}

} // namespace Membrum
