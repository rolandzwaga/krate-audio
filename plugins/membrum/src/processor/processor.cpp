// ==============================================================================
// Membrum Processor -- Phase 2
// ==============================================================================

#include "processor.h"
#include "plugin_ids.h"
#include "dsp/exciter_type.h"
#include "dsp/body_model_type.h"
#include "dsp/tone_shaper.h"
#include "voice_pool/choke_group_table.h"
#include "voice_pool/voice_stealing_policy.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

// FTZ/DAZ for denormal prevention on x86 (SC-007)
#if defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)
#include <xmmintrin.h> // _MM_SET_FLUSH_ZERO_MODE
#include <pmmintrin.h> // _MM_SET_DENORMALS_ZERO_MODE
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Membrum {

using namespace Steinberg;
using namespace Steinberg::Vst;

// ==============================================================================
// Phase 2 continuous-parameter order in the state stream.
// Must stay in sync with data-model.md §10 and getState/setState below.
// ==============================================================================
namespace {

struct Phase2FloatSlot
{
    ParamID      id;
    float        defaultValue;
};

// Defaults here are normalized (0..1) values chosen so that loading a
// Phase 1 blob (version == 1) produces "all Phase 2 bypass" behaviour.
constexpr Phase2FloatSlot kPhase2FloatSlots[] = {
    { .id = kExciterFMRatioId,            .defaultValue = 0.133333f },
    { .id = kExciterFeedbackAmountId,     .defaultValue = 0.0f },
    { .id = kExciterNoiseBurstDurationId, .defaultValue = 0.230769f },
    { .id = kExciterFrictionPressureId,   .defaultValue = 0.3f },

    { .id = kToneShaperFilterTypeId,       .defaultValue = 0.0f },
    { .id = kToneShaperFilterCutoffId,     .defaultValue = 1.0f },
    { .id = kToneShaperFilterResonanceId,  .defaultValue = 0.0f },
    { .id = kToneShaperFilterEnvAmountId,  .defaultValue = 0.5f },
    { .id = kToneShaperDriveAmountId,      .defaultValue = 0.0f },
    { .id = kToneShaperFoldAmountId,       .defaultValue = 0.0f },
    { .id = kToneShaperPitchEnvStartId,    .defaultValue = 0.070721f },
    { .id = kToneShaperPitchEnvEndId,      .defaultValue = 0.0f },
    { .id = kToneShaperPitchEnvTimeId,     .defaultValue = 0.0f },
    { .id = kToneShaperPitchEnvCurveId,    .defaultValue = 0.0f },

    { .id = kToneShaperFilterEnvAttackId,  .defaultValue = 0.0f },
    { .id = kToneShaperFilterEnvDecayId,   .defaultValue = 0.1f },
    { .id = kToneShaperFilterEnvSustainId, .defaultValue = 0.0f },
    { .id = kToneShaperFilterEnvReleaseId, .defaultValue = 0.1f },

    { .id = kUnnaturalModeStretchId,       .defaultValue = 0.333333f },
    { .id = kUnnaturalDecaySkewId,         .defaultValue = 0.5f },
    { .id = kUnnaturalModeInjectAmountId,  .defaultValue = 0.0f },
    { .id = kUnnaturalNonlinearCouplingId, .defaultValue = 0.0f },

    { .id = kMorphEnabledId,    .defaultValue = 0.0f },
    { .id = kMorphStartId,      .defaultValue = 1.0f },
    { .id = kMorphEndId,        .defaultValue = 0.0f },
    { .id = kMorphDurationMsId, .defaultValue = 0.095477f },
    { .id = kMorphCurveId,      .defaultValue = 0.0f },
};

constexpr int kPhase2FloatSlotCount =
    static_cast<int>(sizeof(kPhase2FloatSlots) / sizeof(kPhase2FloatSlots[0]));

static_assert(kPhase2FloatSlotCount == 27,
              "Phase 2 is expected to expose 27 continuous float parameters "
              "(29 total minus the 2 integer selectors).");

} // namespace

Processor::Processor()
{
    setControllerClass(kControllerUID);
}

tresult PLUGIN_API Processor::initialize(FUnknown* context)
{
    auto result = AudioEffect::initialize(context);
    if (result != kResultOk)
        return result;

    addEventInput(STR16("Event In"));
    addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);

    return kResultOk;
}

// ------------------------------------------------------------------
// Helper: write a Phase 2 float slot's atomic from a normalized value.
// ------------------------------------------------------------------
namespace {

void writePhase2FloatAtomic(Processor& /*processor*/,
                            std::atomic<float>*& outAtomic,
                            ParamID id,
                            // The entire set of Phase 2 float atomics, in slot order.
                            std::atomic<float>* slots) noexcept
{
    for (int i = 0; i < kPhase2FloatSlotCount; ++i)
    {
        if (kPhase2FloatSlots[i].id == id)
        {
            outAtomic = &slots[i];
            return;
        }
    }
    outAtomic = nullptr;
}

} // namespace

