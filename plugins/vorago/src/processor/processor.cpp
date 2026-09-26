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
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/vstspeaker.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/scoped_denormal_mode.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
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

    // MB route source table (plan 4.4): one pack atomic per kMbRoutes entry, in
    // table order, so pushMacroBases() needs no per-block switch.
    for (std::size_t i = 0; i < kMbRoutes.size(); ++i) {
        mbSources_[i] = mbSourceFor(kMbRoutes[i].id);
        assert(mbSources_[i] != nullptr && "every kMbRoutes ID must name a pack atomic");
    }
}

// The plain-valued atomic behind each MB-routed ID (param_routes.h kMbRoutes).
// Called only by the constructor. nullptr for any other ID.
const std::atomic<float>* Processor::mbSourceFor(ParamID id) const noexcept {
    switch (id) {
        case kOutputSaturationId: return &globalParams_.outputSaturation;
        case kCloudRichnessId: return &cloudParams_.richness;
        case kCloudTiltId: return &cloudParams_.tiltDb;
        case kCloudMutationId: return &cloudParams_.mutation;
        case kCloudInharmonicityId: return &cloudParams_.inharmonicity;
        case kCloudDriftDepthId: return &cloudParams_.driftCents;
        case kNoiseLevelId: return &noiseParams_.levelDb;
        case kNoiseWakeId: return &noiseParams_.wake;
        case kNoiseWanderRateId: return &noiseParams_.wanderRateHz;
        case kResonanceGravityId: return &resonanceParams_.gravity;
        case kResonanceMixId: return &resonanceParams_.mix;
        case kResonanceWanderRateId: return &resonanceParams_.wanderRateHz;
        case kEcologyMixId: return &ecologyParams_.mix;
        case kEcologyLoopGainId: return &ecologyParams_.loopGain;
        case kSubLevelOffsetId: return &subParams_.levelOffsetDb;
        case kSubTrackingId: return &subParams_.tracking;
        case kSmearAmountId: return &smearParams_.amount;
        case kSmearDecoherenceId: return &smearParams_.decoherence;
        case kSmearTiltId: return &smearParams_.tilt;
        case kEventsRateScaleId: return &eventsParams_.eventRateScale;
        case kEcosystemDepthId: return &ecosystemParams_.depth;
        case kBodyBlendId: return &bodyParams_.blend;
        case kBodyDampingId: return &bodyParams_.damping;
        case kBodyResonanceId: return &bodyParams_.resonance;
        case kBodyMixId: return &bodyParams_.mix;
        case kSpaceSizeId: return &spaceParams_.size;
        case kSpaceDarknessId: return &spaceParams_.darkness;
        case kSpaceDecayId: return &spaceParams_.decaySeconds;
        case kSpaceFogId: return &spaceParams_.fog;
        case kSpaceDamperDepthId: return &spaceParams_.damperDepth;
        case kSpaceMixId: return &spaceParams_.mix;
        case kSpaceWidthId: return &spaceParams_.width;
        case kBloomDepthId: return &bloomParams_.depth;
        case kBloomSpawnRateId: return &bloomParams_.spawnRateHz;
        case kGhostPeakLevelId: return &ghostParams_.peakLevel;
        case kGhostBlurId: return &ghostParams_.blur;
        case kLifeBreathingDepthId: return &lifeParams_.breathingDepth;
        case kLifeBreathingIrregularityId: return &lifeParams_.breathingIrregularity;
        case kLifeTidalDepthId: return &lifeParams_.tidalDepth;
        default: return nullptr;
    }
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
    // 2. Seed BEFORE prepare, FROM THE PARAMETER (FR-023, plan 4.5 step 1):
    //    prepare derives every slot seed from it. Index 0 == kEngineSeed.
    const int seedIndex =
        std::clamp(globalParams_.seedIndex.load(std::memory_order_relaxed), 0, kNumSeeds - 1);
    engine_->setSeed(kVoragoSeedValues[static_cast<std::size_t>(seedIndex)]);
    // 3.
    engine_->prepare(sr, makeVoragoEngineConfig(kMaxBlockSamples));
    cavern_->prepare(sr, makeVoragoCavernConfig(kMaxBlockSamples, cavernSeedFor(seedIndex)));
    lastSeedIndex_ = seedIndex;  // prepare consumed it: no live reseed on the first push

    // 4. Polyphony FROM THE PARAMETER (setState may precede this).
    const std::size_t poly =
        clampPolyphony(globalParams_.polyphony.load(std::memory_order_relaxed));
    engine_->setPolyphony(poly);
    ++setPolyphonyCalls_;
    lastPushedPolyphony_ = engine_->getPolyphony();

    // 5. Master gain: configure, arm the first-block snap.
    masterGain_.configure(kMasterGainSmoothMs, static_cast<float>(sr));
    snapGainPending_ = true;

    // 5b. Plan 4.5 step 5 (FR-030): no note can sound across a re-prepare, so the
    //     sustain latch forgets everything without releasing.
    latch_.clearWithoutRelease();

    // 6. FR-022 (plan 4.5 step 6): VoragoVoice::prepare() just reset every VP
    //    field and envelope value to its default, and the cavern's own setters
    //    to theirs, while the trackers still hold the pre-prepare values. Re-push
    //    everything NOW (the audio thread is stopped here). lastSeedIndex_ was
    //    recorded above, so the seed is not live-reseeded.
    pushAllSurfaces(Scope::Reprepared);

    // 7. No scratch to size. prepared_ last.
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
        // FR-030 / plan 4.7: release every latched note BEFORE silencing (off the
        // render path; engine_->noteOff is a no-op on an unprepared engine).
        latch_.releaseAll([this](std::uint8_t n) noexcept { noteOffToEngine(n); });
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
    // Plan 4.7: every early return applies this block's pedal points to the
    // latch (only when prepared_; see applyPendingPedalPointsImmediately()).
    if (data.numOutputs <= 0 || data.outputs == nullptr) {
        applyPendingPedalPointsImmediately();
        return kResultOk;
    }
    if (data.outputs[0].channelBuffers32 == nullptr) {
        applyPendingPedalPointsImmediately();
        return kResultOk;
    }
    if (data.outputs[0].numChannels < 2) {
        applyPendingPedalPointsImmediately();
        return kResultOk;
    }
    if (data.numSamples <= 0) {
        applyPendingPedalPointsImmediately();
        return kResultOk;
    }
    const auto total = static_cast<std::size_t>(data.numSamples);
    float* outL = data.outputs[0].channelBuffers32[0];
    float* outR = data.outputs[0].channelBuffers32[1];
    if (outL == nullptr || outR == nullptr) {
        applyPendingPedalPointsImmediately();
        return kResultOk;
    }
    // Not ready (process() before setupProcessing, or after terminate): silence.
    if (!prepared_ || engine_ == nullptr || cavern_ == nullptr) {
        std::fill_n(outL, total, 0.0f);
        std::fill_n(outR, total, 0.0f);
        data.outputs[0].silenceFlags = 3;
        applyPendingPedalPointsImmediately();  // not prepared: clears only
        return kResultOk;
    }

    // FR-030 / Q8 (a): setState() asked for every latched note to be released.
    // Consumed after the not-ready path and before the re-push (plan 4.3).
    if (latchReleasePending_.exchange(false, std::memory_order_acquire)) {
        latch_.releaseAll([this](std::uint8_t n) noexcept { noteOffToEngine(n); });
    }

    // FR-020 (2), once per process() before the first slice, never per slice
    // (P-9, P-10, SC-007): the atomics are latched above and cannot change inside
    // this call, and every matrix call writes stored bases that survive a
    // mid-block note-on / steal / retrigger.
    // FR-022 (plan 4.3, D-P7): consumed here, after the not-ready path, so the
    // request survives a process() that arrives before setupProcessing().
    if (forcePushPending_.exchange(false, std::memory_order_acquire)) {
        pushAllSurfaces(Scope::PresetLoad);
    }
    pushGlobalParams();
    pushEngParams();                                               // ENG (plan 4.4)
    pushCavernParams();                                            // CV (plan 4.4)
    pushVoiceParams();                                            // VP (plan 4.4)
    pushMacroBases();                                              // MB (plan 4.4)
    macros_.setMacros(buildMacroVector());                         // MAC (FR-021)
    macros_.apply(*engine_);                                       // vorago_macro_matrix.h:1041
    applyCavernTargets(*cavern_, macros_.computeCavernTargets());  // :1117 + FR-034a
    ++macroPushCount_;

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
    std::size_t pedalNext = 0;  // plan 4.7: second cursor over pedalPoints_

    while (cursor < total) {
        // 1. Every event due at this slice start (a WHILE: same-offset events all fire).
        loadPending();
        while (havePending && std::cmp_less_equal(pendingOffset, cursor)) {
            dispatchEvent(pending);  // FR-031
            havePending = false;
            loadPending();
        }
        // 1b. Then every pedal point due here (D-P6: equal offsets, notes first).
        while (pedalNext < numPedalPoints_ &&
               std::cmp_less_equal(clampOffset(pedalPoints_[pedalNext].offset, total), cursor)) {
            applyPedalPoint(pedalPoints_[pedalNext]);
            ++pedalNext;
        }
        // 2. Slice end. Every offset <= cursor was consumed above, so sliceEnd > cursor.
        std::size_t sliceEnd = std::min(total, cursor + kMaxBlockSamples);
        if (havePending) {
            sliceEnd = std::min(sliceEnd, static_cast<std::size_t>(pendingOffset));
        }
        if (pedalNext < numPedalPoints_) {
            sliceEnd = std::min(
                sliceEnd,
                static_cast<std::size_t>(clampOffset(pedalPoints_[pedalNext].offset, total)));
        }
        renderSlice(outL + cursor, outR + cursor, sliceEnd - cursor);
        ++lastSliceCount_;
        cursor = sliceEnd;
    }
    // Every clamped pedal offset is < total, so the loop consumed them all.
    numPedalPoints_ = 0;

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

// FR-045 / FR-046 (plan 2.5.10), Phase 12 C-7 / FR-040 (plan 4.9): v2, 428 bytes
// (kStateV2Bytes), little-endian; the v1 60-byte stream is its strict prefix.
// Writes atomics only, so it is safe beside process(); no prepare is reachable and
// NO DSP call is made (FR-022, FR-030): the re-push and the latch release are
// requested by release stores and done by the next process().
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
    // Each loader is EOF-safe and returns false at the first failed read, so the
    // short-circuit chain stops there and every later field is left unchanged.
    const auto loadV2Tail = [this](IBStreamer& in) {
        return loadGlobalParamsV2Ext(globalParams_, in) && loadCloudParams(cloudParams_, in) &&
               loadNoiseParams(noiseParams_, in) && loadResonanceParams(resonanceParams_, in) &&
               loadEcologyParams(ecologyParams_, in) && loadSubParams(subParams_, in) &&
               loadSmearParams(smearParams_, in) && loadEventsParams(eventsParams_, in) &&
               loadEcosystemParams(ecosystemParams_, in) && loadBodyParams(bodyParams_, in) &&
               loadSpaceParams(spaceParams_, in) && loadEnvelopeParams(envelopeParams_, in) &&
               loadBloomParams(bloomParams_, in) && loadGhostParams(ghostParams_, in) &&
               loadLifeParams(lifeParams_, in);
    };
    const bool v1Complete = loadGlobalParams(globalParams_, s) && loadMacroParams(macroParams_, s);
    if (version >= 2) {
        if (v1Complete) {
            [[maybe_unused]] const bool v2Complete = loadV2Tail(s);
        }
    } else {
        // C-7 / FR-040: a version < 2 stream is the v1 block only (a version < 1 is
        // read as v1: there has never been another layout) and leaves every Phase 12
        // field at its REGISTERED DEFAULT, whatever the previous state held. The
        // default-constructed packs are those defaults (SC-003); their v2 tail is
        // serialized into stack memory and loaded through the same chain.
        constexpr std::size_t kV2TailBytes = kStateV2Bytes - 60u;  // minus version + v1 block
        std::array<char, kV2TailBytes> tail{};
        MemoryStream tailStream(tail.data(), static_cast<TSize>(tail.size()));
        IBStreamer out(&tailStream, kLittleEndian);
        saveGlobalParamsV2Ext(GlobalParams{}, out);
        saveCloudParams(CloudParams{}, out);
        saveNoiseParams(NoiseParams{}, out);
        saveResonanceParams(ResonanceParams{}, out);
        saveEcologyParams(EcologyParams{}, out);
        saveSubParams(SubParams{}, out);
        saveSmearParams(SmearParams{}, out);
        saveEventsParams(EventsParams{}, out);
        saveEcosystemParams(EcosystemParams{}, out);
        saveBodyParams(BodyParams{}, out);
        saveSpaceParams(SpaceParams{}, out);
        saveEnvelopeParams(EnvelopeParams{}, out);
        saveBloomParams(BloomParams{}, out);
        saveGhostParams(GhostParams{}, out);
        saveLifeParams(LifeParams{}, out);
        tailStream.seek(0, IBStream::kIBSeekSet, nullptr);
        [[maybe_unused]] const bool defaultsLoaded = loadV2Tail(out);
        assert(defaultsLoaded);
    }
    // FR-045: the performance controllers are never persisted and restart at 0.
    globalParams_.sustainPedal.store(0.0f, std::memory_order_relaxed);
    globalParams_.channelPressure.store(0.0f, std::memory_order_relaxed);
    // FR-022 / FR-030: consumed by the next process(), never here.
    forcePushPending_.store(true, std::memory_order_release);
    latchReleasePending_.store(true, std::memory_order_release);
    return kResultOk;
}

