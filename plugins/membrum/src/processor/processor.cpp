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
    { kExciterFMRatioId,            0.133333f },
    { kExciterFeedbackAmountId,     0.0f },
    { kExciterNoiseBurstDurationId, 0.230769f },
    { kExciterFrictionPressureId,   0.3f },

    { kToneShaperFilterTypeId,       0.0f },
    { kToneShaperFilterCutoffId,     1.0f },
    { kToneShaperFilterResonanceId,  0.0f },
    { kToneShaperFilterEnvAmountId,  0.5f },
    { kToneShaperDriveAmountId,      0.0f },
    { kToneShaperFoldAmountId,       0.0f },
    { kToneShaperPitchEnvStartId,    0.070721f },
    { kToneShaperPitchEnvEndId,      0.0f },
    { kToneShaperPitchEnvTimeId,     0.0f },
    { kToneShaperPitchEnvCurveId,    0.0f },

    { kToneShaperFilterEnvAttackId,  0.0f },
    { kToneShaperFilterEnvDecayId,   0.1f },
    { kToneShaperFilterEnvSustainId, 0.0f },
    { kToneShaperFilterEnvReleaseId, 0.1f },

    { kUnnaturalModeStretchId,       0.333333f },
    { kUnnaturalDecaySkewId,         0.5f },
    { kUnnaturalModeInjectAmountId,  0.0f },
    { kUnnaturalNonlinearCouplingId, 0.0f },

    { kMorphEnabledId,    0.0f },
    { kMorphStartId,      1.0f },
    { kMorphEndId,        0.0f },
    { kMorphDurationMsId, 0.095477f },
    { kMorphCurveId,      0.0f },
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

                    // Forward to the stub tone shaper / unnatural zone so
                    // they retain the latest normalized value. Real mapping
                    // happens in Phase 5 (tone shaper) / Phase 6 (unnatural).
                    switch (paramId)
                    {
                    case kUnnaturalModeStretchId:
                        voice_.unnaturalZone().setModeStretch(fValue);
                        break;
                    case kUnnaturalDecaySkewId:
                        voice_.unnaturalZone().setDecaySkew(fValue);
                        break;
                    case kUnnaturalModeInjectAmountId:
                        voice_.unnaturalZone().modeInject.setAmount(fValue);
                        break;
                    case kUnnaturalNonlinearCouplingId:
                        voice_.unnaturalZone().nonlinearCoupling.setAmount(fValue);
                        break;
                    case kToneShaperFilterCutoffId:
                        voice_.toneShaper().setFilterCutoff(fValue);
                        break;
                    case kToneShaperDriveAmountId:
                        voice_.toneShaper().setDriveAmount(fValue);
                        break;
                    case kToneShaperFoldAmountId:
                        voice_.toneShaper().setFoldAmount(fValue);
                        break;
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

        for (int32 i = 0; i < data.numSamples; ++i)
        {
            float s = voice_.process();
            outL[i] = s;
            outR[i] = s;
        }

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

    for (int i = 0; i < kPhase2FloatSlotCount; ++i)
    {
        double v = static_cast<double>(phase2Slots[i]->load());
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
    voice_.unnaturalZone().setModeStretch(unnaturalModeStretch_.load());
    voice_.unnaturalZone().setDecaySkew(unnaturalDecaySkew_.load());
    voice_.unnaturalZone().modeInject.setAmount(unnaturalModeInjectAmount_.load());
    voice_.unnaturalZone().nonlinearCoupling.setAmount(unnaturalNonlinearCoupling_.load());
    voice_.toneShaper().setFilterCutoff(toneShaperFilterCutoff_.load());
    voice_.toneShaper().setDriveAmount(toneShaperDriveAmount_.load());
    voice_.toneShaper().setFoldAmount(toneShaperFoldAmount_.load());

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
