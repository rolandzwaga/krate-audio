// ==============================================================================
// Profundum - Audio Processor implementation   Plan section 2.5
// ==============================================================================
// Wraps one Krate::DSP::ProfundumEngine unchanged (no DSP lives here): buses
// (T008), parameter latching (T009), the 36-byte state (T011), setupProcessing /
// latency / guards (T013), the render loop (T014) and sample-accurate event
// order (T015). Every body was written test-first against profundum_tests.
// ==============================================================================

#include "processor/processor.h"

#include "engine/profundum_engine_config.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/scoped_denormal_mode.h>

#include <algorithm>
#include <memory>
#include <utility>

namespace Profundum {

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

// Plan 3.2 (V-proc:48-54): clamp into [0, total - 1]; past-the-end -> last
// sample, negative -> 0 (FR-025). total > 0 here (guard 5 returned otherwise).
[[nodiscard]] std::int32_t clampOffset(int32 o, std::size_t total) noexcept {
    const auto last = static_cast<std::int32_t>(total - 1u);
    if (o < 0) {
        return 0;
    }
    return (o > last) ? last : static_cast<std::int32_t>(o);
}

// Plan 3.2 (V-proc:58-60, FR-026): v > 0 maps to [1, 127], so a tiny velocity
// (0.003) is still a note-on and never the engine's velocity-0 note-off
// (engine.h:264-270).
[[nodiscard]] std::uint8_t quantiseVelocity(float v) noexcept {
    return static_cast<std::uint8_t>(std::clamp(v * 127.0f + 0.5f, 1.0f, 127.0f));
}

[[nodiscard]] bool isMidiPitch(int16 pitch) noexcept {
    return pitch >= 0 && pitch <= 127;
}

}  // namespace

Processor::Processor() {
    setControllerClass(kControllerUID);
}

Processor::~Processor() = default;

// FR-020 / FR-022 (plan 2.5.2).
tresult PLUGIN_API Processor::initialize(FUnknown* context) {
    const tresult result = AudioEffect::initialize(context);
    if (result != kResultOk) {
        return result;
    }

    // FR-020: instrument shape - one event input, one stereo audio output.
    // Deliberately NO addAudioInput(): an input bus would also make the AU
    // wrapper create an IO element au-info.plist does not declare (-10875).
    addEventInput(STR16("Event In"));
    addAudioOutput(STR16("Main Out"), SpeakerArr::kStereo);

    // FR-022: heap, non-RT, exactly once. No setObserver() call.
    engine_ = std::make_unique<Krate::DSP::ProfundumEngine>();

    return kResultOk;
}

tresult PLUGIN_API Processor::terminate() {
    // `= nullptr`, not `.reset()`: the engine exposes its own reset() member,
    // so `engine_.reset()` would read as "reset DSP state" when it DESTROYS the
    // object (readability-ambiguous-smartptr-reset-call).
    engine_ = nullptr;
    prepared_ = false;
    return AudioEffect::terminate();
}

// FR-021 (plan 2.5.3). The base accepts whatever the host proposes, so reject:
//   (a) any audio input arrangement - no input bus exists;
//   (b) anything but exactly one output bus;
//   (c) a non-stereo output - the render path reads channelBuffers32[0] and [1].
// The rejection is NOT the guard: process() carries its own numChannels < 2
// early-out (FR-030).
tresult PLUGIN_API Processor::setBusArrangements(SpeakerArrangement* /*inputs*/, int32 numIns,
                                                 SpeakerArrangement* outputs, int32 numOuts) {
    if (numIns != 0) {
        return kResultFalse;
    }
    if (numOuts != 1) {
        return kResultFalse;
    }
    if (outputs == nullptr || outputs[0] != SpeakerArr::kStereo) {
        return kResultFalse;
    }
    return kResultTrue;
}

