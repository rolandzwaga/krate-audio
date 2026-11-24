// ==============================================================================
// Membrum Processor -- Phase 4
// ==============================================================================
// Per-pad parameter dispatch: parameter IDs in the per-pad range
// [kPadBaseId, kPadBaseId + kNumPads * kPadParamStride) are dispatched to
// VoicePool::setPadConfigField / setPadConfigSelector. Global proxy IDs
// (100-252) are no-ops in the processor (controller-only proxies).
// ==============================================================================

#include "processor.h"
#include "plugin_ids.h"
#include "dsp/default_kit.h"
#include "dsp/exciter_type.h"
#include "dsp/body_model_type.h"
#include "dsp/macro_offsets.h"
#include "dsp/pad_category.h"
#include "dsp/tone_shaper.h"
#include "voice_pool/choke_group_table.h"
#include "voice_pool/voice_stealing_policy.h"
#include "processor/meters_block.h"
#include "state/state_codec.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/utility/dataexchange.h"

// FTZ/DAZ for denormal prevention (SC-007). Included UNCONDITIONALLY: the
// header does its own per-architecture handling and compiles to an empty,
// zero-cost object off x86. Wrapping it in an arch #if breaks the arm64 build,
// because process() names Krate::DSP::ScopedDenormalMode unconditionally.
#include <krate/dsp/core/scoped_denormal_mode.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace Membrum {

using namespace Steinberg;
using namespace Steinberg::Vst;

Processor::Processor()
    : pendingLoad_(std::make_unique<PendingLoad>())
    , padCopies_(std::make_unique<std::array<std::array<PadConfig, kNumPads>, 3>>())
{
    setControllerClass(kControllerUID);
}

Processor::~Processor() = default;

// T1.22: a pad's category follows what it plays (its macro offsets applied).
PadCategory Processor::effectivePadCategory(int pad) const noexcept
{
    return classifyPad(applyMacroOffsets(voicePool_.padConfig(pad)));
}

tresult PLUGIN_API Processor::initialize(FUnknown* context)
{
    auto result = AudioEffect::initialize(context);
    if (result != kResultOk)
        return result;

    addEventInput(STR16("Event In"));

    // FR-040: 1 main + 15 auxiliary stereo output buses
    addAudioOutput(STR16("Main Out"), SpeakerArr::kStereo);
    for (int i = 1; i < kMaxOutputBuses; ++i)
    {
        char narrowName[32];
        std::snprintf(narrowName, sizeof(narrowName), "Aux %d", i);
        Steinberg::Vst::String128 wideName;
        for (int c = 0; c < 31 && narrowName[c] != '\0'; ++c)
            wideName[c] = static_cast<Steinberg::char16>(narrowName[c]);
        wideName[std::strlen(narrowName)] = 0;
        addAudioOutput(wideName, SpeakerArr::kStereo, BusTypes::kAux, 0);
    }

    // FR-030: Initialize all 32 pads with GM-inspired default templates.
    // This is overwritten by setState() if a saved state is loaded.
    DefaultKit::apply(voicePool_.padConfigsArray());
    publishPadConfigs();

    // Phase 5: Initialize pad categories from default kit configuration.
    for (int i = 0; i < kNumPads; ++i)
        padCategories_[static_cast<size_t>(i)] = effectivePadCategory(i);

    return kResultOk;
}

// ==============================================================================
// processParameterChanges -- Phase 4 per-pad dispatch
// ==============================================================================

// Phase 6 (US4 / FR-014, FR-023): rebuild effectiveGain using the current
// Tier 1 knobs, pad categories, and per-pad coupling amounts (T1.22: the
// effective amount, with the Complexity macro's offset applied).
void Processor::recomputeCouplingMatrix() noexcept
{
    ++couplingMatrixRecomputeCount_;
    std::array<float, kNumPads> amounts{};
    for (int pad = 0; pad < kNumPads; ++pad)
        amounts[static_cast<size_t>(pad)] =
            applyMacroOffsets(voicePool_.padConfig(pad)).couplingAmount;

    couplingMatrix_.recomputeFromTier1(
        snareBuzz_.load(std::memory_order_relaxed),
        tomResonance_.load(std::memory_order_relaxed),
        padCategories_.data(),
        amounts.data());
}