// Plan 4.9 write order: version, v1 block, v2 global extension, 14 packs in band order.
tresult PLUGIN_API Processor::getState(IBStream* state) {
    if (state == nullptr) {
        return kResultFalse;
    }
    IBStreamer s(state, kLittleEndian);
    s.writeInt32(kCurrentStateVersion);
    saveGlobalParams(globalParams_, s);
    saveMacroParams(macroParams_, s);
    saveGlobalParamsV2Ext(globalParams_, s);
    saveCloudParams(cloudParams_, s);
    saveNoiseParams(noiseParams_, s);
    saveResonanceParams(resonanceParams_, s);
    saveEcologyParams(ecologyParams_, s);
    saveSubParams(subParams_, s);
    saveSmearParams(smearParams_, s);
    saveEventsParams(eventsParams_, s);
    saveEcosystemParams(ecosystemParams_, s);
    saveBodyParams(bodyParams_, s);
    saveSpaceParams(spaceParams_, s);
    saveEnvelopeParams(envelopeParams_, s);
    saveBloomParams(bloomParams_, s);
    saveGhostParams(ghostParams_, s);
    saveLifeParams(lifeParams_, s);
    return kResultOk;
}

// FR-043 (plan 2.5.8): the LAST point of each queue wins; routed by ID band.
// FR-013 / plan 4.2 (T036): every band dispatches to its owning pack.
// FR-030 / plan 4.2 (T043): the sustain pedal is the exception - EVERY point is
// kept (collectPedalPoints) for the slice loop.
void Processor::processParameterChanges(IParameterChanges* changes) noexcept {
    numPedalPoints_ = 0;  // this block's points only
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
        if (queue->getParameterId() == kSustainPedalId) {
            collectPedalPoints(queue, count);
            continue;
        }
        int32 offset = 0;
        ParamValue value = 0.0;
        if (queue->getPoint(count - 1, offset, value) != kResultTrue) {
            continue;
        }
        // FR-013 input hygiene (plan 4.2): a non-finite value changes nothing
        // (bit-pattern test, fast-math immune); anything else is clamped to [0, 1]
        // before the pack handler maps it.
        if (!Krate::DSP::detail::isFinite(value)) {  // db_utils.h:125 (double)
            continue;
        }
        value = std::clamp(value, 0.0, 1.0);
        const ParamID id = queue->getParameterId();
        if (id < kGlobalParamRangeEnd) {
            handleGlobalParamChange(globalParams_, id, value);
        } else if (id < kMacroParamRangeEnd) {
            handleMacroParamChange(macroParams_, id, value);
        } else if (id < kCloudParamRangeEnd) {
            handleCloudParamChange(cloudParams_, id, value);
        } else if (id < kNoiseParamRangeEnd) {
            handleNoiseParamChange(noiseParams_, id, value);
        } else if (id < kResonanceParamRangeEnd) {
            handleResonanceParamChange(resonanceParams_, id, value);
        } else if (id < kEcologyParamRangeEnd) {
            handleEcologyParamChange(ecologyParams_, id, value);
        } else if (id < kSubParamRangeEnd) {
            handleSubParamChange(subParams_, id, value);
        } else if (id < kSmearParamRangeEnd) {
            handleSmearParamChange(smearParams_, id, value);
        } else if (id < kEventsParamRangeEnd) {
            handleEventsParamChange(eventsParams_, id, value);
        } else if (id < kEcosystemParamRangeEnd) {
            handleEcosystemParamChange(ecosystemParams_, id, value);
        } else if (id < kBodyParamRangeEnd) {
            handleBodyParamChange(bodyParams_, id, value);
        } else if (id < kSpaceParamRangeEnd) {
            handleSpaceParamChange(spaceParams_, id, value);
        } else if (id < kEnvelopeParamRangeEnd) {
            handleEnvelopeParamChange(envelopeParams_, id, value);
        } else if (id < kBloomParamRangeEnd) {
            handleBloomParamChange(bloomParams_, id, value);
        } else if (id < kGhostParamRangeEnd) {
            handleGhostParamChange(ghostParams_, id, value);
        } else if (id < kLifeParamRangeEnd) {
            handleLifeParamChange(lifeParams_, id, value);
        }
        // id >= kLifeParamRangeEnd: no pack owns it, ignored.
        markDirty(id);
    }
}