// FR-015: Process parameter changes from host
void Processor::processParameterChanges(IParameterChanges* paramChanges)
{
    if (!paramChanges)
        return;

    // Gather pointers to all Phase 2 float atomics in the same order as
    // kPhase2FloatSlots so we can look them up by slot index.
    std::atomic<float>* phase2Slots[kPhase2FloatSlotCount] = {
        &exciterFMRatio_,
        &exciterFeedbackAmount_,
        &exciterNoiseBurstDuration_,
        &exciterFrictionPressure_,

        &toneShaperFilterType_,
        &toneShaperFilterCutoff_,
        &toneShaperFilterResonance_,
        &toneShaperFilterEnvAmount_,
        &toneShaperDriveAmount_,
        &toneShaperFoldAmount_,
        &toneShaperPitchEnvStart_,
        &toneShaperPitchEnvEnd_,
        &toneShaperPitchEnvTime_,
        &toneShaperPitchEnvCurve_,

        &toneShaperFilterEnvAttack_,
        &toneShaperFilterEnvDecay_,
        &toneShaperFilterEnvSustain_,
        &toneShaperFilterEnvRelease_,

        &unnaturalModeStretch_,
        &unnaturalDecaySkew_,
        &unnaturalModeInjectAmount_,
        &unnaturalNonlinearCoupling_,

        &morphEnabled_,
        &morphStart_,
        &morphEnd_,
        &morphDurationMs_,
        &morphCurve_,
    };

    int32 numParams = paramChanges->getParameterCount();
    for (int32 i = 0; i < numParams; ++i)
    {
        auto* queue = paramChanges->getParameterData(i);
        if (!queue)
            continue;

        ParamID paramId = queue->getParameterId();
        int32 numPoints = queue->getPointCount();
        if (numPoints <= 0)
            continue;

        // Use the last point value (most recent)
        ParamValue value = 0.0;
        int32 sampleOffset = 0;
        if (queue->getPoint(numPoints - 1, sampleOffset, value) != kResultOk)
            continue;

        const float fValue = static_cast<float>(value);

        switch (paramId)
        {
        // ---- Phase 1 ----
        case kMaterialId:
            material_.store(fValue);
            voicePool_.forEachMainVoice([=](DrumVoice& v) noexcept { v.setMaterial(fValue); });
            voicePool_.setSharedVoiceParams(
                material_.load(), size_.load(), decay_.load(),
                strikePosition_.load(), level_.load());
            break;
        case kSizeId:
            size_.store(fValue);
            voicePool_.forEachMainVoice([=](DrumVoice& v) noexcept { v.setSize(fValue); });
            voicePool_.setSharedVoiceParams(
                material_.load(), size_.load(), decay_.load(),
                strikePosition_.load(), level_.load());
            break;
        case kDecayId:
            decay_.store(fValue);
            voicePool_.forEachMainVoice([=](DrumVoice& v) noexcept { v.setDecay(fValue); });
            voicePool_.setSharedVoiceParams(
                material_.load(), size_.load(), decay_.load(),
                strikePosition_.load(), level_.load());
            break;
        case kStrikePositionId:
            strikePosition_.store(fValue);
            voicePool_.forEachMainVoice([=](DrumVoice& v) noexcept { v.setStrikePosition(fValue); });
            voicePool_.setSharedVoiceParams(
                material_.load(), size_.load(), decay_.load(),
                strikePosition_.load(), level_.load());
            break;
        case kLevelId:
            level_.store(fValue);
            voicePool_.forEachMainVoice([=](DrumVoice& v) noexcept { v.setLevel(fValue); });
            voicePool_.setSharedVoiceParams(
                material_.load(), size_.load(), decay_.load(),
                strikePosition_.load(), level_.load());
            break;

        // ---- Phase 2 selectors ----
        case kExciterTypeId:
        {
            const int idx = std::clamp(
                static_cast<int>(value * static_cast<double>(ExciterType::kCount)),
                0,
                static_cast<int>(ExciterType::kCount) - 1);
            exciterType_.store(idx);
            const auto typeEnum = static_cast<ExciterType>(idx);
            voicePool_.forEachMainVoice([=](DrumVoice& v) noexcept { v.setExciterType(typeEnum); });
            voicePool_.setSharedExciterType(typeEnum);
            break;
        }
        case kBodyModelId:
        {
            const int idx = std::clamp(
                static_cast<int>(value * static_cast<double>(BodyModelType::kCount)),
                0,
                static_cast<int>(BodyModelType::kCount) - 1);
            bodyModel_.store(idx);
            const auto modelEnum = static_cast<BodyModelType>(idx);
            voicePool_.forEachMainVoice([=](DrumVoice& v) noexcept { v.setBodyModel(modelEnum); });
            voicePool_.setSharedBodyModel(modelEnum);
            break;
        }

        // ---- Phase 3 ----
        case kMaxPolyphonyId:
        {
            // Stepped RangeParameter [4, 16]: normalized value covers 12 steps.
            const float clamped = std::clamp(fValue, 0.0f, 1.0f);
            const int n = 4 + static_cast<int>(std::lround(clamped * 12.0f));
            const int nClamped = std::clamp(n, 4, 16);
            maxPolyphony_.store(nClamped);
            voicePool_.setMaxPolyphony(nClamped);
            break;
        }
        case kVoiceStealingId:
        {
            // StringListParameter with 3 choices: Oldest/Quietest/Priority.
            const float clamped = std::clamp(fValue, 0.0f, 1.0f);
            const int idx = std::clamp(static_cast<int>(clamped * 3.0f), 0, 2);
            voiceStealingPolicy_.store(idx);
            voicePool_.setVoiceStealingPolicy(
                static_cast<VoiceStealingPolicy>(idx));
            break;
        }
        case kChokeGroupId:
        {
            // Stepped RangeParameter [0, 8]: 9 steps.
            const float clamped = std::clamp(fValue, 0.0f, 1.0f);
            const int n = static_cast<int>(std::lround(clamped * 8.0f));
            const int nClamped = std::clamp(n, 0, 8);
            chokeGroup_.store(nClamped);
            voicePool_.setChokeGroup(static_cast<std::uint8_t>(nClamped));
            break;
        }

        default:
        {
            // ---- Phase 2 continuous float params ----
            // Store into the appropriate atomic; the DrumVoice sub-components
            // currently consume these through their stub setters which are
            // no-ops in Phase 2.A but keep parameter values reachable for
            // state round-trip (SC-006).
            for (int slot = 0; slot < kPhase2FloatSlotCount; ++slot)
            {
                if (kPhase2FloatSlots[slot].id == paramId)
                {
                    phase2Slots[slot]->store(fValue);

                    // Broadcast every Phase 2 param to every main voice slot
                    // so the Phase 2 regression path (maxPolyphony=1, slot 0)
                    // is bit-identical to Phase 2 and every sounding voice at
                    // higher polyphony sees the same edit.
                    voicePool_.forEachMainVoice([=](DrumVoice& v) noexcept {
                    switch (paramId)
                    {
                    // ---- Unnatural Zone (Phase 8, T114) ----
                    // Denormalize per spec ranges (data-model.md §9):
                    //   modeStretch [0.5, 2.0] linear
                    //   decaySkew   [-1.0, +1.0] linear
                    //   morph duration [10, 2000] ms
                    case kUnnaturalModeStretchId:
                    {
                        const float denorm = 0.5f + std::clamp(fValue, 0.0f, 1.0f) * 1.5f;
                        v.unnaturalZone().setModeStretch(denorm);
                        break;
                    }
                    case kUnnaturalDecaySkewId:
                    {
                        const float denorm = std::clamp(fValue, 0.0f, 1.0f) * 2.0f - 1.0f;
                        v.unnaturalZone().setDecaySkew(denorm);
                        break;
                    }
                    case kUnnaturalModeInjectAmountId:
                        v.unnaturalZone().modeInject.setAmount(
                            std::clamp(fValue, 0.0f, 1.0f));
                        break;
                    case kUnnaturalNonlinearCouplingId:
                        v.unnaturalZone().nonlinearCoupling.setAmount(
                            std::clamp(fValue, 0.0f, 1.0f));
                        break;

                    // ---- Material Morph (Phase 8, T114) ----
                    case kMorphEnabledId:
                        v.unnaturalZone().materialMorph.setEnabled(fValue >= 0.5f);
                        break;
                    case kMorphStartId:
                        v.unnaturalZone().materialMorph.setStart(
                            std::clamp(fValue, 0.0f, 1.0f));
                        break;
                    case kMorphEndId:
                        v.unnaturalZone().materialMorph.setEnd(
                            std::clamp(fValue, 0.0f, 1.0f));
                        break;
                    case kMorphDurationMsId:
                    {
                        // [10, 2000] ms linear
                        const float ms = 10.0f + std::clamp(fValue, 0.0f, 1.0f) * 1990.0f;
                        v.unnaturalZone().materialMorph.setDurationMs(ms);
                        break;
                    }
                    case kMorphCurveId:
                        // 0 = Linear, 1 = Exponential
                        v.unnaturalZone().materialMorph.setCurve(fValue >= 0.5f);
                        break;

                    // ---- Tone Shaper (Phase 7, T097) ----
                    case kToneShaperFilterTypeId:
                    {
                        // 0..1 normalized -> LP/HP/BP (3 discrete values).
                        const int typeIdx = std::clamp(static_cast<int>(fValue * 3.0f), 0, 2);
                        v.toneShaper().setFilterType(static_cast<ToneShaperFilterType>(typeIdx));
                        break;
                    }
                    case kToneShaperFilterCutoffId:
                    {
                        // Log scale: 20 Hz .. 20000 Hz (3 decades).
                        const float hz = 20.0f * std::pow(1000.0f, std::clamp(fValue, 0.0f, 1.0f));
                        v.toneShaper().setFilterCutoff(hz);
                        break;
                    }
                    case kToneShaperFilterResonanceId:
                        v.toneShaper().setFilterResonance(fValue);
                        break;
                    case kToneShaperFilterEnvAmountId:
                        // Normalized 0..1 -> -1..+1 bipolar.
                        v.toneShaper().setFilterEnvAmount(fValue * 2.0f - 1.0f);
                        break;
                    case kToneShaperFilterEnvAttackId:
                        // 0..500 ms
                        v.toneShaper().setFilterEnvAttackMs(fValue * 500.0f);
                        break;
                    case kToneShaperFilterEnvDecayId:
                        // 0..2000 ms
                        v.toneShaper().setFilterEnvDecayMs(fValue * 2000.0f);
                        break;
                    case kToneShaperFilterEnvSustainId:
                        v.toneShaper().setFilterEnvSustain(fValue);
                        break;
                    case kToneShaperFilterEnvReleaseId:
                        // 0..2000 ms
                        v.toneShaper().setFilterEnvReleaseMs(fValue * 2000.0f);
                        break;
                    case kToneShaperDriveAmountId:
                        v.toneShaper().setDriveAmount(fValue);
                        break;
                    case kToneShaperFoldAmountId:
                        v.toneShaper().setFoldAmount(fValue);
                        break;
                    case kToneShaperPitchEnvStartId:
                    {
                        // Log scale: 20 Hz .. 2000 Hz
                        const float hz = 20.0f * std::pow(100.0f, std::clamp(fValue, 0.0f, 1.0f));
                        v.toneShaper().setPitchEnvStartHz(hz);
                        break;
                    }
                    case kToneShaperPitchEnvEndId:
                    {
                        // Log scale: 20 Hz .. 2000 Hz
                        const float hz = 20.0f * std::pow(100.0f, std::clamp(fValue, 0.0f, 1.0f));
                        v.toneShaper().setPitchEnvEndHz(hz);
                        break;
                    }
                    case kToneShaperPitchEnvTimeId:
                        // 0..500 ms
                        v.toneShaper().setPitchEnvTimeMs(fValue * 500.0f);
                        break;
                    case kToneShaperPitchEnvCurveId:
                    {
                        const int idx = std::clamp(static_cast<int>(fValue * 2.0f), 0, 1);
                        v.toneShaper().setPitchEnvCurve(static_cast<ToneShaperCurve>(idx));
                        break;
                    }
                    default:
                        break;
                    }
                    });
                    break;
                }
            }
            break;
        }
        }
    }
}

