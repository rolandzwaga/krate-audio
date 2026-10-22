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
#include "parameters/sound_param_table.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstattributes.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include "base/source/fstreamer.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/scoped_denormal_mode.h>
#include <krate/dsp/primitives/envelope_utils.h>
#include <krate/dsp/systems/morph_state.h>
#include <krate/dsp/systems/profundum_macro_matrix.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <memory>
#include <string_view>
#include <thread>
#include <utility>

namespace Profundum {

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

// FR-060 (plan 4.5.7): the ampEnv.releaseMs leaf, pinned at compile time against the law table.
constexpr std::uint8_t kLeafAmpReleaseMs = 21;
static_assert(std::string_view(Krate::DSP::BassMorphEngine::kLawTable[kLeafAmpReleaseMs].name) == "ampEnv.releaseMs",
              "getTailSamples reads leaf 21 as the amp release (morph_state.h:210)");

// FR-060: ceil(ms * fs / 1000) as an exact integer under /fp:fast and -ffast-math. Those modes may replace the
// division by a multiplication with the inexact reciprocal 0.001 (or reassociate it with the product), so a quotient
// that is exactly an integer can land one ulp above it and ceil() adds a sample. The quotient is corrected against
// q * 1000, which is exact for any tail length.
[[nodiscard]] double ceilMsToSamples(double ms, double fs) noexcept {
    const double x = ms * fs;
    double q = std::ceil(x / 1000.0);
    if ((q - 1.0) * 1000.0 >= x) {
        q -= 1.0;
    } else if (q * 1000.0 < x) {
        q += 1.0;
    }
    return q;
}

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

// FR-010: the LAST point of a queue, finite (bit-pattern test, fast-math immune) and clamped to [0, 1].
// False (nothing to apply) for an empty queue, a failed read or a non-finite value.
[[nodiscard]] bool lastPointOf(IParamValueQueue* queue, ParamValue& value) noexcept {
    if (queue == nullptr) {
        return false;
    }
    const int32 count = queue->getPointCount();
    if (count <= 0) {
        return false;
    }
    int32 offset = 0;
    ParamValue v = 0.0;
    if (queue->getPoint(count - 1, offset, v) != kResultTrue) {
        return false;
    }
    if (!Krate::DSP::detail::isFinite(v)) {  // db_utils.h:125 (double)
        return false;
    }
    value = std::clamp(v, 0.0, 1.0);
    return true;
}

using EndpointBlob = std::array<std::byte, Krate::DSP::kMorphStateBytes>;

// FR-040: one endpoint slot as its 201-byte serializeMorphState blob. Every slot word is written through a
// leaf law, so the assembled state is always valid (plan 1.1 consequence); should serialisation ever fail, the
// MorphState{} blob is written instead so the layout stays 510 bytes.
[[nodiscard]] EndpointBlob serializeSlot(const EndpointParams& slot) noexcept {
    EndpointBlob buf{};
    if (Krate::DSP::serializeMorphState(morphStateFromLeafBits(slot.load()), buf.data(), buf.size()) != buf.size()) {
        static_cast<void>(Krate::DSP::serializeMorphState(MorphState{}, buf.data(), buf.size()));
    }
    return buf;
}

// FR-044 (plan 4.5.3): the v2 chain after the global and macro blocks. Each step runs only if every earlier
// one read completely; a short read stops the chain, so every later field keeps its prior value. A rejected
// endpoint (all 201 bytes read) keeps its prior value and the chain continues. Atomics only: no engine call.
// Returns true, with @p blob filled, only when the chain reached and completely read the 20-byte runtime blob
// (FR-042: the caller hands it to the audio thread; any earlier stop means "cancel").
[[nodiscard]] bool loadV2Tail(IBStreamer& s, NoteParams& note, MorphControlParams& morph,
                              std::atomic<std::uint32_t>& seed, std::array<EndpointParams, 2>& endpoints,
                              Processor::RuntimeBlob& blob) noexcept {
    if (!loadNoteParams(note, s)) {
        return false;
    }
    if (!loadMorphControlParams(morph, s)) {
        return false;
    }
    uint32 seedValue = 0;
    if (!s.readInt32u(seedValue)) {
        return false;
    }
    seed.store(seedValue, std::memory_order_relaxed);
    for (EndpointParams& slot : endpoints) {
        EndpointBlob buf{};
        if (s.readRaw(buf.data(), static_cast<TSize>(buf.size())) != static_cast<TSize>(buf.size())) {
            return false;
        }
        MorphState decoded{};
        if (Krate::DSP::deserializeMorphState(buf.data(), buf.size(), decoded)) {
            slot.store(leafBitsOf(decoded));
            slot.dirty.store(true, std::memory_order_relaxed);
            slot.syncPending.store(true, std::memory_order_relaxed);
        }
    }
    return s.readRaw(blob.data(), static_cast<TSize>(blob.size())) == static_cast<TSize>(blob.size());
}

}  // namespace

Processor::Processor() {
    setControllerClass(kControllerUID);
}

Processor::~Processor() {
    if (syncTimer_.get() != nullptr) {
        syncTimer_->stop();  // terminate() was skipped: never leave a timer pointing at a destroyed callback
    }
}

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

    // FR-025 (plan 4.5.4): the 50 ms sender timer. Timer::create returns nullptr on Linux without an injected
    // factory (base/source/timer.cpp:346-351); there the next notify / setState / getState / setActive flushes.
    if (syncTimer_.get() == nullptr) {
        syncTimer_ = owned(Steinberg::Timer::create(this, kSyncTimerIntervalMs));
    }

    return kResultOk;
}