// Plan 4.2 / 4.7: copy every sustain point, in queue order, with the same input
// hygiene as any other parameter (non-finite skipped, clamped to [0, 1]). Past
// kMaxPedalPoints each surplus point overwrites the last slot (coalesced), so the
// block's final pedal state is never lost. The last valid plain value is mirrored
// into globalParams_.sustainPedal (display only; Route::Local, nothing to dirty).
void Processor::collectPedalPoints(IParamValueQueue* queue, int32 count) noexcept {
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
        handleGlobalParamChange(globalParams_, kSustainPedalId, lastValue);
    }
}

// Plan 4.7: one pedal transition; a down -> up releases every latched, unheld note.
void Processor::applyPedalPoint(const PedalPoint& p) noexcept {
    latch_.setPedal(p.down, [this](std::uint8_t n) noexcept { noteOffToEngine(n); });
}

// Plan 4.7: an early-return process() still owes the latch this block's pedal
// points - state only, and only on a prepared engine (a pedal-up then releases
// through engine_->noteOff, valid with no audio rendered). Unprepared: dropped.
void Processor::applyPendingPedalPointsImmediately() noexcept {
    if (prepared_ && engine_ != nullptr) {
        for (std::size_t k = 0; k < numPedalPoints_; ++k) {
            applyPedalPoint(pedalPoints_[k]);
        }
    }
    numPedalPoints_ = 0;
}

