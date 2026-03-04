// ==============================================================================
// Audio Processor Implementation
// ==============================================================================

#include "processor.h"

#include "midi/midi_event_dispatcher.h"

#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/base/ibstream.h"
#include "base/source/fstreamer.h"

#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/midi_utils.h>
#include <krate/dsp/core/pitch_utils.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Innexus {

// ==============================================================================
// Constructor / Destructor
// ==============================================================================
Processor::Processor() // NOLINT(cppcoreguidelines-pro-type-member-init) -- sampleAnalyzer_ is default-constructed; clang-tidy can't resolve its include
{
    setControllerClass(kControllerUID);
}

Processor::~Processor()
{
    // Clean up analysis pointers (SC-010: all deletion happens here, never on audio thread)
    auto* current = currentAnalysis_.load(std::memory_order_acquire);
    if (current) {
        delete current;
        currentAnalysis_.store(nullptr, std::memory_order_release);
    }
    delete pendingDeletion_;
    pendingDeletion_ = nullptr;
}

// ==============================================================================
// Initialize
// ==============================================================================
Steinberg::tresult PLUGIN_API Processor::initialize(Steinberg::FUnknown* context)
{
    auto result = AudioEffect::initialize(context);
    if (result != Steinberg::kResultOk)
        return result;

    // MIDI event input
    addEventInput(STR16("Event In"), 1);

    // Stereo audio output
    addAudioOutput(STR16("Stereo Out"), Steinberg::Vst::SpeakerArr::kStereo);

    return Steinberg::kResultOk;
}

// ==============================================================================
// Terminate
// ==============================================================================
Steinberg::tresult PLUGIN_API Processor::terminate()
{
    sampleAnalyzer_.cancel();

    // SC-010: Clean up any pending deletion when deactivating
    cleanupPendingDeletion();

    return AudioEffect::terminate();
}

// ==============================================================================
// Cleanup Pending Deletion (SC-010: safe non-audio-thread deletion)
// ==============================================================================
void Processor::cleanupPendingDeletion()
{
    delete pendingDeletion_;
    pendingDeletion_ = nullptr;
}

// ==============================================================================
// Set Active
// ==============================================================================
Steinberg::tresult PLUGIN_API Processor::setActive(Steinberg::TBool state)
{
    if (state)
    {
        // SC-010: Clean up any pending deletions from previous activation cycle
        cleanupPendingDeletion();

        // Prepare oscillator bank
        oscillatorBank_.prepare(sampleRate_);

        // Prepare residual synthesizer if analysis is available
        {
            const SampleAnalysis* analysis =
                currentAnalysis_.load(std::memory_order_acquire);
            if (analysis && !analysis->residualFrames.empty() &&
                analysis->analysisFFTSize > 0 && analysis->analysisHopSize > 0)
            {
                residualSynth_.prepare(
                    analysis->analysisFFTSize, analysis->analysisHopSize,
                    static_cast<float>(sampleRate_));
            }
        }

        // Configure parameter smoothers (FR-025: 5ms time constant)
        harmonicLevelSmoother_.configure(5.0f, static_cast<float>(sampleRate_));
        harmonicLevelSmoother_.snapTo(1.0f); // default plain value
        residualLevelSmoother_.configure(5.0f, static_cast<float>(sampleRate_));
        residualLevelSmoother_.snapTo(1.0f); // default plain value
        brightnessSmoother_.configure(5.0f, static_cast<float>(sampleRate_));
        brightnessSmoother_.snapTo(0.0f); // default plain value (neutral)
        transientEmphasisSmoother_.configure(5.0f, static_cast<float>(sampleRate_));
        transientEmphasisSmoother_.snapTo(0.0f); // default plain value (no boost)

        // Compute freeze recovery length
        freezeRecoveryLengthSamples_ = static_cast<int>(
            kFreezeRecoveryTimeSec * static_cast<float>(sampleRate_));
        freezeRecoveryLengthSamples_ = std::max(freezeRecoveryLengthSamples_, 1);

        // Compute anti-click voice steal length (20ms)
        antiClickLengthSamples_ = static_cast<int>(
            kAntiClickTimeSec * static_cast<float>(sampleRate_));
        antiClickLengthSamples_ = std::max(antiClickLengthSamples_, 1);

        // Compute initial release decay coefficient
        updateReleaseDecayCoeff();

        // Reset voice state
        noteActive_ = false;
        inRelease_ = false;
        releaseGain_ = 1.0f;
        currentMidiNote_ = -1;
        velocityGain_ = 1.0f;
        pitchBendSemitones_ = 0.0f;
        currentFrameIndex_ = 0;
        frameSampleCounter_ = 0;
        antiClickSamplesRemaining_ = 0;
        antiClickOldLevel_ = 0.0f;
        isFrozen_ = false;
        freezeRecoverySamplesRemaining_ = 0;
        freezeRecoveryOldLevel_ = 0.0f;
    }
    else
    {
        oscillatorBank_.reset();
        residualSynth_.reset();

        // SC-010: Clean up any pending deletions when deactivating
        cleanupPendingDeletion();
    }

    return AudioEffect::setActive(state);
}