tresult PLUGIN_API Processor::terminate() {
    if (syncTimer_.get() != nullptr) {  // FR-025: released here, so no callback outlives the component
        syncTimer_->stop();
        syncTimer_ = nullptr;
    }
    // `= nullptr`, not `.reset()`: the engine exposes its own reset() member,
    // so `engine_.reset()` would read as "reset DSP state" when it DESTROYS the
    // object (readability-ambiguous-smartptr-reset-call).
    engine_ = nullptr;
    prepared_ = false;
    tailSampleRate_.store(0.0, std::memory_order_release);  // FR-060: no stale rate reports a tail
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
    // 3. engine.h:147; allocates (control thread). FR-043: the persisted seed is in force from prepare on
    //    (setSeed only stores it, engine.h:373; prepare ends in reset(), which derives the drift seeds).
    engine_->setSeed(seed_.load(std::memory_order_relaxed));
    // 3a. FR-064 (SC-018 AT-11): setState() may have run first. The endpoint slots and the engine setters go in
    //     BEFORE prepare: prepare resolves its initial state (voices, Mass law, dynamics, field, post stages) from
    //     the endpoints and position the engine holds (engine.h:147-196), so the first block renders the loaded
    //     state with no glide from the engine defaults. prepare keeps every pending setter, the endpoints and the
    //     target (engine.h:144-146). The runtime blob stays with process() step 9 (FR-042).
    applyEndpoints();
    pushParamsToEngine(static_cast<CompareMode>(compareMode_.load(std::memory_order_relaxed)));
    engine_->prepare(sr, makeProfundumEngineConfig());
    // 3b. FR-052 (Vorago processor.cpp:214): no note sounds across a re-prepare, so the sustain latch forgets
    //     everything without releasing.
    latch_.clearWithoutRelease();
    // 4. smoother.h:160
    outputGain_.configure(kOutputGainSmoothMs, static_cast<float>(sr));
    snapGainPending_ = true;
    // 5. (step 3a applied the endpoint slots and the setters before prepare.)
    // 6.
    prepared_ = true;
    tailSampleRate_.store(sr, std::memory_order_release);  // FR-060 (plan 4.5.7)
    return AudioEffect::setupProcessing(setup);
}

// FR-031 (plan 2.5.5, P-3). Phase 11 (plan 4.5.6): setActive(false) resets the compare to A.B (FR-033); the reset
// mode is reported to the controller when the mode actually changed, so a deactivation at A.B sends nothing. Both
// branches end in flushControllerSync() (FR-025), which allocates nothing while nothing is pending or no host
// context exists (SC-017.7).
tresult PLUGIN_API Processor::setActive(TBool state) {
    if (state != 0) {
        snapGainPending_ = true;  // no allocation (SC-017.7)
    } else {
        // FR-052 (plan 4.5.6; Vorago processor.cpp:250): release every latched note before the reset. Processing has
        // stopped, so the engine's event queue is safe to write here.
        latch_.releaseAll([this](std::uint8_t n) noexcept {
            if (engine_ != nullptr) {
                engine_->noteOff(n);
            }
        });
        const auto ab = static_cast<std::uint8_t>(CompareMode::AB);
        if (compareMode_.exchange(ab, std::memory_order_relaxed) != ab) {
            compareSyncPending_.store(true, std::memory_order_relaxed);
        }
        if (engine_ != nullptr && prepared_) {
            engine_->setSeed(seed_.load(std::memory_order_relaxed));  // FR-043: a loaded seed takes effect here
            (*engine_).reset();  // engine.h:199-201: control thread, bounded (the engine's reset, not the unique_ptr's)
        }
    }
    const tresult result = AudioEffect::setActive(state);
    flushControllerSync();
    return result;
}