void Processor::noteOffToEngine(std::uint8_t note) noexcept {
    if (engine_ != nullptr) {
        engine_->noteOff(note);  // vorago_engine.h:679, no-op when unprepared
    }
}

// Plan 4.2: after the pack stored the atomic. Only VP keeps a generation; every
// other route is compared against its own tracker in the push step. An
// unregistered ID has no route and bumps nothing.
void Processor::markDirty(ParamID id) noexcept {
    const std::optional<Route> route = routeOf(id);
    if (route.has_value() && *route == Route::VP) {
        ++voiceParamGeneration_;
    }
}

// FR-022 / plan 4.6: invalidate every route tracker so the next push re-sends
// every value. Two trackers are deliberately left alone (D-P2): the seed
// (prepare consumed it; forcing it would live-reseed an unchanged seed) and the
// polyphony edge detector (it compares against engine_->getPolyphony(), which
// survives prepare). Re-pushing an unchanged value is inert everywhere else.
void Processor::pushAllSurfaces(Scope scope) noexcept {
    mbValid_ = false;
    ++voiceParamGeneration_;
    lastAppliedVpGen_ = kGenerationSentinel;
    engValid_ = false;  // pushEngParams() compares the seed on its own tracker
    cvValid_ = false;
    if (scope == Scope::Reprepared) {
        pushEngParams();
        pushCavernParams();
        pushVoiceParams();
        pushMacroBases();
    }
}