void Processor::processParameterChanges(IParameterChanges* paramChanges)
{
    if (!paramChanges)
        return;

    int32 numParams = paramChanges->getParameterCount();

    // Per-pad IDs (the macros among them) write pad configs: republish them
    // for getState at the end of this block.
    if (numParams > 0)
        padsDirty_ = true;

    // The coupling matrix is rebuilt once, after the loop, when any change of
    // this block touched one of its inputs (MB-075).
    bool couplingMatrixDirty = false;

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

        // ------------------------------------------------------------------
        // 1. Check if paramId is in per-pad range [kPadBaseId, kPadBaseId + 32*128)
        // ------------------------------------------------------------------
        const int padIdx = padIndexFromParamId(static_cast<int>(paramId));
        const int padOff = padOffsetFromParamId(static_cast<int>(paramId));
        if (padIdx >= 0 && padOff >= 0)
        {
            // Dispatch to per-pad config in VoicePool
            if (padOff == kPadExciterType)
            {
                const int idx = std::clamp(
                    static_cast<int>(value * static_cast<double>(ExciterType::kCount)),
                    0, static_cast<int>(ExciterType::kCount) - 1);
                voicePool_.setPadConfigSelector(padIdx, padOff, idx);
                // Phase 5: exciter type change can affect pad category
                padCategories_[static_cast<size_t>(padIdx)] =
                    effectivePadCategory(padIdx);
                couplingMatrixDirty = true;
            }
            else if (padOff == kPadBodyModel)
            {
                const int idx = std::clamp(
                    static_cast<int>(value * static_cast<double>(BodyModelType::kCount)),
                    0, static_cast<int>(BodyModelType::kCount) - 1);
                voicePool_.setPadConfigSelector(padIdx, padOff, idx);
                // Phase 5: body model change can affect pad category
                padCategories_[static_cast<size_t>(padIdx)] =
                    effectivePadCategory(padIdx);
                couplingMatrixDirty = true;
            }
            else if (padOff == kPadTSPitchEnvTime)
            {
                voicePool_.setPadConfigField(padIdx, padOff, fValue);
                // Phase 5: pitch env time change can affect pad category (Kick vs Tom)
                padCategories_[static_cast<size_t>(padIdx)] =
                    effectivePadCategory(padIdx);
                couplingMatrixDirty = true;
            }
            else if (padOff == kPadCouplingAmount)
            {
                // Phase 6 (US4 / FR-014, FR-023, FR-034): per-pad coupling
                // amount feeds the effectiveGain formula. Bake amounts into
                // the matrix so a pad with amount = 0 is fully excluded as
                // both source and receiver.
                voicePool_.setPadConfigField(padIdx, padOff, fValue);
                couplingMatrixDirty = true;
            }
            else if (padOff >= kPadMacroTightness && padOff <= kPadMacroComplexity)
            {
                // T1.22 (MB-073, MB-173): a macro is an offset on the stored
                // base fields, applied where the engine reads the pad
                // (dsp/macro_offsets.h). Only the macro value is stored.
                const bool changed =
                    padOff == kPadMacroComplexity &&
                    voicePool_.padConfig(padIdx).macroComplexity != fValue;
                voicePool_.setPadConfigField(padIdx, padOff, fValue);
                // Complexity drives the effective couplingAmount.
                if (changed)
                    couplingMatrixDirty = true;
            }
            else
            {
                voicePool_.setPadConfigField(padIdx, padOff, fValue);
            }
            continue;
        }

        // ------------------------------------------------------------------
        // 2. Global-only params
        // ------------------------------------------------------------------
        switch (paramId)
        {
        case kMaxPolyphonyId:
        {
            const float clamped = std::clamp(fValue, 0.0f, 1.0f);
            const int n = 4 + static_cast<int>(std::lround(clamped * 12.0f));
            const int nClamped = std::clamp(n, 4, 16);
            maxPolyphony_.store(nClamped);
            voicePool_.setMaxPolyphony(nClamped);
            break;
        }
        case kVoiceStealingId:
        {
            const float clamped = std::clamp(fValue, 0.0f, 1.0f);
            const int idx = std::clamp(static_cast<int>(clamped * 3.0f), 0, 2);
            voiceStealingPolicy_.store(idx);
            voicePool_.setVoiceStealingPolicy(
                static_cast<VoiceStealingPolicy>(idx));
            break;
        }

        // ------------------------------------------------------------------
        // 3. The selected-pad proxy IDs (Material, Body, the tone shaper,
        //    ...) are UI-only binding targets (MB-072, MB-163): the controller
        //    sends a proxy gesture on the selected pad's per-pad ID, so they
        //    are not handled here and fall to default. The per-pad route above
        //    is the only way a pad field changes (MB-131, MB-117).
        // ------------------------------------------------------------------
        case kSelectedPadId:
        {
            const float clamped = std::clamp(fValue, 0.0f, 1.0f);
            selectedPadIndex_ = std::clamp(
                static_cast<int>(std::lround(clamped * 31.0f)), 0, 31);
            break;
        }

        // ---- Phase 5: Coupling parameters ----
        case kGlobalCouplingId:
        {
            const float clamped = std::clamp(fValue, 0.0f, 1.0f);
            globalCoupling_.store(clamped, std::memory_order_relaxed);
            couplingEngine_.setAmount(clamped);
            break;
        }
        case kSnareBuzzId:
        {
            const float clamped = std::clamp(fValue, 0.0f, 1.0f);
            snareBuzz_.store(clamped, std::memory_order_relaxed);
            couplingMatrixDirty = true;
            break;
        }
        case kTomResonanceId:
        {
            const float clamped = std::clamp(fValue, 0.0f, 1.0f);
            tomResonance_.store(clamped, std::memory_order_relaxed);
            couplingMatrixDirty = true;
            break;
        }
        case kCouplingDelayId:
        {
            // Denormalize from [0, 1] to [0.5, 2.0] ms
            const float clamped = std::clamp(fValue, 0.0f, 1.0f);
            const float delayMs = 0.5f + clamped * 1.5f;
            couplingDelayMs_.store(delayMs, std::memory_order_relaxed);
            break;
        }

        // ---- Phase 9: global master output gain ----
        case kMasterGainId:
        {
            const float clamped = std::clamp(fValue, 0.0f, 1.0f);
            masterGainNorm_.store(clamped, std::memory_order_relaxed);
            break;
        }

        default:
            break;
        }
    }

    if (couplingMatrixDirty)
        recomputeCouplingMatrix();
}

// Consume a pending audition request (posted via IMessage "AuditionPad") and
// fire a voice note-on for it from the audio thread.
void Processor::consumePendingAudition()
{
    const std::uint32_t word = pendingAudition_.exchange(0u, std::memory_order_acq_rel);
    if ((word & 0x4000u) == 0u)
        return;
    const int midi  = static_cast<int>(word & 0x7Fu);
    const int velByte = static_cast<int>((word >> 7) & 0x7Fu);
    if (midi < 36 || midi > 67)
        return;
    if (velByte == 0)
    {
        voicePool_.noteOff(static_cast<std::uint8_t>(midi));
        return;
    }
    const float velNorm = static_cast<float>(velByte) / 127.0f;
    voicePool_.noteOn(static_cast<std::uint8_t>(midi), velNorm);
}

// Apply every MIDI event of the list at once, ignoring sampleOffset. Only the
// zero-sample flush call uses it (T1.14 / MB-089); a rendering block applies
// each event at its own sample inside the render loop of process().
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
        applyNoteEvent(event);
    }
}

