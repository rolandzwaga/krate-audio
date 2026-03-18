// ==============================================================================
// Audio Processor Implementation (core lifecycle + process)
// ==============================================================================
//
// Split into focused modules:
//   processor.cpp           - Constructor, lifecycle (init/term/setup/active),
//                             process(), setBusArrangements()
//   processor_state.cpp     - getState(), setState(), applyPresetSnapshot()
//   processor_params.cpp    - processParameterChanges(), applyParamsToEngine()
//   processor_midi.cpp      - processEvents(), onNoteOn(), onNoteOff()
//   processor_messaging.cpp - notify(), sendSkipEvent(), sendVoiceModRouteState()
// ==============================================================================

#include "processor.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <krate/dsp/core/block_context.h>
#include <krate/dsp/core/note_value.h>
#include <krate/dsp/systems/voice_mod_types.h>
#include <krate/dsp/core/modulation_types.h>

#include "parameters/dropdown_mappings.h"

#include <algorithm>
#include <cstdint>

// =============================================================================
// DEBUG: Phaser signal path tracing (remove after debugging)
// =============================================================================
#define RUINAE_PHASER_DEBUG 0
#define RUINAE_TGATE_DEBUG 0

#if RUINAE_PHASER_DEBUG
#include <cstdarg>
#include <cstdio>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

int s_logCounter = 0;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables) shared debug counter with ruinae_effects_chain.h

static void logPhaser(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
#ifdef _WIN32
    OutputDebugStringA(buf);
#else
    fprintf(stderr, "%s", buf);
#endif
}
#else
static void logPhaser(const char*, ...) {}
#endif

#if RUINAE_TGATE_DEBUG
#include <cstdarg>
#include <cstdio>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

static int s_tgLogCounter = 0;  // NOLINT
static int s_tgLastStep = -1;   // NOLINT

static void logTGate(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
#ifdef _WIN32
    OutputDebugStringA(buf);
#else
    fprintf(stderr, "%s", buf);
#endif
}
#else
[[maybe_unused]] static void logTGate(const char*, ...) {}
#endif