// Process MIDI events: note-on/note-off on any MIDI note in [36, 67]
// (FR-113 -- Phase 3 drops the Phase 1/2 single-note filter).
void Processor::processEvents(IEventList* events)
{
    if (!events)
        return;

    int32 numEvents = events->getEventCount();
    for (int32 i = 0; i < numEvents; ++i)
    {
        Event event{};
        if (events->getEvent(i, event) != kResultOk)
            continue;

        switch (event.type)
        {
        case Event::kNoteOnEvent:
        {
            const int16 pitch = event.noteOn.pitch;
            if (pitch < 36 || pitch > 67)
                break;
            if (event.noteOn.velocity <= 0.0f)
            {
                voicePool_.noteOff(static_cast<std::uint8_t>(pitch));
                break;
            }
            voicePool_.noteOn(static_cast<std::uint8_t>(pitch),
                              event.noteOn.velocity);
            break;
        }
        case Event::kNoteOffEvent:
        {
            const int16 pitch = event.noteOff.pitch;
            if (pitch < 36 || pitch > 67)
                break;
            voicePool_.noteOff(static_cast<std::uint8_t>(pitch));
            break;
        }
        default:
            break;
        }
    }
}

tresult PLUGIN_API Processor::process(ProcessData& data)
{
    if (data.numSamples == 0)
        return kResultOk;

    processParameterChanges(data.inputParameterChanges);
    processEvents(data.inputEvents);

    if (data.numOutputs > 0 && data.outputs != nullptr)
    {
        float* outL = data.outputs[0].channelBuffers32[0];
        float* outR = data.outputs[0].channelBuffers32[1];

        // Phase 3 -- voice pool handles mixing of every active + fast-
        // releasing voice into the stereo output buffer directly.
        voicePool_.processBlock(outL, outR, data.numSamples);

        data.outputs[0].silenceFlags = voicePool_.isAnyVoiceActive() ? 0 : 3;
    }

    return kResultOk;
}