// VP route (plan 4.4, FR-020): one applyVoiceParams() broadcast per changed
// generation, built from the pack atomics. Discrete atomics hold list indices
// (handlers and loaders clamp them); index == enum value for every VP list
// except noise type, which goes through kNoiseTypeByIndex (param_mapping.h).
void Processor::pushVoiceParams() noexcept {
    if (voiceParamGeneration_ == lastAppliedVpGen_) {
        return;
    }
    constexpr auto kRelaxed = std::memory_order_relaxed;
    using Krate::DSP::ContinuousBody;
    using Krate::DSP::FeedbackEcology;
    using Krate::DSP::NoiseOrganismModel;
    using Krate::DSP::ResonanceDriftNetwork;
    const auto clampedIndex = [](const std::atomic<int>& a, int count) noexcept {
        return std::clamp(a.load(std::memory_order_relaxed), 0, count - 1);
    };

    Krate::DSP::VoragoVoiceParams p{};
    p.stereoSpread = cloudParams_.stereoSpread.load(kRelaxed);
    p.cloudSpectralGravity = cloudParams_.spectralGravity.load(kRelaxed);
    p.bodyMaterialA = static_cast<ContinuousBody::BodyMaterial>(
        clampedIndex(bodyParams_.materialA, kNumBodyMaterialChoices));
    p.bodyMaterialB = static_cast<ContinuousBody::BodyMaterial>(
        clampedIndex(bodyParams_.materialB, kNumBodyMaterialChoices));
    for (std::size_t s = 0; s < Krate::DSP::VoragoVoiceParams::kNumNoiseSlots; ++s) {
        p.noiseModel[s] = static_cast<NoiseOrganismModel>(
            clampedIndex(noiseParams_.model[s], kNumNoiseModelChoices));
        p.noiseType[s] = kNoiseTypeByIndex[static_cast<std::size_t>(
            clampedIndex(noiseParams_.type[s], kNumNoiseTypeChoices))];
        p.noiseCombFundamentalHz[s] = noiseParams_.combFundamentalHz[s].load(kRelaxed);
        p.noiseCombSpread[s] = noiseParams_.combSpread[s].load(kRelaxed);
        p.noiseCombFeedback[s] = noiseParams_.combFeedback[s].load(kRelaxed);
    }
    p.resonanceAnchorMode = static_cast<ResonanceDriftNetwork::AnchorMode>(
        clampedIndex(resonanceParams_.anchorMode, kNumResonanceAnchorModes));
    for (std::size_t l = 0; l < Krate::DSP::VoragoVoiceParams::kNumLoops; ++l) {
        p.ecologyLoopFilterMode[l] = static_cast<FeedbackEcology::FilterMode>(
            clampedIndex(ecologyParams_.loopFilterMode[l], kEcologyNumFilterModes));
    }

    engine_->applyVoiceParams(p);  // vorago_engine.h:829, every slot < kMaxVoices
    lastAppliedVpGen_ = voiceParamGeneration_;
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
    // FR-053: masterGainSnapProbe_ is set only by the SC-011 test probe.
    if (snapGainPending_ || masterGainSnapProbe_) {
        masterGain_.snapTo(gain);  // smoother.h:263, first block after prepare / activate
        snapGainPending_ = false;
    } else {
        masterGain_.setTarget(gain);  // smoother.h:170
    }
}