// ==============================================================================
// Setup Processing
// ==============================================================================
Steinberg::tresult PLUGIN_API Processor::setupProcessing(
    Steinberg::Vst::ProcessSetup& newSetup)
{
    sampleRate_ = newSetup.sampleRate;
    return AudioEffect::setupProcessing(newSetup);
}

// ==============================================================================
// Process (T082: Main audio processing -- FR-047 to FR-058)
// ==============================================================================
Steinberg::tresult PLUGIN_API Processor::process(Steinberg::Vst::ProcessData& data)
{
    // --- Handle parameter changes ---
    processParameterChanges(data.inputParameterChanges);

    // --- Check for new analysis from background thread (FR-058) ---
    checkForNewAnalysis();

    // --- Read current analysis with acquire semantics (FR-058) ---
    const SampleAnalysis* analysis =
        currentAnalysis_.load(std::memory_order_acquire);

    // --- Process MIDI events ---
    processEvents(data.inputEvents);

    // --- Output ---
    if (data.numOutputs < 1 || data.outputs[0].numChannels < 2)
        return Steinberg::kResultOk;

    auto numSamples = data.numSamples;
    auto** out = data.outputs[0].channelBuffers32;

    // FR-055: If no analysis loaded, output silence
    if (!analysis || analysis->frames.empty() || !noteActive_)
    {
        for (Steinberg::int32 s = 0; s < numSamples; ++s)
        {
            out[0][s] = 0.0f;
            out[1][s] = 0.0f;
        }
        data.outputs[0].silenceFlags = 0x3;
        return Steinberg::kResultOk;
    }

    // --- Update inharmonicity from parameter ---
    float inharm = inharmonicityAmount_.load(std::memory_order_relaxed);
    oscillatorBank_.setInharmonicityAmount(inharm);

    // --- Compute hop size in samples for frame advancement ---
    const size_t hopSizeInSamples = static_cast<size_t>(
        analysis->hopTimeSec * static_cast<float>(sampleRate_) + 0.5f);
    const size_t totalFrames = analysis->totalFrames;

    // --- Get master gain ---
    const float gain = masterGain_.load(std::memory_order_relaxed);

    // --- M2: Get residual parameters and set smoother targets (FR-025) ---
    const float harmLevelNorm = harmonicLevel_.load(std::memory_order_relaxed);
    const float resLevelNorm = residualLevel_.load(std::memory_order_relaxed);
    const float harmLevelPlain = harmLevelNorm * 2.0f;  // plain range 0-2
    const float resLevelPlain = resLevelNorm * 2.0f;    // plain range 0-2
    harmonicLevelSmoother_.setTarget(harmLevelPlain);
    residualLevelSmoother_.setTarget(resLevelPlain);

    // Brightness and transient emphasis: set smoother targets from atomics (FR-025)
    const float brightnessNorm = residualBrightness_.load(std::memory_order_relaxed);
    const float brightnessPlainTarget = brightnessNorm * 2.0f - 1.0f; // plain range -1 to +1
    const float transientEmpNorm = transientEmphasis_.load(std::memory_order_relaxed);
    const float transientEmpPlainTarget = transientEmpNorm * 2.0f;    // plain range 0 to 2
    brightnessSmoother_.setTarget(brightnessPlainTarget);
    transientEmphasisSmoother_.setTarget(transientEmpPlainTarget);

    // Check if residual synthesis is available
    const bool hasResidual = !analysis->residualFrames.empty() &&
                              residualSynth_.isPrepared();

    // --- Process each sample ---
    bool hasSoundOutput = false;

    for (Steinberg::int32 s = 0; s < numSamples; ++s)
    {
        // --- Frame advancement (FR-047) ---
        if (hopSizeInSamples > 0 && totalFrames > 0)
        {
            frameSampleCounter_++;
            if (frameSampleCounter_ >= hopSizeInSamples &&
                currentFrameIndex_ < totalFrames - 1)
            {
                frameSampleCounter_ = 0;
                currentFrameIndex_++;

                // Get the new frame
                const auto& frame = analysis->getFrame(currentFrameIndex_);

                // Confidence-gated freeze (FR-052) with hysteresis (FR-053)
                // Use higher threshold for recovery to prevent oscillation
                float recoveryThreshold = isFrozen_
                    ? (kConfidenceThreshold + kConfidenceHysteresis)
                    : kConfidenceThreshold;

                if (frame.f0Confidence >= recoveryThreshold)
                {
                    if (isFrozen_)
                    {
                        // Recovery from freeze: capture current output level
                        // and start crossfade (FR-053)
                        isFrozen_ = false;
                        freezeRecoveryOldLevel_ = oscillatorBank_.process();
                        // Re-process will happen below, but we need the snapshot
                        // before loading new frame
                        freezeRecoverySamplesRemaining_ = freezeRecoveryLengthSamples_;
                    }
                    lastGoodFrame_ = frame;

                    // Calculate target pitch with pitch bend
                    float basePitch = Krate::DSP::midiNoteToFrequency(currentMidiNote_);
                    float bendRatio = Krate::DSP::semitonesToRatio(pitchBendSemitones_);
                    float targetPitch = basePitch * bendRatio;
                    oscillatorBank_.loadFrame(frame, targetPitch);

                    // M2: Load residual frame (FR-017: synchronized advancement)
                    if (hasResidual)
                    {
                        residualSynth_.loadFrame(
                            analysis->getResidualFrame(currentFrameIndex_),
                            brightnessSmoother_.getCurrentValue(),
                            transientEmphasisSmoother_.getCurrentValue());
                    }
                }
                else
                {
                    // FR-052: Hold last known-good frame
                    isFrozen_ = true;
                    // Don't advance -- oscillator bank keeps current state
                }
            }
        }

        // --- Generate oscillator bank output ---
        float harmonicSample = oscillatorBank_.process();
        float residualSample = hasResidual ? residualSynth_.process() : 0.0f;

        // M2: Mix harmonic and residual (FR-028)
        float harmLevel = harmonicLevelSmoother_.process();
        float resLevel = residualLevelSmoother_.process();
        // Advance brightness/transient smoothers per-sample (FR-025)
        (void)brightnessSmoother_.process();
        (void)transientEmphasisSmoother_.process();
        float sample = harmonicSample * harmLevel + residualSample * resLevel;

        // --- Freeze recovery crossfade (FR-053) ---
        if (freezeRecoverySamplesRemaining_ > 0)
        {
            // Linear crossfade from frozen output level to new live output
            float fadeProgress = static_cast<float>(freezeRecoverySamplesRemaining_) /
                                 static_cast<float>(freezeRecoveryLengthSamples_);
            // old * fadeProgress + new * (1 - fadeProgress)
            sample = freezeRecoveryOldLevel_ * fadeProgress +
                     sample * (1.0f - fadeProgress);
            freezeRecoverySamplesRemaining_--;
        }

        // --- Anti-click voice steal crossfade (FR-054) ---
        if (antiClickSamplesRemaining_ > 0)
        {
            float fadeProgress = static_cast<float>(antiClickSamplesRemaining_) /
                                 static_cast<float>(antiClickLengthSamples_);
            // Blend from old output level to new output
            sample = antiClickOldLevel_ * fadeProgress +
                     sample * (1.0f - fadeProgress);
            antiClickSamplesRemaining_--;
        }

        // --- Velocity scaling (FR-050): applied to summed output, NOT partials ---
        sample *= velocityGain_;

        // --- Release envelope (FR-049) ---
        if (inRelease_)
        {
            releaseGain_ *= releaseDecayCoeff_;
            sample *= releaseGain_;

            // Check if release has faded below threshold
            if (releaseGain_ < 1e-6f)
            {
                noteActive_ = false;
                inRelease_ = false;
                releaseGain_ = 1.0f;
                oscillatorBank_.reset();
                residualSynth_.reset();
                sample = 0.0f;
            }
        }

        // --- Master gain ---
        sample *= gain;

        // Write to both channels (mono -> stereo)
        out[0][s] = sample;
        out[1][s] = sample;

        if (sample != 0.0f)
            hasSoundOutput = true;
    }

    data.outputs[0].silenceFlags = hasSoundOutput ? 0 : 0x3;

    return Steinberg::kResultOk;
}