// ==============================================================================
// State (version 2) -- data-model.md §10
// ==============================================================================
// Binary layout:
//   [int32] version
//   [5 x float64] Phase 1 params (material, size, decay, strikePos, level)
//   if version >= 2:
//     [2 x int32]  selectors (exciterType, bodyModel)
//     [27 x float64] Phase 2 continuous params (kPhase2FloatSlots order)
// ==============================================================================

tresult PLUGIN_API Processor::getState(IBStream* state)
{
    if (!state)
        return kResultFalse;

    int32 version = kCurrentStateVersion;
    state->write(&version, sizeof(version), nullptr);

    const double phase1[] = {
        static_cast<double>(material_.load()),
        static_cast<double>(size_.load()),
        static_cast<double>(decay_.load()),
        static_cast<double>(strikePosition_.load()),
        static_cast<double>(level_.load()),
    };
    for (double p : phase1)
        state->write(&p, sizeof(p), nullptr);

    // Phase 2: selectors first, then continuous floats in slot order.
    int32 exciterTypeI32 = exciterType_.load();
    int32 bodyModelI32   = bodyModel_.load();
    state->write(&exciterTypeI32, sizeof(exciterTypeI32), nullptr);
    state->write(&bodyModelI32, sizeof(bodyModelI32), nullptr);

    std::atomic<float>* phase2Slots[kPhase2FloatSlotCount] = {
        &exciterFMRatio_,
        &exciterFeedbackAmount_,
        &exciterNoiseBurstDuration_,
        &exciterFrictionPressure_,
        &toneShaperFilterType_,
        &toneShaperFilterCutoff_,
        &toneShaperFilterResonance_,
        &toneShaperFilterEnvAmount_,
        &toneShaperDriveAmount_,
        &toneShaperFoldAmount_,
        &toneShaperPitchEnvStart_,
        &toneShaperPitchEnvEnd_,
        &toneShaperPitchEnvTime_,
        &toneShaperPitchEnvCurve_,
        &toneShaperFilterEnvAttack_,
        &toneShaperFilterEnvDecay_,
        &toneShaperFilterEnvSustain_,
        &toneShaperFilterEnvRelease_,
        &unnaturalModeStretch_,
        &unnaturalDecaySkew_,
        &unnaturalModeInjectAmount_,
        &unnaturalNonlinearCoupling_,
        &morphEnabled_,
        &morphStart_,
        &morphEnd_,
        &morphDurationMs_,
        &morphCurve_,
    };

    for (auto* slot : phase2Slots)
    {
        double v = static_cast<double>(slot->load());
        state->write(&v, sizeof(v), nullptr);
    }

    // ------------------------------------------------------------------
    // Phase 3 tail (FR-141, Clarification Q1): written unconditionally on
    // every save -- no length prefix, no feature flag, strictly additive.
    //   offset 268: uint8  maxPolyphony          [4, 16]
    //   offset 269: uint8  voiceStealingPolicy   [0, 2]
    //   offset 270: uint8  chokeGroupAssignments[32]
    //   offset 302: END
    // ------------------------------------------------------------------
    {
        std::uint8_t maxPolyByte =
            static_cast<std::uint8_t>(std::clamp(maxPolyphony_.load(), 4, 16));
        state->write(&maxPolyByte, sizeof(maxPolyByte), nullptr);

        const int policyInt = voiceStealingPolicy_.load();
        std::uint8_t policyByte =
            static_cast<std::uint8_t>((policyInt < 0 || policyInt > 2) ? 0 : policyInt);
        state->write(&policyByte, sizeof(policyByte), nullptr);

        const auto chokes = voicePool_.getChokeGroupAssignments();
        for (std::size_t i = 0; i < chokes.size(); ++i)
        {
            std::uint8_t b = chokes[i];
            state->write(&b, sizeof(b), nullptr);
        }
    }

    return kResultOk;
}

