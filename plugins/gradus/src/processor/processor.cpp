// ==============================================================================
// Gradus Processor Implementation
// ==============================================================================

#include "processor.h"
#include "../plugin_ids.h"

#include <krate/dsp/core/block_context.h>
#include <krate/dsp/core/note_value.h>

#include "midi/midi_event_dispatcher.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace Krate::DSP;

namespace Gradus {

Processor::Processor()
{
    setControllerClass(kControllerUID);
}

tresult PLUGIN_API Processor::initialize(FUnknown* context)
{
    tresult result = AudioEffect::initialize(context);
    if (result != kResultOk)
        return result;

    // Event input: MIDI notes in
    addEventInput(STR16("Event Input"));

    // Event output: arpeggiated MIDI notes out
    addEventOutput(STR16("MIDI Output"));

    // Audio output: stereo (for audition sound)
    addAudioOutput(STR16("Audio Output"), SpeakerArr::kStereo);

    return kResultOk;
}

tresult PLUGIN_API Processor::terminate()
{
    return AudioEffect::terminate();
}

tresult PLUGIN_API Processor::setActive(TBool state)
{
    if (state) {
        arpCore_.reset();
    }
    return AudioEffect::setActive(state);
}

tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& setup)
{
    sampleRate_ = setup.sampleRate;
    maxBlockSize_ = setup.maxSamplesPerBlock;

    arpCore_.prepare(sampleRate_, static_cast<size_t>(maxBlockSize_));
    auditionVoice_.prepare(sampleRate_);

    return AudioEffect::setupProcessing(setup);
}

tresult PLUGIN_API Processor::setBusArrangements(
    SpeakerArrangement* inputs, int32 numIns,
    SpeakerArrangement* outputs, int32 numOuts)
{
    // No audio inputs, one stereo output
    if (numIns != 0 || numOuts != 1)
        return kResultFalse;

    if (outputs[0] != SpeakerArr::kStereo)
        return kResultFalse;

    return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
}