// One MIDI event: note-on/note-off on any MIDI note in [36, 67]; a note-on
// with velocity <= 0 is a note-off.
void Processor::applyNoteEvent(const Event& event)
{
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

tresult PLUGIN_API Processor::process(ProcessData& data)
{
    // Flush denormals for this block. Modal/waveguide resonator tails and the
    // coupling delay decay into denormal range, where x86 traps cost hundreds
    // of cycles per operation.
    //
    // Must be set HERE and not in setupProcessing(): MXCSR is per-thread state,
    // and hosts call setupProcessing() on the main thread while process() runs
    // on a dedicated audio thread (some rotate plugins across a render pool).
    // The guard restores the host's FP environment on scope exit.
    const Krate::DSP::ScopedDenormalMode denormalGuard;

    // T045: start CPU-usage timer (steady_clock::now is lock-free and
    // allocation-free on all supported platforms).
    const auto cpuT0 = std::chrono::steady_clock::now();

    // MB-001: a kit or pad preset the message thread parsed lands here, before
    // this block's parameter changes, so they apply on top of the new kit.
    consumePendingLoad();

    processParameterChanges(data.inputParameterChanges);

    // T1.14 (MB-089): a zero-sample call is the host's parameter flush
    // (ivstaudioprocessor.h, ProcessData::numSamples). Its parameter changes
    // were applied above; its events apply at once. No render, meters or CPU
    // timing.
    if (data.numSamples <= 0)
    {
        processEvents(data.inputEvents);
        consumePendingAudition();
        if (padsDirty_)
            publishPadConfigs();
        return kResultOk;
    }

    const bool hasMainOut = data.numOutputs > 0 && data.outputs != nullptr;
    float* outL = nullptr;
    float* outR = nullptr;

    // FR-044 / FR-048: auxiliary bus buffer pointers from data.outputs[1..N]
    int numOutputBuses = 0;
    std::array<float*, kMaxOutputBuses> auxLPtrs{};
    std::array<float*, kMaxOutputBuses> auxRPtrs{};

    if (hasMainOut)
    {
        outL = data.outputs[0].channelBuffers32[0];
        outR = data.outputs[0].channelBuffers32[1];
        numOutputBuses = std::min(static_cast<int>(data.numOutputs), kMaxOutputBuses);

        for (int b = 1; b < numOutputBuses; ++b)
        {
            if (data.outputs[b].channelBuffers32 != nullptr)
            {
                auxLPtrs[static_cast<std::size_t>(b)] =
                    data.outputs[b].channelBuffers32[0];
                auxRPtrs[static_cast<std::size_t>(b)] =
                    data.outputs[b].channelBuffers32[1];

                // Zero aux buffers once per block (host may not guarantee
                // zeroed); the voice segments below accumulate into them.
                if (auxLPtrs[static_cast<std::size_t>(b)] != nullptr)
                    std::memset(auxLPtrs[static_cast<std::size_t>(b)], 0,
                                static_cast<std::size_t>(data.numSamples) * sizeof(float));
                if (auxRPtrs[static_cast<std::size_t>(b)] != nullptr)
                    std::memset(auxRPtrs[static_cast<std::size_t>(b)], 0,
                                static_cast<std::size_t>(data.numSamples) * sizeof(float));
            }
        }
    }

    // Render the voices over [start, start + len) of this block. Each segment
    // is a normal VoicePool::processBlock call on buffers advanced by `start`.
    const auto renderVoices = [&](int32 start, int32 len) {
        if (!hasMainOut || len <= 0)
            return;
        if (numOutputBuses <= 1)
        {
            // No aux buses available -- fall back to Phase 3 behavior
            voicePool_.processBlock(outL + start, outR + start, len);
            return;
        }
        std::array<float*, kMaxOutputBuses> segL{};
        std::array<float*, kMaxOutputBuses> segR{};
        for (int b = 1; b < numOutputBuses; ++b)
        {
            const auto i = static_cast<std::size_t>(b);
            segL[i] = auxLPtrs[i] != nullptr ? auxLPtrs[i] + start : nullptr;
            segR[i] = auxRPtrs[i] != nullptr ? auxRPtrs[i] + start : nullptr;
        }
        voicePool_.processBlock(outL + start, outR + start,
                                segL.data(), segR.data(),
                                busActive_.data(), numOutputBuses, len);
    };

    // An audition request (IMessage "AuditionPad") starts at sample 0.
    consumePendingAudition();

    // T1.14 (MB-051): every note event starts at its Event::sampleOffset. The
    // block is rendered in segments split at the event offsets, the event
    // applied between them. An out-of-order or out-of-range offset applies at
    // the current position; nothing is rendered backwards. Iterates the host
    // list in place: no allocation.
    {
        const int32 numSamples = data.numSamples;
        int32 pos = 0;
        if (data.inputEvents != nullptr)
        {
            const int32 numEvents = data.inputEvents->getEventCount();
            for (int32 i = 0; i < numEvents; ++i)
            {
                Event event{};
                if (data.inputEvents->getEvent(i, event) != kResultOk)
                    continue;
                const int32 off = std::clamp(event.sampleOffset, pos, numSamples - 1);
                if (off > pos)
                {
                    renderVoices(pos, off - pos);
                    pos = off;
                }
                applyNoteEvent(event);
            }
        }
        renderVoices(pos, numSamples - pos);
    }

    if (hasMainOut)
    {
        if (numOutputBuses > 1)
        {
            // Set silence flags for aux buses
            for (int b = 1; b < numOutputBuses; ++b)
            {
                bool auxSilent = true;
                if (auxLPtrs[static_cast<std::size_t>(b)] != nullptr)
                {
                    for (int32 s = 0; s < data.numSamples; ++s)
                    {
                        if (auxLPtrs[static_cast<std::size_t>(b)][s] != 0.0f ||
                            auxRPtrs[static_cast<std::size_t>(b)][s] != 0.0f)
                        {
                            auxSilent = false;
                            break;
                        }
                    }
                }
                data.outputs[b].silenceFlags = auxSilent ? 3ULL : 0ULL;
            }
        }

        // ============================================================
        // Phase 5: Coupling signal chain (FR-072)
        // mono sum -> delay read -> delay write -> engine -> energy limiter -> add to L/R
        // Skip entire loop when engine is bypassed (global coupling = 0).
        // ============================================================
        if (!couplingEngine_.isBypassed())
        {
            const float delayMs = couplingDelayMs_.load(std::memory_order_relaxed);
            const float delaySamples = delayMs * static_cast<float>(sampleRate_) * 0.001f;

            for (int32 s = 0; s < data.numSamples; ++s)
            {
                const float mono = (outL[s] + outR[s]) * 0.5f;
                const float delayed = couplingDelay_.readLinear(delaySamples);
                couplingDelay_.write(mono);
                float coupling = couplingEngine_.process(delayed);
                coupling = applyEnergyLimiter(coupling);
                outL[s] += coupling;
                outR[s] += coupling;
            }
        }

        // ============================================================
        // Gain-staging Step 1: main-bus true-peak brickwall limiter.
        // Runs AFTER the voice sum + coupling return and BEFORE the master
        // gain, so it bounds the mix to -1 dBTP. This is what lets the
        // per-voice path drop its softClip gain stage (Step 2): the bus,
        // not each voice, guarantees the ceiling.
        // ============================================================
        busLimiter_.processBlock(outL, outR, data.numSamples);

        // ============================================================
        // Phase 9: master output gain on the main bus only.
        // norm 0..1 -> dB -24..+12 -> linear gain. The peak meter below
        // observes the post-master signal so the UI level reflects what
        // the host actually receives.
        // ============================================================
        {
            const float norm = masterGainNorm_.load(std::memory_order_relaxed);
            const float dB   = -24.0f + 36.0f * norm;
            const float gain = std::pow(10.0f, dB * 0.05f);
            if (gain != 1.0f)
            {
                for (int32 s = 0; s < data.numSamples; ++s)
                {
                    outL[s] *= gain;
                    outR[s] *= gain;
                }
            }
        }

        data.outputs[0].silenceFlags =
            (voicePool_.isAnyVoiceActive() || !couplingEngine_.isBypassed())
                ? 0ULL : 3ULL;
    }

    // ============================================================
    // Phase 6 (T043): publish per-pad glow amplitudes.
    // Walk both main and shadow voice slots, take the max currentLevel per
    // pad, and publish. Pads with no active voice are published as 0.0 so
    // the UI can decay its own smoothing.
    // ============================================================
    {
        std::array<float, kNumPads> padAmp{};
        padAmp.fill(0.0f);
        for (int slot = 0; slot < kMaxVoices; ++slot)
        {
            const auto& m = voicePool_.voiceMeta(slot);
            if (m.state != VoiceSlotState::Free)
            {
                const int padIdx = static_cast<int>(m.originatingNote)
                                 - static_cast<int>(kFirstDrumNote);
                if (padIdx >= 0 && padIdx < kNumPads)
                {
                    const float lvl = m.currentLevel * m.fastReleaseGain;
                    padAmp[static_cast<std::size_t>(padIdx)] =
                        std::max(padAmp[static_cast<std::size_t>(padIdx)], lvl);
                }
            }
            const auto& rm = voicePool_.releasingMeta(slot);
            if (rm.state != VoiceSlotState::Free)
            {
                const int padIdx = static_cast<int>(rm.originatingNote)
                                 - static_cast<int>(kFirstDrumNote);
                if (padIdx >= 0 && padIdx < kNumPads)
                {
                    const float lvl = rm.currentLevel * rm.fastReleaseGain;
                    padAmp[static_cast<std::size_t>(padIdx)] =
                        std::max(padAmp[static_cast<std::size_t>(padIdx)], lvl);
                }
            }
        }
        for (int pad = 0; pad < kNumPads; ++pad)
            padGlowPublisher_.publish(pad, padAmp[static_cast<std::size_t>(pad)]);
    }

    // ============================================================
    // Phase 6 (T045): publish MetersBlock via DataExchangeHandler.
    // Peak L/R over this block, active voice count, CPU permille placeholder.
    // ============================================================
    if (dataExchangeHandler_ && hasMainOut)
    {
        float peakL = 0.0f;
        float peakR = 0.0f;
        for (int32 s = 0; s < data.numSamples; ++s)
        {
            const float aL = std::abs(outL[s]);
            const float aR = std::abs(outR[s]);
            peakL = std::max(peakL, aL);
            peakR = std::max(peakR, aR);
        }

        // T045: compute CPU usage (per-mille of the block budget) using a
        // simple EWMA smoother so brief spikes do not dominate the meter.
        // blockBudget = numSamples / sampleRate (in seconds).
        std::uint16_t cpuPermille = 0;
        if (sampleRate_ > 0.0 && data.numSamples > 0)
        {
            const auto cpuT1 = std::chrono::steady_clock::now();
            const double elapsedSec =
                std::chrono::duration<double>(cpuT1 - cpuT0).count();
            const double blockBudgetSec =
                static_cast<double>(data.numSamples) / sampleRate_;
            const double ratio =
                blockBudgetSec > 0.0 ? (elapsedSec / blockBudgetSec) : 0.0;
            const float instantaneousPermille =
                static_cast<float>(std::clamp(ratio * 1000.0, 0.0, 65535.0));
            // EWMA: alpha = 0.1 smooths ~10 blocks; cheap and audio-safe.
            constexpr float kEwmaAlpha = 0.1f;
            cpuPermilleEwma_ =
                cpuPermilleEwma_ + kEwmaAlpha
                * (instantaneousPermille - cpuPermilleEwma_);
            cpuPermille = static_cast<std::uint16_t>(
                std::clamp(cpuPermilleEwma_ + 0.5f, 0.0f, 65535.0f));
        }

        auto block = dataExchangeHandler_->getCurrentOrNewBlock();
        if (block.blockID != Steinberg::Vst::InvalidDataExchangeBlockID
            && block.data != nullptr
            && block.size >= sizeof(MetersBlock))
        {
            MetersBlock mb;
            mb.peakL        = peakL;
            mb.peakR        = peakR;
            mb.activeVoices = static_cast<std::uint16_t>(
                                  voicePool_.getActiveVoiceCount());
            mb.cpuPermille  = cpuPermille;

            // Snapshot the glow publisher directly into the block so the
            // controller-side UI can mirror per-pad amplitude across the
            // separate-component boundary.
            std::array<std::uint8_t, kNumPads> buckets{};
            padGlowPublisher_.snapshot(buckets);
            static_assert(sizeof(mb.padGlowBuckets) == sizeof(buckets),
                          "MetersBlock glow bucket size must match kNumPads");
            std::memcpy(mb.padGlowBuckets, buckets.data(), sizeof(buckets));

            std::memcpy(block.data, &mb, sizeof(MetersBlock));
            dataExchangeHandler_->sendCurrentBlock();
        }
    }

    // MB-001: getState reads the pad configs from this published copy, never
    // the live ones this thread writes.
    if (padsDirty_)
        publishPadConfigs();

    return kResultOk;
}

// ==============================================================================
// State v4 -- data-model.md
// ==============================================================================
// Binary layout (v4):
//   [int32] version = 4
//   [int32] maxPolyphony [4, 16]
//   [int32] voiceStealingPolicy [0, 2]
//   For each of 32 pads:
//     [int32]  exciterType
//     [int32]  bodyModel
//     [34 x float64] sound params (offsets 2-35 as normalized float64)
//     [uint8]  chokeGroup
//     [uint8]  outputBus
//   [int32] selectedPadIndex [0, 31]
// ==============================================================================

// ==============================================================================
// Preset-load hand-off (MB-001)
// ==============================================================================
// setState, notify("LoadPresetState") and notify("LoadPadPreset") run on the
// host's message thread while process() may run on the audio thread, so none
// of them writes the voice pool, the pad configs, the allocator or the
// coupling engine.
//
// - pendingLoad_ (PendingLoad, allocated once in the constructor) holds the
//   parsed load: a kit and/or one pad preset per pad. Only the thread that
//   moved pendingSlot_ to kSlotWriting (a host-thread call) writes or reads
//   it; only the thread that moved it to kSlotApplying (process() on the audio
//   thread, or a lifecycle call while processing is stopped) applies it.
// - pendingSlot_ hands it over:
//     Idle/Ready -> Writing   host thread, CAS, acquire
//     Writing    -> Ready     host thread, store, release (publishes the load)
//     Ready      -> Applying  consumer, CAS, acquire
//     Applying   -> Idle      consumer, store, release
//   The audio thread never waits: a slot held for Writing is applied a block
//   later. The host thread yields while a slot is Applying.
// - A kit replaces a kit and every pad preset not yet applied; a pad preset
//   keeps a pending kit, which applies first.
// - While processing is not active (active_ false) the load applies at once on
//   its own thread. setState, the load messages, setActive, setupProcessing
//   and getState hold lifecycleMutex_ (process() never does).
// - getState reads the pad configs from a copy the writer publishes after
//   every block that changed one (triple buffer, never the live configs), and
//   returns a load that is still pending as if it were applied.
// ==============================================================================

int Processor::acquireLoadSlot() noexcept
{
    for (;;)
    {
        int slot = pendingSlot_.load(std::memory_order_acquire);
        if ((slot == kSlotIdle || slot == kSlotReady) &&
            pendingSlot_.compare_exchange_weak(slot, kSlotWriting,
                                               std::memory_order_acquire,
                                               std::memory_order_relaxed))
            return slot;
        std::this_thread::yield();  // being applied: wait for it
    }
}

template <typename Fill>
void Processor::publishLoad(const Fill& fill)
{
    const std::scoped_lock lifecycle(lifecycleMutex_);
    (void)acquireLoadSlot();
    fill(*pendingLoad_);
    pendingSlot_.store(kSlotReady, std::memory_order_release);

    // Processing is stopped: no audio thread can exist, apply it now.
    if (!active_.load(std::memory_order_acquire))
        consumePendingLoad();
}

void Processor::consumePendingLoad() noexcept
{
    int expected = kSlotReady;
    if (!pendingSlot_.compare_exchange_strong(expected, kSlotApplying,
                                              std::memory_order_acquire,
                                              std::memory_order_relaxed))
        return;  // nothing published, or a host-thread call holds the slot

    PendingLoad& load = *pendingLoad_;
    if (load.hasKit)
    {
        // FR-052: a kit-preset browser load keeps the selected pad.
        if (load.keepSelectedPad)
            load.kit.selectedPadIndex = selectedPadIndex_.load();
        applyKitSnapshot(load.kit);
    }
    for (int pad = 0; pad < kNumPads; ++pad)
    {
        if (load.hasPadPreset[static_cast<std::size_t>(pad)])
            applyPadPresetSnapshot(pad, load.padPresets[static_cast<std::size_t>(pad)]);
    }
    load.hasKit = false;
    load.keepSelectedPad = false;
    load.hasPadPreset.fill(false);

    // Publish before the slot reads Idle, so a getState that sees Idle sees
    // the loaded pads together with the loaded globals.
    publishPadConfigs();
    pendingSlot_.store(kSlotIdle, std::memory_order_release);
}

void Processor::publishPadConfigs() noexcept
{
    auto& copies = *padCopies_;
    copies[padCopyBack_] = voicePool_.padConfigsArray();
    padCopyBack_ = static_cast<std::uint8_t>(
        padCopyMiddle_.exchange(static_cast<std::uint8_t>(padCopyBack_ | kPadCopyFresh),
                                std::memory_order_acq_rel) & kPadCopyIndexMask);
    padsDirty_ = false;
}

const std::array<PadConfig, kNumPads>& Processor::latestPublishedPadConfigs() noexcept
{
    if ((padCopyMiddle_.load(std::memory_order_acquire) & kPadCopyFresh) != 0u)
    {
        padCopyFront_ = static_cast<std::uint8_t>(
            padCopyMiddle_.exchange(padCopyFront_, std::memory_order_acq_rel)
            & kPadCopyIndexMask);
    }
    return (*padCopies_)[padCopyFront_];
}

tresult PLUGIN_API Processor::getState(IBStream* state)
{
    if (!state)
        return kResultFalse;

    // A load, setActive or setupProcessing on another thread finishes first;
    // process() never waits.
    const std::scoped_lock lifecycle(lifecycleMutex_);

    // Hold a load the audio thread has not applied yet (it is the newest
    // state), so it cannot be applied while this reads.
    bool holdsPending = false;
    for (;;)
    {
        int slot = pendingSlot_.load(std::memory_order_acquire);
        if (slot == kSlotIdle)
            break;
        if (slot == kSlotReady &&
            pendingSlot_.compare_exchange_weak(slot, kSlotWriting,
                                               std::memory_order_acquire,
                                               std::memory_order_relaxed))
        {
            holdsPending = true;
            break;
        }
        if (slot != kSlotReady)
            std::this_thread::yield();  // being applied: wait for it
    }

    // Build a KitSnapshot from the current processor state and delegate to
    // the shared codec. Session-scoped fields (uiMode) are NOT emitted from
    // the processor path -- the controller resets them to defaults before
    // setComponentState is consumed. hasSession=false.
    State::KitSnapshot kit;
    kit.maxPolyphony        = maxPolyphony_.load();
    kit.voiceStealingPolicy = voiceStealingPolicy_.load();
    kit.selectedPadIndex    = selectedPadIndex_.load();
    kit.globalCoupling      =
        static_cast<double>(globalCoupling_.load(std::memory_order_relaxed));
    kit.snareBuzz           =
        static_cast<double>(snareBuzz_.load(std::memory_order_relaxed));
    kit.tomResonance        =
        static_cast<double>(tomResonance_.load(std::memory_order_relaxed));
    kit.couplingDelayMs     =
        static_cast<double>(couplingDelayMs_.load(std::memory_order_relaxed));
    kit.masterGainNorm      =
        static_cast<double>(masterGainNorm_.load(std::memory_order_relaxed));

    const PendingLoad* pending = holdsPending ? pendingLoad_.get() : nullptr;
    if (pending != nullptr && pending->hasKit)
    {
        // The values applyKitSnapshot would store (the globals live in float
        // atomics).
        const auto& k = pending->kit;
        kit.maxPolyphony        = k.maxPolyphony;
        kit.voiceStealingPolicy = k.voiceStealingPolicy;
        if (!pending->keepSelectedPad)
            kit.selectedPadIndex = k.selectedPadIndex;
        kit.globalCoupling  = static_cast<double>(static_cast<float>(k.globalCoupling));
        kit.snareBuzz       = static_cast<double>(static_cast<float>(k.snareBuzz));
        kit.tomResonance    = static_cast<double>(static_cast<float>(k.tomResonance));
        kit.couplingDelayMs = static_cast<double>(static_cast<float>(k.couplingDelayMs));
        kit.masterGainNorm  = static_cast<double>(static_cast<float>(k.masterGainNorm));
    }

    const auto& pads = latestPublishedPadConfigs();
    for (int pad = 0; pad < kNumPads; ++pad)
    {
        const auto p = static_cast<std::size_t>(pad);
        if (pending == nullptr)
        {
            kit.pads[p] = State::toPadSnapshot(pads[p]);
            continue;
        }
        // The pad as the pending apply leaves it: the kit, then the pad preset.
        PadConfig cfg = pads[p];
        if (pending->hasKit)
            State::applyPadSnapshot(pending->kit.pads[p], cfg);
        if (pending->hasPadPreset[p])
            State::applyPadPresetSnapshot(pending->padPresets[p], cfg);
        kit.pads[p] = State::toPadSnapshot(cfg);
    }

    if (holdsPending)
        pendingSlot_.store(kSlotReady, std::memory_order_release);

    kit.hasSession = false;
    return State::writeKitBlob(state, kit);
}

tresult PLUGIN_API Processor::setState(IBStream* state)
{
    if (!state)
        return kResultFalse;

    State::KitSnapshot kit;
    if (State::readKitBlob(state, kit) != kResultOk)
        return kResultFalse;

    // A host load carries its selected pad. A kit replaces any pad preset not
    // yet applied.
    publishLoad([&kit](PendingLoad& load) {
        load.hasKit = true;
        load.kit = kit;
        load.keepSelectedPad = false;
        load.hasPadPreset.fill(false);
    });
    return kResultOk;
}

void Processor::applyKitSnapshot(const State::KitSnapshot& kit)
{
    // Hard reset every voice slot's DSP state (envelopes, modal banks,
    // filters, coupling integrators) so the prior kit's residual audio
    // state can't bleed into the new kit's first hits. Per-pad params
    // are re-applied by the next noteOn; this only wipes the *audio
    // path* state -- the DSP equivalent of pressing "panic" on the
    // pool. Resolves the user-reported intermittent tonal beep that
    // appears after kit switch + rapid hits.
    voicePool_.resetAllVoicesForKitSwitch();

    // Polyphony / stealing.
    maxPolyphony_.store(kit.maxPolyphony);
    voiceStealingPolicy_.store(kit.voiceStealingPolicy);
    voicePool_.setMaxPolyphony(kit.maxPolyphony);
    voicePool_.setVoiceStealingPolicy(
        static_cast<VoiceStealingPolicy>(kit.voiceStealingPolicy));

    // Per-pad configs.
    for (int pad = 0; pad < kNumPads; ++pad)
    {
        State::applyPadSnapshot(
            kit.pads[static_cast<std::size_t>(pad)],
            voicePool_.padConfigMut(pad));
    }

    // Sync choke group table from padConfigs.
    for (int pad = 0; pad < kNumPads; ++pad)
        voicePool_.setPadChokeGroup(pad, voicePool_.padConfig(pad).chokeGroup);

    selectedPadIndex_ = kit.selectedPadIndex;

    // Flush the global SympatheticResonance engine BEFORE installing the
    // new kit's coupling amount. The engine's resonator pool holds
    // orphaned resonators (filter state in y1s_/y2s_) from voices the
    // prior kit triggered. SympatheticResonance::noteOff(voiceId) detaches
    // the owner but leaves actives_[idx] = true, so those resonators keep
    // ringing at their Q-determined decay rate. If the prior kit had a
    // large globalCoupling and the new kit has a smaller one (or zero),
    // the orphans never get re-driven and keep producing a sustained
    // tonal residue above the silence threshold -- voices register fresh
    // partials but the engine's existing state dominates the output. The
    // user's "infinite ring" reproduces exactly this scenario
    // (test_kit_switch_infinite_ring.cpp test 3).
    //
    // The reset must come BEFORE the amount is applied; otherwise the
    // smoother target would be wiped by the reset. resetCouplingState() does
    // both, in that order, with the kit's Global Coupling stored first.
    const float gcF = static_cast<float>(kit.globalCoupling);
    const float sbF = static_cast<float>(kit.snareBuzz);
    const float trF = static_cast<float>(kit.tomResonance);
    const float cdF = static_cast<float>(kit.couplingDelayMs);
    globalCoupling_.store(gcF, std::memory_order_relaxed);
    snareBuzz_.store(sbF, std::memory_order_relaxed);
    tomResonance_.store(trF, std::memory_order_relaxed);
    couplingDelayMs_.store(cdF, std::memory_order_relaxed);
    resetCouplingState();

    // Phase 9: master gain.
    masterGainNorm_.store(static_cast<float>(kit.masterGainNorm),
                          std::memory_order_relaxed);

    // Refresh categories and recompute the coupling matrix.
    for (int i = 0; i < kNumPads; ++i)
        padCategories_[static_cast<size_t>(i)] = effectivePadCategory(i);
    recomputeCouplingMatrix();
}

void Processor::resetCouplingState() noexcept
{
    // T1.19 (MB-067, MB-115, MB-094): SympatheticResonance::prepare and
    // reset both clear the amount, so the stored Global Coupling is applied
    // after the flush; otherwise coupling stays silent until the knob moves.
    couplingEngine_.reset();
    couplingDelay_.reset();
    energyEnvelope_ = 0.0f;
    couplingEngine_.setAmount(globalCoupling_.load(std::memory_order_relaxed));
}

void Processor::applyPadPresetSnapshot(int pad, const State::PadPresetSnapshot& preset)
{
    auto& cfg = voicePool_.padConfigMut(pad);
    State::applyPadPresetSnapshot(preset, cfg);

    // Exciter, body and pitch-env time can change the pad's category. The pad
    // keeps its macros (FR-061); they now offset the preset's fields.
    padCategories_[static_cast<size_t>(pad)] = effectivePadCategory(pad);
    recomputeCouplingMatrix();
}

// ==============================================================================
// activateBus -- FR-045: track bus activation for multi-bus output
// ==============================================================================

tresult PLUGIN_API Processor::activateBus(MediaType type, BusDirection dir,
                                           int32 index, TBool state)
{
    auto result = AudioEffect::activateBus(type, dir, index, state);
    if (result == kResultTrue && type == kAudio && dir == kOutput)
    {
        if (index >= 0 && std::cmp_less(index, busActive_.size()))
        {
            busActive_[static_cast<std::size_t>(index)] = (state != 0);
            // FR-045: main bus (index 0) is always active
            busActive_[0] = true;
        }
    }
    return result;
}

// Audit finding 14: lock every output bus to stereo. The render path
// (process(), :713-746) reads channelBuffers32[0] and [1] unconditionally on
// every bus, so the base AudioEffect::setBusArrangements default (which accepts
// any host-proposed arrangement, including mono/quad) would let a non-compliant
// host trigger an out-of-bounds heap read on the audio thread. Membrum is an
// instrument: no audio inputs, 1 main + 15 aux stereo outputs.
tresult PLUGIN_API Processor::setBusArrangements(
    SpeakerArrangement* /*inputs*/, int32 numIns,
    SpeakerArrangement* outputs, int32 numOuts)
{
    // No audio input buses exist; reject any input arrangement request.
    if (numIns != 0)
        return kResultFalse;

    if (numOuts < 0 || numOuts > kMaxOutputBuses)
        return kResultFalse;

    // Every proposed output bus must be stereo.
    for (int32 i = 0; i < numOuts; ++i)
    {
        if (outputs[i] != SpeakerArr::kStereo)
            return kResultFalse;
    }

    return kResultTrue;
}

tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& setup)
{
    // NOTE: denormal (FTZ/DAZ) control used to live here. MXCSR is per-thread,
    // so setting it on the setup thread never reached the audio thread; it is
    // now a ScopedDenormalMode at the top of process(). Do not re-add it here.

    // MB-001: processing is stopped here; apply a load still pending.
    const std::scoped_lock lifecycle(lifecycleMutex_);
    consumePendingLoad();

    // T1.6: no strike-norm measurement may run while the pool is re-prepared.
    voicePool_.strikeNorm().stopWorker();

    sampleRate_ = setup.sampleRate;
    maxBlockSize_ = setup.maxSamplesPerBlock;
    voicePool_.prepare(sampleRate_, maxBlockSize_);
    voicePool_.setMaxPolyphony(maxPolyphony_.load());
    voicePool_.setVoiceStealingPolicy(
        static_cast<VoiceStealingPolicy>(voiceStealingPolicy_.load()));

    // Phase 5: prepare coupling engine and delay
    couplingEngine_.prepare(sampleRate_);
    couplingDelay_.prepare(sampleRate_, 0.002f);  // 2 ms max delay
    voicePool_.setCouplingEngine(&couplingEngine_);
    resetCouplingState();  // MB-067 / MB-115: re-applies Global Coupling

    // Gain-staging Step 1: main-bus true-peak brickwall limiter at -1 dBTP.
    busLimiter_.prepare(sampleRate_, static_cast<std::size_t>(maxBlockSize_));
    busLimiter_.setCeilingDb(-1.0f);

    return AudioEffect::setupProcessing(setup);
}