tresult PLUGIN_API Processor::setState(IBStream* state)
{
    if (!state)
        return kResultFalse;

    int32 version = 0;
    if (state->read(&version, sizeof(version), nullptr) != kResultOk)
        return kResultFalse;

    // FR-141 / Q1: we know about v1, v2, v3. A future v4+ blob MUST be
    // rejected so we do not silently drop unknown fields.
    if (version > kCurrentStateVersion)
        return kResultFalse;

    // ---- Phase 1 params ----
    const float phase1Defaults[] = {0.5f, 0.5f, 0.3f, 0.3f, 0.8f};
    std::atomic<float>* phase1Atomics[] = {
        &material_, &size_, &decay_, &strikePosition_, &level_};

    for (int i = 0; i < 5; ++i)
    {
        double value = static_cast<double>(phase1Defaults[i]);
        if (state->read(&value, sizeof(value), nullptr) != kResultOk)
        {
            phase1Atomics[i]->store(phase1Defaults[i]);
            continue;
        }
        phase1Atomics[i]->store(static_cast<float>(value));
    }

    // Sync Phase 1 values into every voice so a note-on immediately after
    // setState() uses the restored parameters.
    voicePool_.setSharedVoiceParams(material_.load(), size_.load(), decay_.load(),
                                    strikePosition_.load(), level_.load());
    voicePool_.forEachMainVoice([this](DrumVoice& v) noexcept {
        v.setMaterial(material_.load());
        v.setSize(size_.load());
        v.setDecay(decay_.load());
        v.setStrikePosition(strikePosition_.load());
        v.setLevel(level_.load());
    });

    // ---- Phase 2 params ----
    // Collect Phase 2 float atomics in slot order.
    std::atomic<float>* phase2Slots[kPhase2FloatSlotCount] = {
        &exciterFMRatio_,
        &exciterFeedbackAmount_,
        &exciterNoiseBurstDuration_,
        &exciterFrictionPressure_,
        &toneShaperFilterType_,
        &toneShaperFilterCutoff_,
        &toneShaperFilterResonance_,
        &toneShaperFilterEnvAmount_,
        &toneShaperDriveAmount_,
        &toneShaperFoldAmount_,
        &toneShaperPitchEnvStart_,
        &toneShaperPitchEnvEnd_,
        &toneShaperPitchEnvTime_,
        &toneShaperPitchEnvCurve_,
        &toneShaperFilterEnvAttack_,
        &toneShaperFilterEnvDecay_,
        &toneShaperFilterEnvSustain_,
        &toneShaperFilterEnvRelease_,
        &unnaturalModeStretch_,
        &unnaturalDecaySkew_,
        &unnaturalModeInjectAmount_,
        &unnaturalNonlinearCoupling_,
        &morphEnabled_,
        &morphStart_,
        &morphEnd_,
        &morphDurationMs_,
        &morphCurve_,
    };

    if (version >= 2)
    {
        // Read selectors (int32)
        int32 exciterTypeI32 = 0;
        int32 bodyModelI32   = 0;

        if (state->read(&exciterTypeI32, sizeof(exciterTypeI32), nullptr) != kResultOk)
            exciterTypeI32 = 0;
        if (state->read(&bodyModelI32, sizeof(bodyModelI32), nullptr) != kResultOk)
            bodyModelI32 = 0;

        exciterTypeI32 = std::clamp(exciterTypeI32, 0,
                                    static_cast<int>(ExciterType::kCount) - 1);
        bodyModelI32 = std::clamp(bodyModelI32, 0,
                                  static_cast<int>(BodyModelType::kCount) - 1);

        exciterType_.store(exciterTypeI32);
        bodyModel_.store(bodyModelI32);
        const auto excEnum  = static_cast<ExciterType>(exciterTypeI32);
        const auto bodyEnum = static_cast<BodyModelType>(bodyModelI32);
        voicePool_.setSharedExciterType(excEnum);
        voicePool_.setSharedBodyModel(bodyEnum);
        voicePool_.forEachMainVoice([=](DrumVoice& v) noexcept {
            v.setExciterType(excEnum);
            v.setBodyModel(bodyEnum);
        });

        // Read Phase 2 float params
        for (int i = 0; i < kPhase2FloatSlotCount; ++i)
        {
            double value = static_cast<double>(kPhase2FloatSlots[i].defaultValue);
            if (state->read(&value, sizeof(value), nullptr) != kResultOk)
            {
                phase2Slots[i]->store(kPhase2FloatSlots[i].defaultValue);
                continue;
            }
            phase2Slots[i]->store(static_cast<float>(value));
        }
    }
    else
    {
        // FR-082: Phase 1 blob — fill Phase 2 with defaults (all bypass).
        exciterType_.store(0);
        bodyModel_.store(0);
        voicePool_.setSharedExciterType(ExciterType::Impulse);
        voicePool_.setSharedBodyModel(BodyModelType::Membrane);
        voicePool_.forEachMainVoice([](DrumVoice& v) noexcept {
            v.setExciterType(ExciterType::Impulse);
            v.setBodyModel(BodyModelType::Membrane);
        });

        for (int i = 0; i < kPhase2FloatSlotCount; ++i)
            phase2Slots[i]->store(kPhase2FloatSlots[i].defaultValue);
    }

    // Sync selected Phase 2 stub setters so the next process block reflects
    // the restored normalized values. Broadcast to every main voice slot so
    // Phase 2 regression (maxPolyphony=1 on slot 0) is bit-identical, and
    // higher-polyphony sessions see every voice stamped with the same template.
    // Denormalize per data-model.md §9 / T114:
    //   modeStretch [0.5, 2.0] linear; decaySkew [-1, 1] linear;
    //   morph duration [10, 2000] ms linear.
    voicePool_.forEachMainVoice([this](DrumVoice& v) noexcept {
        const float normStretch = std::clamp(unnaturalModeStretch_.load(), 0.0f, 1.0f);
        v.unnaturalZone().setModeStretch(0.5f + normStretch * 1.5f);

        const float normSkew = std::clamp(unnaturalDecaySkew_.load(), 0.0f, 1.0f);
        v.unnaturalZone().setDecaySkew(normSkew * 2.0f - 1.0f);

        v.unnaturalZone().modeInject.setAmount(
            std::clamp(unnaturalModeInjectAmount_.load(), 0.0f, 1.0f));
        v.unnaturalZone().nonlinearCoupling.setAmount(
            std::clamp(unnaturalNonlinearCoupling_.load(), 0.0f, 1.0f));

        v.unnaturalZone().materialMorph.setEnabled(morphEnabled_.load() >= 0.5f);
        v.unnaturalZone().materialMorph.setStart(
            std::clamp(morphStart_.load(), 0.0f, 1.0f));
        v.unnaturalZone().materialMorph.setEnd(
            std::clamp(morphEnd_.load(), 0.0f, 1.0f));
        const float normDur = std::clamp(morphDurationMs_.load(), 0.0f, 1.0f);
        v.unnaturalZone().materialMorph.setDurationMs(10.0f + normDur * 1990.0f);
        v.unnaturalZone().materialMorph.setCurve(morphCurve_.load() >= 0.5f);
    });

    // Tone Shaper: denormalize stored normalized values and push into every voice.
    voicePool_.forEachMainVoice([this](DrumVoice& v) noexcept {
        const float normFilterType   = toneShaperFilterType_.load();
        const int   filterTypeIdx    = std::clamp(static_cast<int>(normFilterType * 3.0f), 0, 2);
        v.toneShaper().setFilterType(static_cast<ToneShaperFilterType>(filterTypeIdx));

        const float normCutoff = toneShaperFilterCutoff_.load();
        const float cutoffHz   = 20.0f * std::pow(1000.0f, std::clamp(normCutoff, 0.0f, 1.0f));
        v.toneShaper().setFilterCutoff(cutoffHz);

        v.toneShaper().setFilterResonance(toneShaperFilterResonance_.load());
        v.toneShaper().setFilterEnvAmount(toneShaperFilterEnvAmount_.load() * 2.0f - 1.0f);
        v.toneShaper().setFilterEnvAttackMs(toneShaperFilterEnvAttack_.load() * 500.0f);
        v.toneShaper().setFilterEnvDecayMs(toneShaperFilterEnvDecay_.load() * 2000.0f);
        v.toneShaper().setFilterEnvSustain(toneShaperFilterEnvSustain_.load());
        v.toneShaper().setFilterEnvReleaseMs(toneShaperFilterEnvRelease_.load() * 2000.0f);
        v.toneShaper().setDriveAmount(toneShaperDriveAmount_.load());
        v.toneShaper().setFoldAmount(toneShaperFoldAmount_.load());

        const float normStart = toneShaperPitchEnvStart_.load();
        const float startHz   = 20.0f * std::pow(100.0f, std::clamp(normStart, 0.0f, 1.0f));
        v.toneShaper().setPitchEnvStartHz(startHz);

        const float normEnd = toneShaperPitchEnvEnd_.load();
        const float endHz   = 20.0f * std::pow(100.0f, std::clamp(normEnd, 0.0f, 1.0f));
        v.toneShaper().setPitchEnvEndHz(endHz);

        v.toneShaper().setPitchEnvTimeMs(toneShaperPitchEnvTime_.load() * 500.0f);

        const float normCurve = toneShaperPitchEnvCurve_.load();
        const int   curveIdx  = std::clamp(static_cast<int>(normCurve * 2.0f), 0, 1);
        v.toneShaper().setPitchEnvCurve(static_cast<ToneShaperCurve>(curveIdx));
    });

    // ------------------------------------------------------------------
    // Phase 3 tail (FR-140, FR-141, FR-142, FR-143, FR-144)
    //
    //   v1 or v2 input: apply Phase 3 defaults -- maxPoly=8, policy=Oldest,
    //                   all choke assignments = 0.
    //   v3 input      : read the 34-byte tail, clamping every field to its
    //                   valid range (corrupt values are clamped, NEVER
    //                   rejected -- preserves user projects).
    // ------------------------------------------------------------------
    int          loadedMaxPoly = 8;  // FR-142 / FR-143 default
    int          loadedPolicy  = 0;  // Oldest
    std::array<std::uint8_t, ChokeGroupTable::kSize> loadedChokes{};  // all 0

    if (version >= 3)
    {
        // FR-141: read exactly 34 bytes in the documented order.
        std::uint8_t rawMaxPoly = 8;
        std::uint8_t rawPolicy  = 0;
        if (state->read(&rawMaxPoly, sizeof(rawMaxPoly), nullptr) != kResultOk)
            rawMaxPoly = 8;
        if (state->read(&rawPolicy, sizeof(rawPolicy), nullptr) != kResultOk)
            rawPolicy = 0;

        // FR-144: clamp-on-load. maxPoly into [4, 16]; anything outside
        // snaps to the nearest valid endpoint.
        loadedMaxPoly = std::clamp(static_cast<int>(rawMaxPoly), 4, 16);

        // FR-144: policy > 2 -> 0 (Oldest).
        loadedPolicy = (static_cast<int>(rawPolicy) > 2)
                           ? 0
                           : static_cast<int>(rawPolicy);

        // FR-141 / FR-144: 32 choke assignment bytes; per-byte clamp.
        for (std::size_t i = 0; i < loadedChokes.size(); ++i)
        {
            std::uint8_t b = 0;
            if (state->read(&b, sizeof(b), nullptr) != kResultOk)
                b = 0;
            loadedChokes[i] = (b > 8U) ? std::uint8_t{0} : b;
        }
    }
    // else: v1 or v2 -- apply Phase 3 documented defaults as initialized
    // above. No additional parsing (v1/v2 blobs have no Phase 3 tail).

    maxPolyphony_.store(loadedMaxPoly);
    voiceStealingPolicy_.store(loadedPolicy);
    // Note: chokeGroup_ (the "current" kChokeGroupId parameter value) is NOT
    // part of the persisted state. The per-pad table held by VoicePool is
    // the single source of truth for choke assignments; the parameter itself
    // is re-applied by the host when it re-sends parameter changes.

    voicePool_.setMaxPolyphony(loadedMaxPoly);
    voicePool_.setVoiceStealingPolicy(static_cast<VoiceStealingPolicy>(loadedPolicy));
    voicePool_.loadChokeGroupAssignments(loadedChokes);

    return kResultOk;
}

tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& setup)
{
#if defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#endif

    sampleRate_   = setup.sampleRate;
    maxBlockSize_ = setup.maxSamplesPerBlock;
    voicePool_.prepare(sampleRate_, maxBlockSize_);
    // Seed the pool's shared-param snapshot from current atomics so the
    // first note-on after setupProcessing uses the restored parameters.
    voicePool_.setSharedVoiceParams(material_.load(), size_.load(), decay_.load(),
                                    strikePosition_.load(), level_.load());
    voicePool_.setSharedExciterType(static_cast<ExciterType>(exciterType_.load()));
    voicePool_.setSharedBodyModel(static_cast<BodyModelType>(bodyModel_.load()));
    voicePool_.setMaxPolyphony(maxPolyphony_.load());
    voicePool_.setVoiceStealingPolicy(
        static_cast<VoiceStealingPolicy>(voiceStealingPolicy_.load()));
    voicePool_.setChokeGroup(static_cast<std::uint8_t>(chokeGroup_.load()));
    return AudioEffect::setupProcessing(setup);
}

tresult PLUGIN_API Processor::setActive(TBool state)
{
    if (state)
    {
        voicePool_.prepare(sampleRate_, maxBlockSize_);
        voicePool_.setSharedVoiceParams(material_.load(), size_.load(), decay_.load(),
                                        strikePosition_.load(), level_.load());
        voicePool_.setSharedExciterType(static_cast<ExciterType>(exciterType_.load()));
        voicePool_.setSharedBodyModel(static_cast<BodyModelType>(bodyModel_.load()));
        voicePool_.setMaxPolyphony(maxPolyphony_.load());
        voicePool_.setVoiceStealingPolicy(
            static_cast<VoiceStealingPolicy>(voiceStealingPolicy_.load()));
        voicePool_.setChokeGroup(static_cast<std::uint8_t>(chokeGroup_.load()));
    }
    return kResultOk;
}

} // namespace Membrum