tresult PLUGIN_API Processor::process(ProcessData& data)
{
    // --- 1. Process parameter changes ---
    if (data.inputParameterChanges)
        processParameterChanges(data.inputParameterChanges);

    // --- 2. Process incoming MIDI events → feed to arpCore ---
    if (data.inputEvents) {
        int32 eventCount = data.inputEvents->getEventCount();
        for (int32 i = 0; i < eventCount; ++i) {
            Event e{};
            if (data.inputEvents->getEvent(i, e) == kResultOk) {
                if (e.type == Event::kNoteOnEvent) { // NOLINT(bugprone-branch-clone)
                    arpCore_.noteOn(
                        static_cast<uint8_t>(e.noteOn.pitch),
                        static_cast<uint8_t>(e.noteOn.velocity * 127.0f));
                } else if (e.type == Event::kNoteOffEvent) {
                    arpCore_.noteOff(
                        static_cast<uint8_t>(e.noteOff.pitch));
                }
            }
        }
    }

    // --- 3. Apply params to arp engine ---
    applyParamsToEngine();

    // --- 4. Transport sync ---
    if (data.processContext) {
        const auto& ctx = *data.processContext;
        const bool isPlaying = (ctx.state & ProcessContext::kPlaying) != 0;
        const bool hasTempo = (ctx.state & ProcessContext::kTempoValid) != 0;
        const bool hasMusicalPos = (ctx.state & ProcessContext::kProjectTimeMusicValid) != 0;

        if (hasTempo) {
            hostSupportsTransport_ = true;
        }

        // Detect transport start/loop
        if (hasMusicalPos) {
            if (isPlaying && ctx.projectTimeMusic < prevProjectTimeMusic_) {
                arpCore_.notifyTransportLoop();
            }
            if (isPlaying) {
                arpCore_.syncToMusicalPosition(ctx.projectTimeMusic);
            }
            prevProjectTimeMusic_ = ctx.projectTimeMusic;
        }

        if (isPlaying && !wasTransportPlaying_) {
            arpCore_.reset();
        }
        wasTransportPlaying_ = isPlaying;
    }

    // --- 5. Process arp block ---
    BlockContext blockCtx{};
    blockCtx.sampleRate = sampleRate_;
    blockCtx.blockSize = static_cast<size_t>(data.numSamples);
    if (data.processContext) {
        const bool transportPlaying =
            (data.processContext->state & ProcessContext::kPlaying) != 0;
        blockCtx.tempoBPM = data.processContext->tempo;
        // In Gradus, the arp is always "playing" — when transport is stopped,
        // the arp still clocks itself (free-rate mode works without transport,
        // tempo-sync uses host tempo but doesn't require transport running).
        blockCtx.isPlaying = true;
        // Only sync to musical position when transport is actually playing
        if (!transportPlaying) {
            blockCtx.tempoBPM = data.processContext->tempo > 0
                ? data.processContext->tempo : 120.0;
        }
    } else {
        blockCtx.tempoBPM = 120.0;
        blockCtx.isPlaying = true;
    }

    size_t numArpEvents = arpCore_.processBlock(
        blockCtx, std::span<ArpEvent>(arpEvents_.data(), arpEvents_.size()));

    // --- 6. Route arp events → MIDI output ---
    if (data.outputEvents) {
        for (size_t i = 0; i < numArpEvents; ++i) {
            const auto& evt = arpEvents_[i];

            Event outEvent{};
            outEvent.busIndex = 0;
            outEvent.sampleOffset = evt.sampleOffset;
            outEvent.ppqPosition = 0;
            outEvent.flags = 0;

            if (evt.type == ArpEvent::Type::NoteOn) {
                outEvent.type = Event::kNoteOnEvent;
                outEvent.noteOn.channel = 0;
                outEvent.noteOn.pitch = static_cast<int16>(evt.note);
                outEvent.noteOn.velocity = static_cast<float>(evt.velocity) / 127.0f;
                outEvent.noteOn.noteId = -1;
                outEvent.noteOn.tuning = 0.0f;
                outEvent.noteOn.length = 0;
                data.outputEvents->addEvent(outEvent);

                // Feed audition voice
                if (auditionEnabled_.load(std::memory_order_relaxed)) {
                    auditionVoice_.noteOn(evt.note, evt.velocity);
                }
            } else if (evt.type == ArpEvent::Type::NoteOff) {
                outEvent.type = Event::kNoteOffEvent;
                outEvent.noteOff.channel = 0;
                outEvent.noteOff.pitch = static_cast<int16>(evt.note);
                outEvent.noteOff.velocity = 0.0f;
                outEvent.noteOff.noteId = -1;
                outEvent.noteOff.tuning = 0.0f;
                data.outputEvents->addEvent(outEvent);

                // Feed audition voice
                auditionVoice_.noteOff();
            }
        }
    }

    // --- 7. Output playhead parameters ---
    if (data.outputParameterChanges) {
        auto outputPlayhead = [&](ParamID id, float value) {
            int32 index = 0;
            auto* queue = data.outputParameterChanges->addParameterData(id, index);
            if (queue) {
                int32 sampleOffset = 0;
                queue->addPoint(sampleOffset, static_cast<ParamValue>(value), index);
            }
        };

        outputPlayhead(kArpVelocityPlayheadId,
            static_cast<float>(arpCore_.velocityLane().currentStep()) / 32.0f);
        outputPlayhead(kArpGatePlayheadId,
            static_cast<float>(arpCore_.gateLane().currentStep()) / 32.0f);
        outputPlayhead(kArpPitchPlayheadId,
            static_cast<float>(arpCore_.pitchLane().currentStep()) / 32.0f);
        outputPlayhead(kArpRatchetPlayheadId,
            static_cast<float>(arpCore_.ratchetLane().currentStep()) / 32.0f);
        outputPlayhead(kArpModifierPlayheadId,
            static_cast<float>(arpCore_.modifierLane().currentStep()) / 32.0f);
        outputPlayhead(kArpConditionPlayheadId,
            static_cast<float>(arpCore_.conditionLane().currentStep()) / 32.0f);
        outputPlayhead(kArpChordPlayheadId,
            static_cast<float>(arpCore_.chordLane().currentStep()) / 32.0f);
        outputPlayhead(kArpInversionPlayheadId,
            static_cast<float>(arpCore_.inversionLane().currentStep()) / 32.0f);
    }

    // --- 8. Audio output: audition voice ---
    if (data.numOutputs > 0 && data.outputs[0].numChannels >= 2) {
        auto numSamples = static_cast<size_t>(data.numSamples);
        float* outL = data.outputs[0].channelBuffers32[0];
        float* outR = data.outputs[0].channelBuffers32[1];

        // Clear buffers
        for (size_t s = 0; s < numSamples; ++s) {
            outL[s] = 0.0f;
            outR[s] = 0.0f;
        }

        // Apply audition voice params and render
        if (auditionEnabled_.load(std::memory_order_relaxed)) {
            auditionVoice_.setWaveform(auditionWaveform_.load(std::memory_order_relaxed));
            auditionVoice_.setDecay(auditionDecay_.load(std::memory_order_relaxed));
            auditionVoice_.setVolume(auditionVolume_.load(std::memory_order_relaxed));
            auditionVoice_.processBlock(outL, outR, numSamples);
        }

        data.outputs[0].silenceFlags = auditionVoice_.isActive() ? 0 : 0x3;
    }

    return kResultOk;
}