tresult PLUGIN_API Processor::setActive(TBool state)
{
    // MB-001: processing is stopped on both edges; a load still pending
    // applies here. A load on another thread waits for the flag to settle.
    const std::scoped_lock lifecycle(lifecycleMutex_);
    if (!state)
        active_.store(false, std::memory_order_release);
    consumePendingLoad();

    if (state)
    {
        voicePool_.prepare(sampleRate_, maxBlockSize_);
        // MB-094: the coupling resonators, delay and energy envelope go with
        // the voices, and the stored Global Coupling stays live.
        resetCouplingState();
        // T1.6: pads edited while processing runs are re-measured on this
        // low-priority worker, off the audio thread.
        voicePool_.strikeNorm().startWorker();
        voicePool_.setMaxPolyphony(maxPolyphony_.load());
        voicePool_.setVoiceStealingPolicy(
            static_cast<VoiceStealingPolicy>(voiceStealingPolicy_.load()));

        // Clear the bus limiter's detector + gain state so a reactivation does
        // not inherit stale gain reduction or oversampler history.
        busLimiter_.reset();

        // Phase 6 (T045): open the DataExchange queue for MetersBlock.
        if (dataExchangeHandler_)
        {
            Steinberg::Vst::ProcessSetup setup{};
            setup.sampleRate          = sampleRate_;
            setup.maxSamplesPerBlock  = maxBlockSize_;
            setup.processMode         = Steinberg::Vst::kRealtime;
            setup.symbolicSampleSize  = Steinberg::Vst::kSample32;
            dataExchangeHandler_->onActivate(setup);
        }

        active_.store(true, std::memory_order_release);
    }
    else
    {
        // T1.6: processing has stopped; so does the strike-norm worker.
        voicePool_.strikeNorm().stopWorker();

        // Phase 6 (T045): close the DataExchange queue cleanly.
        if (dataExchangeHandler_)
            dataExchangeHandler_->onDeactivate();

        // Reset publishers so stale values do not persist after bypass.
        padGlowPublisher_.reset();
    }
    return kResultOk;
}

