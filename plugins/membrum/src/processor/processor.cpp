// ==============================================================================
// Membrum Processor -- Phase 2
// ==============================================================================

#include "processor.h"
#include "plugin_ids.h"
#include "dsp/exciter_type.h"
#include "dsp/body_model_type.h"
#include "dsp/tone_shaper.h"

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
            voice_.setMaterial(fValue);
            break;
        case kSizeId:
            size_.store(fValue);
            voice_.setSize(fValue);
            break;
        case kDecayId:
            decay_.store(fValue);
            voice_.setDecay(fValue);
            break;
        case kStrikePositionId:
            strikePosition_.store(fValue);
            voice_.setStrikePosition(fValue);
            break;
        case kLevelId:
            level_.store(fValue);
            voice_.setLevel(fValue);
            break;

        // ---- Phase 2 selectors ----
        case kExciterTypeId:
        {
            const int idx = std::clamp(
                static_cast<int>(value * static_cast<double>(ExciterType::kCount)),
                0,
                static_cast<int>(ExciterType::kCount) - 1);
            exciterType_.store(idx);
            voice_.setExciterType(static_cast<ExciterType>(idx));
            break;
        }
        case kBodyModelId:
        {
            const int idx = std::clamp(
                static_cast<int>(value * static_cast<double>(BodyModelType::kCount)),
                0,
                static_cast<int>(BodyModelType::kCount) - 1);
            bodyModel_.store(idx);
            voice_.setBodyModel(static_cast<BodyModelType>(idx));
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

                    // Forward to the Tone Shaper / Unnatural Zone. Phase 7
                    // denormalizes continuous tone shaper params per
                    // tone_shaper_contract.md §Parameter ranges.
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
                        voice_.unnaturalZone().setModeStretch(denorm);
                        break;
                    }
                    case kUnnaturalDecaySkewId:
                    {
                        const float denorm = std::clamp(fValue, 0.0f, 1.0f) * 2.0f - 1.0f;
                        voice_.unnaturalZone().setDecaySkew(denorm);
                        break;
                    }
                    case kUnnaturalModeInjectAmountId:
                        voice_.unnaturalZone().modeInject.setAmount(
                            std::clamp(fValue, 0.0f, 1.0f));
                        break;
                    case kUnnaturalNonlinearCouplingId:
                        voice_.unnaturalZone().nonlinearCoupling.setAmount(
                            std::clamp(fValue, 0.0f, 1.0f));
                        break;

                    // ---- Material Morph (Phase 8, T114) ----
                    case kMorphEnabledId:
                        voice_.unnaturalZone().materialMorph.setEnabled(fValue >= 0.5f);
                        break;
                    case kMorphStartId:
                        voice_.unnaturalZone().materialMorph.setStart(
                            std::clamp(fValue, 0.0f, 1.0f));
                        break;
                    case kMorphEndId:
                        voice_.unnaturalZone().materialMorph.setEnd(
                            std::clamp(fValue, 0.0f, 1.0f));
                        break;
                    case kMorphDurationMsId:
                    {
                        // [10, 2000] ms linear
                        const float ms = 10.0f + std::clamp(fValue, 0.0f, 1.0f) * 1990.0f;
                        voice_.unnaturalZone().materialMorph.setDurationMs(ms);
                        break;
                    }
                    case kMorphCurveId:
                        // 0 = Linear, 1 = Exponential
                        voice_.unnaturalZone().materialMorph.setCurve(fValue >= 0.5f);
                        break;

                    // ---- Tone Shaper (Phase 7, T097) ----
                    case kToneShaperFilterTypeId:
                    {
                        // 0..1 normalized -> LP/HP/BP (3 discrete values).
                        const int typeIdx = std::clamp(static_cast<int>(fValue * 3.0f), 0, 2);
                        voice_.toneShaper().setFilterType(static_cast<ToneShaperFilterType>(typeIdx));
                        break;
                    }
                    case kToneShaperFilterCutoffId:
                    {
                        // Log scale: 20 Hz .. 20000 Hz (3 decades).
                        const float hz = 20.0f * std::pow(1000.0f, std::clamp(fValue, 0.0f, 1.0f));
                        voice_.toneShaper().setFilterCutoff(hz);
                        break;
                    }
                    case kToneShaperFilterResonanceId:
                        voice_.toneShaper().setFilterResonance(fValue);
                        break;
                    case kToneShaperFilterEnvAmountId:
                        // Normalized 0..1 -> -1..+1 bipolar.
                        voice_.toneShaper().setFilterEnvAmount(fValue * 2.0f - 1.0f);
                        break;
                    case kToneShaperFilterEnvAttackId:
                        // 0..500 ms
                        voice_.toneShaper().setFilterEnvAttackMs(fValue * 500.0f);
                        break;
                    case kToneShaperFilterEnvDecayId:
                        // 0..2000 ms
                        voice_.toneShaper().setFilterEnvDecayMs(fValue * 2000.0f);
                        break;
                    case kToneShaperFilterEnvSustainId:
                        voice_.toneShaper().setFilterEnvSustain(fValue);
                        break;
                    case kToneShaperFilterEnvReleaseId:
                        // 0..2000 ms
                        voice_.toneShaper().setFilterEnvReleaseMs(fValue * 2000.0f);
                        break;
                    case kToneShaperDriveAmountId:
                        voice_.toneShaper().setDriveAmount(fValue);
                        break;
                    case kToneShaperFoldAmountId:
                        voice_.toneShaper().setFoldAmount(fValue);
                        break;
                    case kToneShaperPitchEnvStartId:
                    {
                        // Log scale: 20 Hz .. 2000 Hz
                        const float hz = 20.0f * std::pow(100.0f, std::clamp(fValue, 0.0f, 1.0f));
                        voice_.toneShaper().setPitchEnvStartHz(hz);
                        break;
                    }
                    case kToneShaperPitchEnvEndId:
                    {
                        // Log scale: 20 Hz .. 2000 Hz
                        const float hz = 20.0f * std::pow(100.0f, std::clamp(fValue, 0.0f, 1.0f));
                        voice_.toneShaper().setPitchEnvEndHz(hz);
                        break;
                    }
                    case kToneShaperPitchEnvTimeId:
                        // 0..500 ms
                        voice_.toneShaper().setPitchEnvTimeMs(fValue * 500.0f);
                        break;
                    case kToneShaperPitchEnvCurveId:
                    {
                        const int idx = std::clamp(static_cast<int>(fValue * 2.0f), 0, 1);
                        voice_.toneShaper().setPitchEnvCurve(static_cast<ToneShaperCurve>(idx));
                        break;
                    }
                    default:
                        break;
                    }
                    break;
                }
            }
            break;
        }
        }
    }
}