// ==============================================================================
// State Save/Load
// ==============================================================================

tresult PLUGIN_API Processor::getState(IBStream* state)
{
    if (!state)
        return kResultFalse;

    IBStreamer streamer(state, kLittleEndian);

    // 1. State version
    streamer.writeInt32(kCurrentStateVersion);

    // 2. Arp params (audition params are session-only, not saved in presets)
    saveArpParams(arpParams_, streamer);

    return kResultOk;
}

tresult PLUGIN_API Processor::setState(IBStream* state)
{
    if (!state)
        return kResultFalse;

    IBStreamer streamer(state, kLittleEndian);

    // 1. Read and discard state version (single version, no migration needed)
    int32 version = 0;
    if (!streamer.readInt32(version))
        return kResultFalse;
    (void)version;

    // 2. Load arp params
    if (!loadArpParams(arpParams_, streamer))
        return kResultFalse;

    // Force arp to MIDI mode (always on in Gradus)
    arpParams_.operatingMode.store(kArpMIDI, std::memory_order_relaxed);

    // Audition params are session-only — not loaded from presets

    return kResultOk;
}

// ==============================================================================
// Parameter Changes
// ==============================================================================

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void Processor::processParameterChanges(IParameterChanges* changes)
{
    int32 numParams = changes->getParameterCount();
    for (int32 i = 0; i < numParams; ++i) {
        auto* queue = changes->getParameterData(i);
        if (!queue) continue;

        ParamID id = queue->getParameterId();
        int32 numPoints = queue->getPointCount();
        ParamValue value = 0.0;
        int32 sampleOffset = 0;

        // Use the last point value
        if (queue->getPoint(numPoints - 1, sampleOffset, value) != kResultOk)
            continue;

        // Try arp params first
        if (id >= kArpBaseId && id <= kArpEndId) {
            handleArpParamChange(arpParams_, id, value);
            continue;
        }

        // Audition params
        if (id == kAuditionEnabledId) { // NOLINT(bugprone-branch-clone)
            auditionEnabled_.store(value >= 0.5, std::memory_order_relaxed);
        } else if (id == kAuditionVolumeId) {
            auditionVolume_.store(static_cast<float>(value), std::memory_order_relaxed);
        } else if (id == kAuditionWaveformId) {
            int wf = static_cast<int>(value * 2.0 + 0.5);
            auditionWaveform_.store(std::clamp(wf, 0, 2), std::memory_order_relaxed);
        } else if (id == kAuditionDecayId) {
            float decay = static_cast<float>(10.0 + value * 1990.0);
            auditionDecay_.store(decay, std::memory_order_relaxed);
        }
    }
}