// FR-023 (plan 2.5.4, Q6, P-12). Refuse, never clamp: a refused rate or sample
// size leaves the processor unprepared and does NOT call the base class, so
// processSetup keeps the last accepted setup and process() takes guard 7.
tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& setup) {
    // 1. Out-of-order host call: no engine -> stay unprepared.
    if (engine_ == nullptr) {
        prepared_ = false;
        return AudioEffect::setupProcessing(setup);
    }
    // 2. Q6: db_utils.h:125 (double overload) is fast-math immune; engine.h:118-119.
    const double sr = setup.sampleRate;
    if (!Krate::DSP::detail::isFinite(sr) || sr < Krate::DSP::ProfundumEngine::kMinSampleRate ||
        sr > Krate::DSP::ProfundumEngine::kMaxSampleRate) {
        prepared_ = false;
        return kResultFalse;  // the engine is NOT prepared by this call
    }
    // 2b. P-12: the base class overwrites processSetup BEFORE it refuses a size
    //     (vstaudioeffect.cpp:151-156), and guard 3 cannot catch a 64-bit block
    //     (channelBuffers32/64 share a union, ivstaudioprocessor.h:209-213).
    if (canProcessSampleSize(setup.symbolicSampleSize) != kResultTrue) {  // vstaudioeffect.cpp:170-172
        prepared_ = false;
        return kResultFalse;
    }
    // 3. engine.h:147; allocates (control thread).
    engine_->prepare(sr, makeProfundumEngineConfig());
    // 4. smoother.h:160
    outputGain_.configure(kOutputGainSmoothMs, static_cast<float>(sr));
    snapGainPending_ = true;
    // 5. setState() may have run first; the engine keeps pending values across
    //    prepare (engine.h:144-146) and applies them at the first grid point.
    pushParamsToEngine();
    // 6.
    prepared_ = true;
    return AudioEffect::setupProcessing(setup);
}

// FR-031 (plan 2.5.5, P-3).
tresult PLUGIN_API Processor::setActive(TBool state) {
    if (state != 0) {
        snapGainPending_ = true;  // no allocation (SC-017.7)
    } else if (engine_ != nullptr && prepared_) {
        (*engine_).reset();  // engine.h:199-201: control thread, bounded (the engine's reset, not the unique_ptr's)
    }
    return AudioEffect::setActive(state);
}

// FR-024, FR-025, FR-029, FR-030 (plan 2.5.6, 3.2).
tresult PLUGIN_API Processor::process(ProcessData& data) {
    const Krate::DSP::ScopedDenormalMode denormalGuard;  // FIRST statement (FR-024.1)
    processParameterChanges(data.inputParameterChanges);  // FR-024.2: before the guards
    if (data.numOutputs <= 0 || data.outputs == nullptr) {
        return kResultOk;  // guard 2: parameter-only call
    }
    if (data.outputs[0].channelBuffers32 == nullptr) {
        return kResultOk;  // guard 3
    }
    if (data.outputs[0].numChannels < 2) {
        return kResultOk;  // guard 4
    }
    if (data.numSamples <= 0) {
        return kResultOk;  // guard 5
    }
    const auto total = static_cast<std::size_t>(data.numSamples);
    float* outL = data.outputs[0].channelBuffers32[0];
    float* outR = data.outputs[0].channelBuffers32[1];
    if (outL == nullptr || outR == nullptr) {
        return kResultOk;  // guard 6
    }
    if (!prepared_ || engine_ == nullptr) {  // guard 7: the not-ready path
        std::fill_n(outL, total, 0.0f);
        std::fill_n(outR, total, 0.0f);
        data.outputs[0].silenceFlags = 3;
        return kResultOk;
    }

    pushParamsToEngine();  // FR-024.4: every rendered block (change-only no-ops in the engine)
    if (data.processContext != nullptr &&
        (data.processContext->state & ProcessContext::kTempoValid) != 0) {
        // FR-027; engine.h:299-303 keeps the previous tempo for a non-finite or <= 0 value.
        engine_->setTempoBpm(static_cast<float>(data.processContext->tempo));
    }
    outputGain_.setTarget(globalParams_.outputGain.load(std::memory_order_relaxed));  // smoother.h:170
    if (snapGainPending_) {
        outputGain_.snapTo(outputGain_.getTarget());  // smoother.h:263
        snapGainPending_ = false;
    }

    // Plan 3.2 / FR-025: the first kMaxEventsPerBlock events, offsets clamped,
    // stable-sorted by offset.
    const std::size_t numSorted = buildEventOrder(data.inputEvents, total);

    // Plan 3.2 overflow queue (V-proc:375-415): events past kMaxEventsPerBlock are
    // NOT stored. They are read straight from data.inputEvents in list order once
    // the sorted queue is exhausted, each at max(clampOffset, previous effective
    // offset) seeded with the last sorted offset - non-decreasing, so the cursor
    // never rewinds and nothing is dropped or allocated.
    const int32 eventCount = (data.inputEvents != nullptr) ? data.inputEvents->getEventCount() : 0;
    const std::size_t overflowEnd =
        (eventCount > 0) ? std::max(static_cast<std::size_t>(eventCount), kMaxEventsPerBlock)
                         : kMaxEventsPerBlock;
    std::size_t next = 0;                           // sorted-queue read index
    std::size_t overflowNext = kMaxEventsPerBlock;  // overflow-queue list index
    std::int32_t overflowMax = (numSorted > 0) ? eventOrder_[numSorted - 1u].offset : 0;

    // The ONE "next pending event" helper feeding both the dispatch loop and the
    // slice-end computation (plan 2.5.6): sorted queue first, then overflow.
    Event pending{};
    std::int32_t pendingOffset = 0;
    bool havePending = false;
    const auto loadPending = [&]() noexcept {
        while (!havePending) {
            if (next < numSorted) {
                if (data.inputEvents->getEvent(eventOrder_[next].listIndex, pending) == kResultOk) {
                    pendingOffset = eventOrder_[next].offset;
                    havePending = true;
                }
                ++next;
            } else if (overflowNext < overflowEnd) {
                if (data.inputEvents->getEvent(static_cast<int32>(overflowNext), pending) ==
                    kResultOk) {
                    overflowMax = std::max(clampOffset(pending.sampleOffset, total), overflowMax);
                    pendingOffset = overflowMax;
                    havePending = true;
                }
                ++overflowNext;
            } else {
                return;
            }
        }
    };

    std::size_t cursor = 0;
    lastSliceCount_ = 0;
    while (cursor < total) {
        // 1. Every event due at this slice start (a WHILE: same-offset events all
        //    fire, so equal offsets never make a zero-length slice).
        loadPending();
        while (havePending && std::cmp_less_equal(pendingOffset, cursor)) {
            dispatchEvent(pending);  // FR-026
            havePending = false;
            loadPending();
        }
        // 2. Slice end. Every offset <= cursor was consumed above, so sliceEnd > cursor.
        //    No slice cap: the engine splits on its own 32-sample grid (engine.h:420-440).
        const std::size_t sliceEnd = havePending ? static_cast<std::size_t>(pendingOffset) : total;
        const std::size_t n = sliceEnd - cursor;
        engine_->processBlock(outL + cursor, outR + cursor, n);  // engine.h:422; no scratch (FR-029)
        applyOutputGain(outL + cursor, outR + cursor, n);
        ++lastSliceCount_;
        cursor = sliceEnd;
    }

    data.outputs[0].silenceFlags = 0;  // FR-024.6: every rendered block
    return kResultOk;
}