// FR-024, FR-025, FR-029, FR-030 (plan 2.5.6, 3.2).
tresult PLUGIN_API Processor::process(ProcessData& data) {
    const Krate::DSP::ScopedDenormalMode denormalGuard;  // FIRST statement (FR-024.1)
    // Plan 4.5.1 step 1: the compare mode is read ONCE per call (FR-032).
    const auto cmp = static_cast<CompareMode>(compareMode_.load(std::memory_order_relaxed));
    applyPendingCopy();  // step 2, FR-030: before the parameters, so a same-block edit lands on top of the copy
    processParameterChanges(data.inputParameterChanges);  // FR-024.2: before the guards
    // Plan 4.5.1 step 4 (FR-052): every early return after the parameters still owes the latch this block's pedal
    // points (state only, and only when prepared; Vorago processor.cpp:839-846).
    if (data.numOutputs <= 0 || data.outputs == nullptr) {
        applyPendingPedalPointsImmediately();
        return kResultOk;  // guard 2: parameter-only call
    }
    if (data.outputs[0].channelBuffers32 == nullptr) {
        applyPendingPedalPointsImmediately();
        return kResultOk;  // guard 3
    }
    if (data.outputs[0].numChannels < 2) {
        applyPendingPedalPointsImmediately();
        return kResultOk;  // guard 4
    }
    if (data.numSamples <= 0) {
        applyPendingPedalPointsImmediately();
        return kResultOk;  // guard 5
    }
    const auto total = static_cast<std::size_t>(data.numSamples);
    float* outL = data.outputs[0].channelBuffers32[0];
    float* outR = data.outputs[0].channelBuffers32[1];
    if (outL == nullptr || outR == nullptr) {
        applyPendingPedalPointsImmediately();
        return kResultOk;  // guard 6
    }
    if (!prepared_ || engine_ == nullptr) {  // guard 7: the not-ready path
        std::fill_n(outL, total, 0.0f);
        std::fill_n(outR, total, 0.0f);
        data.outputs[0].silenceFlags = 3;
        applyPendingPedalPointsImmediately();  // step 5: unprepared, so the points are dropped (as in Vorago)
        return kResultOk;
    }

    // Plan 4.5.1 step 6 (FR-052): setState asked for the latch to forget everything without releasing.
    if (clearLatchPending_.exchange(false, std::memory_order_acquire)) {
        latch_.clearWithoutRelease();
    }

    applyEndpoints();         // plan 4.5.1 step 7 (FR-013, FR-055)
    pushParamsToEngine(cmp);  // step 8, FR-024.4: every rendered block (change-only no-ops in the engine)
    applyPendingRestore();    // step 9, FR-042: endpoints, then the explicit morph fields, then the runtime blob
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
    std::size_t pedalNext = 0;  // FR-052: the second cursor, over pedalPoints_ (Vorago processor.cpp:419-447)
    while (cursor < total) {
        // 1. Every event due at this slice start (a WHILE: same-offset events all
        //    fire, so equal offsets never make a zero-length slice).
        loadPending();
        while (havePending && std::cmp_less_equal(pendingOffset, cursor)) {
            dispatchEvent(pending);  // FR-026, latch-aware (FR-052)
            havePending = false;
            loadPending();
        }
        // 1b. Then every pedal point due here (equal offsets: notes first).
        while (pedalNext < numPedalPoints_ &&
               std::cmp_less_equal(clampOffset(pedalPoints_[pedalNext].offset, total), cursor)) {
            applyPedalPoint(pedalPoints_[pedalNext]);
            ++pedalNext;
        }
        // 2. Slice end = min(next event, next pedal point, total). Every offset <= cursor was consumed above, so
        //    sliceEnd > cursor. No slice cap: the engine splits on its own 32-sample grid (engine.h:420-440).
        std::size_t sliceEnd = havePending ? static_cast<std::size_t>(pendingOffset) : total;
        if (pedalNext < numPedalPoints_) {
            sliceEnd = std::min(sliceEnd,
                                static_cast<std::size_t>(clampOffset(pedalPoints_[pedalNext].offset, total)));
        }
        const std::size_t n = sliceEnd - cursor;
        engine_->processBlock(outL + cursor, outR + cursor, n);  // engine.h:422; no scratch (FR-029)
        applyOutputGain(outL + cursor, outR + cursor, n);
        ++lastSliceCount_;
        cursor = sliceEnd;
    }
    // Every clamped pedal offset is < total, so the loop consumed them all.
    numPedalPoints_ = 0;

    // Plan 4.5.1 step 12 (FR-041, FR-032): publish the runtime blob of every rendered block, on the cmp read
    // once at the top of this call, and freeze the publication while the compare is A or B.
    // FR-042: the consumed restore sequence is marked applied only AFTER the publication, so getState keeps
    // writing the loaded blob until the restored engine's own blob has been published (plan 4.5.2 precedence 1).
    if (cmp == CompareMode::AB) {
        publishRuntime();
        restoreAppliedSeq_.store(restoreSeenSeq_, std::memory_order_release);
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

// FR-060 (plan 4.5.7): ceil((R_eff + 1250) * fs / 1000) + 64. R = max of the two endpoint slots' amp release
// (leaf 21); R_eff applies the Tight <-> Loose contribution with apply()'s law (profundum_macro_matrix.h:245, :260-267):
// skipped at +-0, else clamp(R * 2^c) in float. The 1250 ms term is Phase 10 ruling D-1's exact-zero reading of the
// DC blocker after voice silence; the 64 term is the limiter look-ahead (profundum_engine.h:107). Atomics and a
// stack-local matrix only: valid on any thread, allocation-free.
uint32 PLUGIN_API Processor::getTailSamples() {
    const double fs = tailSampleRate_.load(std::memory_order_acquire);
    if (!(fs > 0.0)) {
        return 0u;  // before setupProcessing, or after terminate
    }
    const float r = std::max(
        std::bit_cast<float>(endpoints_[0].leaf[kLeafAmpReleaseMs].load(std::memory_order_relaxed)),
        std::bit_cast<float>(endpoints_[1].leaf[kLeafAmpReleaseMs].load(std::memory_order_relaxed)));
    Krate::DSP::ProfundumMacroMatrix m;
    m.setMacros(macroValues(macroParams_));
    const float c = m.contributions()[static_cast<std::size_t>(Krate::DSP::ProfundumMacroTarget::AmpReleaseOct)];
    float rEff = r;
    if ((std::bit_cast<std::uint32_t>(c) & 0x7FFFFFFFu) != 0u) {  // apply()'s +-0 skip (isNonZero)
        const float v = r * std::exp2(c);                          // float, as scaleTimeClamped
        if (Krate::DSP::detail::isFinite(v)) {
            rEff = std::clamp(v, Krate::DSP::kMinEnvelopeTimeMs, Krate::DSP::kMaxEnvelopeTimeMs);
        }
    }
    return static_cast<uint32>(ceilMsToSamples(static_cast<double>(rEff) + 1250.0, fs)) +
           static_cast<uint32>(Krate::DSP::ProfundumEngine::kLimiterLookaheadSamples);
}

// FR-040 / FR-042 / FR-044 / FR-046 (plan 4.5.3). Touches the atomics only: NO engine call. The next
// process() (or setupProcessing) applies the loaded values. A future version is refused before any field is
// read. A v1 stream (a Phase 10 project) loads its globals and macros and RESETS every Phase 11 field; a v2
// stream cut short keeps the prior value of every field after the cut (loadV2Tail).
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
    const bool v1BodyComplete = loadGlobalParams(globalParams_, s) && loadMacroParams(macroParams_, s);
    if (version < kCurrentStateVersion) {
        // FR-044 v1: every Phase 11 field takes its default, whatever the instance held before.
        for (EndpointParams& slot : endpoints_) {
            slot.store(kDefaultLeafBits);
            slot.dirty.store(true, std::memory_order_relaxed);
        }
        resetNoteParams(noteParams_);
        resetMorphControlParams(morphParams_);  // position 0, ramp 40, Blend, Edit Target A
        seed_.store(Krate::DSP::ProfundumEngine::kDefaultSeed, std::memory_order_relaxed);
        publishRestore(RuntimeBlob{});  // cancel: a blob pending from an earlier setState is dropped
    } else {
        // FR-042: the loaded blob is handed to the audio thread; a chain stopped before it cancels instead
        // (five zero words: version byte 0, never passed to the engine).
        RuntimeBlob blob{};
        const bool blobRead =
            v1BodyComplete && loadV2Tail(s, noteParams_, morphParams_, seed_, endpoints_, blob);
        publishRestore(blobRead ? blob : RuntimeBlob{});
    }
    // Always, for both versions (FR-033, FR-046, FR-052, FR-025): the compare resets to A.B, and both endpoints
    // and the compare mode are reported to the controller.
    compareMode_.store(static_cast<std::uint8_t>(CompareMode::AB), std::memory_order_relaxed);
    compareSyncPending_.store(true, std::memory_order_relaxed);
    resetPerformanceParams(perfParams_);
    clearLatchPending_.store(true, std::memory_order_relaxed);
    for (EndpointParams& slot : endpoints_) {
        slot.syncPending.store(true, std::memory_order_relaxed);
    }
    flushControllerSync();
    return kResultOk;
}

// FR-040 / FR-041 (plan 4.5.3): the 510-byte v2 layout, little-endian. The endpoints come from the slots, never
// from the engine, so the bytes depend neither on the audio thread nor on Mod Wheel / Channel Pressure (FR-056).
tresult PLUGIN_API Processor::getState(IBStream* state) {
    if (state == nullptr) {
        return kResultFalse;
    }
    IBStreamer s(state, kLittleEndian);
    s.writeInt32(kCurrentStateVersion);
    saveGlobalParams(globalParams_, s);
    saveMacroParams(macroParams_, s);
    saveNoteParams(noteParams_, s);
    saveMorphControlParams(morphParams_, s);
    s.writeInt32u(seed_.load(std::memory_order_relaxed));
    for (const EndpointParams& slot : endpoints_) {
        const EndpointBlob buf = serializeSlot(slot);
        s.writeRaw(buf.data(), static_cast<TSize>(buf.size()));
    }
    // Plan 4.5.2 precedence 1 -> 2 -> 3: a loaded blob the audio thread has not yet applied and published
    // (so load-then-save is byte-stable), else the last blob published while the compare was A.B, else the
    // synthesized blob of the registered morph-control values.
    RuntimeBlob blob{};
    if (!readPendingRestore(blob) && !readPublishedRuntime(blob)) {
        blob = synthesizedRuntimeBlob();
    }
    s.writeRaw(blob.data(), static_cast<TSize>(blob.size()));
    flushControllerSync();  // FR-025: a flush point (the Linux path has no timer)
    return kResultOk;
}

// FR-030 / FR-031 / FR-025 (plan 4.5.5). Non-audio thread. The request is stored in an atomic (copy, compare) or
// marks every report pending (sync pull); either way the sender runs at once, so the reply carries the compare
// mode the processor now holds. An out-of-range attribute stores nothing. Every other message -> the base class.
tresult PLUGIN_API Processor::notify(IMessage* message) {
    if (message == nullptr) {
        return kInvalidArgument;
    }
    const FIDString id = message->getMessageID();
    IAttributeList* attrs = message->getAttributes();
    if (FIDStringsEqual(id, kMsgCopyEndpoint)) {
        int64 from = -1;
        if (attrs != nullptr && attrs->getInt("from", from) == kResultOk && (from == 0 || from == 1)) {
            copyRequest_.store(static_cast<std::int32_t>(from), std::memory_order_release);
        }
    } else if (FIDStringsEqual(id, kMsgCompare)) {
        int64 mode = -1;
        if (attrs != nullptr && attrs->getInt("mode", mode) == kResultOk && mode >= 0 &&
            mode <= static_cast<int64>(CompareMode::B)) {
            compareMode_.store(static_cast<std::uint8_t>(mode), std::memory_order_relaxed);
            compareSyncPending_.store(true, std::memory_order_relaxed);
        }
    } else if (FIDStringsEqual(id, kMsgRequestSync)) {
        for (EndpointParams& slot : endpoints_) {
            slot.syncPending.store(true, std::memory_order_relaxed);
        }
        compareSyncPending_.store(true, std::memory_order_relaxed);
    } else {
        return AudioEffect::notify(message);
    }
    flushControllerSync();
    return kResultOk;
}

// FR-025: the 50 ms SDK timer (Windows / macOS), on the thread that created it; never the audio thread.
void Processor::onTimer(Steinberg::Timer* /*timer*/) {
    flushControllerSync();
}

// FR-025 sender (plan 4.5.4). Non-audio threads only: it allocates IMessages, and no path from process() reaches
// it. Each flag is taken with exchange(acq_rel) BEFORE its slots are read, so an audio-thread edit made during the
// send sets the flag again and is reported by the next call. A message that cannot be allocated (no host context)
// leaves its flag set. flushBusy_ makes a concurrent or re-entrant call return at once; its flags stay set. The
// blobs carry the persisted slots, never the Mass performance offset (FR-056).
void Processor::flushControllerSync() noexcept {
    if (flushBusy_.test_and_set(std::memory_order_acquire)) {
        return;
    }
    for (std::size_t e = 0; e < endpoints_.size(); ++e) {
        EndpointParams& slot = endpoints_[e];
        if (!slot.syncPending.exchange(false, std::memory_order_acq_rel)) {
            continue;
        }
        const EndpointBlob blob = serializeSlot(slot);
        IPtr<IMessage> msg = owned(allocateMessage());
        IAttributeList* attrs = (msg.get() != nullptr) ? msg->getAttributes() : nullptr;
        if (attrs == nullptr) {
            slot.syncPending.store(true, std::memory_order_relaxed);
            continue;
        }
        msg->setMessageID(kMsgEndpoint);
        attrs->setInt("endpoint", static_cast<int64>(e));
        attrs->setBinary("blob", blob.data(), static_cast<uint32>(blob.size()));
        static_cast<void>(sendMessage(msg));
    }
    if (compareSyncPending_.exchange(false, std::memory_order_acq_rel)) {
        IPtr<IMessage> msg = owned(allocateMessage());
        IAttributeList* attrs = (msg.get() != nullptr) ? msg->getAttributes() : nullptr;
        if (attrs == nullptr) {
            compareSyncPending_.store(true, std::memory_order_relaxed);
        } else {
            msg->setMessageID(kMsgCompareMode);
            attrs->setInt("mode", static_cast<int64>(compareMode_.load(std::memory_order_relaxed)));
            static_cast<void>(sendMessage(msg));
        }
    }
    flushBusy_.clear(std::memory_order_release);
}

// ---- private helpers (T004 STUBS: empty) ----

// FR-030 (plan 4.5.1 step 2), audio thread: a pending Copy request copies the 62 source slot words into the
// destination and marks it dirty (the engine endpoint) and syncPending (the controller report). Atomic loads and
// stores only.
void Processor::applyPendingCopy() noexcept {
    const std::int32_t from = copyRequest_.exchange(-1, std::memory_order_acq_rel);
    if (from != 0 && from != 1) {
        return;
    }
    const EndpointParams& src = endpoints_[static_cast<std::size_t>(from)];
    EndpointParams& dst = endpoints_[static_cast<std::size_t>(1 - from)];
    dst.store(src.load());
    dst.dirty.store(true, std::memory_order_release);
    dst.syncPending.store(true, std::memory_order_release);
}

// FR-010 / FR-011 (plan 4.5.1 step 3): two passes over the queues, O(Q).
//   pass 1: the LAST point of queue 1001 (Edit Target), whatever the queue order;
//   pass 2: every other queue; the last point wins, a non-finite value changes
//           nothing, anything else is clamped to [0, 1] and routed by kParamRoutes.
// The Edit Target is read once, after pass 1, and addresses every sound row of
// pass 2. An Edit Target change alone writes nothing else (SC-009 #2).
void Processor::processParameterChanges(IParameterChanges* changes) noexcept {
    numPedalPoints_ = 0;  // FR-052: this block's points only
    if (changes == nullptr) {
        return;
    }
    const int32 numQueues = changes->getParameterCount();
    for (int32 i = 0; i < numQueues; ++i) {  // pass 1
        IParamValueQueue* queue = changes->getParameterData(i);
        if (queue == nullptr || queue->getParameterId() != kMorphEditTargetId) {
            continue;
        }
        ParamValue value = 0.0;
        if (lastPointOf(queue, value)) {
            static_cast<void>(handleMorphControlParamChange(morphParams_, kMorphEditTargetId, value));
        }
    }
    const int target = morphParams_.editTarget.load(std::memory_order_relaxed);
    EndpointParams& slot = endpoints_[target == 1 ? 1u : 0u];
    for (int32 i = 0; i < numQueues; ++i) {  // pass 2
        IParamValueQueue* queue = changes->getParameterData(i);
        if (queue == nullptr) {
            continue;
        }
        const ParamID id = queue->getParameterId();
        if (id == kMorphEditTargetId) {
            continue;  // applied in pass 1
        }
        if (id == kSustainPedalId) {
            collectPedalPoints(queue);  // FR-052: EVERY point, for the slice loop
            continue;
        }
        ParamValue value = 0.0;
        if (lastPointOf(queue, value)) {
            routeParamChange(slot, id, value);
        }
    }
}

// FR-010 (plan 4.3 route table). id >= kNumRoutedIds or a None route: ignored.
// ID 5 (Sustain Pedal) never arrives here: pass 2 hands its queue to collectPedalPoints (FR-052).
void Processor::routeParamChange(EndpointParams& slot, ParamID id, double value) noexcept {
    if (id >= static_cast<ParamID>(kNumRoutedIds)) {
        return;
    }
    switch (kParamRoutes[id].kind) {
        case RouteKind::Global:
            handleGlobalParamChange(globalParams_, id, value);
            return;
        case RouteKind::Macro:
            handleMacroParamChange(macroParams_, id, value);
            return;
        case RouteKind::Performance:
            static_cast<void>(handlePerformanceParamChange(perfParams_, id, value));
            return;
        case RouteKind::Sound:
            static_cast<void>(handleSoundParamChange(slot, id, value));
            return;
        case RouteKind::Note:
            handleNoteParamChange(noteParams_, id, value);
            return;
        case RouteKind::MorphControl:
            static_cast<void>(handleMorphControlParamChange(morphParams_, id, value));
            return;
        case RouteKind::None:
            return;
    }
}

// FR-013 / FR-055 (plan 4.5.1 step 7, plan 5). c = clamp(Mod Wheel + Channel
// Pressure, 0, 1); a change of its bits marks both endpoints dirty. A dirty slot
// is assembled into a MorphState; when c != 0 its Mass is offset by c * (hi - lo)
// and clamped to the Mass row of kLawTable (the slot itself is never written).
// When c == 0 the add is skipped, so the assembled leaf is bit-identical to the
// slot. A rejected state is counted (FR-067).
void Processor::applyEndpoints() noexcept {
    const float c = std::clamp(perfParams_.modWheel.load(std::memory_order_relaxed) +
                                   perfParams_.channelPressure.load(std::memory_order_relaxed),
                               0.0f, 1.0f);
    if (std::bit_cast<std::uint32_t>(c) != std::bit_cast<std::uint32_t>(lastPerfOffsetC_)) {
        lastPerfOffsetC_ = c;
        endpoints_[0].dirty.store(true, std::memory_order_relaxed);
        endpoints_[1].dirty.store(true, std::memory_order_relaxed);
    }
    const FieldInfo& massLaw = lawOf(kMassLeaf);
    for (std::size_t e = 0; e < endpoints_.size(); ++e) {
        if (!endpoints_[e].dirty.exchange(false, std::memory_order_acq_rel)) {
            continue;
        }
        MorphState s = morphStateFromLeafBits(endpoints_[e].load());
        if (c != 0.0f) {
            s.mass = std::clamp(s.mass + c * (massLaw.hi - massLaw.lo), massLaw.lo, massLaw.hi);
        }
        const auto which =
            (e == 1u) ? Krate::DSP::BassMorphEngine::Endpoint::B : Krate::DSP::BassMorphEngine::Endpoint::A;
        if (!engine_->setEndpoint(which, s)) {
            endpointRejections_.fetch_add(1u, std::memory_order_relaxed);
        }
    }
}

// FR-023.5, FR-024.4, FR-014 (plan 4.5.1 step 8): O(1) pending stores, no
// allocation. A value equal to the applied one changes nothing at the next grid
// point. The morph position is compare-aware on the caller's cmp: A -> 0,
// B -> 1, AB -> the registered (sanitised) position.
void Processor::pushParamsToEngine(CompareMode cmp) noexcept {
    using Engine = Krate::DSP::ProfundumEngine;
    const int mode = globalParams_.voiceMode.load(std::memory_order_relaxed);
    engine_->setVoiceMode(mode == 1 ? Engine::VoiceMode::Poly : Engine::VoiceMode::Mono);  // engine.h:326
    engine_->setPolyphony(static_cast<std::size_t>(
        clampPolyphony(globalParams_.polyphony.load(std::memory_order_relaxed))));  // engine.h:330
    engine_->setOutputCeilingDb(globalParams_.outputCeilingDb.load(std::memory_order_relaxed));  // engine.h:364
    engine_->setMacros(macroValues(macroParams_));  // engine.h:371

    pushNoteParams(noteParams_, *engine_);  // note_params.h: the eight change-only setters
    engine_->setPitchBend(2.0f * perfParams_.pitchBendNorm.load(std::memory_order_relaxed) - 1.0f);  // FR-051
    engine_->setMorphRampMs(morphParams_.rampMs.load(std::memory_order_relaxed));
    const int anchorMode = morphParams_.anchorMorphMode.load(std::memory_order_relaxed);
    if (anchorMode >= 0 && anchorMode < kNumAnchorMorphModes) {
        engine_->setAnchorMorphMode(static_cast<Krate::DSP::BassMorphEngine::AnchorMorphMode>(anchorMode));
    }
    float position = morphParams_.position.load(std::memory_order_relaxed);
    if (cmp == CompareMode::A) {
        position = 0.0f;
    } else if (cmp == CompareMode::B) {
        position = 1.0f;
    }
    engine_->setMorphPosition(position);
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

// FR-026 / FR-052 (plan 3.2, 4.5.1; Vorago processor.cpp:1131-1153). A pitch
// outside [0, 127] is dropped before the uint8_t cast; every other event type is
// ignored. Latch-aware: a note-on marks the key held (a re-strike clears its latch
// mark), and a note-off - or a velocity-0 note-on - reaches the engine only when
// the latch does not defer it (pedal down).
void Processor::dispatchEvent(const Event& e) noexcept {
    switch (e.type) {
        case Event::kNoteOnEvent: {
            if (!isMidiPitch(e.noteOn.pitch)) {
                return;
            }
            const auto pitch = static_cast<std::uint8_t>(e.noteOn.pitch);
            if (e.noteOn.velocity > 0.0f) {
                latch_.noteOn(pitch);
                engine_->noteOn(pitch, quantiseVelocity(e.noteOn.velocity));  // engine.h:264
            } else if (latch_.noteOff(pitch)) {  // velocity 0 == note-off, through the latch
                engine_->noteOff(pitch);         // engine.h:280
            }
            return;
        }
        case Event::kNoteOffEvent: {
            if (!isMidiPitch(e.noteOff.pitch)) {
                return;
            }
            const auto pitch = static_cast<std::uint8_t>(e.noteOff.pitch);
            if (latch_.noteOff(pitch)) {  // false: the pedal is down, the note is latched (FR-052)
                engine_->noteOff(pitch);  // engine.h:280
            }
            return;
        }
        default:
            return;
    }
}

// FR-052 (Vorago processor.cpp:803-829): copy EVERY Sustain Pedal point, in queue
// order, with the input hygiene of any other parameter (a failed read or a
// non-finite value is skipped, the rest clamped to [0, 1]); down = value >= 0.5.
// Past kMaxPedalPoints each surplus point overwrites the last slot (coalesced),
// so the block's final pedal state is never lost. The last valid value is
// mirrored into perfParams_.sustainNorm (display only).
void Processor::collectPedalPoints(IParamValueQueue* queue) noexcept {
    const int32 count = queue->getPointCount();
    bool haveValue = false;
    ParamValue lastValue = 0.0;
    for (int32 k = 0; k < count; ++k) {
        int32 offset = 0;
        ParamValue value = 0.0;
        if (queue->getPoint(k, offset, value) != kResultTrue) {
            continue;
        }
        if (!Krate::DSP::detail::isFinite(value)) {  // db_utils.h:125 (double)
            continue;
        }
        value = std::clamp(value, 0.0, 1.0);
        const PedalPoint p{.offset = static_cast<std::int32_t>(offset), .down = value >= 0.5};
        if (numPedalPoints_ < kMaxPedalPoints) {
            pedalPoints_[numPedalPoints_] = p;
            ++numPedalPoints_;
        } else {
            pedalPoints_[kMaxPedalPoints - 1u] = p;
        }
        lastValue = value;
        haveValue = true;
    }
    if (haveValue) {
        static_cast<void>(handlePerformanceParamChange(perfParams_, kSustainPedalId, lastValue));
    }
}

// FR-052: one pedal transition; a down -> up releases every latched note whose key is not held, ascending.
void Processor::applyPedalPoint(const PedalPoint& p) noexcept {
    latch_.setPedal(p.down, [this](std::uint8_t n) noexcept { engine_->noteOff(n); });
}

// FR-052 (Vorago processor.cpp:839-846): an early-return process() still owes the
// latch this block's pedal points - state only, and only on a prepared engine (a
// pedal-up then releases through engine_->noteOff, valid with no audio rendered).
// Unprepared: dropped.
void Processor::applyPendingPedalPointsImmediately() noexcept {
    if (prepared_ && engine_ != nullptr) {
        for (std::size_t k = 0; k < numPedalPoints_; ++k) {
            applyPedalPoint(pedalPoints_[k]);
        }
    }
    numPedalPoints_ = 0;
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

// FR-041 (plan 4.5.2) writer, audio thread only, once per rendered block while the compare is A.B. A Boehm
// seqlock: the sequence goes odd, the words are stored, the sequence goes even. Lock-free, never waits.
void Processor::publishRuntime() noexcept {
    RuntimeBlob buf{};
    if (engine_->serializeMorphRuntime(buf.data(), buf.size()) != buf.size()) {
        return;  // cannot happen for a 20-byte buffer (bass_morph_engine.h:234); publish nothing rather than garbage
    }
    std::array<std::uint32_t, kRuntimeWords> w{};
    std::memcpy(w.data(), buf.data(), buf.size());
    const std::uint32_t seq = rtSeq_.load(std::memory_order_relaxed);
    rtSeq_.store(seq + 1u, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    for (std::size_t i = 0; i < kRuntimeWords; ++i) {
        rtWords_[i].store(w[i], std::memory_order_relaxed);
    }
    rtSeq_.store(seq + 2u, std::memory_order_release);
    rtPublished_.store(true, std::memory_order_release);
}

// FR-041 (plan 4.5.2) reader, any non-audio thread (getState). Up to kRuntimeReadTries tries; false if nothing
// was ever published or every try overlapped a write (the caller then writes the synthesized blob).
bool Processor::readPublishedRuntime(RuntimeBlob& out) const noexcept {
    if (!rtPublished_.load(std::memory_order_acquire)) {
        return false;
    }
    for (int attempt = 0; attempt < kRuntimeReadTries; ++attempt) {
        const std::uint32_t s1 = rtSeq_.load(std::memory_order_acquire);
        if ((s1 & 1u) != 0u) {
            continue;  // a write is in progress
        }
        std::array<std::uint32_t, kRuntimeWords> w{};
        for (std::size_t i = 0; i < kRuntimeWords; ++i) {
            w[i] = rtWords_[i].load(std::memory_order_relaxed);
        }
        std::atomic_thread_fence(std::memory_order_acquire);
        const std::uint32_t s2 = rtSeq_.load(std::memory_order_relaxed);
        if (s1 == s2) {
            std::memcpy(out.data(), w.data(), out.size());
            return true;
        }
    }
    return false;
}

// FR-041 (ruling 2026-10-22): the blob of the registered morph-control values before any publication:
// target = p = position, p_a = BassMorphEngine::curve(mode, position) (bass_morph_engine.h:188), every selection
// A. The offsets are the plugin's re-statement of the engine's private layout (plugin_ids.h kRt*Offset).
Processor::RuntimeBlob Processor::synthesizedRuntimeBlob() const noexcept {
    using Krate::DSP::BassMorphEngine;
    const float position = morphParams_.position.load(std::memory_order_relaxed);
    const float rampMs = morphParams_.rampMs.load(std::memory_order_relaxed);
    int modeIndex = morphParams_.anchorMorphMode.load(std::memory_order_relaxed);
    if (modeIndex < 0 || modeIndex >= kNumAnchorMorphModes) {
        modeIndex = kDefaultAnchorMorphModeIndex;  // unreachable: every store is range-checked
    }
    const auto mode = static_cast<BassMorphEngine::AnchorMorphMode>(modeIndex);
    const float anchorPosition = BassMorphEngine::curve(mode, position);

    RuntimeBlob b{};
    const auto putFloat = [&b](std::size_t at, float v) noexcept { std::memcpy(b.data() + at, &v, sizeof(float)); };
    b[kRtVersionOffset] = static_cast<std::byte>(BassMorphEngine::kMorphRuntimeFormatVersion);
    putFloat(kRtTargetOffset, position);
    putFloat(kRtPositionOffset, position);
    putFloat(kRtAnchorPositionOffset, anchorPosition);
    b[kRtModeOffset] = static_cast<std::byte>(modeIndex);
    putFloat(kRtRampOffset, rampMs);
    b[kRtMainMaskOffset] = std::byte{0};         // every main group on A
    b[kRtAnchorSelectionOffset] = std::byte{0};  // Anchor on A
    return b;
}

// FR-042 (plan 4.5.2 "Restore hand-off") writer, setState only, never the audio thread. A Boehm seqlock whose
// writers are serialised by restoreWriterBusy_: the spin is on the non-audio side and contends only with another
// setState. A cancel is five zero words (version byte 0 is never handed to the engine).
void Processor::publishRestore(const RuntimeBlob& blob) noexcept {
    std::array<std::uint32_t, kRuntimeWords> w{};
    std::memcpy(w.data(), blob.data(), blob.size());
    while (restoreWriterBusy_.test_and_set(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    const std::uint32_t seq = restoreSeq_.load(std::memory_order_relaxed);
    restoreSeq_.store(seq + 1u, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    for (std::size_t i = 0; i < kRuntimeWords; ++i) {
        restoreWords_[i].store(w[i], std::memory_order_relaxed);
    }
    restoreSeq_.store(seq + 2u, std::memory_order_release);
    restoreWriterBusy_.clear(std::memory_order_release);
}

// FR-042 (plan 4.5.1 step 9) reader, audio thread: ONE seqlock read try per block, never a spin. Nothing is
// pending if the sequence is odd or already consumed; a torn read does nothing and is retried next block. The
// audio thread consumes a SEQUENCE, not a flag, so a second setState after a clean read is installed by the next
// block. A clean read of a version-1 blob whose target, mode and ramp equal the registered morph-control values
// BITWISE goes to restoreMorphRuntime, which re-validates it against the current endpoints.
void Processor::applyPendingRestore() noexcept {
    const std::uint32_t s1 = restoreSeq_.load(std::memory_order_acquire);
    if ((s1 & 1u) != 0u || s1 == restoreSeenSeq_) {
        return;
    }
    std::array<std::uint32_t, kRuntimeWords> w{};
    for (std::size_t i = 0; i < kRuntimeWords; ++i) {
        w[i] = restoreWords_[i].load(std::memory_order_relaxed);
    }
    std::atomic_thread_fence(std::memory_order_acquire);
    const std::uint32_t s2 = restoreSeq_.load(std::memory_order_relaxed);
    if (s1 != s2) {
        return;  // torn: a setState wrote meanwhile; the next block tries again
    }
    restoreSeenSeq_ = s1;

    RuntimeBlob blob{};
    std::memcpy(blob.data(), w.data(), blob.size());
    if (blob[kRtVersionOffset] != static_cast<std::byte>(Krate::DSP::BassMorphEngine::kMorphRuntimeFormatVersion)) {
        return;  // a cancel (version 0) or a foreign version
    }
    const auto bitsAt = [&blob](std::size_t at) noexcept {
        std::uint32_t v = 0;
        std::memcpy(&v, blob.data() + at, sizeof(v));
        return v;
    };
    const float position = morphParams_.position.load(std::memory_order_relaxed);
    const float rampMs = morphParams_.rampMs.load(std::memory_order_relaxed);
    const int mode = morphParams_.anchorMorphMode.load(std::memory_order_relaxed);
    if (bitsAt(kRtTargetOffset) != std::bit_cast<std::uint32_t>(position) ||
        bitsAt(kRtRampOffset) != std::bit_cast<std::uint32_t>(rampMs) || mode < 0 ||
        mode >= kNumAnchorMorphModes || blob[kRtModeOffset] != static_cast<std::byte>(mode)) {
        return;  // the blob does not belong to the explicit morph fields now in force
    }
    if (!engine_->restoreMorphRuntime(blob.data(), blob.size())) {
        return;
    }
    lastRestoreBlob_ = blob;
    ++restoreApplyCount_;
}

// FR-041 precedence 1 (plan 4.5.2), getState on a non-audio thread: a bounded seqlock read of the hand-off. True
// only when a version-1 loaded blob is pending, i.e. its sequence is not yet marked applied by the audio thread.
// Every try overlapping a write -> false (the rule is skipped).
bool Processor::readPendingRestore(RuntimeBlob& out) const noexcept {
    for (int attempt = 0; attempt < kRuntimeReadTries; ++attempt) {
        const std::uint32_t s1 = restoreSeq_.load(std::memory_order_acquire);
        if ((s1 & 1u) != 0u) {
            continue;  // a write is in progress
        }
        std::array<std::uint32_t, kRuntimeWords> w{};
        for (std::size_t i = 0; i < kRuntimeWords; ++i) {
            w[i] = restoreWords_[i].load(std::memory_order_relaxed);
        }
        std::atomic_thread_fence(std::memory_order_acquire);
        const std::uint32_t s2 = restoreSeq_.load(std::memory_order_relaxed);
        if (s1 != s2) {
            continue;
        }
        if (s1 == restoreAppliedSeq_.load(std::memory_order_acquire)) {
            return false;  // nothing loaded since the last applied-and-published sequence
        }
        RuntimeBlob blob{};
        std::memcpy(blob.data(), w.data(), blob.size());
        if (blob[kRtVersionOffset] !=
            static_cast<std::byte>(Krate::DSP::BassMorphEngine::kMorphRuntimeFormatVersion)) {
            return false;  // a cancel
        }
        out = blob;
        return true;
    }
    return false;
}

}  // namespace Profundum