// ==============================================================================
// Apply Params to Engine (simplified from Ruinae — no mod offsets)
// ==============================================================================

void Processor::applyParamsToEngine()
{
    using namespace Krate::DSP;

    // Mode — only call when value changes (resets step index)
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

    // Direct parameter application (no modulation)
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

    // --- Velocity Lane ---
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
    // --- Gate Lane ---
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
    // --- Pitch Lane ---
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
    // --- Modifier Lane ---
    {
        const auto modLen = arpParams_.modifierLaneLength.load(std::memory_order_relaxed);
        arpCore_.modifierLane().setLength(32);
        for (int i = 0; i < 32; ++i) {
            arpCore_.modifierLane().setStep(
                static_cast<size_t>(i),
                static_cast<uint8_t>(arpParams_.modifierLaneSteps[i].load(
                    std::memory_order_relaxed)));
        }
        arpCore_.modifierLane().setLength(static_cast<size_t>(modLen));
    }
    arpCore_.setAccentVelocity(arpParams_.accentVelocity.load(std::memory_order_relaxed));
    arpCore_.setSlideTime(arpParams_.slideTime.load(std::memory_order_relaxed));

    // --- Ratchet Lane ---
    {
        const auto ratchetLen = arpParams_.ratchetLaneLength.load(std::memory_order_relaxed);
        arpCore_.ratchetLane().setLength(32);
        for (int i = 0; i < 32; ++i) {
            int val = std::clamp(
                arpParams_.ratchetLaneSteps[i].load(std::memory_order_relaxed), 1, 4);
            arpCore_.ratchetLane().setStep(
                static_cast<size_t>(i), static_cast<uint8_t>(val));
        }
        arpCore_.ratchetLane().setLength(static_cast<size_t>(ratchetLen));
    }

    // --- Euclidean Timing ---
    arpCore_.setEuclideanSteps(
        arpParams_.euclideanSteps.load(std::memory_order_relaxed));
    arpCore_.setEuclideanHits(
        arpParams_.euclideanHits.load(std::memory_order_relaxed));
    arpCore_.setEuclideanRotation(
        arpParams_.euclideanRotation.load(std::memory_order_relaxed));
    arpCore_.setEuclideanEnabled(
        arpParams_.euclideanEnabled.load(std::memory_order_relaxed));

    // --- Condition Lane ---
    {
        const auto condLen = arpParams_.conditionLaneLength.load(std::memory_order_relaxed);
        arpCore_.conditionLane().setLength(32);
        for (int i = 0; i < 32; ++i) {
            int val = std::clamp(
                arpParams_.conditionLaneSteps[i].load(std::memory_order_relaxed), 0, 17);
            arpCore_.conditionLane().setStep(
                static_cast<size_t>(i), static_cast<uint8_t>(val));
        }
        arpCore_.conditionLane().setLength(static_cast<size_t>(condLen));
    }
    arpCore_.setFillActive(arpParams_.fillToggle.load(std::memory_order_relaxed));

    // --- Dice & Humanize ---
    {
        bool expected = true;
        if (arpParams_.diceTrigger.compare_exchange_strong(
                expected, false, std::memory_order_relaxed)) {
            arpCore_.triggerDice();
        }
    }
    arpCore_.setHumanize(arpParams_.humanize.load(std::memory_order_relaxed));

    // --- Ratchet Swing ---
    arpCore_.setRatchetSwing(arpParams_.ratchetSwing.load(std::memory_order_relaxed));

    // --- Scale Mode ---
    arpCore_.setScaleType(static_cast<Krate::DSP::ScaleType>(
        arpParams_.scaleType.load(std::memory_order_relaxed)));
    arpCore_.setRootNote(arpParams_.rootNote.load(std::memory_order_relaxed));
    arpCore_.setScaleQuantizeInput(
        arpParams_.scaleQuantizeInput.load(std::memory_order_relaxed));

    // --- Chord Lane ---
    {
        const auto chordLen = arpParams_.chordLaneLength.load(std::memory_order_relaxed);
        arpCore_.chordLane().setLength(32);
        for (int i = 0; i < 32; ++i) {
            int val = std::clamp(
                arpParams_.chordLaneSteps[i].load(std::memory_order_relaxed), 0, 4);
            arpCore_.chordLane().setStep(
                static_cast<size_t>(i), static_cast<uint8_t>(val));
        }
        arpCore_.chordLane().setLength(static_cast<size_t>(chordLen));
    }
    // --- Inversion Lane ---
    {
        const auto invLen = arpParams_.inversionLaneLength.load(std::memory_order_relaxed);
        arpCore_.inversionLane().setLength(32);
        for (int i = 0; i < 32; ++i) {
            int val = std::clamp(
                arpParams_.inversionLaneSteps[i].load(std::memory_order_relaxed), 0, 3);
            arpCore_.inversionLane().setStep(
                static_cast<size_t>(i), static_cast<uint8_t>(val));
        }
        arpCore_.inversionLane().setLength(static_cast<size_t>(invLen));
    }
    arpCore_.setVoicingMode(static_cast<Krate::DSP::VoicingMode>(
        arpParams_.voicingMode.load(std::memory_order_relaxed)));

    // Per-lane speed multipliers
    arpCore_.setLaneSpeed(0, arpParams_.velocityLaneSpeed.load(std::memory_order_relaxed));
    arpCore_.setLaneSpeed(1, arpParams_.gateLaneSpeed.load(std::memory_order_relaxed));
    arpCore_.setLaneSpeed(2, arpParams_.pitchLaneSpeed.load(std::memory_order_relaxed));
    arpCore_.setLaneSpeed(3, arpParams_.modifierLaneSpeed.load(std::memory_order_relaxed));
    arpCore_.setLaneSpeed(4, arpParams_.ratchetLaneSpeed.load(std::memory_order_relaxed));
    arpCore_.setLaneSpeed(5, arpParams_.conditionLaneSpeed.load(std::memory_order_relaxed));
    arpCore_.setLaneSpeed(6, arpParams_.chordLaneSpeed.load(std::memory_order_relaxed));
    arpCore_.setLaneSpeed(7, arpParams_.inversionLaneSpeed.load(std::memory_order_relaxed));

    // v1.5 Features
    arpCore_.setRatchetDecay(arpParams_.ratchetDecay.load(std::memory_order_relaxed));
    arpCore_.setStrumTime(arpParams_.strumTime.load(std::memory_order_relaxed));
    arpCore_.setStrumDirection(arpParams_.strumDirection.load(std::memory_order_relaxed));
    arpCore_.setLaneSwing(0, arpParams_.velocityLaneSwing.load(std::memory_order_relaxed));
    arpCore_.setLaneSwing(1, arpParams_.gateLaneSwing.load(std::memory_order_relaxed));
    arpCore_.setLaneSwing(2, arpParams_.pitchLaneSwing.load(std::memory_order_relaxed));
    arpCore_.setLaneSwing(3, arpParams_.modifierLaneSwing.load(std::memory_order_relaxed));
    arpCore_.setLaneSwing(4, arpParams_.ratchetLaneSwing.load(std::memory_order_relaxed));
    arpCore_.setLaneSwing(5, arpParams_.conditionLaneSwing.load(std::memory_order_relaxed));
    arpCore_.setLaneSwing(6, arpParams_.chordLaneSwing.load(std::memory_order_relaxed));
    arpCore_.setLaneSwing(7, arpParams_.inversionLaneSwing.load(std::memory_order_relaxed));

    // v1.5 Part 2 Features
    arpCore_.setVelocityCurveType(arpParams_.velocityCurveType.load(std::memory_order_relaxed));
    arpCore_.setVelocityCurveAmount(arpParams_.velocityCurveAmount.load(std::memory_order_relaxed));
    arpCore_.setTranspose(arpParams_.transpose.load(std::memory_order_relaxed));
    arpCore_.setLengthJitter(arpParams_.lengthJitter.load(std::memory_order_relaxed));

    // Always enabled in Gradus (no operating mode selector)
    arpCore_.setEnabled(true);
}

} // namespace Gradus
