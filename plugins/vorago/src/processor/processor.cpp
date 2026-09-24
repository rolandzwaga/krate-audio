// ==============================================================================
// Vorago - Audio Processor implementation
// ==============================================================================
// T005 STUB: every body below is behaviour-free (forwards to AudioEffect or
// returns a neutral value), so later test TUs compile and fail at run time.
// Owners of the real bodies, in sequence: T011 (buses), T012 (parameter latching
// + state), T013 (setupProcessing / latency / FR-030 guards), T014 (render loop),
// T015 (event ordering / slicing), T016 (setActive / allocation / convergence).
// ==============================================================================

#include "processor/processor.h"

#include "engine/vorago_engine_config.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/vstspeaker.h"

#include <krate/dsp/core/scoped_denormal_mode.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace Vorago {

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

// Plan 3.2: clamp into [0, total - 1]; past-the-end -> last sample, negative -> 0
// (FR-025). total > 0 here (process() returns before any event work otherwise).
[[nodiscard]] std::int32_t clampOffset(int32 o, std::size_t total) noexcept {
    const auto last = static_cast<std::int32_t>(total - 1u);
    if (o < 0) {
        return 0;
    }
    return (o > last) ? last : static_cast<std::int32_t>(o);
}

// Plan 3.3 (FR-031): v > 0 maps to [1, 127], so a tiny velocity (0.003) is still a
// note-on and never the engine's velocity-0 note-off (vorago_engine.h:568-571).
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
    // Model: plugins/seraphis/src/processor/processor.cpp:560-561.
    // Anti-model: plugins/ruinae/src/processor/processor.cpp:56.
    addEventInput(STR16("Event In"));
    addAudioOutput(STR16("Main Out"), SpeakerArr::kStereo);

    // FR-022: heap, non-RT, exactly once.
    engine_ = std::make_unique<Krate::DSP::VoragoEngine>();
    cavern_ = std::make_unique<Krate::DSP::CavernVerb>();

    return kResultOk;
}

tresult PLUGIN_API Processor::terminate() {
    // `= nullptr`, not `.reset()`: both pointees expose their own reset() member,
    // so `engine_.reset()` would read as "reset DSP state" when it DESTROYS the
    // object (readability-ambiguous-smartptr-reset-call).
    engine_ = nullptr;
    cavern_ = nullptr;
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

// FR-023 / FR-028 (plan 2.5.4), in FR-023 order. Not the audio thread: prepare()
// is the only allocating path of both components.
tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& setup) {
    // 0. Out-of-order host calls (pluginval strictness 5): stay unprepared.
    if (engine_ == nullptr || cavern_ == nullptr) {
        return AudioEffect::setupProcessing(setup);
    }
    const double sr = setup.sampleRate;  // the engine floors a bad rate itself

    // 1. Both configs from the CONSTANT kMaxBlockSamples, never maxSamplesPerBlock.
    // 2. Seed BEFORE prepare: prepare derives every slot seed from it.
    engine_->setSeed(kEngineSeed);
    // 3.
    engine_->prepare(sr, makeVoragoEngineConfig(kMaxBlockSamples));
    cavern_->prepare(sr, makeVoragoCavernConfig(kMaxBlockSamples));

    // 4. Polyphony FROM THE PARAMETER (setState may precede this).
    const std::size_t poly =
        clampPolyphony(globalParams_.polyphony.load(std::memory_order_relaxed));
    engine_->setPolyphony(poly);
    ++setPolyphonyCalls_;
    lastPushedPolyphony_ = engine_->getPolyphony();

    // 5. Master gain: configure, arm the first-block snap.
    masterGain_.configure(kMasterGainSmoothMs, static_cast<float>(sr));
    snapGainPending_ = true;

    // 6. No scratch to size. prepared_ last.
    prepared_ = true;
    return AudioEffect::setupProcessing(setup);
}

// FR-032 (plan 2.5.5). Not the audio thread. Activation re-arms the first-block
// gain snap and NOTHING ELSE (SC-026: zero allocations); deactivation clears
// every voice and the cavern tail. Both calls are allocation-free no-ops on an
// unprepared component (vorago_engine.h:447-450), so an out-of-order
// setActive(false) before setupProcessing is safe.
tresult PLUGIN_API Processor::setActive(TBool state) {
    if (state != 0) {
        snapGainPending_ = true;
    } else {
        if (engine_ != nullptr) {
            engine_->silence();  // vorago_engine.h:447
        }
        if (cavern_ != nullptr) {
            // Dereference: the pointee's reset() clears the tail and keeps the object
            // (readability-ambiguous-smartptr-reset-call).
            (*cavern_).reset();  // cavern_verb.h:487
        }
    }
    return AudioEffect::setActive(state);
}

