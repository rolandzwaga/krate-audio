// ==============================================================================
// Audio Processor Implementation
// ==============================================================================

#include "processor.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <krate/dsp/core/block_context.h>
#include <krate/dsp/core/note_value.h>
#include <krate/dsp/effects/reverb.h>
#include <krate/dsp/processors/trance_gate.h>
#include "ruinae_types.h"
#include <krate/dsp/systems/oscillator_types.h>
#include <krate/dsp/systems/voice_mod_types.h>
#include <krate/dsp/primitives/lfo.h>
#include <krate/dsp/primitives/svf.h>
#include <krate/dsp/processors/mono_handler.h>
#include <krate/dsp/systems/poly_synth_engine.h>
#include <krate/dsp/core/modulation_types.h>

#include "parameters/dropdown_mappings.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

// =============================================================================
// DEBUG: Phaser signal path tracing (remove after debugging)
// =============================================================================
#define RUINAE_PHASER_DEBUG 0

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
    // - Event input (MIDI notes)
    // - Stereo audio output (no audio input)
    addEventInput(STR16("Event Input"));
    addAudioOutput(STR16("Audio Output"), Steinberg::Vst::SpeakerArr::kStereo);

    // Pre-allocate skip event IMessages (Phase 11c, FR-012)
    for (int i = 0; i < 6; ++i) {
        skipMessages_[static_cast<size_t>(i)] = Steinberg::owned(allocateMessage());
        if (skipMessages_[static_cast<size_t>(i)]) {
            skipMessages_[static_cast<size_t>(i)]->setMessageID("ArpSkipEvent");
        }
    }

    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Processor::terminate() {
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
        bool pEn = phaserEnabled_.load(std::memory_order_relaxed);
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
                // Convert musical time (beats) to samples approximation
                blockCtx.transportPositionSamples = static_cast<int64_t>(
                    pc->projectTimeMusic * (60.0 / blockCtx.tempoBPM) * blockCtx.sampleRate);
            }
        }

        engine_.setBlockContext(blockCtx);
    }

    // Process MIDI events (FR-006: branches on arp enabled state)
    if (data.inputEvents) {
        processEvents(data.inputEvents);
    }

    // Arp block processing (FR-007, FR-017, FR-018)
    // Must run after processEvents() and before engine_.processBlock()
    if (arpParams_.enabled.load(std::memory_order_relaxed)) {
        // FR-017: setEnabled(false) queues cleanup note-offs internally.
        // The processBlock() call below drains them through the standard
        // routing loop, ensuring every note-on has a matching note-off.

        // The arp must always advance when enabled, regardless of host
        // transport state. Simple hosts (e.g., Plugin Buddy) may never set
        // kPlaying, and even in DAWs the arp should be playable without
        // pressing Play. We force isPlaying=true so ArpeggiatorCore's
        // internal timing always runs. The arp uses tempoBPM (always
        // available, default 120) for tempo-sync timing.
        Krate::DSP::BlockContext arpCtx = blockCtx;
        arpCtx.isPlaying = true;

        // Process arp block (processBlock takes std::span<ArpEvent>, returns event count)
        size_t numArpEvents = arpCore_.processBlock(arpCtx, arpEvents_);

        // Route arp events to engine (FR-007)
        for (size_t i = 0; i < numArpEvents; ++i) {
            const auto& evt = arpEvents_[i];
            if (evt.type == Krate::DSP::ArpEvent::Type::NoteOn) {
                engine_.noteOn(evt.note, evt.velocity, evt.legato);
            } else if (evt.type == Krate::DSP::ArpEvent::Type::NoteOff) {
                engine_.noteOff(evt.note);
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

    // Process audio through the engine
    engine_.processBlock(outputL, outputR, numSamples);

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

    // Send playback pointer message to controller (one-time setup)
    if (!playbackMessageSent_) {
        auto msg = Steinberg::owned(allocateMessage());
        if (msg) {
            msg->setMessageID("TranceGatePlayback");
            auto* attrs = msg->getAttributes();
            if (attrs) {
                attrs->setInt("stepPtr",
                    static_cast<Steinberg::int64>(
                        reinterpret_cast<intptr_t>(&tranceGatePlaybackStep_)));
                attrs->setInt("playingPtr",
                    static_cast<Steinberg::int64>(
                        reinterpret_cast<intptr_t>(&isTransportPlaying_)));
                sendMessage(msg);
                playbackMessageSent_ = true;
            }
        }
    }

    // Send envelope display state pointers to controller (one-time setup)
    if (!envDisplayMessageSent_) {
        auto msg = Steinberg::owned(allocateMessage());
        if (msg) {
            msg->setMessageID("EnvelopeDisplayState");
            auto* attrs = msg->getAttributes();
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
                sendMessage(msg);
                envDisplayMessageSent_ = true;
            }
        }
    }

    // Send morph pad modulation pointers to controller (one-time setup)
    if (!morphPadModMessageSent_) {
        auto msg = Steinberg::owned(allocateMessage());
        if (msg) {
            msg->setMessageID("MorphPadModulation");
            auto* attrs = msg->getAttributes();
            if (attrs) {
                attrs->setInt("morphXPtr",
                    static_cast<Steinberg::int64>(
                        reinterpret_cast<intptr_t>(&modulatedMorphX_)));
                attrs->setInt("morphYPtr",
                    static_cast<Steinberg::int64>(
                        reinterpret_cast<intptr_t>(&modulatedMorphY_)));
                sendMessage(msg);
                morphPadModMessageSent_ = true;
            }
        }
    }

    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Processor::setBusArrangements(
    Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
    Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) {

    // Ruinae is an instrument: no audio inputs, stereo output only
    if (numIns == 0 && numOuts == 1 &&
        outputs[0] == Steinberg::Vst::SpeakerArr::kStereo) {
        return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
    }

    return Steinberg::kResultFalse;
}

// ==============================================================================
// IComponent - State Management
// ==============================================================================

Steinberg::tresult PLUGIN_API Processor::getState(Steinberg::IBStream* state) {
    Steinberg::IBStreamer streamer(state, kLittleEndian);

    // Write state version first
    streamer.writeInt32(kCurrentStateVersion);

    // Save all 19 parameter packs in deterministic order
    saveGlobalParams(globalParams_, streamer);
    saveOscAParams(oscAParams_, streamer);
    saveOscBParams(oscBParams_, streamer);
    saveMixerParams(mixerParams_, streamer);
    saveFilterParams(filterParams_, streamer);
    saveDistortionParams(distortionParams_, streamer);
    saveTranceGateParams(tranceGateParams_, streamer);
    saveAmpEnvParams(ampEnvParams_, streamer);
    saveFilterEnvParams(filterEnvParams_, streamer);
    saveModEnvParams(modEnvParams_, streamer);
    saveLFO1Params(lfo1Params_, streamer);
    saveLFO2Params(lfo2Params_, streamer);
    saveChaosModParams(chaosModParams_, streamer);
    saveModMatrixParams(modMatrixParams_, streamer);
    saveGlobalFilterParams(globalFilterParams_, streamer);
    saveDelayParams(delayParams_, streamer);
    saveReverbParams(reverbParams_, streamer);
    saveMonoModeParams(monoModeParams_, streamer);

    // Voice routes (16 slots)
    for (const auto& r : voiceRoutes_) {
        streamer.writeInt8(static_cast<Steinberg::int8>(r.source));
        streamer.writeInt8(static_cast<Steinberg::int8>(r.destination));
        streamer.writeFloat(r.amount);
        streamer.writeInt8(static_cast<Steinberg::int8>(r.curve));
        streamer.writeFloat(r.smoothMs);
        streamer.writeInt8(static_cast<Steinberg::int8>(r.scale));
        streamer.writeInt8(static_cast<Steinberg::int8>(r.bypass));
        streamer.writeInt8(static_cast<Steinberg::int8>(r.active));
    }

    // FX enable flags
    streamer.writeInt8(delayEnabled_.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeInt8(reverbEnabled_.load(std::memory_order_relaxed) ? 1 : 0);

    // Phaser params + enable flag
    savePhaserParams(phaserParams_, streamer);
    streamer.writeInt8(phaserEnabled_.load(std::memory_order_relaxed) ? 1 : 0);

    // Extended LFO params
    saveLFO1ExtendedParams(lfo1Params_, streamer);
    saveLFO2ExtendedParams(lfo2Params_, streamer);

    // Macro and Rungler params
    saveMacroParams(macroParams_, streamer);
    saveRunglerParams(runglerParams_, streamer);

    // Settings params
    saveSettingsParams(settingsParams_, streamer);

    // Mod source params
    saveEnvFollowerParams(envFollowerParams_, streamer);
    saveSampleHoldParams(sampleHoldParams_, streamer);
    saveRandomParams(randomParams_, streamer);
    savePitchFollowerParams(pitchFollowerParams_, streamer);
    saveTransientParams(transientParams_, streamer);

    // Harmonizer params + enable flag
    saveHarmonizerParams(harmonizerParams_, streamer);
    streamer.writeInt8(harmonizerEnabled_.load(std::memory_order_relaxed) ? 1 : 0);

    // Arpeggiator params (FR-011)
    saveArpParams(arpParams_, streamer);

    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Processor::setState(Steinberg::IBStream* state) {
    Steinberg::IBStreamer streamer(state, kLittleEndian);

    Steinberg::int32 version = 0;
    if (!streamer.readInt32(version)) {
        return Steinberg::kResultTrue; // Empty stream, keep defaults
    }

    if (version != 1) {
        return Steinberg::kResultTrue; // Unknown version, keep defaults
    }

    // Load all parameter packs in deterministic order (matching getState)
    if (!loadGlobalParams(globalParams_, streamer)) return Steinberg::kResultTrue;
    if (!loadOscAParams(oscAParams_, streamer)) return Steinberg::kResultTrue;
    if (!loadOscBParams(oscBParams_, streamer)) return Steinberg::kResultTrue;
    if (!loadMixerParams(mixerParams_, streamer)) return Steinberg::kResultTrue;
    if (!loadFilterParams(filterParams_, streamer)) return Steinberg::kResultTrue;
    if (!loadDistortionParams(distortionParams_, streamer)) return Steinberg::kResultTrue;
    if (!loadTranceGateParams(tranceGateParams_, streamer)) return Steinberg::kResultTrue;
    if (!loadAmpEnvParams(ampEnvParams_, streamer)) return Steinberg::kResultTrue;
    if (!loadFilterEnvParams(filterEnvParams_, streamer)) return Steinberg::kResultTrue;
    if (!loadModEnvParams(modEnvParams_, streamer)) return Steinberg::kResultTrue;
    if (!loadLFO1Params(lfo1Params_, streamer)) return Steinberg::kResultTrue;
    if (!loadLFO2Params(lfo2Params_, streamer)) return Steinberg::kResultTrue;
    if (!loadChaosModParams(chaosModParams_, streamer)) return Steinberg::kResultTrue;
    if (!loadModMatrixParams(modMatrixParams_, streamer)) return Steinberg::kResultTrue;
    if (!loadGlobalFilterParams(globalFilterParams_, streamer)) return Steinberg::kResultTrue;
    if (!loadDelayParams(delayParams_, streamer)) return Steinberg::kResultTrue;
    if (!loadReverbParams(reverbParams_, streamer)) return Steinberg::kResultTrue;
    if (!loadMonoModeParams(monoModeParams_, streamer)) return Steinberg::kResultTrue;

    // Voice routes (16 slots)
    for (auto& r : voiceRoutes_) {
        Steinberg::int8 i8 = 0;
        if (!streamer.readInt8(i8)) break;
        r.source = static_cast<uint8_t>(i8);
        if (!streamer.readInt8(i8)) break;
        r.destination = static_cast<uint8_t>(i8);
        if (!streamer.readFloat(r.amount)) break;
        if (!streamer.readInt8(i8)) break;
        r.curve = static_cast<uint8_t>(i8);
        if (!streamer.readFloat(r.smoothMs)) break;
        if (!streamer.readInt8(i8)) break;
        r.scale = static_cast<uint8_t>(i8);
        if (!streamer.readInt8(i8)) break;
        r.bypass = static_cast<uint8_t>(i8);
        if (!streamer.readInt8(i8)) break;
        r.active = static_cast<uint8_t>(i8);
    }
    sendVoiceModRouteState();

    // FX enable flags
    Steinberg::int8 i8 = 0;
    if (streamer.readInt8(i8))
        delayEnabled_.store(i8 != 0, std::memory_order_relaxed);
    if (streamer.readInt8(i8))
        reverbEnabled_.store(i8 != 0, std::memory_order_relaxed);

    // Phaser params + enable flag
    loadPhaserParams(phaserParams_, streamer);
    if (streamer.readInt8(i8))
        phaserEnabled_.store(i8 != 0, std::memory_order_relaxed);

    // Extended LFO params
    loadLFO1ExtendedParams(lfo1Params_, streamer);
    loadLFO2ExtendedParams(lfo2Params_, streamer);

    // Macro and Rungler params
    loadMacroParams(macroParams_, streamer);
    loadRunglerParams(runglerParams_, streamer);

    // Settings params
    loadSettingsParams(settingsParams_, streamer);

    // Mod source params
    loadEnvFollowerParams(envFollowerParams_, streamer);
    loadSampleHoldParams(sampleHoldParams_, streamer);
    loadRandomParams(randomParams_, streamer);
    loadPitchFollowerParams(pitchFollowerParams_, streamer);
    loadTransientParams(transientParams_, streamer);

    // Harmonizer params + enable flag
    loadHarmonizerParams(harmonizerParams_, streamer);
    if (streamer.readInt8(i8))
        harmonizerEnabled_.store(i8 != 0, std::memory_order_relaxed);

    // Arpeggiator params (FR-011) -- backward compat: loadArpParams returns
    // false on truncated/old streams, leaving arpParams_ at defaults
    loadArpParams(arpParams_, streamer);

    return Steinberg::kResultTrue;
}

// ==============================================================================
// Parameter Handling
// ==============================================================================

void Processor::processParameterChanges(Steinberg::Vst::IParameterChanges* changes) {
    if (!changes) {
        return;
    }

    const Steinberg::int32 numParamsChanged = changes->getParameterCount();

    for (Steinberg::int32 i = 0; i < numParamsChanged; ++i) {
        Steinberg::Vst::IParamValueQueue* paramQueue = changes->getParameterData(i);
        if (!paramQueue) {
            continue;
        }

        const Steinberg::Vst::ParamID paramId = paramQueue->getParameterId();
        const Steinberg::int32 numPoints = paramQueue->getPointCount();

        // Get the last value (most recent)
        Steinberg::int32 sampleOffset = 0;
        Steinberg::Vst::ParamValue value = 0.0;

        if (paramQueue->getPoint(numPoints - 1, sampleOffset, value)
            != Steinberg::kResultTrue) {
            continue;
        }

        // =======================================================================
        // Route parameter changes by ID range
        // =======================================================================

        if (paramId <= kGlobalEndId) {
            handleGlobalParamChange(globalParams_, paramId, value);
        } else if (paramId >= kOscABaseId && paramId <= kOscAEndId) {
            handleOscAParamChange(oscAParams_, paramId, value);
        } else if (paramId >= kOscBBaseId && paramId <= kOscBEndId) {
            handleOscBParamChange(oscBParams_, paramId, value);
        } else if (paramId >= kMixerBaseId && paramId <= kMixerEndId) {
            handleMixerParamChange(mixerParams_, paramId, value);
        } else if (paramId >= kFilterBaseId && paramId <= kFilterEndId) {
            handleFilterParamChange(filterParams_, paramId, value);
        } else if (paramId >= kDistortionBaseId && paramId <= kDistortionEndId) {
            handleDistortionParamChange(distortionParams_, paramId, value);
        } else if (paramId >= kTranceGateBaseId && paramId <= kTranceGateEndId) {
            handleTranceGateParamChange(tranceGateParams_, paramId, value);
        } else if (paramId >= kAmpEnvBaseId && paramId <= kAmpEnvEndId) {
            handleAmpEnvParamChange(ampEnvParams_, paramId, value);
        } else if (paramId >= kFilterEnvBaseId && paramId <= kFilterEnvEndId) {
            handleFilterEnvParamChange(filterEnvParams_, paramId, value);
        } else if (paramId >= kModEnvBaseId && paramId <= kModEnvEndId) {
            handleModEnvParamChange(modEnvParams_, paramId, value);
        } else if (paramId >= kLFO1BaseId && paramId <= kLFO1EndId) {
            handleLFO1ParamChange(lfo1Params_, paramId, value);
        } else if (paramId >= kLFO2BaseId && paramId <= kLFO2EndId) {
            handleLFO2ParamChange(lfo2Params_, paramId, value);
        } else if (paramId >= kChaosModBaseId && paramId <= kChaosModEndId) {
            handleChaosModParamChange(chaosModParams_, paramId, value);
        } else if (paramId >= kModMatrixBaseId && paramId <= kModMatrixEndId) {
            handleModMatrixParamChange(modMatrixParams_, paramId, value);
        } else if (paramId >= kGlobalFilterBaseId && paramId <= kGlobalFilterEndId) {
            handleGlobalFilterParamChange(globalFilterParams_, paramId, value);
        } else if (paramId == kDelayEnabledId) {
            delayEnabled_.store(value >= 0.5, std::memory_order_relaxed);
        } else if (paramId == kReverbEnabledId) {
            reverbEnabled_.store(value >= 0.5, std::memory_order_relaxed);
        } else if (paramId == kPhaserEnabledId) {
            phaserEnabled_.store(value >= 0.5, std::memory_order_relaxed);
            logPhaser("[RUINAE][PARAM] kPhaserEnabledId received: raw=%.4f -> enabled=%d\n",
                value, (value >= 0.5) ? 1 : 0);
        } else if (paramId == kHarmonizerEnabledId) {
            harmonizerEnabled_.store(value >= 0.5, std::memory_order_relaxed);
        } else if (paramId >= kDelayBaseId && paramId <= kDelayEndId) {
            handleDelayParamChange(delayParams_, paramId, value);
        } else if (paramId >= kReverbBaseId && paramId <= kReverbEndId) {
            handleReverbParamChange(reverbParams_, paramId, value);
        } else if (paramId >= kPhaserBaseId && paramId <= kPhaserEndId) {
            handlePhaserParamChange(phaserParams_, paramId, value);
            logPhaser("[RUINAE][PARAM] phaser param %d received: raw=%.4f\n", paramId, value);
        } else if (paramId >= kMonoBaseId && paramId <= kMonoEndId) {
            handleMonoModeParamChange(monoModeParams_, paramId, value);
        } else if (paramId >= kMacroBaseId && paramId <= kMacroEndId) {
            handleMacroParamChange(macroParams_, paramId, value);
        } else if (paramId >= kRunglerBaseId && paramId <= kRunglerEndId) {
            handleRunglerParamChange(runglerParams_, paramId, value);
        } else if (paramId >= kSettingsBaseId && paramId <= kSettingsEndId) {
            handleSettingsParamChange(settingsParams_, paramId, value);
        } else if (paramId >= kEnvFollowerBaseId && paramId <= kEnvFollowerEndId) {
            handleEnvFollowerParamChange(envFollowerParams_, paramId, value);
        } else if (paramId >= kSampleHoldBaseId && paramId <= kSampleHoldEndId) {
            handleSampleHoldParamChange(sampleHoldParams_, paramId, value);
        } else if (paramId >= kRandomBaseId && paramId <= kRandomEndId) {
            handleRandomParamChange(randomParams_, paramId, value);
        } else if (paramId >= kPitchFollowerBaseId && paramId <= kPitchFollowerEndId) {
            handlePitchFollowerParamChange(pitchFollowerParams_, paramId, value);
        } else if (paramId >= kTransientBaseId && paramId <= kTransientEndId) {
            handleTransientParamChange(transientParams_, paramId, value);
        } else if (paramId >= kHarmonizerBaseId && paramId <= kHarmonizerEndId) {
            handleHarmonizerParamChange(harmonizerParams_, paramId, value);
        } else if (paramId >= kArpBaseId && paramId <= kArpEndId) {
            handleArpParamChange(arpParams_, paramId, value);
        }
    }
}

// ==============================================================================
// Apply Parameters to Engine
// ==============================================================================

void Processor::applyParamsToEngine() {
    using namespace Krate::DSP;

    // --- Global ---
    engine_.setMasterGain(globalParams_.masterGain.load(std::memory_order_relaxed));
    engine_.setMode(globalParams_.voiceMode.load(std::memory_order_relaxed) == 0
        ? VoiceMode::Poly : VoiceMode::Mono);
    engine_.setPolyphony(static_cast<size_t>(
        globalParams_.polyphony.load(std::memory_order_relaxed)));
    engine_.setSoftLimitEnabled(globalParams_.softLimit.load(std::memory_order_relaxed));
    engine_.setStereoWidth(globalParams_.width.load(std::memory_order_relaxed));
    engine_.setStereoSpread(globalParams_.spread.load(std::memory_order_relaxed));

    // --- OSC A ---
    engine_.setOscAType(static_cast<OscType>(
        oscAParams_.type.load(std::memory_order_relaxed)));
    engine_.setOscATuneSemitones(oscAParams_.tuneSemitones.load(std::memory_order_relaxed));
    engine_.setOscAFineCents(oscAParams_.fineCents.load(std::memory_order_relaxed));
    engine_.setOscALevel(oscAParams_.level.load(std::memory_order_relaxed));
    engine_.setOscAPhaseMode(oscAParams_.phase.load(std::memory_order_relaxed) >= 0.5f
        ? PhaseMode::Continuous : PhaseMode::Reset);

    // --- OSC B ---
    engine_.setOscBType(static_cast<OscType>(
        oscBParams_.type.load(std::memory_order_relaxed)));
    engine_.setOscBTuneSemitones(oscBParams_.tuneSemitones.load(std::memory_order_relaxed));
    engine_.setOscBFineCents(oscBParams_.fineCents.load(std::memory_order_relaxed));
    engine_.setOscBLevel(oscBParams_.level.load(std::memory_order_relaxed));
    engine_.setOscBPhaseMode(oscBParams_.phase.load(std::memory_order_relaxed) >= 0.5f
        ? PhaseMode::Continuous : PhaseMode::Reset);

    // --- OSC A Type-Specific Parameters (068-osc-type-params) ---
    {
        using Krate::DSP::OscParam;
        // Read denormalized DSP-domain values from atomics and forward to engine.
        // Integer atomics are cast to float -- the adapter casts back internally.
        const float oscAValues[] = {
            static_cast<float>(oscAParams_.waveform.load(std::memory_order_relaxed)),         // 0: Waveform
            oscAParams_.pulseWidth.load(std::memory_order_relaxed),                           // 1: PulseWidth
            oscAParams_.phaseMod.load(std::memory_order_relaxed),                             // 2: PhaseModulation
            oscAParams_.freqMod.load(std::memory_order_relaxed),                              // 3: FrequencyModulation
            static_cast<float>(oscAParams_.pdWaveform.load(std::memory_order_relaxed)),       // 4: PDWaveform
            oscAParams_.pdDistortion.load(std::memory_order_relaxed),                         // 5: PDDistortion
            oscAParams_.syncRatio.load(std::memory_order_relaxed),                            // 6: SyncSlaveRatio
            static_cast<float>(oscAParams_.syncWaveform.load(std::memory_order_relaxed)),     // 7: SyncSlaveWaveform
            static_cast<float>(oscAParams_.syncMode.load(std::memory_order_relaxed)),         // 8: SyncMode
            oscAParams_.syncAmount.load(std::memory_order_relaxed),                           // 9: SyncAmount
            oscAParams_.syncPulseWidth.load(std::memory_order_relaxed),                       // 10: SyncSlavePulseWidth
            static_cast<float>(oscAParams_.additivePartials.load(std::memory_order_relaxed)), // 11: AdditiveNumPartials
            oscAParams_.additiveTilt.load(std::memory_order_relaxed),                         // 12: AdditiveSpectralTilt
            oscAParams_.additiveInharm.load(std::memory_order_relaxed),                       // 13: AdditiveInharmonicity
            static_cast<float>(oscAParams_.chaosAttractor.load(std::memory_order_relaxed)),   // 14: ChaosAttractor
            oscAParams_.chaosAmount.load(std::memory_order_relaxed),                          // 15: ChaosAmount
            oscAParams_.chaosCoupling.load(std::memory_order_relaxed),                        // 16: ChaosCoupling
            static_cast<float>(oscAParams_.chaosOutput.load(std::memory_order_relaxed)),      // 17: ChaosOutput
            oscAParams_.particleScatter.load(std::memory_order_relaxed),                      // 18: ParticleScatter
            oscAParams_.particleDensity.load(std::memory_order_relaxed),                      // 19: ParticleDensity
            oscAParams_.particleLifetime.load(std::memory_order_relaxed),                     // 20: ParticleLifetime
            static_cast<float>(oscAParams_.particleSpawnMode.load(std::memory_order_relaxed)),// 21: ParticleSpawnMode
            static_cast<float>(oscAParams_.particleEnvType.load(std::memory_order_relaxed)),  // 22: ParticleEnvType
            oscAParams_.particleDrift.load(std::memory_order_relaxed),                        // 23: ParticleDrift
            static_cast<float>(oscAParams_.formantVowel.load(std::memory_order_relaxed)),     // 24: FormantVowel
            oscAParams_.formantMorph.load(std::memory_order_relaxed),                         // 25: FormantMorph
            oscAParams_.spectralPitch.load(std::memory_order_relaxed),                        // 26: SpectralPitchShift
            oscAParams_.spectralTilt.load(std::memory_order_relaxed),                         // 27: SpectralTilt
            oscAParams_.spectralFormant.load(std::memory_order_relaxed),                      // 28: SpectralFormantShift
            static_cast<float>(oscAParams_.noiseColor.load(std::memory_order_relaxed)),       // 29: NoiseColor
        };
        for (size_t i = 0; i < Ruinae::kOscTypeSpecificParamCount; ++i) {
            engine_.setOscAParam(Ruinae::kParamIdToOscParam[i], oscAValues[i]);
        }
    }

    // --- OSC B Type-Specific Parameters (068-osc-type-params) ---
    {
        using Krate::DSP::OscParam;
        const float oscBValues[] = {
            static_cast<float>(oscBParams_.waveform.load(std::memory_order_relaxed)),
            oscBParams_.pulseWidth.load(std::memory_order_relaxed),
            oscBParams_.phaseMod.load(std::memory_order_relaxed),
            oscBParams_.freqMod.load(std::memory_order_relaxed),
            static_cast<float>(oscBParams_.pdWaveform.load(std::memory_order_relaxed)),
            oscBParams_.pdDistortion.load(std::memory_order_relaxed),
            oscBParams_.syncRatio.load(std::memory_order_relaxed),
            static_cast<float>(oscBParams_.syncWaveform.load(std::memory_order_relaxed)),
            static_cast<float>(oscBParams_.syncMode.load(std::memory_order_relaxed)),
            oscBParams_.syncAmount.load(std::memory_order_relaxed),
            oscBParams_.syncPulseWidth.load(std::memory_order_relaxed),
            static_cast<float>(oscBParams_.additivePartials.load(std::memory_order_relaxed)),
            oscBParams_.additiveTilt.load(std::memory_order_relaxed),
            oscBParams_.additiveInharm.load(std::memory_order_relaxed),
            static_cast<float>(oscBParams_.chaosAttractor.load(std::memory_order_relaxed)),
            oscBParams_.chaosAmount.load(std::memory_order_relaxed),
            oscBParams_.chaosCoupling.load(std::memory_order_relaxed),
            static_cast<float>(oscBParams_.chaosOutput.load(std::memory_order_relaxed)),
            oscBParams_.particleScatter.load(std::memory_order_relaxed),
            oscBParams_.particleDensity.load(std::memory_order_relaxed),
            oscBParams_.particleLifetime.load(std::memory_order_relaxed),
            static_cast<float>(oscBParams_.particleSpawnMode.load(std::memory_order_relaxed)),
            static_cast<float>(oscBParams_.particleEnvType.load(std::memory_order_relaxed)),
            oscBParams_.particleDrift.load(std::memory_order_relaxed),
            static_cast<float>(oscBParams_.formantVowel.load(std::memory_order_relaxed)),
            oscBParams_.formantMorph.load(std::memory_order_relaxed),
            oscBParams_.spectralPitch.load(std::memory_order_relaxed),
            oscBParams_.spectralTilt.load(std::memory_order_relaxed),
            oscBParams_.spectralFormant.load(std::memory_order_relaxed),
            static_cast<float>(oscBParams_.noiseColor.load(std::memory_order_relaxed)),
        };
        for (size_t i = 0; i < Ruinae::kOscTypeSpecificParamCount; ++i) {
            engine_.setOscBParam(Ruinae::kParamIdToOscParam[i], oscBValues[i]);
        }
    }

    // --- Mixer ---
    engine_.setMixMode(static_cast<MixMode>(
        mixerParams_.mode.load(std::memory_order_relaxed)));
    engine_.setMixPosition(mixerParams_.position.load(std::memory_order_relaxed));
    engine_.setMixTilt(mixerParams_.tilt.load(std::memory_order_relaxed));

    // --- Filter ---
    engine_.setFilterType(static_cast<RuinaeFilterType>(
        filterParams_.type.load(std::memory_order_relaxed)));
    engine_.setFilterCutoff(filterParams_.cutoffHz.load(std::memory_order_relaxed));
    engine_.setFilterResonance(filterParams_.resonance.load(std::memory_order_relaxed));
    engine_.setFilterEnvAmount(filterParams_.envAmount.load(std::memory_order_relaxed));
    engine_.setFilterKeyTrack(filterParams_.keyTrack.load(std::memory_order_relaxed));
    engine_.setFilterLadderSlope(filterParams_.ladderSlope.load(std::memory_order_relaxed));
    engine_.setFilterLadderDrive(filterParams_.ladderDrive.load(std::memory_order_relaxed));
    engine_.setFilterFormantMorph(filterParams_.formantMorph.load(std::memory_order_relaxed));
    engine_.setFilterFormantGender(filterParams_.formantGender.load(std::memory_order_relaxed));
    engine_.setFilterCombDamping(filterParams_.combDamping.load(std::memory_order_relaxed));
    engine_.setFilterSvfSlope(filterParams_.svfSlope.load(std::memory_order_relaxed));
    engine_.setFilterSvfDrive(filterParams_.svfDrive.load(std::memory_order_relaxed));
    engine_.setFilterSvfGain(filterParams_.svfGain.load(std::memory_order_relaxed));
    engine_.setFilterEnvSubType(filterParams_.envSubType.load(std::memory_order_relaxed));
    engine_.setFilterEnvSensitivity(filterParams_.envSensitivity.load(std::memory_order_relaxed));
    engine_.setFilterEnvDepth(filterParams_.envDepth.load(std::memory_order_relaxed));
    engine_.setFilterEnvAttack(filterParams_.envAttack.load(std::memory_order_relaxed));
    engine_.setFilterEnvRelease(filterParams_.envRelease.load(std::memory_order_relaxed));
    engine_.setFilterEnvDirection(filterParams_.envDirection.load(std::memory_order_relaxed));
    engine_.setFilterSelfOscGlide(filterParams_.selfOscGlide.load(std::memory_order_relaxed));
    engine_.setFilterSelfOscExtMix(filterParams_.selfOscExtMix.load(std::memory_order_relaxed));
    engine_.setFilterSelfOscShape(filterParams_.selfOscShape.load(std::memory_order_relaxed));
    engine_.setFilterSelfOscRelease(filterParams_.selfOscRelease.load(std::memory_order_relaxed));

    // --- Distortion ---
    engine_.setDistortionType(static_cast<RuinaeDistortionType>(
        distortionParams_.type.load(std::memory_order_relaxed)));
    engine_.setDistortionDrive(distortionParams_.drive.load(std::memory_order_relaxed));
    engine_.setDistortionCharacter(distortionParams_.character.load(std::memory_order_relaxed));
    engine_.setDistortionMix(distortionParams_.mix.load(std::memory_order_relaxed));

    // Distortion type-specific params
    engine_.setDistortionChaosModel(distortionParams_.chaosModel.load(std::memory_order_relaxed));
    engine_.setDistortionChaosSpeed(distortionParams_.chaosSpeed.load(std::memory_order_relaxed));
    engine_.setDistortionChaosCoupling(distortionParams_.chaosCoupling.load(std::memory_order_relaxed));

    engine_.setDistortionSpectralMode(distortionParams_.spectralMode.load(std::memory_order_relaxed));
    engine_.setDistortionSpectralCurve(distortionParams_.spectralCurve.load(std::memory_order_relaxed));
    engine_.setDistortionSpectralBits(distortionParams_.spectralBits.load(std::memory_order_relaxed));

    engine_.setDistortionGrainSize(distortionParams_.grainSize.load(std::memory_order_relaxed));
    engine_.setDistortionGrainDensity(distortionParams_.grainDensity.load(std::memory_order_relaxed));
    engine_.setDistortionGrainVariation(distortionParams_.grainVariation.load(std::memory_order_relaxed));
    engine_.setDistortionGrainJitter(distortionParams_.grainJitter.load(std::memory_order_relaxed));

    engine_.setDistortionFoldType(distortionParams_.foldType.load(std::memory_order_relaxed));

    engine_.setDistortionTapeModel(distortionParams_.tapeModel.load(std::memory_order_relaxed));
    engine_.setDistortionTapeSaturation(distortionParams_.tapeSaturation.load(std::memory_order_relaxed));
    engine_.setDistortionTapeBias(distortionParams_.tapeBias.load(std::memory_order_relaxed));

    // --- Trance Gate ---
    engine_.setTranceGateEnabled(tranceGateParams_.enabled.load(std::memory_order_relaxed));
    {
        TranceGateParams tgp;
        tgp.numSteps = tranceGateParams_.numSteps.load(std::memory_order_relaxed);
        tgp.rateHz = tranceGateParams_.rateHz.load(std::memory_order_relaxed);
        tgp.depth = tranceGateParams_.depth.load(std::memory_order_relaxed);
        tgp.attackMs = tranceGateParams_.attackMs.load(std::memory_order_relaxed);
        tgp.releaseMs = tranceGateParams_.releaseMs.load(std::memory_order_relaxed);
        tgp.phaseOffset = tranceGateParams_.phaseOffset.load(std::memory_order_relaxed);
        tgp.tempoSync = tranceGateParams_.tempoSync.load(std::memory_order_relaxed);
        auto tgNoteMapping = getNoteValueFromDropdown(
            tranceGateParams_.noteValue.load(std::memory_order_relaxed));
        tgp.noteValue = tgNoteMapping.note;
        tgp.noteModifier = tgNoteMapping.modifier;
        engine_.setTranceGateParams(tgp);

        // Apply step levels to DSP engine
        for (int i = 0; i < 32; ++i) {
            engine_.setTranceGateStep(i,
                tranceGateParams_.stepLevels[static_cast<size_t>(i)].load(
                    std::memory_order_relaxed));
        }
    }

    // --- Amp Envelope ---
    engine_.setAmpAttack(ampEnvParams_.attackMs.load(std::memory_order_relaxed));
    engine_.setAmpDecay(ampEnvParams_.decayMs.load(std::memory_order_relaxed));
    engine_.setAmpSustain(ampEnvParams_.sustain.load(std::memory_order_relaxed));
    engine_.setAmpRelease(ampEnvParams_.releaseMs.load(std::memory_order_relaxed));

    // --- Filter Envelope ---
    engine_.setFilterAttack(filterEnvParams_.attackMs.load(std::memory_order_relaxed));
    engine_.setFilterDecay(filterEnvParams_.decayMs.load(std::memory_order_relaxed));
    engine_.setFilterSustain(filterEnvParams_.sustain.load(std::memory_order_relaxed));
    engine_.setFilterRelease(filterEnvParams_.releaseMs.load(std::memory_order_relaxed));
    engine_.setFilterAttackCurve(filterEnvParams_.attackCurve.load(std::memory_order_relaxed));
    engine_.setFilterDecayCurve(filterEnvParams_.decayCurve.load(std::memory_order_relaxed));
    engine_.setFilterReleaseCurve(filterEnvParams_.releaseCurve.load(std::memory_order_relaxed));

    // --- Mod Envelope ---
    engine_.setModAttack(modEnvParams_.attackMs.load(std::memory_order_relaxed));
    engine_.setModDecay(modEnvParams_.decayMs.load(std::memory_order_relaxed));
    engine_.setModSustain(modEnvParams_.sustain.load(std::memory_order_relaxed));
    engine_.setModRelease(modEnvParams_.releaseMs.load(std::memory_order_relaxed));

    // --- LFO 1 ---
    engine_.setGlobalLFO1Rate(lfo1Params_.rateHz.load(std::memory_order_relaxed));
    engine_.setGlobalLFO1Waveform(static_cast<Waveform>(
        lfo1Params_.shape.load(std::memory_order_relaxed)));
    engine_.setGlobalLFO1TempoSync(lfo1Params_.sync.load(std::memory_order_relaxed));
    engine_.setGlobalLFO1PhaseOffset(lfo1Params_.phaseOffset.load(std::memory_order_relaxed));
    engine_.setGlobalLFO1Retrigger(lfo1Params_.retrigger.load(std::memory_order_relaxed));
    {
        auto mapping = getNoteValueFromDropdown(
            lfo1Params_.noteValue.load(std::memory_order_relaxed));
        engine_.setGlobalLFO1NoteValue(mapping.note, mapping.modifier);
    }
    engine_.setGlobalLFO1Unipolar(lfo1Params_.unipolar.load(std::memory_order_relaxed));
    engine_.setGlobalLFO1FadeIn(lfo1Params_.fadeInMs.load(std::memory_order_relaxed));
    engine_.setGlobalLFO1Symmetry(lfo1Params_.symmetry.load(std::memory_order_relaxed));
    engine_.setGlobalLFO1Quantize(lfo1Params_.quantizeSteps.load(std::memory_order_relaxed));

    // --- LFO 2 ---
    engine_.setGlobalLFO2Rate(lfo2Params_.rateHz.load(std::memory_order_relaxed));
    engine_.setGlobalLFO2Waveform(static_cast<Waveform>(
        lfo2Params_.shape.load(std::memory_order_relaxed)));
    engine_.setGlobalLFO2TempoSync(lfo2Params_.sync.load(std::memory_order_relaxed));
    engine_.setGlobalLFO2PhaseOffset(lfo2Params_.phaseOffset.load(std::memory_order_relaxed));
    engine_.setGlobalLFO2Retrigger(lfo2Params_.retrigger.load(std::memory_order_relaxed));
    {
        auto mapping = getNoteValueFromDropdown(
            lfo2Params_.noteValue.load(std::memory_order_relaxed));
        engine_.setGlobalLFO2NoteValue(mapping.note, mapping.modifier);
    }
    engine_.setGlobalLFO2Unipolar(lfo2Params_.unipolar.load(std::memory_order_relaxed));
    engine_.setGlobalLFO2FadeIn(lfo2Params_.fadeInMs.load(std::memory_order_relaxed));
    engine_.setGlobalLFO2Symmetry(lfo2Params_.symmetry.load(std::memory_order_relaxed));
    engine_.setGlobalLFO2Quantize(lfo2Params_.quantizeSteps.load(std::memory_order_relaxed));

    // --- Chaos Mod ---
    engine_.setChaosSpeed(chaosModParams_.rateHz.load(std::memory_order_relaxed));
    engine_.setChaosModel(static_cast<ChaosModel>(
        chaosModParams_.type.load(std::memory_order_relaxed)));
    engine_.setChaosTempoSync(chaosModParams_.sync.load(std::memory_order_relaxed));
    {
        auto mapping = getNoteValueFromDropdown(
            chaosModParams_.noteValue.load(std::memory_order_relaxed));
        engine_.setChaosNoteValue(mapping.note, mapping.modifier);
    }

    // --- Mod Matrix (8 slots) ---
    static constexpr float kScaleMultipliers[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f};
    for (int i = 0; i < 8; ++i) {
        const auto& slot = modMatrixParams_.slots[static_cast<size_t>(i)];
        int srcInt = slot.source.load(std::memory_order_relaxed);
        int dstInt = slot.dest.load(std::memory_order_relaxed);
        auto src = static_cast<ModSource>(srcInt);
        auto dst = modDestFromIndex(dstInt);
        float amt = slot.amount.load(std::memory_order_relaxed);
        int curveIdx = std::clamp(slot.curve.load(std::memory_order_relaxed), 0, 3);
        int scaleIdx = std::clamp(slot.scale.load(std::memory_order_relaxed), 0, 4);
        bool bypass = slot.bypass.load(std::memory_order_relaxed) != 0;
        float smoothMs = slot.smoothMs.load(std::memory_order_relaxed);
        auto curve = static_cast<ModCurve>(curveIdx);
        float scaleMul = kScaleMultipliers[scaleIdx];
        engine_.setGlobalModRoute(i, src, dst, amt, curve, scaleMul, bypass, smoothMs);

    }

    // --- Global Filter ---
    engine_.setGlobalFilterEnabled(globalFilterParams_.enabled.load(std::memory_order_relaxed));
    {
        int typeIdx = globalFilterParams_.type.load(std::memory_order_relaxed);
        // Map 0-3 to LP, HP, BP, Notch
        SVFMode modes[] = { SVFMode::Lowpass, SVFMode::Highpass,
                            SVFMode::Bandpass, SVFMode::Notch };
        engine_.setGlobalFilterType(modes[std::clamp(typeIdx, 0, 3)]);
    }
    engine_.setGlobalFilterCutoff(globalFilterParams_.cutoffHz.load(std::memory_order_relaxed));
    engine_.setGlobalFilterResonance(globalFilterParams_.resonance.load(std::memory_order_relaxed));

    // --- FX Enable ---
    engine_.setDelayEnabled(delayEnabled_.load(std::memory_order_relaxed));
    engine_.setReverbEnabled(reverbEnabled_.load(std::memory_order_relaxed));
    engine_.setPhaserEnabled(phaserEnabled_.load(std::memory_order_relaxed));

    // --- Delay ---
    engine_.setDelayType(static_cast<RuinaeDelayType>(
        delayParams_.type.load(std::memory_order_relaxed)));
    if (delayParams_.sync.load(std::memory_order_relaxed)) {
        engine_.setDelayTime(dropdownToDelayMs(
            delayParams_.noteValue.load(std::memory_order_relaxed), tempoBPM_));
    } else {
        engine_.setDelayTime(delayParams_.timeMs.load(std::memory_order_relaxed));
    }
    engine_.setDelayFeedback(delayParams_.feedback.load(std::memory_order_relaxed));
    engine_.setDelayMix(delayParams_.mix.load(std::memory_order_relaxed));

    // --- Delay type-specific ---
    // Digital
    engine_.setDelayDigitalEra(delayParams_.digitalEra.load(std::memory_order_relaxed));
    engine_.setDelayDigitalAge(delayParams_.digitalAge.load(std::memory_order_relaxed));
    engine_.setDelayDigitalLimiter(delayParams_.digitalLimiter.load(std::memory_order_relaxed));
    engine_.setDelayDigitalModDepth(delayParams_.digitalModDepth.load(std::memory_order_relaxed));
    engine_.setDelayDigitalModRate(delayParams_.digitalModRateHz.load(std::memory_order_relaxed));
    engine_.setDelayDigitalModWaveform(delayParams_.digitalModWaveform.load(std::memory_order_relaxed));
    engine_.setDelayDigitalWidth(delayParams_.digitalWidth.load(std::memory_order_relaxed));
    engine_.setDelayDigitalWavefoldAmount(delayParams_.digitalWavefoldAmt.load(std::memory_order_relaxed));
    engine_.setDelayDigitalWavefoldModel(delayParams_.digitalWavefoldModel.load(std::memory_order_relaxed));
    engine_.setDelayDigitalWavefoldSymmetry(delayParams_.digitalWavefoldSym.load(std::memory_order_relaxed));
    // Tape
    engine_.setDelayTapeMotorInertia(delayParams_.tapeInertiaMs.load(std::memory_order_relaxed));
    engine_.setDelayTapeWear(delayParams_.tapeWear.load(std::memory_order_relaxed));
    engine_.setDelayTapeSaturation(delayParams_.tapeSaturation.load(std::memory_order_relaxed));
    engine_.setDelayTapeAge(delayParams_.tapeAge.load(std::memory_order_relaxed));
    engine_.setDelayTapeSpliceEnabled(delayParams_.tapeSpliceEnabled.load(std::memory_order_relaxed));
    engine_.setDelayTapeSpliceIntensity(delayParams_.tapeSpliceIntensity.load(std::memory_order_relaxed));
    engine_.setDelayTapeHeadEnabled(0, delayParams_.tapeHead1Enabled.load(std::memory_order_relaxed));
    engine_.setDelayTapeHeadLevel(0, delayParams_.tapeHead1Level.load(std::memory_order_relaxed));
    engine_.setDelayTapeHeadPan(0, delayParams_.tapeHead1Pan.load(std::memory_order_relaxed));
    engine_.setDelayTapeHeadEnabled(1, delayParams_.tapeHead2Enabled.load(std::memory_order_relaxed));
    engine_.setDelayTapeHeadLevel(1, delayParams_.tapeHead2Level.load(std::memory_order_relaxed));
    engine_.setDelayTapeHeadPan(1, delayParams_.tapeHead2Pan.load(std::memory_order_relaxed));
    engine_.setDelayTapeHeadEnabled(2, delayParams_.tapeHead3Enabled.load(std::memory_order_relaxed));
    engine_.setDelayTapeHeadLevel(2, delayParams_.tapeHead3Level.load(std::memory_order_relaxed));
    engine_.setDelayTapeHeadPan(2, delayParams_.tapeHead3Pan.load(std::memory_order_relaxed));
    // Granular
    engine_.setDelayGranularSize(delayParams_.granularSizeMs.load(std::memory_order_relaxed));
    engine_.setDelayGranularDensity(delayParams_.granularDensity.load(std::memory_order_relaxed));
    engine_.setDelayGranularPitch(delayParams_.granularPitch.load(std::memory_order_relaxed));
    engine_.setDelayGranularPitchSpray(delayParams_.granularPitchSpray.load(std::memory_order_relaxed));
    engine_.setDelayGranularPitchQuant(delayParams_.granularPitchQuant.load(std::memory_order_relaxed));
    engine_.setDelayGranularPositionSpray(delayParams_.granularPosSpray.load(std::memory_order_relaxed));
    engine_.setDelayGranularReverseProb(delayParams_.granularReverseProb.load(std::memory_order_relaxed));
    engine_.setDelayGranularPanSpray(delayParams_.granularPanSpray.load(std::memory_order_relaxed));
    engine_.setDelayGranularJitter(delayParams_.granularJitter.load(std::memory_order_relaxed));
    engine_.setDelayGranularTexture(delayParams_.granularTexture.load(std::memory_order_relaxed));
    engine_.setDelayGranularWidth(delayParams_.granularWidth.load(std::memory_order_relaxed));
    engine_.setDelayGranularEnvelope(delayParams_.granularEnvelope.load(std::memory_order_relaxed));
    engine_.setDelayGranularFreeze(delayParams_.granularFreeze.load(std::memory_order_relaxed));
    // Spectral
    engine_.setDelaySpectralFFTSize(delayParams_.spectralFFTSize.load(std::memory_order_relaxed));
    engine_.setDelaySpectralSpread(delayParams_.spectralSpreadMs.load(std::memory_order_relaxed));
    engine_.setDelaySpectralDirection(delayParams_.spectralDirection.load(std::memory_order_relaxed));
    engine_.setDelaySpectralCurve(delayParams_.spectralCurve.load(std::memory_order_relaxed));
    engine_.setDelaySpectralTilt(delayParams_.spectralTilt.load(std::memory_order_relaxed));
    engine_.setDelaySpectralDiffusion(delayParams_.spectralDiffusion.load(std::memory_order_relaxed));
    engine_.setDelaySpectralWidth(delayParams_.spectralWidth.load(std::memory_order_relaxed));
    engine_.setDelaySpectralFreeze(delayParams_.spectralFreeze.load(std::memory_order_relaxed));
    // PingPong
    engine_.setDelayPingPongRatio(delayParams_.pingPongRatio.load(std::memory_order_relaxed));
    engine_.setDelayPingPongCrossFeed(delayParams_.pingPongCrossFeed.load(std::memory_order_relaxed));
    engine_.setDelayPingPongWidth(delayParams_.pingPongWidth.load(std::memory_order_relaxed));
    engine_.setDelayPingPongModDepth(delayParams_.pingPongModDepth.load(std::memory_order_relaxed));
    engine_.setDelayPingPongModRate(delayParams_.pingPongModRateHz.load(std::memory_order_relaxed));

    // --- Reverb ---
    {
        ReverbParams rp;
        rp.roomSize = reverbParams_.size.load(std::memory_order_relaxed);
        rp.damping = reverbParams_.damping.load(std::memory_order_relaxed);
        rp.width = reverbParams_.width.load(std::memory_order_relaxed);
        rp.mix = reverbParams_.mix.load(std::memory_order_relaxed);
        rp.preDelayMs = reverbParams_.preDelayMs.load(std::memory_order_relaxed);
        rp.diffusion = reverbParams_.diffusion.load(std::memory_order_relaxed);
        rp.freeze = reverbParams_.freeze.load(std::memory_order_relaxed);
        rp.modRate = reverbParams_.modRateHz.load(std::memory_order_relaxed);
        rp.modDepth = reverbParams_.modDepth.load(std::memory_order_relaxed);
        engine_.setReverbParams(rp);
    }

    // --- Phaser ---
    engine_.setPhaserRate(phaserParams_.rateHz.load(std::memory_order_relaxed));
    engine_.setPhaserDepth(phaserParams_.depth.load(std::memory_order_relaxed));
    engine_.setPhaserFeedback(phaserParams_.feedback.load(std::memory_order_relaxed));
    engine_.setPhaserMix(phaserParams_.mix.load(std::memory_order_relaxed));
    engine_.setPhaserStages(phaserStagesFromIndex(
        phaserParams_.stages.load(std::memory_order_relaxed)));
    engine_.setPhaserCenterFrequency(phaserParams_.centerFreqHz.load(std::memory_order_relaxed));
    engine_.setPhaserStereoSpread(phaserParams_.stereoSpread.load(std::memory_order_relaxed));
    engine_.setPhaserWaveform(phaserParams_.waveform.load(std::memory_order_relaxed));
    engine_.setPhaserTempoSync(phaserParams_.sync.load(std::memory_order_relaxed));
    {
        auto mapping = getNoteValueFromDropdown(
            phaserParams_.noteValue.load(std::memory_order_relaxed));
        engine_.setPhaserNoteValue(mapping.note, mapping.modifier);
    }

    // --- Harmonizer ---
    engine_.setHarmonizerEnabled(harmonizerEnabled_.load(std::memory_order_relaxed));
    engine_.setHarmonizerHarmonyMode(harmonizerParams_.harmonyMode.load(std::memory_order_relaxed));
    engine_.setHarmonizerKey(harmonizerParams_.key.load(std::memory_order_relaxed));
    engine_.setHarmonizerScale(harmonizerParams_.scale.load(std::memory_order_relaxed));
    engine_.setHarmonizerPitchShiftMode(harmonizerParams_.pitchShiftMode.load(std::memory_order_relaxed));
    engine_.setHarmonizerFormantPreserve(harmonizerParams_.formantPreserve.load(std::memory_order_relaxed));
    engine_.setHarmonizerNumVoices(harmonizerParams_.numVoices.load(std::memory_order_relaxed));
    engine_.setHarmonizerDryLevel(harmonizerParams_.dryLevelDb.load(std::memory_order_relaxed));
    engine_.setHarmonizerWetLevel(harmonizerParams_.wetLevelDb.load(std::memory_order_relaxed));
    for (int v = 0; v < 4; ++v) {
        auto vi = static_cast<size_t>(v);
        engine_.setHarmonizerVoiceInterval(v,
            harmonizerParams_.voiceInterval[vi].load(std::memory_order_relaxed));
        engine_.setHarmonizerVoiceLevel(v,
            harmonizerParams_.voiceLevelDb[vi].load(std::memory_order_relaxed));
        engine_.setHarmonizerVoicePan(v,
            harmonizerParams_.voicePan[vi].load(std::memory_order_relaxed));
        engine_.setHarmonizerVoiceDelay(v,
            harmonizerParams_.voiceDelayMs[vi].load(std::memory_order_relaxed));
        engine_.setHarmonizerVoiceDetune(v,
            harmonizerParams_.voiceDetuneCents[vi].load(std::memory_order_relaxed));
    }

    // --- Macros ---
    for (int i = 0; i < 4; ++i) {
        engine_.setMacroValue(static_cast<size_t>(i),
            macroParams_.values[i].load(std::memory_order_relaxed));
    }

    // --- Rungler ---
    engine_.setRunglerOsc1Freq(runglerParams_.osc1FreqHz.load(std::memory_order_relaxed));
    engine_.setRunglerOsc2Freq(runglerParams_.osc2FreqHz.load(std::memory_order_relaxed));
    engine_.setRunglerDepth(runglerParams_.depth.load(std::memory_order_relaxed));
    engine_.setRunglerFilter(runglerParams_.filter.load(std::memory_order_relaxed));
    engine_.setRunglerBits(static_cast<size_t>(runglerParams_.bits.load(std::memory_order_relaxed)));
    engine_.setRunglerLoopMode(runglerParams_.loopMode.load(std::memory_order_relaxed));

    // --- Settings ---
    engine_.setPitchBendRange(settingsParams_.pitchBendRangeSemitones.load(std::memory_order_relaxed));
    engine_.setVelocityCurve(static_cast<Krate::DSP::VelocityCurve>(
        settingsParams_.velocityCurve.load(std::memory_order_relaxed)));
    engine_.setTuningReference(settingsParams_.tuningReferenceHz.load(std::memory_order_relaxed));
    engine_.setAllocationMode(static_cast<Krate::DSP::AllocationMode>(
        settingsParams_.voiceAllocMode.load(std::memory_order_relaxed)));
    engine_.setStealMode(static_cast<Krate::DSP::StealMode>(
        settingsParams_.voiceStealMode.load(std::memory_order_relaxed)));
    engine_.setGainCompensationEnabled(settingsParams_.gainCompensation.load(std::memory_order_relaxed));

    // --- Mono Mode ---
    engine_.setMonoPriority(static_cast<MonoMode>(
        monoModeParams_.priority.load(std::memory_order_relaxed)));
    engine_.setLegato(monoModeParams_.legato.load(std::memory_order_relaxed));
    engine_.setPortamentoTime(monoModeParams_.portamentoTimeMs.load(std::memory_order_relaxed));
    engine_.setPortamentoMode(static_cast<PortaMode>(
        monoModeParams_.portaMode.load(std::memory_order_relaxed)));

    // --- Env Follower ---
    engine_.setEnvFollowerSensitivity(envFollowerParams_.sensitivity.load(std::memory_order_relaxed));
    engine_.setEnvFollowerAttack(envFollowerParams_.attackMs.load(std::memory_order_relaxed));
    engine_.setEnvFollowerRelease(envFollowerParams_.releaseMs.load(std::memory_order_relaxed));

    // --- Sample & Hold ---
    if (sampleHoldParams_.sync.load(std::memory_order_relaxed)) {
        // When synced, convert NoteValue + tempo to rate in Hz
        int noteIdx = sampleHoldParams_.noteValue.load(std::memory_order_relaxed);
        float delayMs = Krate::DSP::dropdownToDelayMs(noteIdx, static_cast<float>(tempoBPM_));
        // Fallback to 4 Hz if tempo invalid or delayMs <= 0
        float rateHz = (delayMs > 0.0f) ? (1000.0f / delayMs) : 4.0f;
        engine_.setSampleHoldRate(rateHz);
    } else {
        // When not synced, use Rate knob value (already clamped in handleParamChange)
        engine_.setSampleHoldRate(sampleHoldParams_.rateHz.load(std::memory_order_relaxed));
    }
    engine_.setSampleHoldSlew(sampleHoldParams_.slewMs.load(std::memory_order_relaxed));

    // --- Random ---
    // Note: RandomSource built-in tempo sync is NOT used. Sync is handled at processor level
    // via NoteValue-to-Hz conversion (same pattern as S&H) for consistent UX across all sources.
    if (randomParams_.sync.load(std::memory_order_relaxed)) {
        // When synced, convert NoteValue + tempo to rate in Hz
        int noteIdx = randomParams_.noteValue.load(std::memory_order_relaxed);
        float delayMs = Krate::DSP::dropdownToDelayMs(noteIdx, static_cast<float>(tempoBPM_));
        // Fallback to 4 Hz if tempo invalid or delayMs <= 0
        float rateHz = (delayMs > 0.0f) ? (1000.0f / delayMs) : 4.0f;
        engine_.setRandomRate(rateHz);
    } else {
        // When not synced, use Rate knob value (already clamped in handleParamChange)
        engine_.setRandomRate(randomParams_.rateHz.load(std::memory_order_relaxed));
    }
    engine_.setRandomSmoothness(randomParams_.smoothness.load(std::memory_order_relaxed));

    // --- Pitch Follower ---
    engine_.setPitchFollowerMinHz(pitchFollowerParams_.minHz.load(std::memory_order_relaxed));
    engine_.setPitchFollowerMaxHz(pitchFollowerParams_.maxHz.load(std::memory_order_relaxed));
    engine_.setPitchFollowerConfidence(pitchFollowerParams_.confidence.load(std::memory_order_relaxed));
    engine_.setPitchFollowerTrackingSpeed(pitchFollowerParams_.speedMs.load(std::memory_order_relaxed));

    // --- Transient ---
    engine_.setTransientSensitivity(transientParams_.sensitivity.load(std::memory_order_relaxed));
    engine_.setTransientAttack(transientParams_.attackMs.load(std::memory_order_relaxed));
    engine_.setTransientDecay(transientParams_.decayMs.load(std::memory_order_relaxed));

    // --- Arpeggiator (FR-009) ---
    // IMPORTANT: Only call setters when the value actually changes.
    // Several ArpeggiatorCore setters (setMode, setRetrigger) reset internal
    // state (step index, swing counter). Calling them unconditionally every
    // block would prevent the arp from ever advancing past step 0.
    {
        const auto modeInt = arpParams_.mode.load(std::memory_order_relaxed);
        const auto mode = static_cast<ArpMode>(modeInt);
        if (mode != prevArpMode_) {
            arpCore_.setMode(mode);
            prevArpMode_ = mode;
        }
    }
    {
        const auto octaveMode = static_cast<OctaveMode>(
            arpParams_.octaveMode.load(std::memory_order_relaxed));
        if (octaveMode != prevArpOctaveMode_) {
            arpCore_.setOctaveMode(octaveMode);
            prevArpOctaveMode_ = octaveMode;
        }
    }
    {
        const auto noteValue = arpParams_.noteValue.load(std::memory_order_relaxed);
        if (noteValue != prevArpNoteValue_) {
            auto mapping = getNoteValueFromDropdown(noteValue);
            arpCore_.setNoteValue(mapping.note, mapping.modifier);
            prevArpNoteValue_ = noteValue;
        }
    }

    // --- Arp Modulation (078-modulation-integration) ---
    // Read mod offsets and apply to arp parameters when arp is enabled (FR-015).
    // When disabled, skip mod reads for performance optimization.
    if (arpParams_.enabled.load(std::memory_order_relaxed)) {
        const float rateOffset = engine_.getGlobalModOffset(
            RuinaeModDest::ArpRate);
        const float gateOffset = engine_.getGlobalModOffset(
            RuinaeModDest::ArpGateLength);
        const float octaveOffset = engine_.getGlobalModOffset(
            RuinaeModDest::ArpOctaveRange);
        const float swingOffset = engine_.getGlobalModOffset(
            RuinaeModDest::ArpSwing);
        const float spiceOffset = engine_.getGlobalModOffset(
            RuinaeModDest::ArpSpice);

        // --- Rate modulation (FR-008, FR-014) ---
        const bool tempoSync = arpParams_.tempoSync.load(std::memory_order_relaxed);
        const float baseRate = arpParams_.freeRate.load(std::memory_order_relaxed);

        if (rateOffset != 0.0f && tempoSync) {
            // Tempo-sync override: compute equivalent free rate from modulated duration
            const int noteIdx = arpParams_.noteValue.load(std::memory_order_relaxed);
            const float baseDurationMs = Krate::DSP::dropdownToDelayMs(
                noteIdx, static_cast<float>(tempoBPM_));
            if (baseDurationMs > 0.0f) {
                const float scaleFactor = 1.0f + 0.5f * rateOffset;
                const float effectiveDurationMs = (scaleFactor > 0.001f)
                    ? baseDurationMs / scaleFactor
                    : baseDurationMs / 0.001f;
                const float effectiveHz = 1000.0f / effectiveDurationMs;
                arpCore_.setTempoSync(false);
                arpCore_.setFreeRate(std::clamp(effectiveHz, 0.5f, 50.0f));
            } else {
                arpCore_.setTempoSync(true);
                arpCore_.setFreeRate(baseRate);
            }
        } else {
            // Free-rate mode or zero offset in tempo-sync (no override needed)
            arpCore_.setTempoSync(tempoSync);
            const float effectiveRate = std::clamp(
                baseRate * (1.0f + 0.5f * rateOffset), 0.5f, 50.0f);
            arpCore_.setFreeRate(effectiveRate);
        }

        // --- Gate length modulation (FR-009) ---
        {
            const float baseGate = arpParams_.gateLength.load(std::memory_order_relaxed);
            const float effectiveGate = std::clamp(
                baseGate + 100.0f * gateOffset, 1.0f, 200.0f);
            arpCore_.setGateLength(effectiveGate);
        }

        // --- Octave range modulation (FR-010, 078-modulation-integration) ---
        // Integer destination: rounded to nearest integer, +/-3 octaves, clamped [1, 4].
        // prevArpOctaveRange_ tracks the EFFECTIVE (modulated) value, not the raw base.
        {
            const int baseOctave = arpParams_.octaveRange.load(std::memory_order_relaxed);
            const int effectiveOctave = std::clamp(
                baseOctave + static_cast<int>(std::round(3.0f * octaveOffset)),
                1, 4);
            if (effectiveOctave != prevArpOctaveRange_) {
                arpCore_.setOctaveRange(effectiveOctave);
                prevArpOctaveRange_ = effectiveOctave;
            }
        }

        // --- Swing modulation (FR-011, 078-modulation-integration) ---
        // Additive +/-50 points, clamped [0, 75]%.
        // setSwing() takes 0-75 percent as-is, NOT normalized 0-1
        {
            const float baseSwing = arpParams_.swing.load(std::memory_order_relaxed);
            const float effectiveSwing = std::clamp(
                baseSwing + 50.0f * swingOffset, 0.0f, 75.0f);
            arpCore_.setSwing(effectiveSwing);
        }

        // --- Spice modulation (FR-012, 078-modulation-integration) ---
        // Bipolar additive: effectiveSpice = baseSpice + offset, clamped [0, 1]
        {
            const float baseSpice = arpParams_.spice.load(std::memory_order_relaxed);
            const float effectiveSpice = std::clamp(
                baseSpice + spiceOffset, 0.0f, 1.0f);
            arpCore_.setSpice(effectiveSpice);
        }
    } else {
        // Arp disabled: use raw params, no mod reads (FR-015)
        arpCore_.setTempoSync(arpParams_.tempoSync.load(std::memory_order_relaxed));
        arpCore_.setFreeRate(arpParams_.freeRate.load(std::memory_order_relaxed));
        arpCore_.setGateLength(arpParams_.gateLength.load(std::memory_order_relaxed));
        {
            const auto octaveRange = arpParams_.octaveRange.load(std::memory_order_relaxed);
            if (octaveRange != prevArpOctaveRange_) {
                arpCore_.setOctaveRange(octaveRange);
                prevArpOctaveRange_ = octaveRange;
            }
        }
        arpCore_.setSwing(arpParams_.swing.load(std::memory_order_relaxed));
        arpCore_.setSpice(arpParams_.spice.load(std::memory_order_relaxed));
    }
    {
        const auto latchMode = static_cast<LatchMode>(
            arpParams_.latchMode.load(std::memory_order_relaxed));
        if (latchMode != prevArpLatchMode_) {
            arpCore_.setLatchMode(latchMode);
            prevArpLatchMode_ = latchMode;
        }
    }
    {
        const auto retrigger = static_cast<ArpRetriggerMode>(
            arpParams_.retrigger.load(std::memory_order_relaxed));
        if (retrigger != prevArpRetrigger_) {
            arpCore_.setRetrigger(retrigger);
            prevArpRetrigger_ = retrigger;
        }
    }
    // --- Velocity Lane (072-independent-lanes, US1) ---
    // Expand to max length before writing steps to prevent index clamping,
    // then set the actual length afterward.
    {
        const auto velLen = arpParams_.velocityLaneLength.load(std::memory_order_relaxed);
        arpCore_.velocityLane().setLength(32);
        for (int i = 0; i < 32; ++i) {
            arpCore_.velocityLane().setStep(
                static_cast<size_t>(i),
                arpParams_.velocityLaneSteps[i].load(std::memory_order_relaxed));
        }
        arpCore_.velocityLane().setLength(static_cast<size_t>(velLen));
    }
    // --- Gate Lane (072-independent-lanes, US2) ---
    {
        const auto gateLen = arpParams_.gateLaneLength.load(std::memory_order_relaxed);
        arpCore_.gateLane().setLength(32);
        for (int i = 0; i < 32; ++i) {
            arpCore_.gateLane().setStep(
                static_cast<size_t>(i),
                arpParams_.gateLaneSteps[i].load(std::memory_order_relaxed));
        }
        arpCore_.gateLane().setLength(static_cast<size_t>(gateLen));
    }
    // --- Pitch Lane (072-independent-lanes, US3) ---
    {
        const auto pitchLen = arpParams_.pitchLaneLength.load(std::memory_order_relaxed);
        arpCore_.pitchLane().setLength(32);
        for (int i = 0; i < 32; ++i) {
            int val = std::clamp(
                arpParams_.pitchLaneSteps[i].load(std::memory_order_relaxed), -24, 24);
            arpCore_.pitchLane().setStep(
                static_cast<size_t>(i), static_cast<int8_t>(val));
        }
        arpCore_.pitchLane().setLength(static_cast<size_t>(pitchLen));
    }
    // --- Modifier Lane (073-per-step-mods) ---
    {
        const auto modLen = arpParams_.modifierLaneLength.load(std::memory_order_relaxed);
        arpCore_.modifierLane().setLength(32);  // Expand first (FR-031)
        for (int i = 0; i < 32; ++i) {
            arpCore_.modifierLane().setStep(
                static_cast<size_t>(i),
                static_cast<uint8_t>(arpParams_.modifierLaneSteps[i].load(
                    std::memory_order_relaxed)));
        }
        arpCore_.modifierLane().setLength(static_cast<size_t>(modLen));  // Shrink to actual
    }
    arpCore_.setAccentVelocity(arpParams_.accentVelocity.load(std::memory_order_relaxed));
    arpCore_.setSlideTime(arpParams_.slideTime.load(std::memory_order_relaxed));
    // Forward slide time to engine for both Poly and Mono portamento (FR-034)
    engine_.setPortamentoTime(arpParams_.slideTime.load(std::memory_order_relaxed));
    // --- Ratchet Lane (074-ratcheting, FR-035) ---
    {
        const auto ratchetLen = arpParams_.ratchetLaneLength.load(std::memory_order_relaxed);
        arpCore_.ratchetLane().setLength(32);  // Expand first
        for (int i = 0; i < 32; ++i) {
            int val = std::clamp(
                arpParams_.ratchetLaneSteps[i].load(std::memory_order_relaxed), 1, 4);
            arpCore_.ratchetLane().setStep(
                static_cast<size_t>(i), static_cast<uint8_t>(val));
        }
        arpCore_.ratchetLane().setLength(static_cast<size_t>(ratchetLen));  // Shrink to actual
    }

    // --- Euclidean Timing (075-euclidean-timing) ---
    // Prescribed call order: steps -> hits -> rotation -> enabled (FR-032)
    arpCore_.setEuclideanSteps(
        arpParams_.euclideanSteps.load(std::memory_order_relaxed));
    arpCore_.setEuclideanHits(
        arpParams_.euclideanHits.load(std::memory_order_relaxed));
    arpCore_.setEuclideanRotation(
        arpParams_.euclideanRotation.load(std::memory_order_relaxed));
    arpCore_.setEuclideanEnabled(
        arpParams_.euclideanEnabled.load(std::memory_order_relaxed));

    // --- Condition Lane (076-conditional-trigs) ---
    {
        const auto condLen = arpParams_.conditionLaneLength.load(std::memory_order_relaxed);
        arpCore_.conditionLane().setLength(32);  // Expand first
        for (int i = 0; i < 32; ++i) {
            int val = std::clamp(
                arpParams_.conditionLaneSteps[i].load(std::memory_order_relaxed), 0, 17);
            arpCore_.conditionLane().setStep(
                static_cast<size_t>(i), static_cast<uint8_t>(val));
        }
        arpCore_.conditionLane().setLength(static_cast<size_t>(condLen));  // Shrink to actual
    }
    arpCore_.setFillActive(arpParams_.fillToggle.load(std::memory_order_relaxed));

    // --- Dice & Humanize (077-spice-dice-humanize) ---
    // NOTE: setSpice() moved into arp-enabled mod block above (078-modulation-integration)
    // Dice trigger: consume rising edge via compare_exchange_strong (FR-036)
    {
        bool expected = true;
        if (arpParams_.diceTrigger.compare_exchange_strong(
                expected, false, std::memory_order_relaxed)) {
            arpCore_.triggerDice();
        }
    }
    arpCore_.setHumanize(arpParams_.humanize.load(std::memory_order_relaxed));

    // --- Ratchet Swing (078-ratchet-swing) ---
    arpCore_.setRatchetSwing(arpParams_.ratchetSwing.load(std::memory_order_relaxed));

    // FR-017: setEnabled() LAST -- cleanup note-offs depend on all other params
    arpCore_.setEnabled(arpParams_.enabled.load(std::memory_order_relaxed));
}

// ==============================================================================
// MIDI Event Handling
// ==============================================================================

void Processor::processEvents(Steinberg::Vst::IEventList* events) {
    if (!events) {
        return;
    }

    const Steinberg::int32 numEvents = events->getEventCount();
    const bool arpEnabled = arpParams_.enabled.load(std::memory_order_relaxed);

    for (Steinberg::int32 i = 0; i < numEvents; ++i) {
        Steinberg::Vst::Event event{};
        if (events->getEvent(i, event) != Steinberg::kResultTrue) {
            continue;
        }

        switch (event.type) {
            case Steinberg::Vst::Event::kNoteOnEvent: {
                // Velocity-0 noteOn is treated as noteOff per MIDI convention
                auto pitch = static_cast<uint8_t>(event.noteOn.pitch);
                auto velocity = static_cast<uint8_t>(
                    event.noteOn.velocity * 127.0f + 0.5f);
                if (velocity == 0) {
                    // FR-006: velocity-0 note-on = note-off, respects arp branch
                    if (arpEnabled) {
                        arpCore_.noteOff(pitch);
                    } else {
                        engine_.noteOff(pitch);
                    }
                } else {
                    // FR-006: route note-on based on arp enabled state
                    if (arpEnabled) {
                        arpCore_.noteOn(pitch, velocity);
                    } else {
                        engine_.noteOn(pitch, velocity);
                    }
                }
                break;
            }

            case Steinberg::Vst::Event::kNoteOffEvent: {
                // FR-006: route note-off based on arp enabled state
                auto pitch = static_cast<uint8_t>(event.noteOff.pitch);
                if (arpEnabled) {
                    arpCore_.noteOff(pitch);
                } else {
                    engine_.noteOff(pitch);
                }
                break;
            }

            default:
                // Ignore unsupported event types gracefully
                break;
        }
    }
}

// ==============================================================================
// IMessage: Receive Controller Messages (T085)
// ==============================================================================

Steinberg::tresult PLUGIN_API Processor::notify(Steinberg::Vst::IMessage* message) {
    if (!message)
        return Steinberg::kInvalidArgument;

    if (strcmp(message->getMessageID(), "VoiceModRouteUpdate") == 0) {
        auto* attrs = message->getAttributes();
        if (!attrs)
            return Steinberg::kResultFalse;

        Steinberg::int64 slotIndex = 0;
        if (attrs->getInt("slotIndex", slotIndex) != Steinberg::kResultOk)
            return Steinberg::kResultFalse;

        if (slotIndex < 0 || slotIndex >= Krate::Plugins::kMaxVoiceRoutes)
            return Steinberg::kResultFalse;

        auto& route = voiceRoutes_[static_cast<size_t>(slotIndex)];

        Steinberg::int64 val = 0;
        double dval = 0.0;

        if (attrs->getInt("source", val) == Steinberg::kResultOk)
            route.source = static_cast<uint8_t>(std::clamp(val, Steinberg::int64{0}, Steinberg::int64{9}));

        if (attrs->getInt("destination", val) == Steinberg::kResultOk)
            route.destination = static_cast<uint8_t>(std::clamp(val, Steinberg::int64{0}, Steinberg::int64{kModDestCount - 1}));

        if (attrs->getFloat("amount", dval) == Steinberg::kResultOk)
            route.amount = static_cast<float>(std::clamp(dval, -1.0, 1.0));

        if (attrs->getInt("curve", val) == Steinberg::kResultOk)
            route.curve = static_cast<uint8_t>(std::clamp(val, Steinberg::int64{0}, Steinberg::int64{3}));

        if (attrs->getFloat("smoothMs", dval) == Steinberg::kResultOk)
            route.smoothMs = static_cast<float>(std::clamp(dval, 0.0, 100.0));

        if (attrs->getInt("scale", val) == Steinberg::kResultOk)
            route.scale = static_cast<uint8_t>(std::clamp(val, Steinberg::int64{0}, Steinberg::int64{4}));

        if (attrs->getInt("bypass", val) == Steinberg::kResultOk)
            route.bypass = static_cast<uint8_t>(val != 0 ? 1 : 0);

        if (attrs->getInt("active", val) == Steinberg::kResultOk)
            route.active = static_cast<uint8_t>(val != 0 ? 1 : 0);

        // Send authoritative state back to controller (T086)
        sendVoiceModRouteState();

        return Steinberg::kResultOk;
    }

    if (strcmp(message->getMessageID(), "VoiceModRouteRemove") == 0) {
        auto* attrs = message->getAttributes();
        if (!attrs)
            return Steinberg::kResultFalse;

        Steinberg::int64 slotIndex = 0;
        if (attrs->getInt("slotIndex", slotIndex) != Steinberg::kResultOk)
            return Steinberg::kResultFalse;

        if (slotIndex < 0 || slotIndex >= Krate::Plugins::kMaxVoiceRoutes)
            return Steinberg::kResultFalse;

        // Deactivate the slot
        voiceRoutes_[static_cast<size_t>(slotIndex)] = Krate::Plugins::VoiceModRoute{};

        // Send authoritative state back to controller (T086)
        sendVoiceModRouteState();

        return Steinberg::kResultOk;
    }

    // EditorState message: controller tells processor whether editor is open (Phase 11c)
    if (strcmp(message->getMessageID(), "EditorState") == 0) {
        auto* attrs = message->getAttributes();
        if (attrs) {
            Steinberg::int64 open = 0;
            if (attrs->getInt("open", open) == Steinberg::kResultOk) {
                editorOpen_.store(open != 0, std::memory_order_relaxed);
            }
        }
        return Steinberg::kResultOk;
    }

    return AudioEffect::notify(message);
}

// ==============================================================================
// Arp Skip Event Sender (081-interaction-polish, FR-007, FR-008, FR-012)
// ==============================================================================

void Processor::sendSkipEvent(int lane, int step) {
    // FR-012: don't send when editor is closed
    if (!editorOpen_.load(std::memory_order_relaxed))
        return;

    if (lane < 0 || lane >= 6) return;
    if (step < 0 || step >= 32) return;

    auto* msg = skipMessages_[static_cast<size_t>(lane)].get();
    if (!msg) return;

    auto* attrs = msg->getAttributes();
    if (!attrs) return;

    attrs->setInt("lane", static_cast<Steinberg::int64>(lane));
    attrs->setInt("step", static_cast<Steinberg::int64>(step));
    sendMessage(msg);
}

// ==============================================================================
// Voice Route State Sender (T086)
// ==============================================================================

void Processor::sendVoiceModRouteState() {
    auto msg = Steinberg::owned(allocateMessage());
    if (!msg) return;

    msg->setMessageID("VoiceModRouteState");
    auto* attrs = msg->getAttributes();
    if (!attrs) return;

    // Count active routes
    Steinberg::int64 activeCount = 0;
    for (const auto& r : voiceRoutes_) {
        if (r.active != 0) ++activeCount;
    }
    attrs->setInt("routeCount", activeCount);

    // Pack route data as binary blob (14 bytes per route, 16 routes = 224 bytes)
    // Per contract: source(1), dest(1), amount(4), curve(1), smoothMs(4), scale(1),
    //              bypass(1), active(1) = 14 bytes
    static constexpr size_t kBytesPerRoute = 14;
    static constexpr size_t kTotalBytes = kBytesPerRoute * Krate::Plugins::kMaxVoiceRoutes;
    uint8_t buffer[kTotalBytes]{};

    for (int i = 0; i < Krate::Plugins::kMaxVoiceRoutes; ++i) {
        const auto& r = voiceRoutes_[static_cast<size_t>(i)];
        auto* ptr = &buffer[static_cast<size_t>(i) * kBytesPerRoute];

        ptr[0] = r.source;
        ptr[1] = r.destination;
        std::memcpy(&ptr[2], &r.amount, sizeof(float));
        ptr[6] = r.curve;
        std::memcpy(&ptr[7], &r.smoothMs, sizeof(float));
        ptr[11] = r.scale;
        ptr[12] = r.bypass;
        ptr[13] = r.active;
    }

    attrs->setBinary("routeData", buffer, kTotalBytes);
    sendMessage(msg);
}

} // namespace Ruinae