// ==============================================================================
// Process Events (MIDI -- FR-048, FR-049, FR-051, FR-054)
// ==============================================================================
void Processor::processEvents(Steinberg::Vst::IEventList* events)
{
    Krate::Plugins::dispatchMidiEvents(events, *this);
}

// ==============================================================================
// MIDI Dispatcher Callbacks
// ==============================================================================
void Processor::onNoteOn(int16_t pitch, float velocity)
{
    handleNoteOn(static_cast<int>(pitch), velocity);
}

void Processor::onNoteOff([[maybe_unused]] int16_t pitch)
{
    handleNoteOff(); // Monophonic: pitch ignored
}

void Processor::onPitchBend(float bipolar)
{
    float bendSemitones = Krate::DSP::pitchBendToSemitones(
        bipolar, kPitchBendRangeSemitones);
    handlePitchBend(bendSemitones);
}

// ==============================================================================
// Handle Note On (FR-048, FR-054: Monophonic Last-Note-Priority)
// ==============================================================================
void Processor::handleNoteOn(int noteNumber, float velocity)
{
    const SampleAnalysis* analysis =
        currentAnalysis_.load(std::memory_order_acquire);

    // FR-055: No sample loaded -> no sound
    if (!analysis || analysis->frames.empty())
        return;

    // FR-054: Monophonic -- if note already active, capture current output
    // for anti-click crossfade (20ms)
    if (noteActive_ && !inRelease_)
    {
        // Capture last output sample before resetting for crossfade
        antiClickOldLevel_ = oscillatorBank_.process();
        antiClickSamplesRemaining_ = antiClickLengthSamples_;
    }

    // Set new note state
    currentMidiNote_ = noteNumber;
    noteActive_ = true;
    inRelease_ = false;
    releaseGain_ = 1.0f;
    isFrozen_ = false;
    freezeRecoverySamplesRemaining_ = 0;
    freezeRecoveryOldLevel_ = 0.0f;

    // FR-050: Velocity scales global amplitude, NOT partial amplitudes
    velocityGain_ = velocity; // VST3 velocity is already 0.0-1.0

    // Start from first frame (FR-047)
    currentFrameIndex_ = 0;
    frameSampleCounter_ = 0;

    // Load initial frame
    const auto& frame = analysis->getFrame(0);
    lastGoodFrame_ = frame;

    // Calculate target pitch with pitch bend
    float basePitch = Krate::DSP::midiNoteToFrequency(noteNumber);
    float bendRatio = Krate::DSP::semitonesToRatio(pitchBendSemitones_);
    float targetPitch = basePitch * bendRatio;

    // Reset oscillator bank for new note and load first frame
    oscillatorBank_.reset();
    oscillatorBank_.loadFrame(frame, targetPitch);

    // M2: Load initial residual frame (FR-017)
    if (!analysis->residualFrames.empty() && residualSynth_.isPrepared())
    {
        residualSynth_.reset();
        // Use current smoothed brightness/transient values for initial frame
        residualSynth_.loadFrame(
            analysis->getResidualFrame(0),
            brightnessSmoother_.getCurrentValue(),
            transientEmphasisSmoother_.getCurrentValue());
    }
}