// Plan 2.5.6. T012: denormal guard, parameter latch, FR-030 shape guards.
tresult PLUGIN_API Processor::process(ProcessData& data) {
    const Krate::DSP::ScopedDenormalMode denormalGuard;  // FR-029, first statement

    // FR-043: latched BEFORE the shape guards, so a parameter-only call
    // (numOutputs = 0 / numSamples = 0) still updates the atomics.
    processParameterChanges(data.inputParameterChanges);

    // FR-030 guard order (binding; plugins/seraphis/src/processor/processor.cpp:1325-1356).
    if (data.numOutputs <= 0 || data.outputs == nullptr) {
        return kResultOk;
    }
    if (data.outputs[0].channelBuffers32 == nullptr) {
        return kResultOk;
    }
    if (data.outputs[0].numChannels < 2) {
        return kResultOk;
    }
    if (data.numSamples <= 0) {
        return kResultOk;
    }
    const auto total = static_cast<std::size_t>(data.numSamples);
    float* outL = data.outputs[0].channelBuffers32[0];
    float* outR = data.outputs[0].channelBuffers32[1];
    if (outL == nullptr || outR == nullptr) {
        return kResultOk;
    }
    // Not ready (process() before setupProcessing, or after terminate): silence.
    if (!prepared_ || engine_ == nullptr || cavern_ == nullptr) {
        std::fill_n(outL, total, 0.0f);
        std::fill_n(outR, total, 0.0f);
        data.outputs[0].silenceFlags = 3;
        return kResultOk;
    }

    // FR-024 step 0 and step 2, once per process() (P-9, P-10): the atomics are
    // latched above and cannot change inside this call, and both matrix calls
    // write stored bases that survive a mid-block note-on / steal / retrigger.
    pushGlobalParams();
    macros_.apply(*engine_);                                       // vorago_macro_matrix.h:954
    applyCavernTargets(*cavern_, macros_.computeCavernTargets());  // :1026 + FR-034a

    // Plan 3.2 / FR-025 / FR-026: slice at every event offset, the block end, or
    // cursor + kMaxBlockSamples, whichever comes first.
    const std::size_t numSorted = buildEventOrder(data.inputEvents, total);

    // Plan 3.2 step 3: events past kMaxEventsPerBlock are NOT stored. They form a
    // second queue read straight from data.inputEvents in list order once the
    // sorted queue is exhausted, each at max(clampOffset, previous effective
    // offset) - non-decreasing, so the cursor never rewinds and nothing is dropped.
    const Steinberg::int32 eventCount =
        (data.inputEvents != nullptr) ? data.inputEvents->getEventCount() : 0;
    const std::size_t overflowEnd = (eventCount > 0)
        ? std::max(static_cast<std::size_t>(eventCount), kMaxEventsPerBlock)
        : kMaxEventsPerBlock;
    std::size_t next = 0;                          // sorted-queue read index
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
        // 1. Every event due at this slice start (a WHILE: same-offset events all fire).
        loadPending();
        while (havePending && std::cmp_less_equal(pendingOffset, cursor)) {
            dispatchEvent(pending);  // FR-031
            havePending = false;
            loadPending();
        }
        // 2. Slice end. Every offset <= cursor was consumed above, so sliceEnd > cursor.
        std::size_t sliceEnd = std::min(total, cursor + kMaxBlockSamples);
        if (havePending) {
            sliceEnd = std::min(sliceEnd, static_cast<std::size_t>(pendingOffset));
        }
        renderSlice(outL + cursor, outR + cursor, sliceEnd - cursor);
        ++lastSliceCount_;
        cursor = sliceEnd;
    }

    data.outputs[0].silenceFlags = 0;  // FR-024: every rendered block
    return kResultOk;
}

// FR-033 (plan 2.5.9): smear (2048) + cavern diffusion (1024) = 3072 after any
// prepare, at every rate. No restartComponent: an AudioEffect has no route to
// IComponentHandler.
uint32 PLUGIN_API Processor::getLatencySamples() {
    if (engine_ == nullptr || cavern_ == nullptr) {
        return 0u;
    }
    return static_cast<uint32>(engine_->getLatencySamples() + cavern_->getLatencySamples());
}