// ==============================================================================
// Phase 6 (T045): connect/disconnect -- DataExchange lifecycle.
// ==============================================================================
tresult PLUGIN_API Processor::connect(IConnectionPoint* other)
{
    const auto result = AudioEffect::connect(other);
    if (result == kResultTrue)
    {
        auto configCallback = [](Steinberg::Vst::DataExchangeHandler::Config& config,
                                 const Steinberg::Vst::ProcessSetup& /*setup*/) {
            // sizeof(MetersBlock) rounded UP to the alignment: the SDK fallback
            // allocates each block with std::aligned_alloc(alignment, blockSize),
            // and macOS returns null for a size that is not a multiple of the
            // alignment (44 % 32 = 12), so every lockBlock() failed and no meter
            // block ever reached the controller there.
            constexpr Steinberg::uint32 kAlignment = 32;
            constexpr Steinberg::uint32 kBlockSize =
                ((static_cast<Steinberg::uint32>(sizeof(MetersBlock)) + kAlignment - 1u) /
                 kAlignment) * kAlignment;
            static_assert(kBlockSize % kAlignment == 0 && kBlockSize >= sizeof(MetersBlock));
            config.blockSize     = kBlockSize;
            config.numBlocks     = 4;
            config.alignment     = kAlignment;
            config.userContextID = kMetersDataExchangeUserContextId;
            return true;
        };
        dataExchangeHandler_ =
            std::make_unique<Steinberg::Vst::DataExchangeHandler>(this, configCallback);
        dataExchangeHandler_->onConnect(other, getHostContext());
    }
    return result;
}