// ==============================================================================
// Handle Note Off (FR-049, FR-057)
// ==============================================================================
void Processor::handleNoteOff()
{
    if (!noteActive_ || inRelease_)
        return;

    // Enter release phase
    inRelease_ = true;

    // FR-057: Enforce minimum 20ms anti-click fade
    updateReleaseDecayCoeff();
}

// ==============================================================================
// Handle Pitch Bend (FR-051, T084)
// ==============================================================================
void Processor::handlePitchBend(float bendSemitones)
{
    pitchBendSemitones_ = bendSemitones;

    if (!noteActive_ || currentMidiNote_ < 0)
        return;

    // Recalculate pitch with bend
    float basePitch = Krate::DSP::midiNoteToFrequency(currentMidiNote_);
    float bendRatio = Krate::DSP::semitonesToRatio(pitchBendSemitones_);
    float targetPitch = basePitch * bendRatio;

    // FR-051: Recalculate epsilon and anti-aliasing for all partials
    oscillatorBank_.setTargetPitch(targetPitch);
}

// ==============================================================================
// Update Release Decay Coefficient (FR-049, FR-057)
// ==============================================================================
void Processor::updateReleaseDecayCoeff()
{
    float userReleaseMs = releaseTimeMs_.load(std::memory_order_relaxed);

    // FR-057: Enforce minimum 20ms anti-click fade
    float effectiveReleaseMs = std::max(userReleaseMs, kMinAntiClickMs);

    // Exponential decay: A(t) = exp(-t/tau)
    // Per-sample coefficient: exp(-1.0 / (tau_samples))
    // where tau_samples = effectiveReleaseMs * 0.001 * sampleRate
    float tauSamples = effectiveReleaseMs * 0.001f * static_cast<float>(sampleRate_);
    releaseDecayCoeff_ = std::exp(-1.0f / tauSamples);
}