// FR-045 / FR-046 (plan 2.5.10, byte layout plan 3.4): 60 bytes, little-endian.
// Writes atomics only, so it is safe beside process(); no prepare is reachable.
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
        return kResultFalse;  // FR-046: a future version loads nothing
    }
    // A version < 1 is read as v1: there has never been another layout.
    if (loadGlobalParams(globalParams_, s)) {
        loadMacroParams(macroParams_, s);  // short stream: stop, keep the rest
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

// FR-043 (plan 2.5.8): the LAST point of each queue wins; routed by ID band.
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
        const ParamID id = queue->getParameterId();
        if (id < kGlobalParamRangeEnd) {
            handleGlobalParamChange(globalParams_, id, value);
        } else if (id < kMacroParamRangeEnd) {
            handleMacroParamChange(macroParams_, id, value);
        }
    }
}

// FR-024a (plan 2.5.7). Once per process(), never per slice (P-9).
void Processor::pushGlobalParams() noexcept {
    const std::size_t poly =
        clampPolyphony(globalParams_.polyphony.load(std::memory_order_relaxed));
    if (poly != lastPushedPolyphony_) {  // EDGE-TRIGGERED (FR-024a.1)
        engine_->setPolyphony(poly);     // vorago_engine.h:507
        ++setPolyphonyCalls_;
        lastPushedPolyphony_ = engine_->getPolyphony();  // :523
    }
    const float gain = globalParams_.masterGain.load(std::memory_order_relaxed);
    if (snapGainPending_) {
        masterGain_.snapTo(gain);  // smoother.h:263, first block after prepare / activate
        snapGainPending_ = false;
    } else {
        masterGain_.setTarget(gain);  // smoother.h:170
    }
}

// Plan 3.2 steps 1-2, 4: the first kMaxEventsPerBlock events, offsets clamped,
// placed by STABLE insertion sort on offset (strict `>` shift, so equal offsets
// keep list order: a same-offset NoteOff -> NoteOn pair keeps its meaning).
// O(n) for an already-sorted (VST3-conformant) list, O(n^2) worst case, n <= 1024.
// Events past kMaxEventsPerBlock are the overflow queue, read by process().
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

// FR-031 (plan 3.3). Pitch outside [0, 127] is dropped before the uint8_t cast;
// everything but note-on / note-off (CC64 included) is ignored.
void Processor::dispatchEvent(const Event& e) noexcept {
    switch (e.type) {
        case Event::kNoteOnEvent: {
            if (!isMidiPitch(e.noteOn.pitch)) {
                return;
            }
            const auto pitch = static_cast<std::uint8_t>(e.noteOn.pitch);
            if (e.noteOn.velocity > 0.0f) {
                engine_->noteOn(pitch, quantiseVelocity(e.noteOn.velocity));  // vorago_engine.h:564
            } else {
                engine_->noteOff(pitch);  // :601
            }
            return;
        }
        case Event::kNoteOffEvent: {
            if (!isMidiPitch(e.noteOff.pitch)) {
                return;
            }
            engine_->noteOff(static_cast<std::uint8_t>(e.noteOff.pitch));
            return;
        }
        default:
            return;
    }
}

// Plan 2.5.6, steps 3-6, all in place on the host buffers (no scratch, FR-028).
// Step 2 is NOT here: it runs once per process() (P-10). processOutputStage's
// internal 64-sample cadence is its own business and is not copied (FR-027).
void Processor::renderSlice(float* outL, float* outR, std::size_t n) noexcept {
    engine_->processStereoBlock(outL, outR, n);                // step 3, vorago_engine.h:891
    cavern_->processStereoBlock(outL, outR, outL, outR, n);    // step 4, cavern_verb.h:568
    renderGainAndOutputStage(outL, outR, n);                   // steps 5-6
}

// Steps 5-6 of FR-024, split out so SC-006's discrimination arm can measure the
// gain -> limiter order directly (ruling B-1, 2026-09-24).
void Processor::renderGainAndOutputStage(float* outL, float* outR, std::size_t n) noexcept {
    for (std::size_t s = 0; s < n; ++s) {                      // step 5, per sample
        const float g = masterGain_.process();                 // smoother.h:197
        outL[s] *= g;
        outR[s] *= g;
    }
    engine_->processOutputStage(outL, outR, n);                // step 6, limiter LAST (:1006)
}

}  // namespace Vorago