namespace Ruinae {

// ==============================================================================
// Constructor
// ==============================================================================

Processor::Processor() {
    setControllerClass(kControllerUID);
}

// ==============================================================================
// IPluginBase
// ==============================================================================

Steinberg::tresult PLUGIN_API Processor::initialize(FUnknown* context) {
    Steinberg::tresult result = AudioEffect::initialize(context);
    if (result != Steinberg::kResultTrue) {
        return result;
    }

    // Ruinae is a synthesizer instrument:
    // - Auxiliary sidechain audio input (for EnvFollower, PitchFollower, Transient)
    // - Event input (MIDI notes)
    // - Stereo audio output
    addAudioInput(
        STR16("Sidechain"),
        Steinberg::Vst::SpeakerArr::kStereo,
        Steinberg::Vst::BusTypes::kAux,
        0 /* not default-active — host activates when user routes audio */);
    addEventInput(STR16("Event Input"));
    addEventOutput(STR16("MIDI Output"));
    addAudioOutput(STR16("Audio Output"), Steinberg::Vst::SpeakerArr::kStereo);

    // Pre-allocate skip event IMessages (Phase 11c, FR-012)
    for (int i = 0; i < 6; ++i) {
        skipMessages_[static_cast<size_t>(i)] = Steinberg::owned(allocateMessage());
        if (skipMessages_[static_cast<size_t>(i)]) {
            skipMessages_[static_cast<size_t>(i)]->setMessageID("ArpSkipEvent");
        }
    }

    // Pre-allocate voice route sync message (Issue 1: avoid allocateMessage on audio thread)
    voiceRouteSyncMsg_ = Steinberg::owned(allocateMessage());
    if (voiceRouteSyncMsg_) {
        voiceRouteSyncMsg_->setMessageID("VoiceModRouteState");
        // Pre-warm the binary attribute with a dummy payload so the attribute
        // list allocates storage once, not on every sendVoiceModRouteState().
        static constexpr size_t kRouteDataBytes = 14 * Krate::Plugins::kMaxVoiceRoutes;
        uint8_t dummy[kRouteDataBytes]{};
        auto* attrs = voiceRouteSyncMsg_->getAttributes();
        if (attrs) {
            attrs->setBinary("routeData", dummy, kRouteDataBytes);
            attrs->setInt("routeCount", 0);
        }
    }

    // Pre-allocate one-time pointer messages (Issue 3: avoid allocateMessage on audio thread)
    playbackMsg_ = Steinberg::owned(allocateMessage());
    if (playbackMsg_) {
        playbackMsg_->setMessageID("TranceGatePlayback");
        auto* attrs = playbackMsg_->getAttributes();
        if (attrs) {
            attrs->setInt("stepPtr",
                static_cast<Steinberg::int64>(
                    reinterpret_cast<intptr_t>(&tranceGatePlaybackStep_)));
            attrs->setInt("playingPtr",
                static_cast<Steinberg::int64>(
                    reinterpret_cast<intptr_t>(&isTransportPlaying_)));
        }
    }

    envDisplayMsg_ = Steinberg::owned(allocateMessage());
    if (envDisplayMsg_) {
        envDisplayMsg_->setMessageID("EnvelopeDisplayState");
        auto* attrs = envDisplayMsg_->getAttributes();
        if (attrs) {
            attrs->setInt("ampOutputPtr",
                static_cast<Steinberg::int64>(
                    reinterpret_cast<intptr_t>(&ampEnvDisplayOutput_)));
            attrs->setInt("ampStagePtr",
                static_cast<Steinberg::int64>(
                    reinterpret_cast<intptr_t>(&ampEnvDisplayStage_)));
            attrs->setInt("filterOutputPtr",
                static_cast<Steinberg::int64>(
                    reinterpret_cast<intptr_t>(&filterEnvDisplayOutput_)));
            attrs->setInt("filterStagePtr",
                static_cast<Steinberg::int64>(
                    reinterpret_cast<intptr_t>(&filterEnvDisplayStage_)));
            attrs->setInt("modOutputPtr",
                static_cast<Steinberg::int64>(
                    reinterpret_cast<intptr_t>(&modEnvDisplayOutput_)));
            attrs->setInt("modStagePtr",
                static_cast<Steinberg::int64>(
                    reinterpret_cast<intptr_t>(&modEnvDisplayStage_)));
            attrs->setInt("voiceActivePtr",
                static_cast<Steinberg::int64>(
                    reinterpret_cast<intptr_t>(&envVoiceActive_)));
        }
    }

    morphPadModMsg_ = Steinberg::owned(allocateMessage());
    if (morphPadModMsg_) {
        morphPadModMsg_->setMessageID("MorphPadModulation");
        auto* attrs = morphPadModMsg_->getAttributes();
        if (attrs) {
            attrs->setInt("morphXPtr",
                static_cast<Steinberg::int64>(
                    reinterpret_cast<intptr_t>(&modulatedMorphX_)));
            attrs->setInt("morphYPtr",
                static_cast<Steinberg::int64>(
                    reinterpret_cast<intptr_t>(&modulatedMorphY_)));
        }
    }

    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Processor::terminate() {
    stateTransfer_.clear_ui();
    return AudioEffect::terminate();
}

// ==============================================================================
// IAudioProcessor
// ==============================================================================

Steinberg::tresult PLUGIN_API Processor::setupProcessing(
    Steinberg::Vst::ProcessSetup& setup) {

    sampleRate_ = setup.sampleRate;
    maxBlockSize_ = setup.maxSamplesPerBlock;

    // Constitution Principle II: Pre-allocate ALL buffers HERE
    mixBufferL_.resize(static_cast<size_t>(maxBlockSize_));
    mixBufferR_.resize(static_cast<size_t>(maxBlockSize_));

    // Prepare engine (allocates internal buffers)
    engine_.prepare(sampleRate_, static_cast<size_t>(maxBlockSize_));

    // Prepare arpeggiator (FR-008)
    arpCore_.prepare(sampleRate_, static_cast<size_t>(maxBlockSize_));

    // Reset transport detection so the flag is re-learned if plugin moves between hosts
    hostSupportsTransport_ = false;

    logPhaser("[RUINAE] setupProcessing: sampleRate=%.0f maxBlock=%d\n", sampleRate_, maxBlockSize_);

    return AudioEffect::setupProcessing(setup);
}

Steinberg::tresult PLUGIN_API Processor::setActive(Steinberg::TBool state) {
    if (state) {
        // Activating: reset DSP state
        engine_.reset();
        arpCore_.reset();
        std::fill(mixBufferL_.begin(), mixBufferL_.end(), 0.0f);
        std::fill(mixBufferR_.begin(), mixBufferR_.end(), 0.0f);
    }

    return AudioEffect::setActive(state);
}

Steinberg::tresult PLUGIN_API Processor::process(Steinberg::Vst::ProcessData& data) {
    // ==========================================================================
    // Constitution Principle II: REAL-TIME SAFETY CRITICAL
    // - NO memory allocation, NO locks, NO exceptions
    // ==========================================================================

    // Drain any pending preset snapshot from the UI thread (lock-free).
    // This applies the full preset atomically in one block, preventing
    // "parameter tearing" crashes during preset switching.
    stateTransfer_.accessTransferObject_rt([this](PresetSnapshot& snap) {
        applyPresetSnapshot(snap);
    });

    // Process parameter changes first
    if (data.inputParameterChanges) {
        processParameterChanges(data.inputParameterChanges);
    }

    // Cache host tempo for sync computations in applyParamsToEngine()
    if (data.processContext &&
        (data.processContext->state & Steinberg::Vst::ProcessContext::kTempoValid)) {
        tempoBPM_ = data.processContext->tempo;
    }

    // Apply all parameter values to the engine
    applyParamsToEngine();

#if RUINAE_PHASER_DEBUG
    if (s_logCounter % 200 == 0) {
        bool pEn = modulationType_.load(std::memory_order_relaxed) == 1; // Phaser
        float pRate = phaserParams_.rateHz.load(std::memory_order_relaxed);
        float pDepth = phaserParams_.depth.load(std::memory_order_relaxed);
        float pMix = phaserParams_.mix.load(std::memory_order_relaxed);
        float pFb = phaserParams_.feedback.load(std::memory_order_relaxed);
        int pStages = phaserParams_.stages.load(std::memory_order_relaxed);
        float pCenter = phaserParams_.centerFreqHz.load(std::memory_order_relaxed);
        logPhaser("[RUINAE][block %d] phaserEnabled=%d rate=%.2f depth=%.2f mix=%.2f fb=%.2f stages=%d(%d) center=%.0f\n",
            s_logCounter, pEn ? 1 : 0, pRate, pDepth, pMix, pFb,
            pStages, phaserStagesFromIndex(pStages), pCenter);
    }
    ++s_logCounter;
#endif

    // Build and forward BlockContext from host tempo/transport
    Krate::DSP::BlockContext blockCtx;
    {
        blockCtx.sampleRate = sampleRate_;
        blockCtx.blockSize = (data.numSamples > 0) ? static_cast<size_t>(data.numSamples) : 0;

        if (data.processContext) {
            auto* pc = data.processContext;
            if (pc->state & Steinberg::Vst::ProcessContext::kTempoValid) {
                blockCtx.tempoBPM = pc->tempo;
            }
            if (pc->state & Steinberg::Vst::ProcessContext::kTimeSigValid) {
                blockCtx.timeSignatureNumerator = static_cast<uint8_t>(pc->timeSigNumerator);
                blockCtx.timeSignatureDenominator = static_cast<uint8_t>(pc->timeSigDenominator);
            }
            blockCtx.isPlaying = (pc->state & Steinberg::Vst::ProcessContext::kPlaying) != 0;
            if (pc->state & Steinberg::Vst::ProcessContext::kProjectTimeMusicValid) {
                blockCtx.projectTimeMusic = pc->projectTimeMusic;
                blockCtx.projectTimeMusicValid = true;
                // Convert musical time (beats) to samples approximation
                blockCtx.transportPositionSamples = static_cast<int64_t>(
                    pc->projectTimeMusic * (60.0 / blockCtx.tempoBPM) * blockCtx.sampleRate);
            }
        }

        engine_.setBlockContext(blockCtx);
        engine_.setTempo(blockCtx.tempoBPM);
    }

    // Process MIDI events (FR-006: branches on arp enabled state)
    if (data.inputEvents) {
        processEvents(data.inputEvents);
    }

    // Arp block processing (FR-007, FR-017, FR-018)
    // Must run after processEvents() and before engine_.processBlock()
    const int arpOpMode = arpParams_.operatingMode.load(std::memory_order_relaxed);
    const bool isArpRunning = (arpOpMode != kArpOff);
    const bool isArpDispatchingNotes = (arpOpMode == kArpMIDI || arpOpMode == kArpMIDIMod);
    const bool isArpModSource = (arpOpMode == kArpMod || arpOpMode == kArpMIDIMod);
    if (isArpRunning) {
        // FR-017: setEnabled(false) queues cleanup note-offs internally.
        // The processBlock() call below drains them through the standard
        // routing loop, ensuring every note-on has a matching note-off.

        // Track whether the host ever reports kPlaying — if so, it supports
        // transport and we should respect its stop state.
        if (blockCtx.isPlaying) {
            hostSupportsTransport_ = true;
        }

        Krate::DSP::BlockContext arpCtx = blockCtx;
        // Simple hosts that never set kPlaying: force the arp to always run.
        // DAW hosts that do set kPlaying: pass the real transport state
        // through so ArpeggiatorCore can emit NoteOffs on transport stop.
        if (!hostSupportsTransport_) {
            arpCtx.isPlaying = true;
        }

        // Detect DAW transport loop (backward PPQ jump) and notify the arp
        // so it cleanly restarts: NoteOffs, lane reset, immediate step 0.
        // Works in both tempo-sync and free-rate modes.
        if (blockCtx.projectTimeMusicValid && blockCtx.isPlaying) {
            if (prevProjectTimeMusic_ >= 0.0 &&
                blockCtx.projectTimeMusic < prevProjectTimeMusic_ - 0.01) {
                arpCore_.notifyTransportLoop();
            }
            prevProjectTimeMusic_ = blockCtx.projectTimeMusic;
        }

        // Sync arp step clock to host musical position so transport
        // rewind/reposition keeps the arp grid-locked.
        if (blockCtx.projectTimeMusicValid && blockCtx.isPlaying) {
            arpCore_.syncToMusicalPosition(blockCtx.projectTimeMusic);
        }

        // Process arp block (processBlock takes std::span<ArpEvent>, returns event count)
        size_t numArpEvents = arpCore_.processBlock(arpCtx, arpEvents_);

        // Route arp events to engine (FR-007)
        const bool midiOutEnabled = arpParams_.midiOut.load(std::memory_order_relaxed);
        for (size_t i = 0; i < numArpEvents; ++i) {
            const auto& evt = arpEvents_[i];
            if (evt.type == Krate::DSP::ArpEvent::Type::NoteOn) {
                // Capture pitch for mod source (normalized 0-1)
                if (isArpModSource) {
                    lastArpPitch_ = static_cast<float>(evt.note) / 127.0f;
                }
                // Only dispatch note events to engine in MIDI/MIDI+Mod modes
                if (isArpDispatchingNotes) {
#if RUINAE_TGATE_DEBUG
                    logTGate("[TGATE] >>> arp noteOn note=%d vel=%d legato=%d (gate will reset)\n", evt.note, evt.velocity, evt.legato ? 1 : 0);
#endif
                    engine_.noteOn(evt.note, evt.velocity, evt.legato);
                    if (midiOutEnabled && data.outputEvents) {
                        Steinberg::Vst::Event e{};
                        e.busIndex = 0;
                        e.sampleOffset = evt.sampleOffset;
                        e.type = Steinberg::Vst::Event::kNoteOnEvent;
                        e.noteOn.channel = 0;
                        e.noteOn.pitch = evt.note;
                        e.noteOn.velocity = evt.velocity / 127.0f;
                        e.noteOn.tuning = 0.0f;
                        e.noteOn.length = 0;
                        e.noteOn.noteId = -1;
                        data.outputEvents->addEvent(e);
                    }
                }
            } else if (evt.type == Krate::DSP::ArpEvent::Type::NoteOff) {
                if (isArpDispatchingNotes) {
                    engine_.noteOff(evt.note);
                    if (midiOutEnabled && data.outputEvents) {
                        Steinberg::Vst::Event e{};
                        e.busIndex = 0;
                        e.sampleOffset = evt.sampleOffset;
                        e.type = Steinberg::Vst::Event::kNoteOffEvent;
                        e.noteOff.channel = 0;
                        e.noteOff.pitch = evt.note;
                        e.noteOff.velocity = 0.0f;
                        e.noteOff.noteId = -1;
                        e.noteOff.tuning = 0.0f;
                        data.outputEvents->addEvent(e);
                    }
                }
            } else if (evt.type == Krate::DSP::ArpEvent::Type::kSkip) {
                // 081-interaction-polish: send skip event to controller (FR-007, FR-008)
                // evt.note carries the step index (0-31)
                const int step = static_cast<int>(evt.note);
                for (int lane = 0; lane < 6; ++lane) {
                    sendSkipEvent(lane, step);
                }
            }
        }

        // 079-layout-framework US5: Write per-lane playhead positions to output
        // parameters. The controller polls these at ~30fps to update the UI.
        // Encoding: stepIndex / 32.0 (kMaxSteps as denominator, consistent
        // regardless of actual lane length).
        if (data.outputParameterChanges) {
            constexpr float kMaxStepsF = 32.0f;
            const auto velStep = static_cast<float>(
                arpCore_.velocityLane().currentStep());
            const auto gateStep = static_cast<float>(
                arpCore_.gateLane().currentStep());

            Steinberg::int32 queueIndex = 0;
            auto* velQueue = data.outputParameterChanges->addParameterData(
                kArpVelocityPlayheadId, queueIndex);
            if (velQueue) {
                Steinberg::int32 pointIndex = 0;
                velQueue->addPoint(0, static_cast<double>(velStep / kMaxStepsF),
                                   pointIndex);
            }
            auto* gateQueue = data.outputParameterChanges->addParameterData(
                kArpGatePlayheadId, queueIndex);
            if (gateQueue) {
                Steinberg::int32 pointIndex = 0;
                gateQueue->addPoint(0, static_cast<double>(gateStep / kMaxStepsF),
                                    pointIndex);
            }
        }
    } else {
        // 079-layout-framework US5: Arp disabled -- write sentinel (1.0f) to
        // indicate no playback. Decoded as stepIndex=32 >= kMaxSteps -> -1.
        if (data.outputParameterChanges) {
            Steinberg::int32 queueIndex = 0;
            auto* velQueue = data.outputParameterChanges->addParameterData(
                kArpVelocityPlayheadId, queueIndex);
            if (velQueue) {
                Steinberg::int32 pointIndex = 0;
                velQueue->addPoint(0, 1.0, pointIndex);
            }
            auto* gateQueue = data.outputParameterChanges->addParameterData(
                kArpGatePlayheadId, queueIndex);
            if (gateQueue) {
                Steinberg::int32 pointIndex = 0;
                gateQueue->addPoint(0, 1.0, pointIndex);
            }
        }
    }

    // Check if we have audio to process
    if (data.numSamples == 0) {
        return Steinberg::kResultTrue;
    }

    // Verify we have valid output
    if (data.numOutputs == 0 || data.outputs[0].numChannels < 2) {
        return Steinberg::kResultTrue;
    }

    float* outputL = data.outputs[0].channelBuffers32[0];
    float* outputR = data.outputs[0].channelBuffers32[1];

    if (!outputL || !outputR) {
        return Steinberg::kResultTrue;
    }

    const auto numSamples = static_cast<size_t>(data.numSamples);

    // ==========================================================================
    // Main Audio Processing
    // ==========================================================================

    // Clear output buffers (engine writes into them)
    std::fill_n(outputL, numSamples, 0.0f);
    std::fill_n(outputR, numSamples, 0.0f);

    // Feed arp pitch as External0 mod source (before mod engine processes)
    if (isArpModSource) {
        engine_.setExternalSourceValue(0, lastArpPitch_);
    }

    // Extract sidechain audio if available (for EnvFollower, PitchFollower, Transient)
    const float* sidechainL = nullptr;
    const float* sidechainR = nullptr;
    bool sidechainActive = false;
    if (data.numInputs > 0 && data.inputs) {
        const auto& scBus = data.inputs[0];
        if (scBus.numChannels >= 2 &&
            scBus.channelBuffers32 &&
            scBus.channelBuffers32[0] && scBus.channelBuffers32[1]) {
            sidechainL = scBus.channelBuffers32[0];
            sidechainR = scBus.channelBuffers32[1];
            sidechainActive = true;
        } else if (scBus.numChannels == 1 &&
                   scBus.channelBuffers32 && scBus.channelBuffers32[0]) {
            sidechainL = scBus.channelBuffers32[0];
            sidechainR = scBus.channelBuffers32[0];  // mono → both channels
            sidechainActive = true;
        }
    }

    // Pass sidechain to engine (overrides self-analysis when connected)
    engine_.setSidechainInput(sidechainL, sidechainR);

    // Write sidechain active state as output parameter for UI indicator
    if (data.outputParameterChanges) {
        float activeVal = sidechainActive ? 1.0f : 0.0f;
        if (activeVal != lastSidechainActive_) {
            Steinberg::int32 queueIndex = 0;
            auto* queue = data.outputParameterChanges->addParameterData(
                kSidechainActiveId, queueIndex);
            if (queue) {
                Steinberg::int32 pointIndex = 0;
                queue->addPoint(0, static_cast<double>(activeVal), pointIndex);
            }
            lastSidechainActive_ = activeVal;
        }
    }

    // Process audio through the engine
    engine_.processBlock(outputL, outputR, numSamples);

#if RUINAE_TGATE_DEBUG
    {
        const int tgStep = engine_.getTranceGateCurrentStep();
        const bool stepChanged = (tgStep != s_tgLastStep && tgStep >= 0);

        // Log on step change or every 200 blocks
        if (stepChanged || (s_tgLogCounter % 200 == 0 && tgStep >= 0)) {
            const int tgNumSteps = engine_.getTranceGateNumSteps();
            const size_t tgSamplesPerStep = engine_.getTranceGateSamplesPerStep();
            const double tgTempo = engine_.getTranceGateTempoBPM();
            const bool tgSync = engine_.getTranceGateTempoSync();
            const size_t tgSampleCtr = engine_.getTranceGateSampleCounter();
            const int voiceIdx = engine_.getTranceGateActiveVoiceIndex();

            if (stepChanged) {
                logTGate("[TGATE] STEP %d->%d | voice=%d numSteps=%d samplesPerStep=%zu tempo=%.1f sync=%d sampleCtr=%zu hostTempo=%.1f\n",
                    s_tgLastStep, tgStep, voiceIdx, tgNumSteps, tgSamplesPerStep, tgTempo, tgSync ? 1 : 0, tgSampleCtr, tempoBPM_);
            } else {
                logTGate("[TGATE] periodic | step=%d/%d voice=%d samplesPerStep=%zu tempo=%.1f sync=%d sampleCtr=%zu hostTempo=%.1f\n",
                    tgStep, tgNumSteps, voiceIdx, tgSamplesPerStep, tgTempo, tgSync ? 1 : 0, tgSampleCtr, tempoBPM_);
            }
        }

        s_tgLastStep = tgStep;
        ++s_tgLogCounter;
    }
#endif

    // Update morph pad modulated position for UI animation
    {
        using Krate::DSP::RuinaeModDest;
        const float morphOffset = engine_.getGlobalModOffset(
            RuinaeModDest::AllVoiceMorphPosition);
        const float tiltOffset = engine_.getGlobalModOffset(
            RuinaeModDest::AllVoiceSpectralTilt);

        const float baseX = mixerParams_.position.load(std::memory_order_relaxed);
        modulatedMorphX_.store(
            std::clamp(baseX + morphOffset, 0.0f, 1.0f),
            std::memory_order_relaxed);

        // Tilt: base is dB [-12,+12], offset is normalized scaled by 24 → dB
        const float baseTiltDb = mixerParams_.tilt.load(std::memory_order_relaxed);
        const float modTiltDb = std::clamp(
            baseTiltDb + tiltOffset * 24.0f, -12.0f, 12.0f);
        modulatedMorphY_.store(
            (modTiltDb + 12.0f) / 24.0f, std::memory_order_relaxed);
    }

    // Update shared playback position atomics for controller UI
    tranceGatePlaybackStep_.store(
        engine_.getTranceGateCurrentStep(), std::memory_order_relaxed);
    bool playing = data.processContext != nullptr &&
        (data.processContext->state & Steinberg::Vst::ProcessContext::kPlaying) != 0;
    isTransportPlaying_.store(playing, std::memory_order_relaxed);

    // Update envelope display state from the most recently triggered voice
    {
        // Find most recently triggered active voice
        size_t bestVoice = 0;
        bool anyActive = false;

        // Check all voices for the one with highest timestamp
        for (size_t i = 0; i < 16; ++i) {
            if (engine_.isVoiceActive(i)) {
                anyActive = true;
                bestVoice = i;
                break; // Use first active voice as fallback
            }
        }

        // Use the most recently triggered voice from engine
        size_t mrv = engine_.getMostRecentActiveVoice();
        if (engine_.isVoiceActive(mrv)) {
            bestVoice = mrv;
            anyActive = true;
        }

        envVoiceActive_.store(anyActive, std::memory_order_relaxed);

        if (anyActive) {
            const auto& ampEnv = engine_.getVoiceAmpEnvelope(bestVoice);
            ampEnvDisplayOutput_.store(ampEnv.getOutput(), std::memory_order_relaxed);
            ampEnvDisplayStage_.store(
                static_cast<int>(ampEnv.getStage()), std::memory_order_relaxed);

            const auto& filterEnv = engine_.getVoiceFilterEnvelope(bestVoice);
            filterEnvDisplayOutput_.store(filterEnv.getOutput(), std::memory_order_relaxed);
            filterEnvDisplayStage_.store(
                static_cast<int>(filterEnv.getStage()), std::memory_order_relaxed);

            const auto& modEnv = engine_.getVoiceModEnvelope(bestVoice);
            modEnvDisplayOutput_.store(modEnv.getOutput(), std::memory_order_relaxed);
            modEnvDisplayStage_.store(
                static_cast<int>(modEnv.getStage()), std::memory_order_relaxed);
        }
    }

    // Send playback pointer message to controller (one-time, pre-allocated)
    if (!playbackMessageSent_ && playbackMsg_) {
        sendMessage(playbackMsg_);
        playbackMessageSent_ = true;
    }

    // Send envelope display state pointers to controller (one-time, pre-allocated)
    if (!envDisplayMessageSent_ && envDisplayMsg_) {
        sendMessage(envDisplayMsg_);
        envDisplayMessageSent_ = true;
    }

    // Send morph pad modulation pointers to controller (one-time, pre-allocated)
    if (!morphPadModMessageSent_ && morphPadModMsg_) {
        sendMessage(morphPadModMsg_);
        morphPadModMessageSent_ = true;
    }

    // Send voice route state to controller after a preset snapshot was applied
    if (needVoiceRouteSync_.exchange(false, std::memory_order_relaxed)) {
        sendVoiceModRouteState();
    }

    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Processor::setBusArrangements(
    Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
    Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) {

    // Ruinae is an instrument with optional sidechain input:
    // Accept either no inputs (instrument-only) or 1 stereo input (sidechain)
    if (numOuts == 1 && outputs[0] == Steinberg::Vst::SpeakerArr::kStereo) {
        if (numIns == 0) {
            return Steinberg::kResultTrue;
        }
        if (numIns == 1 && inputs[0] == Steinberg::Vst::SpeakerArr::kStereo) {
            return Steinberg::kResultTrue;
        }
    }

    return Steinberg::kResultFalse;
}

} // namespace Ruinae