// ENG route (plan 4.4, FR-023): each direct engine setter is called only when
// its plain atomic changed (bit-exact float compare is the intent), or on the
// first push (engValid_ false). The seed is tracked apart from engValid_ (plan
// 4.6, D-P2): a changed index is a live reseed of the engine and the cavern
// (allocation-free, vorago_engine.h:597-603), an unchanged one never re-pushes.
void Processor::pushEngParams() noexcept {
    constexpr auto kRelaxed = std::memory_order_relaxed;

    const int seedIndex =
        std::clamp(globalParams_.seedIndex.load(kRelaxed), 0, kNumSeeds - 1);
    if (seedIndex != lastSeedIndex_) {
        // Spec B-4 (FR-023, SC-014 (4)): a live reseed is setSeed() PLUS the two
        // opt-in rewinds, so a silent instance reproduces a fresh one prepared
        // at that seed - the engine re-draws every idle slot's prepare-time
        // state, the cavern rebuilds its Dimensionality matrix endpoint. Both
        // are RT-safe and allocation-free (vorago_engine.h rewindIdleVoices,
        // aether_reverb.h rebuildMatrixFromSeed).
        engine_->setSeed(kVoragoSeedValues[static_cast<std::size_t>(seedIndex)]);
        engine_->rewindIdleVoices();
        cavern_->setSeed(cavernSeedFor(seedIndex));
        cavern_->rebuildMatrixFromSeed();
        lastSeedIndex_ = seedIndex;
    }

    const int envMode = std::clamp(envelopeParams_.mode.load(kRelaxed), 0, kNumEnvelopeModes - 1);
    if (!engValid_ || envMode != lastEnvMode_) {
        engine_->setEnvelopeMode(static_cast<Krate::DSP::VoragoVoice::EnvelopeMode>(envMode));
        lastEnvMode_ = envMode;
    }
    const std::array<float, 4> stageMs = {
        envelopeParams_.stage0TimeMs.load(kRelaxed), envelopeParams_.stage1TimeMs.load(kRelaxed),
        envelopeParams_.stage2TimeMs.load(kRelaxed), envelopeParams_.stage3TimeMs.load(kRelaxed)};
    for (std::size_t st = 0; st < stageMs.size(); ++st) {
        if (!engValid_ || stageMs[st] != lastStageMs_[st]) {
            engine_->setEnvelopeStageTimeMs(static_cast<int>(st), stageMs[st]);
            lastStageMs_[st] = stageMs[st];
        }
    }
    const float releaseMs = envelopeParams_.releaseMs.load(kRelaxed);
    if (!engValid_ || releaseMs != lastReleaseMs_) {
        engine_->setEnvelopeReleaseMs(releaseMs);
        lastReleaseMs_ = releaseMs;
    }
    const float growthS = envelopeParams_.growthDurationSeconds.load(kRelaxed);
    if (!engValid_ || growthS != lastGrowthS_) {
        engine_->setGrowthDurationSeconds(growthS);
        lastGrowthS_ = growthS;
    }

    // Tone order == SubharmonicEngine::kDefaultToneLevelDb order (-18, -24, -30).
    const std::array<float, 3> subToneDb = {subParams_.div2LevelDb.load(kRelaxed),
                                            subParams_.div4LevelDb.load(kRelaxed),
                                            subParams_.fifthBelowLevelDb.load(kRelaxed)};
    for (std::size_t t = 0; t < subToneDb.size(); ++t) {
        if (!engValid_ || subToneDb[t] != lastSubToneDb_[t]) {
            engine_->setSubToneLevelDb(t, subToneDb[t]);  // vorago_engine.h:884
            lastSubToneDb_[t] = subToneDb[t];
        }
    }

    const float ghostReverse = ghostParams_.reverseProbability.load(kRelaxed);
    if (!engValid_ || ghostReverse != lastGhostReverse_) {
        engine_->setGhostReverseProbability(ghostReverse);  // :959
        lastGhostReverse_ = ghostReverse;
    }
    const bool ghostTriggers = ghostParams_.eventTriggers.load(kRelaxed) != 0;
    if (!engValid_ || ghostTriggers != lastGhostTriggers_) {
        engine_->setGhostEventTriggers(ghostTriggers);  // :974
        lastGhostTriggers_ = ghostTriggers;
    }

    engValid_ = true;
}