// ==============================================================================
// Check For New Analysis (FR-058, SC-010)
// ==============================================================================
// NOLINTNEXTLINE(readability-convert-member-functions-to-static) -- accesses members via includes clang-tidy can't resolve
void Processor::checkForNewAnalysis()
{
    if (!sampleAnalyzer_.isComplete())
        return;

    auto result = sampleAnalyzer_.takeResult();
    if (!result)
        return;

    // M2: Prepare residual synth if new analysis has residual data
    if (result->analysisFFTSize > 0 && result->analysisHopSize > 0 &&
        !result->residualFrames.empty())
    {
        residualSynth_.prepare(
            result->analysisFFTSize, result->analysisHopSize,
            static_cast<float>(sampleRate_));
    }

    // Publish new analysis with release semantics
    auto* newAnalysis = result.release();
    auto* oldAnalysis = currentAnalysis_.exchange(newAnalysis,
                                                   std::memory_order_acq_rel);

    // SC-010: Do NOT delete on audio thread. Flag for deferred cleanup.
    // The old pendingDeletion_ (if any) must be cleaned up first.
    // Since this runs once per analysis completion (rare), we accept the
    // theoretical leak of one extra SampleAnalysis if two analyses complete
    // back-to-back before cleanup. In practice, analysis takes seconds
    // and setActive/terminate runs between them.
    // The destructor and setActive(true)/setActive(false) handle cleanup.
    pendingDeletion_ = oldAnalysis;
}

// ==============================================================================
// Load Sample (public API for triggering analysis)
// ==============================================================================
// NOLINTNEXTLINE(readability-convert-member-functions-to-static) -- accesses members via includes clang-tidy can't resolve
void Processor::loadSample(const std::string& filePath)
{
    loadedFilePath_ = filePath;
    if (!filePath.empty())
    {
        sampleAnalyzer_.startAnalysis(filePath);
    }
}