// Process MIDI events: note-on/note-off for MIDI note 36 only
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
            if (event.noteOn.pitch != 36)
                break;
            if (event.noteOn.velocity <= 0.0f)
            {
                voice_.noteOff();
                break;
            }
            voice_.noteOn(event.noteOn.velocity);
            break;
        }
        case Event::kNoteOffEvent:
        {
            if (event.noteOff.pitch != 36)
                break;
            voice_.noteOff();
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

        // T043 / FR-001 / FR-002 / research.md §1: hot path must use the
        // block-level entry point so ExciterBank/BodyBank variant dispatch
        // happens exactly ONCE per block, not per sample.
        voice_.processBlock(outL, data.numSamples);

        // Mirror mono output to the right channel without a second dispatch.
        for (int32 i = 0; i < data.numSamples; ++i)
            outR[i] = outL[i];

        data.outputs[0].silenceFlags = voice_.isActive() ? 0 : 3;
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

    return kResultOk;
}

tresult PLUGIN_API Processor::setState(IBStream* state)
{
    if (!state)
        return kResultFalse;

    int32 version = 0;
    if (state->read(&version, sizeof(version), nullptr) != kResultOk)
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

    // Sync Phase 1 values into the voice so a note-on immediately after
    // setState() uses the restored parameters.
    voice_.setMaterial(material_.load());
    voice_.setSize(size_.load());
    voice_.setDecay(decay_.load());
    voice_.setStrikePosition(strikePosition_.load());
    voice_.setLevel(level_.load());

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
        voice_.setExciterType(static_cast<ExciterType>(exciterTypeI32));
        voice_.setBodyModel(static_cast<BodyModelType>(bodyModelI32));

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
        voice_.setExciterType(ExciterType::Impulse);
        voice_.setBodyModel(BodyModelType::Membrane);

        for (int i = 0; i < kPhase2FloatSlotCount; ++i)
            phase2Slots[i]->store(kPhase2FloatSlots[i].defaultValue);
    }

    // Sync selected Phase 2 stub setters so the next process block reflects
    // the restored normalized values.
    // Denormalize per data-model.md §9 / T114:
    //   modeStretch [0.5, 2.0] linear; decaySkew [-1, 1] linear;
    //   morph duration [10, 2000] ms linear.
    {
        const float normStretch = std::clamp(unnaturalModeStretch_.load(), 0.0f, 1.0f);
        voice_.unnaturalZone().setModeStretch(0.5f + normStretch * 1.5f);

        const float normSkew = std::clamp(unnaturalDecaySkew_.load(), 0.0f, 1.0f);
        voice_.unnaturalZone().setDecaySkew(normSkew * 2.0f - 1.0f);

        voice_.unnaturalZone().modeInject.setAmount(
            std::clamp(unnaturalModeInjectAmount_.load(), 0.0f, 1.0f));
        voice_.unnaturalZone().nonlinearCoupling.setAmount(
            std::clamp(unnaturalNonlinearCoupling_.load(), 0.0f, 1.0f));

        voice_.unnaturalZone().materialMorph.setEnabled(morphEnabled_.load() >= 0.5f);
        voice_.unnaturalZone().materialMorph.setStart(
            std::clamp(morphStart_.load(), 0.0f, 1.0f));
        voice_.unnaturalZone().materialMorph.setEnd(
            std::clamp(morphEnd_.load(), 0.0f, 1.0f));
        const float normDur = std::clamp(morphDurationMs_.load(), 0.0f, 1.0f);
        voice_.unnaturalZone().materialMorph.setDurationMs(10.0f + normDur * 1990.0f);
        voice_.unnaturalZone().materialMorph.setCurve(morphCurve_.load() >= 0.5f);
    }

    // Tone Shaper: denormalize stored normalized values and push into the voice.
    {
        const float normFilterType   = toneShaperFilterType_.load();
        const int   filterTypeIdx    = std::clamp(static_cast<int>(normFilterType * 3.0f), 0, 2);
        voice_.toneShaper().setFilterType(static_cast<ToneShaperFilterType>(filterTypeIdx));

        const float normCutoff = toneShaperFilterCutoff_.load();
        const float cutoffHz   = 20.0f * std::pow(1000.0f, std::clamp(normCutoff, 0.0f, 1.0f));
        voice_.toneShaper().setFilterCutoff(cutoffHz);

        voice_.toneShaper().setFilterResonance(toneShaperFilterResonance_.load());
        voice_.toneShaper().setFilterEnvAmount(toneShaperFilterEnvAmount_.load() * 2.0f - 1.0f);
        voice_.toneShaper().setFilterEnvAttackMs(toneShaperFilterEnvAttack_.load() * 500.0f);
        voice_.toneShaper().setFilterEnvDecayMs(toneShaperFilterEnvDecay_.load() * 2000.0f);
        voice_.toneShaper().setFilterEnvSustain(toneShaperFilterEnvSustain_.load());
        voice_.toneShaper().setFilterEnvReleaseMs(toneShaperFilterEnvRelease_.load() * 2000.0f);
        voice_.toneShaper().setDriveAmount(toneShaperDriveAmount_.load());
        voice_.toneShaper().setFoldAmount(toneShaperFoldAmount_.load());

        const float normStart = toneShaperPitchEnvStart_.load();
        const float startHz   = 20.0f * std::pow(100.0f, std::clamp(normStart, 0.0f, 1.0f));
        voice_.toneShaper().setPitchEnvStartHz(startHz);

        const float normEnd = toneShaperPitchEnvEnd_.load();
        const float endHz   = 20.0f * std::pow(100.0f, std::clamp(normEnd, 0.0f, 1.0f));
        voice_.toneShaper().setPitchEnvEndHz(endHz);

        voice_.toneShaper().setPitchEnvTimeMs(toneShaperPitchEnvTime_.load() * 500.0f);

        const float normCurve = toneShaperPitchEnvCurve_.load();
        const int   curveIdx  = std::clamp(static_cast<int>(normCurve * 2.0f), 0, 1);
        voice_.toneShaper().setPitchEnvCurve(static_cast<ToneShaperCurve>(curveIdx));
    }

    return kResultOk;
}

tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& setup)
{
#if defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#endif

    sampleRate_ = setup.sampleRate;
    voice_.prepare(sampleRate_);
    return AudioEffect::setupProcessing(setup);
}

tresult PLUGIN_API Processor::setActive(TBool state)
{
    if (state)
        voice_.prepare(sampleRate_);
    return kResultOk;
}

} // namespace Membrum