// CV route (plan 4.4): the nine CavernVerb setters outside the matrix
// (cavern_verb.h:632-738), each called only when its plain atomic changed
// (bit-exact float compare is the intent) or on the first push (cvValid_ false).
// applyCavernTargets() writes only the seven matrix-owned targets, so nothing
// later in the push step overwrites these.
void Processor::pushCavernParams() noexcept {
    constexpr auto kRelaxed = std::memory_order_relaxed;
    using Setter = void (Krate::DSP::CavernVerb::*)(float) noexcept;
    static constexpr std::array<Setter, 8> kSetters = {
        &Krate::DSP::CavernVerb::setDensity,         &Krate::DSP::CavernVerb::setDimensionality,
        &Krate::DSP::CavernVerb::setBreath,          &Krate::DSP::CavernVerb::setEarlySizeMs,
        &Krate::DSP::CavernVerb::setEarlyLevel,      &Krate::DSP::CavernVerb::setEarlyAbsorption,
        &Krate::DSP::CavernVerb::setEarlySend,       &Krate::DSP::CavernVerb::setDamperRate};
    const std::array<float, 8> values = {
        spaceParams_.density.load(kRelaxed),         spaceParams_.dimensionality.load(kRelaxed),
        spaceParams_.breath.load(kRelaxed),          spaceParams_.earlySizeMs.load(kRelaxed),
        spaceParams_.earlyLevel.load(kRelaxed),      spaceParams_.earlyAbsorption.load(kRelaxed),
        spaceParams_.earlySend.load(kRelaxed),       spaceParams_.damperRate.load(kRelaxed)};
    static_assert(kSetters.size() == std::tuple_size_v<decltype(lastCv_)>);

    for (std::size_t k = 0; k < kSetters.size(); ++k) {
        if (!cvValid_ || values[k] != lastCv_[k]) {
            ((*cavern_).*kSetters[k])(values[k]);
            lastCv_[k] = values[k];
        }
    }
    const bool freeze = spaceParams_.freeze.load(kRelaxed) != 0;
    if (!cvValid_ || freeze != lastFreeze_) {
        cavern_->setFreeze(freeze);  // cavern_verb.h:735, idempotent at the engine
        lastFreeze_ = freeze;
    }
    cvValid_ = true;
}