// ==============================================================================
// Process Parameter Changes
// ==============================================================================
// NOLINTNEXTLINE(readability-convert-member-functions-to-static) -- accesses atomic members via includes clang-tidy can't resolve
void Processor::processParameterChanges(
    Steinberg::Vst::IParameterChanges* changes)
{
    if (!changes)
        return;

    auto numParams = changes->getParameterCount();
    for (Steinberg::int32 i = 0; i < numParams; ++i)
    {
        auto* paramQueue = changes->getParameterData(i);
        if (!paramQueue)
            continue;

        Steinberg::Vst::ParamValue value;
        Steinberg::int32 sampleOffset;
        auto numPoints = paramQueue->getPointCount();
        if (paramQueue->getPoint(numPoints - 1, sampleOffset, value) ==
            Steinberg::kResultTrue)
        {
            switch (paramQueue->getParameterId())
            {
            case kBypassId:
                bypass_.store(static_cast<float>(value));
                break;
            case kMasterGainId:
                masterGain_.store(static_cast<float>(value));
                break;
            case kReleaseTimeId:
            {
                // Exponential mapping: 20ms to 5000ms
                constexpr float kMinMs = 20.0f;
                constexpr float kMaxMs = 5000.0f;
                constexpr float kRatio = kMaxMs / kMinMs;
                auto timeMs = kMinMs * std::pow(kRatio, static_cast<float>(value));
                releaseTimeMs_.store(std::clamp(timeMs, kMinMs, kMaxMs));
                break;
            }
            case kInharmonicityAmountId:
                inharmonicityAmount_.store(
                    std::clamp(static_cast<float>(value), 0.0f, 1.0f));
                break;
            case kHarmonicLevelId:
                harmonicLevel_.store(
                    std::clamp(static_cast<float>(value), 0.0f, 1.0f));
                break;
            case kResidualLevelId:
                residualLevel_.store(
                    std::clamp(static_cast<float>(value), 0.0f, 1.0f));
                break;
            case kResidualBrightnessId:
                residualBrightness_.store(
                    std::clamp(static_cast<float>(value), 0.0f, 1.0f));
                break;
            case kTransientEmphasisId:
                transientEmphasis_.store(
                    std::clamp(static_cast<float>(value), 0.0f, 1.0f));
                break;
            default:
                break;
            }
        }
    }
}

// ==============================================================================
// Set Bus Arrangements (instrument: no audio inputs, stereo output only)
// ==============================================================================
Steinberg::tresult PLUGIN_API Processor::setBusArrangements(
    Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
    Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts)
{
    if (numIns == 0 && numOuts == 1 &&
        outputs[0] == Steinberg::Vst::SpeakerArr::kStereo) {
        return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
    }
    return Steinberg::kResultFalse;
}

// ==============================================================================
// Can Process Sample Size
// ==============================================================================
Steinberg::tresult PLUGIN_API Processor::canProcessSampleSize(
    Steinberg::int32 symbolicSampleSize)
{
    if (symbolicSampleSize == Steinberg::Vst::kSample32)
        return Steinberg::kResultTrue;
    return Steinberg::kResultFalse;
}