tresult PLUGIN_API Processor::disconnect(IConnectionPoint* other)
{
    if (dataExchangeHandler_)
    {
        dataExchangeHandler_->onDisconnect(other);
        dataExchangeHandler_.reset();
    }
    return AudioEffect::disconnect(other);
}

// ==============================================================================
// notify() -- IMessage handler: the audition-pad messages and the kit- and
// pad-preset browser loads (LoadPresetState, LoadPadPreset); everything else
// delegates to the SDK base.
// ==============================================================================
tresult PLUGIN_API Processor::notify(Steinberg::Vst::IMessage* message)
{
    if (message == nullptr)
        return AudioEffect::notify(message);

    const auto* id = message->getMessageID();
    if (id == nullptr)
        return AudioEffect::notify(message);

    if (std::strcmp(id, "AuditionPad") == 0)
    {
        auto* attrs = message->getAttributes();
        if (attrs == nullptr) return kResultOk;
        Steinberg::int64 midi = 0;
        Steinberg::int64 velocity = 100;
        attrs->getInt("midi", midi);
        attrs->getInt("velocity", velocity);
        const std::uint32_t word =
            0x4000u
            | (static_cast<std::uint32_t>(midi & 0x7F))
            | ((static_cast<std::uint32_t>(velocity & 0x7F)) << 7);
        pendingAudition_.store(word, std::memory_order_release);
        return kResultOk;
    }

    if (std::strcmp(id, "AuditionPadOff") == 0)
    {
        auto* attrs = message->getAttributes();
        if (attrs == nullptr) return kResultOk;
        Steinberg::int64 midi = 0;
        attrs->getInt("midi", midi);
        // Encode velocity=0 in the pending-audition word so the audio-thread
        // consume path takes the noteOff branch (same as a MIDI release).
        const std::uint32_t word =
            0x4000u
            | (static_cast<std::uint32_t>(midi & 0x7F))
            | (0u << 7);  // velocity 0
        pendingAudition_.store(word, std::memory_order_release);
        return kResultOk;
    }

    // LoadPresetState: a kit-preset browser load (R-202). The controller hands
    // over the kit blob; it takes the same parse and apply as setState, so the
    // voice and coupling flushes, noiseLayerGain, the body-damping sentinel and
    // the macro cache resync all run. FR-052: a kit load keeps the selected pad.
    // MB-001: parsed here (message thread), applied by the next process().
    if (std::strcmp(id, "LoadPresetState") == 0)
    {
        auto* attrs = message->getAttributes();
        if (attrs == nullptr) return kResultFalse;
        const void* data = nullptr;
        Steinberg::uint32 size = 0;
        if (attrs->getBinary("bytes", data, size) != kResultOk || data == nullptr)
            return kResultFalse;

        const auto* first = static_cast<const char*>(data);
        std::vector<char> bytes(first, first + size);
        Steinberg::MemoryStream stream(bytes.data(),
                                       static_cast<Steinberg::TSize>(bytes.size()));
        State::KitSnapshot kit;
        if (State::readKitBlob(&stream, kit) != kResultOk)
            return kResultFalse;
        publishLoad([&kit](PendingLoad& load) {
            load.hasKit = true;
            load.kit = kit;
            load.keepSelectedPad = true;
            load.hasPadPreset.fill(false);  // a kit replaces unapplied pad presets
        });
        return kResultOk;
    }

    // LoadPadPreset: a pad-preset browser load (R-203). The controller hands
    // over the pad blob and the target pad; State::applyPadPresetSnapshot is
    // the one apply (sound fields incl. noiseLayerGain and the body-damping
    // sentinel; FR-061 keeps choke, bus, coupling, macros, enabled and pan).
    if (std::strcmp(id, "LoadPadPreset") == 0)
    {
        auto* attrs = message->getAttributes();
        if (attrs == nullptr) return kResultFalse;
        const void* data = nullptr;
        Steinberg::uint32 size = 0;
        Steinberg::int64 pad = -1;
        if (attrs->getBinary("bytes", data, size) != kResultOk || data == nullptr ||
            attrs->getInt("pad", pad) != kResultOk || pad < 0 || pad >= kNumPads)
            return kResultFalse;

        const auto* first = static_cast<const char*>(data);
        std::vector<char> bytes(first, first + size);
        Steinberg::MemoryStream stream(bytes.data(),
                                       static_cast<Steinberg::TSize>(bytes.size()));
        State::PadPresetSnapshot preset;
        if (State::readPadPresetBlob(&stream, preset) != kResultOk)
            return kResultFalse;
        // MB-001: applied by the next process(), after a pending kit.
        const auto padIdx = static_cast<std::size_t>(pad);
        publishLoad([&preset, padIdx](PendingLoad& load) {
            load.hasPadPreset[padIdx] = true;
            load.padPresets[padIdx] = preset;
        });
        return kResultOk;
    }

    return AudioEffect::notify(message);
}

} // namespace Membrum