// MB route (plan 4.4, FR-020): a base is handed to the matrix only when its
// plain atomic changed (bit-exact float compare is the intent: any change
// pushes, an unchanged value never re-pushes), or on the first push.
void Processor::pushMacroBases() noexcept {
    for (std::size_t i = 0; i < kMbRoutes.size(); ++i) {
        const float v = mbSources_[i]->load(std::memory_order_relaxed);
        if (!mbValid_ || v != lastPushedMb_[i]) {
            macros_.setTargetBase(kMbRoutes[i].target, v);
            lastPushedMb_[i] = v;
        }
    }
    mbValid_ = true;
}

// MAC route (FR-021, plan 4.8): the twelve knobs unmodified, except Pressure,
// which adds channel pressure (OQ-1 (a)). With pressure 0 the sum is exactly the
// knob, so the vector equals the atomics bit-for-bit.
Krate::DSP::VoragoMacroValues Processor::buildMacroVector() const noexcept {
    constexpr auto kRelaxed = std::memory_order_relaxed;
    Krate::DSP::VoragoMacroValues m{};
    m.darkness = macroParams_.darkness.load(kRelaxed);
    m.age = macroParams_.age.load(kRelaxed);
    m.density = macroParams_.density.load(kRelaxed);
    m.movement = macroParams_.movement.load(kRelaxed);
    m.gravity = macroParams_.gravity.load(kRelaxed);
    m.entropy = macroParams_.entropy.load(kRelaxed);
    m.pressure = std::clamp(macroParams_.pressure.load(kRelaxed) +
                                globalParams_.channelPressure.load(kRelaxed),
                            0.0f, 1.0f);
    m.weight = macroParams_.weight.load(kRelaxed);
    m.fog = macroParams_.fog.load(kRelaxed);
    m.life = macroParams_.life.load(kRelaxed);
    m.depth = macroParams_.depth.load(kRelaxed);
    m.mass = macroParams_.mass.load(kRelaxed);
    return m;
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
                latch_.noteOn(pitch);  // a re-strike clears the latch mark (plan 4.7)
                engine_->noteOn(pitch, quantiseVelocity(e.noteOn.velocity));  // vorago_engine.h:642
            } else if (latch_.noteOff(pitch)) {  // velocity 0 == note-off, through the latch
                engine_->noteOff(pitch);         // :679
            }
            return;
        }
        case Event::kNoteOffEvent: {
            if (!isMidiPitch(e.noteOff.pitch)) {
                return;
            }
            const auto pitch = static_cast<std::uint8_t>(e.noteOff.pitch);
            if (latch_.noteOff(pitch)) {  // false: pedal down, the note is latched (FR-030)
                engine_->noteOff(pitch);
            }
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