// ==============================================================================
// State Management (T083: FR-056, SC-009)
// ==============================================================================
Steinberg::tresult PLUGIN_API Processor::getState(Steinberg::IBStream* state)
{
    if (!state)
        return Steinberg::kResultFalse;

    Steinberg::IBStreamer streamer(state, kLittleEndian);

    // Write state version -- M2: version 2 (FR-027)
    streamer.writeInt32(2);

    // --- M1 parameters (unchanged) ---
    streamer.writeFloat(releaseTimeMs_.load(std::memory_order_relaxed));
    streamer.writeFloat(inharmonicityAmount_.load(std::memory_order_relaxed));
    streamer.writeFloat(masterGain_.load(std::memory_order_relaxed));
    streamer.writeFloat(bypass_.load(std::memory_order_relaxed));

    // Write sample file path (FR-056)
    auto pathLen = static_cast<Steinberg::int32>(loadedFilePath_.size());
    streamer.writeInt32(pathLen);
    if (pathLen > 0)
    {
        state->write(
            const_cast<char*>(loadedFilePath_.data()),
            pathLen, nullptr);
    }

    // --- M2 parameters (FR-027) ---
    // Write plain parameter values for residual controls
    const float harmLevelNorm = harmonicLevel_.load(std::memory_order_relaxed);
    const float resLevelNorm = residualLevel_.load(std::memory_order_relaxed);
    const float brightnessNorm = residualBrightness_.load(std::memory_order_relaxed);
    const float transientEmpNorm = transientEmphasis_.load(std::memory_order_relaxed);

    // Convert normalized to plain for persistence (data-model.md)
    streamer.writeFloat(harmLevelNorm * 2.0f);           // plain 0.0-2.0
    streamer.writeFloat(resLevelNorm * 2.0f);            // plain 0.0-2.0
    streamer.writeFloat(brightnessNorm * 2.0f - 1.0f);   // plain -1.0 to +1.0
    streamer.writeFloat(transientEmpNorm * 2.0f);        // plain 0.0-2.0

    // --- M2 residual frames (FR-027) ---
    const SampleAnalysis* analysis =
        currentAnalysis_.load(std::memory_order_acquire);

    if (analysis && !analysis->residualFrames.empty())
    {
        auto frameCount = static_cast<Steinberg::int32>(analysis->residualFrames.size());
        streamer.writeInt32(frameCount);
        streamer.writeInt32(static_cast<Steinberg::int32>(analysis->analysisFFTSize));
        streamer.writeInt32(static_cast<Steinberg::int32>(analysis->analysisHopSize));

        for (const auto& frame : analysis->residualFrames)
        {
            // 16 floats: bandEnergies
            for (size_t b = 0; b < Krate::DSP::kResidualBands; ++b)
            {
                streamer.writeFloat(frame.bandEnergies[b]);
            }
            // 1 float: totalEnergy
            streamer.writeFloat(frame.totalEnergy);
            // 1 int8: transientFlag
            streamer.writeInt8(frame.transientFlag ? static_cast<Steinberg::int8>(1)
                                                    : static_cast<Steinberg::int8>(0));
        }
    }
    else
    {
        // No residual data
        streamer.writeInt32(0); // residualFrameCount = 0
        streamer.writeInt32(0); // analysisFFTSize
        streamer.writeInt32(0); // analysisHopSize
    }

    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Processor::setState(Steinberg::IBStream* state)
{
    if (!state)
        return Steinberg::kResultFalse;

    Steinberg::IBStreamer streamer(state, kLittleEndian);

    // Read version
    Steinberg::int32 version = 0;
    if (!streamer.readInt32(version))
        return Steinberg::kResultFalse;

    if (version >= 1)
    {
        float floatVal = 0.0f;

        // --- M1 parameters (unchanged) ---
        if (streamer.readFloat(floatVal))
            releaseTimeMs_.store(std::clamp(floatVal, 20.0f, 5000.0f));

        if (streamer.readFloat(floatVal))
            inharmonicityAmount_.store(std::clamp(floatVal, 0.0f, 1.0f));

        if (streamer.readFloat(floatVal))
            masterGain_.store(std::clamp(floatVal, 0.0f, 1.0f));

        if (streamer.readFloat(floatVal))
            bypass_.store(floatVal > 0.5f ? 1.0f : 0.0f);

        // Read sample file path (FR-056)
        Steinberg::int32 pathLen = 0;
        if (streamer.readInt32(pathLen) && pathLen > 0 && pathLen < 4096)
        {
            std::string filePath(static_cast<size_t>(pathLen), '\0'); // NOLINT(bugprone-unused-local-non-trivial-variable) -- used on line below; clang-tidy can't resolve includes
            Steinberg::int32 bytesRead = 0;
            if (state->read(filePath.data(), pathLen, &bytesRead) ==
                Steinberg::kResultOk && bytesRead == pathLen)
            {
                // Trigger re-analysis (FR-056: restore analysis on session reload)
                loadSample(filePath);
            }
        }
        else if (pathLen == 0)
        {
            loadedFilePath_.clear();
        }

        // --- M2 parameters and residual frames (FR-027) ---
        if (version >= 2)
        {
            // Read 4 new plain parameter values and convert to normalized
            if (streamer.readFloat(floatVal))
            {
                // harmonicLevel: plain 0.0-2.0, normalized = plain / 2.0
                harmonicLevel_.store(
                    std::clamp(floatVal, 0.0f, 2.0f) / 2.0f);
            }

            if (streamer.readFloat(floatVal))
            {
                // residualLevel: plain 0.0-2.0, normalized = plain / 2.0
                residualLevel_.store(
                    std::clamp(floatVal, 0.0f, 2.0f) / 2.0f);
            }

            if (streamer.readFloat(floatVal))
            {
                // brightness: plain -1.0 to +1.0, normalized = (plain + 1.0) / 2.0
                residualBrightness_.store(
                    (std::clamp(floatVal, -1.0f, 1.0f) + 1.0f) / 2.0f);
            }

            if (streamer.readFloat(floatVal))
            {
                // transientEmphasis: plain 0.0-2.0, normalized = plain / 2.0
                transientEmphasis_.store(
                    std::clamp(floatVal, 0.0f, 2.0f) / 2.0f);
            }

            // Read residual frames
            Steinberg::int32 residualFrameCount = 0;
            Steinberg::int32 analysisFFTSizeInt = 0;
            Steinberg::int32 analysisHopSizeInt = 0;

            if (streamer.readInt32(residualFrameCount) &&
                streamer.readInt32(analysisFFTSizeInt) &&
                streamer.readInt32(analysisHopSizeInt) &&
                residualFrameCount > 0)
            {
                auto analysisFFTSize = static_cast<size_t>(analysisFFTSizeInt);
                auto analysisHopSize = static_cast<size_t>(analysisHopSizeInt);

                // Reconstruct analysis with residual frames
                auto* analysis = currentAnalysis_.load(std::memory_order_acquire);
                auto* newAnalysis = analysis
                    ? new SampleAnalysis(*analysis)   // preserve harmonic data
                    : new SampleAnalysis();

                newAnalysis->analysisFFTSize = analysisFFTSize;
                newAnalysis->analysisHopSize = analysisHopSize;
                newAnalysis->residualFrames.clear();
                newAnalysis->residualFrames.reserve(
                    static_cast<size_t>(residualFrameCount));

                bool readOk = true;
                for (Steinberg::int32 f = 0; f < residualFrameCount && readOk; ++f)
                {
                    Krate::DSP::ResidualFrame frame;

                    // Read 16 band energies
                    for (size_t b = 0; b < Krate::DSP::kResidualBands; ++b)
                    {
                        if (!streamer.readFloat(floatVal))
                        {
                            readOk = false;
                            break;
                        }
                        frame.bandEnergies[b] = std::max(floatVal, 0.0f);
                    }
                    if (!readOk) break;

                    // Read totalEnergy
                    if (!streamer.readFloat(floatVal))
                    {
                        readOk = false;
                        break;
                    }
                    frame.totalEnergy = std::max(floatVal, 0.0f);

                    // Read transientFlag (int8)
                    Steinberg::int8 transientByte = 0;
                    if (!streamer.readInt8(transientByte))
                    {
                        readOk = false;
                        break;
                    }
                    frame.transientFlag = (transientByte != 0);

                    newAnalysis->residualFrames.push_back(frame);
                }

                if (readOk && !newAnalysis->residualFrames.empty())
                {
                    // Prepare residual synth for playback
                    if (analysisFFTSize > 0 && analysisHopSize > 0)
                    {
                        residualSynth_.prepare(
                            analysisFFTSize, analysisHopSize,
                            static_cast<float>(sampleRate_));
                    }

                    // Publish reconstructed analysis
                    auto* old = currentAnalysis_.exchange(
                        newAnalysis, std::memory_order_acq_rel);
                    pendingDeletion_ = old;
                }
                else
                {
                    delete newAnalysis;
                }
            }
        }
        else
        {
            // Version 1: set residual parameters to defaults (normalized)
            harmonicLevel_.store(0.5f);      // plain 1.0
            residualLevel_.store(0.5f);      // plain 1.0
            residualBrightness_.store(0.5f); // plain 0.0 (neutral)
            transientEmphasis_.store(0.0f);  // plain 0.0 (no boost)
        }
    }

    return Steinberg::kResultOk;
}

} // namespace Innexus