// FR-032 (plan 2.5.9, Q5): constant regardless of prepared_. Never
// engine_->latencySamples(), which reads 0 until prepare (true_peak_limiter.h:76).
// No restartComponent and no IMessage (SC-014).
uint32 PLUGIN_API Processor::getLatencySamples() {
    return engine_ != nullptr
               ? static_cast<uint32>(Krate::DSP::ProfundumEngine::kLimiterLookaheadSamples)  // engine.h:107
               : 0u;
}

// FR-044 / FR-045 (plan 2.5.10, layout 3.4). Touches the atomics only: NO engine
// call. The unconditional per-block push in process() (or setupProcessing) applies
// the loaded values. A future version is rejected before any field is read; each
// loader returns false at its first failed read, so the chain stops there and leaves every
// later field unchanged (a stream cut at byte 20 keeps the macros).
tresult PLUGIN_API Processor::setState(IBStream* state) {
    if (state == nullptr) {
        return kResultFalse;
    }
    IBStreamer s(state, kLittleEndian);
    int32 version = 0;
    if (!s.readInt32(version)) {
        return kResultFalse;
    }
    if (version > kCurrentStateVersion) {
        return kResultFalse;
    }
    if (loadGlobalParams(globalParams_, s)) {
        static_cast<void>(loadMacroParams(macroParams_, s));
    }
    return kResultOk;
}

tresult PLUGIN_API Processor::getState(IBStream* state) {
    if (state == nullptr) {
        return kResultFalse;
    }
    IBStreamer s(state, kLittleEndian);
    s.writeInt32(kCurrentStateVersion);
    saveGlobalParams(globalParams_, s);
    saveMacroParams(macroParams_, s);
    return kResultOk;
}

// ---- private helpers (T004 STUBS: empty) ----

// FR-042 / FR-043 (plan 2.5.8): the LAST point of each queue wins; a non-finite
// value changes nothing (bit-pattern test, fast-math immune); anything else is
// clamped to [0, 1] and dispatched to its owning band. Other IDs are ignored.
void Processor::processParameterChanges(IParameterChanges* changes) noexcept {
    if (changes == nullptr) {
        return;
    }
    const int32 numQueues = changes->getParameterCount();
    for (int32 i = 0; i < numQueues; ++i) {
        IParamValueQueue* queue = changes->getParameterData(i);
        if (queue == nullptr) {
            continue;
        }
        const int32 count = queue->getPointCount();
        if (count <= 0) {
            continue;
        }
        int32 offset = 0;
        ParamValue value = 0.0;
        if (queue->getPoint(count - 1, offset, value) != kResultTrue) {
            continue;
        }
        if (!Krate::DSP::detail::isFinite(value)) {  // db_utils.h:125 (double)
            continue;
        }
        value = std::clamp(value, 0.0, 1.0);
        const ParamID id = queue->getParameterId();
        if (id < kGlobalParamRangeEnd) {
            handleGlobalParamChange(globalParams_, id, value);
        } else if (id < kMacroParamRangeEnd) {
            handleMacroParamChange(macroParams_, id, value);
        }
        // id >= kMacroParamRangeEnd: no band owns it, ignored (FR-042).
    }
}

// FR-023.5, FR-024.4 (plan 2.5.7): O(1) pending stores, no allocation. A value
// equal to the applied one changes nothing at the next grid point.
void Processor::pushParamsToEngine() noexcept {
    using Engine = Krate::DSP::ProfundumEngine;
    const int mode = globalParams_.voiceMode.load(std::memory_order_relaxed);
    engine_->setVoiceMode(mode == 1 ? Engine::VoiceMode::Poly : Engine::VoiceMode::Mono);  // engine.h:326
    engine_->setPolyphony(static_cast<std::size_t>(
        clampPolyphony(globalParams_.polyphony.load(std::memory_order_relaxed))));  // engine.h:330
    engine_->setOutputCeilingDb(globalParams_.outputCeilingDb.load(std::memory_order_relaxed));  // engine.h:364
    engine_->setMacros(macroValues(macroParams_));  // engine.h:371
}

// Plan 3.2 (V-proc:1097-1124): the first kMaxEventsPerBlock events, offsets
// clamped, placed by STABLE insertion sort on offset (strict `>` shift, so equal
// offsets keep list order: a same-offset NoteOff -> NoteOn pair keeps its
// meaning). O(n) for an already-sorted (VST3-conformant) list, O(n^2) worst
// case, n <= 1024. Events past kMaxEventsPerBlock are the overflow queue, read
// by process().
std::size_t Processor::buildEventOrder(IEventList* events, std::size_t total) noexcept {
    if (events == nullptr) {
        return 0;
    }
    const int32 count = events->getEventCount();
    if (count <= 0) {
        return 0;
    }
    const std::size_t n = std::min(static_cast<std::size_t>(count), kMaxEventsPerBlock);
    std::size_t stored = 0;
    for (std::size_t i = 0; i < n; ++i) {
        Event e{};
        if (events->getEvent(static_cast<int32>(i), e) != kResultOk) {
            continue;
        }
        const EventSlot slot{.offset = clampOffset(e.sampleOffset, total),
                             .listIndex = static_cast<std::int32_t>(i)};
        std::size_t j = stored;
        while (j > 0 && eventOrder_[j - 1u].offset > slot.offset) {
            eventOrder_[j] = eventOrder_[j - 1u];
            --j;
        }
        eventOrder_[j] = slot;
        ++stored;
    }
    return stored;
}

// FR-026 (plan 3.2; V-proc:1129-1160 without the latch). A pitch outside
// [0, 127] is dropped before the uint8_t cast; every other event type is ignored.
void Processor::dispatchEvent(const Event& e) noexcept {
    switch (e.type) {
        case Event::kNoteOnEvent: {
            if (!isMidiPitch(e.noteOn.pitch)) {
                return;
            }
            const auto pitch = static_cast<std::uint8_t>(e.noteOn.pitch);
            if (e.noteOn.velocity > 0.0f) {
                engine_->noteOn(pitch, quantiseVelocity(e.noteOn.velocity));  // engine.h:264
            } else {
                engine_->noteOff(pitch);  // velocity 0 == note-off (engine.h:280)
            }
            return;
        }
        case Event::kNoteOffEvent: {
            if (!isMidiPitch(e.noteOff.pitch)) {
                return;
            }
            engine_->noteOff(static_cast<std::uint8_t>(e.noteOff.pitch));  // engine.h:280
            return;
        }
        default:
            return;
    }
}

// FR-028 (plan 2.5.6, 3.1): one smoother step per output sample, in place, after
// the limiter. g is in [0, 1], so |out| <= the ceiling still holds.
void Processor::applyOutputGain(float* outL, float* outR, std::size_t n) noexcept {
    for (std::size_t s = 0; s < n; ++s) {
        const float g = outputGain_.process();  // smoother.h:197
        outL[s] *= g;
        outR[s] *= g;
    }
}

}  // namespace Profundum
