// ==============================================================================
// Audio Processor Implementation
// ==============================================================================
// Split into multiple .cpp files for maintainability:
//   processor.cpp          - Lifecycle, setup, process(), small helpers
//   processor_params.cpp   - processParameterChanges()
//   processor_state.cpp    - getState() / setState()
//   processor_midi.cpp     - MIDI event handling, note on/off, pitch bend
//   processor_messages.cpp - IMessage notify(), sendDisplayData()
// ==============================================================================

#include "processor.h"
#include "dsp/dual_stft_config.h"

#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/midi_utils.h>
#include <krate/dsp/core/pitch_utils.h>
#include <krate/dsp/core/sigmoid.h>

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

    // Sidechain audio input (FR-001: auxiliary stereo bus, NOT default-active)
    // Must be inactive by default so AU wrapper can initialize without inputs.
    // Host activates this bus when user routes audio to the sidechain.
    addAudioInput(
        STR16("Sidechain"),
        Steinberg::Vst::SpeakerArr::kStereo,
        Steinberg::Vst::BusTypes::kAux,
        0 /* not default active */);

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

        // Prepare residual synthesizer.
        // Use sample analysis config if available, otherwise use short STFT config
        // so it's ready for sidechain mode regardless of current input source.
        // Always prepare here (not in process()) to avoid audio-thread allocation (FR-008).
        {
            const SampleAnalysis* analysis =
                currentAnalysis_.load(std::memory_order_acquire);
            const size_t fftSize =
                (analysis && !analysis->residualFrames.empty() &&
                 analysis->analysisFFTSize > 0)
                    ? analysis->analysisFFTSize
                    : kShortWindowConfig.fftSize;
            const size_t hopSize =
                (analysis && !analysis->residualFrames.empty() &&
                 analysis->analysisHopSize > 0)
                    ? analysis->analysisHopSize
                    : kShortWindowConfig.hopSize;
            residualSynth_.prepare(
                fftSize, hopSize, static_cast<float>(sampleRate_));
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

        // Reset manual freeze state (M4)
        manualFreezeActive_ = false;
        manualFreezeRecoverySamplesRemaining_ = 0;
        manualFreezeRecoveryOldLevel_ = 0.0f;
        previousFreezeState_ = freeze_.load(std::memory_order_relaxed) > 0.5f;

        // Spec A: Snap harmonic physics smoothers and reset physics state
        warmthSmoother_.snapTo(warmth_.load(std::memory_order_relaxed));
        couplingSmoother_.snapTo(coupling_.load(std::memory_order_relaxed));
        stabilitySmoother_.snapTo(stability_.load(std::memory_order_relaxed));
        entropySmoother_.snapTo(entropy_.load(std::memory_order_relaxed));
        harmonicPhysics_.reset();

        // Reset sidechain crossfade state
        sourceCrossfadeSamplesRemaining_ = 0;
        sourceCrossfadeOldLevel_ = 0.0f;
        previousInputSource_ =
            inputSource_.load(std::memory_order_relaxed) > 0.5f ? 1 : 0;

        // Spec B FR-005, FR-018: Reset feedback buffer on activation
        feedbackBuffer_.fill(0.0f);
        previousFreezeForFeedback_ = freeze_.load(std::memory_order_relaxed) > 0.5f;
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

    // FR-011: source crossfade length = 20ms in samples
    sourceCrossfadeLengthSamples_ = static_cast<int>(
        0.020 * sampleRate_);
    sourceCrossfadeLengthSamples_ = std::max(sourceCrossfadeLengthSamples_, 1);

    // M4 FR-006: Manual freeze recovery crossfade length = 10ms in samples
    manualFreezeRecoveryLengthSamples_ = static_cast<int>(
        std::round(kManualFreezeRecoveryTimeSec * static_cast<float>(sampleRate_)));
    manualFreezeRecoveryLengthSamples_ = std::max(manualFreezeRecoveryLengthSamples_, 1);

    // M4 FR-017: Configure morph position smoother (7ms time constant)
    morphPositionSmoother_.configure(7.0f, static_cast<float>(sampleRate_));
    morphPositionSmoother_.snapTo(
        morphPosition_.load(std::memory_order_relaxed));

    // M4: Initialize filter mask to all-pass (1.0)
    filterMask_.fill(1.0f);
    currentFilterType_ = 0;

    // M6: Configure stereo/detune smoothers
    const float sr = static_cast<float>(sampleRate_);
    timbralBlendSmoother_.configure(5.0f, sr);
    timbralBlendSmoother_.snapTo(timbralBlend_.load(std::memory_order_relaxed));
    stereoSpreadSmoother_.configure(10.0f, sr);
    stereoSpreadSmoother_.snapTo(stereoSpread_.load(std::memory_order_relaxed));
    evolutionSpeedSmoother_.configure(5.0f, sr);
    evolutionSpeedSmoother_.snapTo(evolutionSpeed_.load(std::memory_order_relaxed));
    evolutionDepthSmoother_.configure(5.0f, sr);
    evolutionDepthSmoother_.snapTo(evolutionDepth_.load(std::memory_order_relaxed));
    mod1RateSmoother_.configure(5.0f, sr);
    mod1RateSmoother_.snapTo(mod1Rate_.load(std::memory_order_relaxed));
    mod1DepthSmoother_.configure(5.0f, sr);
    mod1DepthSmoother_.snapTo(mod1Depth_.load(std::memory_order_relaxed));
    mod2RateSmoother_.configure(5.0f, sr);
    mod2RateSmoother_.snapTo(mod2Rate_.load(std::memory_order_relaxed));
    mod2DepthSmoother_.configure(5.0f, sr);
    mod2DepthSmoother_.snapTo(mod2Depth_.load(std::memory_order_relaxed));
    detuneSpreadSmoother_.configure(5.0f, sr);
    detuneSpreadSmoother_.snapTo(detuneSpread_.load(std::memory_order_relaxed));
    for (auto& smoother : blendWeightSmootherArray_) {
        smoother.configure(5.0f, sr);
        smoother.snapTo(0.0f);
    }

    // Spec A: Configure harmonic physics smoothers and prepare physics processor
    warmthSmoother_.configure(5.0f, sr);
    warmthSmoother_.snapTo(warmth_.load(std::memory_order_relaxed));
    couplingSmoother_.configure(5.0f, sr);
    couplingSmoother_.snapTo(coupling_.load(std::memory_order_relaxed));
    stabilitySmoother_.configure(5.0f, sr);
    stabilitySmoother_.snapTo(stability_.load(std::memory_order_relaxed));
    entropySmoother_.configure(5.0f, sr);
    entropySmoother_.snapTo(entropy_.load(std::memory_order_relaxed));
    harmonicPhysics_.prepare(sampleRate_, 512); // default hop size

    // M6 FR-004, R-004: Construct pure harmonic reference frame
    // relativeFreq[n] = n+1 (1-indexed), rawAmp[n] = 1/(n+1), L2-normalized
    {
        pureHarmonicFrame_ = {};
        pureHarmonicFrame_.numPartials = static_cast<int>(Krate::DSP::kMaxPartials);
        pureHarmonicFrame_.f0 = 1.0f; // Placeholder; actual F0 comes from MIDI note
        pureHarmonicFrame_.f0Confidence = 1.0f;
        pureHarmonicFrame_.globalAmplitude = 1.0f;

        // Build 1/n amplitude series and compute L2 norm
        float sumSquares = 0.0f;
        for (size_t i = 0; i < Krate::DSP::kMaxPartials; ++i)
        {
            const float n = static_cast<float>(i + 1);
            auto& p = pureHarmonicFrame_.partials[i];
            p.harmonicIndex = static_cast<int>(i + 1);
            p.relativeFrequency = n;
            p.amplitude = 1.0f / n; // Raw 1/n amplitude (pre-normalization)
            p.inharmonicDeviation = 0.0f;
            p.stability = 1.0f;
            p.age = 1;
            p.phase = 0.0f;
            p.frequency = n; // Placeholder
            sumSquares += p.amplitude * p.amplitude;
        }

        // L2-normalize amplitudes
        const float l2Norm = std::sqrt(sumSquares);
        if (l2Norm > 0.0f)
        {
            for (size_t i = 0; i < Krate::DSP::kMaxPartials; ++i)
                pureHarmonicFrame_.partials[i].amplitude /= l2Norm;
        }
    }

    // M6 FR-014: Prepare evolution engine
    evolutionEngine_.prepare(sampleRate_);
    evolutionEngine_.updateWaypoints(memorySlots_);

    // M6 FR-024, FR-051: Prepare harmonic modulators (phase init to 0.0)
    mod1_.prepare(sampleRate_);
    mod2_.prepare(sampleRate_);

    // FR-003/FR-005: Prepare live analysis pipeline
    auto currentMode = latencyMode_.load(std::memory_order_relaxed) > 0.5f
        ? LatencyMode::HighPrecision
        : LatencyMode::LowLatency;
    liveAnalysis_.prepare(sampleRate_, currentMode);

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


    // --- Forward responsiveness to live analysis pipeline (FR-030, FR-031) ---
    {
        float resp = responsiveness_.load(std::memory_order_relaxed);
        liveAnalysis_.setResponsiveness(resp);
    }

    // --- Detect input source switch and trigger crossfade (FR-011) ---
    {
        int currentSource = inputSource_.load(std::memory_order_relaxed) > 0.5f ? 1 : 0;
        if (currentSource != previousInputSource_)
        {
            // Source changed -- initiate crossfade
            if (noteActive_) {
                float captL = 0.0f;
                float captR = 0.0f;
                oscillatorBank_.processStereo(captL, captR);
                sourceCrossfadeOldLevel_ = (captL + captR) * 0.5f;
            } else {
                sourceCrossfadeOldLevel_ = 0.0f;
            }
            sourceCrossfadeSamplesRemaining_ = sourceCrossfadeLengthSamples_;
            previousInputSource_ = currentSource;
            // Note: residualSynth_ is always prepared in setActive() for sidechain mode.
            // No prepare() call here to avoid heap allocation on audio thread (FR-008).
        }
    }

    // --- Sidechain downmix to mono (FR-001, R-002) ---
    const float* sidechainMono = nullptr;
    {
        int currentSource = inputSource_.load(std::memory_order_relaxed) > 0.5f ? 1 : 0;
        if (currentSource == 1 && data.numInputs > 0 && data.inputs)
        {
            const auto& scBus = data.inputs[0];
            if (scBus.numChannels >= 2 &&
                scBus.channelBuffers32 &&
                scBus.channelBuffers32[0] && scBus.channelBuffers32[1])
            {
                // Stereo downmix
                auto count = std::min(data.numSamples,
                    static_cast<Steinberg::int32>(sidechainBuffer_.size()));
                for (Steinberg::int32 s = 0; s < count; ++s)
                {
                    sidechainBuffer_[static_cast<size_t>(s)] =
                        (scBus.channelBuffers32[0][s] +
                         scBus.channelBuffers32[1][s]) * 0.5f;
                }
                sidechainMono = sidechainBuffer_.data();
            }
            else if (scBus.numChannels == 1 &&
                     scBus.channelBuffers32 && scBus.channelBuffers32[0])
            {
                sidechainMono = scBus.channelBuffers32[0];
            }
        }
    }
    // Feed sidechain audio to live analysis pipeline (FR-003, FR-009)
    {
        int currentSource = inputSource_.load(std::memory_order_relaxed) > 0.5f ? 1 : 0;
        if (currentSource == 1 && sidechainMono != nullptr)
        {
            // Spec B FR-001, FR-003, FR-009, FR-014, FR-015: Mix feedback into
            // sidechain input before pushing to analysis pipeline.
            const float fbAmount = feedbackAmount_.load(std::memory_order_relaxed);
            const bool manualFrozen = freeze_.load(std::memory_order_relaxed) > 0.5f;

            if (fbAmount > 0.0f && !manualFrozen)
            {
                // If sidechainMono points to raw bus data (mono case), copy to
                // sidechainBuffer_ first so we can modify in-place.
                if (sidechainMono != sidechainBuffer_.data())
                {
                    auto count = std::min(data.numSamples,
                        static_cast<Steinberg::int32>(sidechainBuffer_.size()));
                    std::memcpy(sidechainBuffer_.data(), sidechainMono,
                        static_cast<size_t>(count) * sizeof(float));
                    sidechainMono = sidechainBuffer_.data();
                }

                // FR-001, FR-009: Per-sample soft-limited feedback mixing
                auto count = std::min(data.numSamples,
                    static_cast<Steinberg::int32>(sidechainBuffer_.size()));
                for (Steinberg::int32 s = 0; s < count; ++s)
                {
                    const float fbSample = std::tanh(
                        feedbackBuffer_[static_cast<size_t>(s)]
                        * fbAmount * 2.0f) * 0.5f;
                    sidechainBuffer_[static_cast<size_t>(s)] =
                        sidechainBuffer_[static_cast<size_t>(s)]
                        * (1.0f - fbAmount) + fbSample;
                }
            }

            // T091: Skip spectral coring when residual level is zero (~10% CPU reduction)
            const bool residualActive =
                residualLevel_.load(std::memory_order_relaxed) > 0.0f;
            liveAnalysis_.setResidualEnabled(residualActive);

            liveAnalysis_.pushSamples(
                sidechainMono,
                static_cast<size_t>(data.numSamples));

            // Consume new frames if available
            if (liveAnalysis_.hasNewFrame())
            {
                currentLiveFrame_ = liveAnalysis_.consumeFrame();
                currentLiveResidualFrame_ = liveAnalysis_.consumeResidualFrame();
            }
        }
    }

    // --- Read current analysis with acquire semantics (FR-058) ---
    const SampleAnalysis* analysis =
        currentAnalysis_.load(std::memory_order_acquire);

    // --- Process MIDI events ---
    processEvents(data.inputEvents);

    // --- Output ---
    if (data.numOutputs < 1 || !data.outputs)
        return Steinberg::kResultOk;
    const int numOutputChannels = data.outputs[0].numChannels;
    if (numOutputChannels < 1)
        return Steinberg::kResultOk;

    auto numSamples = data.numSamples;
    if (numSamples <= 0)
        return Steinberg::kResultOk;

    auto** out = data.outputs[0].channelBuffers32;
    if (!out || !out[0] || (numOutputChannels >= 2 && !out[1]))
        return Steinberg::kResultOk;


    // Determine current input source
    const int currentInputSource =
        inputSource_.load(std::memory_order_relaxed) > 0.5f ? 1 : 0;
    const bool isSidechainMode = (currentInputSource == 1);

    // FR-055: If no analysis loaded (sample mode) or no note active, output silence
    // In sidechain mode, we can synthesize even without a loaded sample analysis
    // as long as a note is active and the live pipeline has produced a frame.
    const bool hasSampleAnalysis = analysis && !analysis->frames.empty();
    const bool hasLiveFrame = isSidechainMode && currentLiveFrame_.f0 > 0.0f;


    // =========================================================================
    // M5: Recall Trigger Detection (FR-011, FR-012, FR-013, FR-015)
    // Fires at block start, BEFORE capture detection (T057).
    // Must run before the early-return so recall works even with no note/analysis.
    // =========================================================================
    {
        const float currentRecallTrigger =
            memoryRecall_.load(std::memory_order_relaxed);

        if (currentRecallTrigger > 0.5f && previousRecallTrigger_ <= 0.5f)
        {
            // Read selected slot index
            const int slot = std::clamp(
                static_cast<int>(std::round(
                    memorySlot_.load(std::memory_order_relaxed) * 7.0f)),
                0, 7);

            // FR-013: Silently ignore recall on empty slot
            if (memorySlots_[static_cast<size_t>(slot)].occupied)
            {
                // FR-012: Reconstruct frame from snapshot
                Krate::DSP::HarmonicFrame tempHarmonicFrame{};
                Krate::DSP::ResidualFrame tempResidualFrame{};
                Krate::DSP::recallSnapshotToFrame(
                    memorySlots_[static_cast<size_t>(slot)].snapshot,
                    tempHarmonicFrame, tempResidualFrame);

                // FR-015: If already frozen (slot-to-slot recall), initiate crossfade
                if (manualFreezeActive_)
                {
                    if (noteActive_) {
                        float captL = 0.0f;
                        float captR = 0.0f;
                        oscillatorBank_.processStereo(captL, captR);
                        manualFreezeRecoveryOldLevel_ = (captL + captR) * 0.5f;
                    } else {
                        manualFreezeRecoveryOldLevel_ = 0.0f;
                    }
                    manualFreezeRecoverySamplesRemaining_ =
                        manualFreezeRecoveryLengthSamples_;
                }

                // FR-012: Load into freeze frame and engage freeze
                manualFrozenFrame_ = tempHarmonicFrame;
                manualFrozenResidualFrame_ = tempResidualFrame;
                manualFreezeActive_ = true;

                // Sync freeze state so the M4 freeze detection block
                // correctly handles disengage when the user toggles
                // the Freeze parameter off (FR-018).
                freeze_.store(1.0f, std::memory_order_relaxed);
                previousFreezeState_ = true;
            }

            // FR-011: Auto-reset trigger
            memoryRecall_.store(0.0f, std::memory_order_relaxed);

            // Notify host UI of the auto-reset (T059b)
            if (data.outputParameterChanges)
            {
                Steinberg::int32 index = 0;
                auto* queue = data.outputParameterChanges->addParameterData(
                    kMemoryRecallId, index);
                if (queue)
                    queue->addPoint(0, 0.0, index);
            }
        }

        previousRecallTrigger_ = currentRecallTrigger;
    }


    // =========================================================================
    // M5: Capture Trigger Detection (FR-006, FR-007, FR-008, FR-009)
    // Fires at block start, BEFORE any filter application (pre-filter capture).
    // Must run before the early-return so capture works even with no note/analysis.
    // =========================================================================
    {
        const float currentCaptureTrigger =
            memoryCapture_.load(std::memory_order_relaxed);

        if (currentCaptureTrigger > 0.5f && previousCaptureTrigger_ <= 0.5f)
        {
            // Determine capture source (FR-007)
            Krate::DSP::HarmonicFrame captureFrame{};
            Krate::DSP::ResidualFrame captureResidual{};

            const float smoothedMorph =
                morphPositionSmoother_.getCurrentValue();

            if (manualFreezeActive_ && smoothedMorph > 1e-6f)
            { // NOLINT(bugprone-branch-clone) branches assign from different sources
                // (a) Freeze active + morph > 0: capture post-morph blended state (FR-008)
                // morphedFrame_ contains the last-computed pre-filter morph result
                captureFrame = morphedFrame_;
                captureResidual = morphedResidualFrame_;
            }
            else if (manualFreezeActive_)
            {
                // (b) Freeze active + morph == 0: capture frozen frame
                captureFrame = manualFrozenFrame_;
                captureResidual = manualFrozenResidualFrame_;
            }
            else if (isSidechainMode)
            {
                // (c) Sidechain mode, no freeze: capture live analysis frame
                captureFrame = currentLiveFrame_;
                captureResidual = currentLiveResidualFrame_;
            }
            else if (hasSampleAnalysis)
            {
                // (d) Sample mode, no freeze: capture current sample frame
                captureFrame = analysis->getFrame(currentFrameIndex_);
                if (!analysis->residualFrames.empty())
                    captureResidual =
                        analysis->getResidualFrame(currentFrameIndex_);
            }
            // else: (e) No analysis -- captureFrame/captureResidual are default (empty)

            // Store snapshot in selected slot (FR-010)
            const int slot = std::clamp(
                static_cast<int>(std::round(
                    memorySlot_.load(std::memory_order_relaxed) * 7.0f)),
                0, 7);
            memorySlots_[static_cast<size_t>(slot)].snapshot =
                Krate::DSP::captureSnapshot(captureFrame, captureResidual);
            memorySlots_[static_cast<size_t>(slot)].occupied = true;

            // M6: Update evolution waypoints after capture (FR-018)
            evolutionEngine_.updateWaypoints(memorySlots_);

            // Auto-reset trigger (FR-006)
            memoryCapture_.store(0.0f, std::memory_order_relaxed);

            // Notify host UI of the auto-reset so the Capture button
            // reflects the reset state (T042b)
            if (data.outputParameterChanges)
            {
                Steinberg::int32 index = 0;
                auto* queue = data.outputParameterChanges->addParameterData(
                    kMemoryCaptureId, index);
                if (queue)
                    queue->addPoint(0, 0.0, index);
            }
        }

        previousCaptureTrigger_ = currentCaptureTrigger;
    }

    // M4/T108: Freeze transition detection must happen BEFORE the early return
    // so that engaging freeze with no analysis still captures an empty frame.
    // This block runs only when the early return would fire; the main freeze
    // detection block handles the normal (has-analysis) case.
    if (!noteActive_ || (!hasSampleAnalysis && !hasLiveFrame))
    {
        const bool currentFreezeState = freeze_.load(std::memory_order_relaxed) > 0.5f;

        if (currentFreezeState && !previousFreezeState_)
        {
            // Capture default-constructed empty frames
            manualFrozenFrame_ = {};
            manualFrozenResidualFrame_ = {};
            manualFreezeActive_ = true;
        }

        if (!currentFreezeState && previousFreezeState_)
        {
            manualFreezeRecoverySamplesRemaining_ = manualFreezeRecoveryLengthSamples_;
            manualFreezeActive_ = false;

            // Spec B FR-016: Clear feedback buffer on freeze disengage
            feedbackBuffer_.fill(0.0f);
        }

        previousFreezeState_ = currentFreezeState;
    }

    if (!noteActive_ || (!hasSampleAnalysis && !hasLiveFrame))
    {
        for (Steinberg::int32 s = 0; s < numSamples; ++s)
        {
            out[0][s] = 0.0f;
            if (numOutputChannels >= 2) out[1][s] = 0.0f;
        }
        data.outputs[0].silenceFlags = (numOutputChannels >= 2) ? 0x3 : 0x1;
        return Steinberg::kResultOk;
    }

    // --- Update inharmonicity from parameter ---
    float inharm = inharmonicityAmount_.load(std::memory_order_relaxed);
    oscillatorBank_.setInharmonicityAmount(inharm);

    // --- Compute hop size in samples for frame advancement ---
    // Guard: analysis may be nullptr in sidechain mode (no sample loaded)
    const size_t hopSizeInSamples = analysis
        ? static_cast<size_t>(
            analysis->hopTimeSec * static_cast<float>(sampleRate_) + 0.5f)
        : 0;
    const size_t totalFrames = analysis ? analysis->totalFrames : 0;

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
    const bool hasSampleResidual = hasSampleAnalysis &&
                                    !analysis->residualFrames.empty() &&
                                    residualSynth_.isPrepared();

    // In sidechain mode, live residual is always available if the pipeline produces it
    const bool hasLiveResidual = isSidechainMode && residualSynth_.isPrepared();


    // =========================================================================
    // M4: Manual Freeze Detection (FR-001, FR-002, FR-003, FR-007, FR-008)
    // =========================================================================
    {
        const bool currentFreezeState = freeze_.load(std::memory_order_relaxed) > 0.5f;

        // Detect freeze engagement transition (off -> on)
        if (currentFreezeState && !previousFreezeState_)
        {
            // FR-002, FR-003: Capture current live frame as frozen state
            // In sidechain mode, use currentLiveFrame_/currentLiveResidualFrame_
            // In sample mode, use the current frame from analysis
            if (isSidechainMode)
            { // NOLINT(bugprone-branch-clone) branches assign from different sources
                manualFrozenFrame_ = currentLiveFrame_;
                manualFrozenResidualFrame_ = currentLiveResidualFrame_;
            }
            else if (hasSampleAnalysis)
            {
                manualFrozenFrame_ = analysis->getFrame(currentFrameIndex_);
                if (!analysis->residualFrames.empty())
                    manualFrozenResidualFrame_ =
                        analysis->getResidualFrame(currentFrameIndex_);
                else
                    manualFrozenResidualFrame_ = {};
            }
            else
            {
                // No analysis: capture empty/silent frames
                manualFrozenFrame_ = {};
                manualFrozenResidualFrame_ = {};
            }
            manualFreezeActive_ = true;
        }

        // Detect freeze disengage transition (on -> off)
        if (!currentFreezeState && previousFreezeState_)
        {
            // FR-006: Initiate 10ms crossfade from frozen to live
            // Capture current oscillator output level for smooth crossfade
            if (noteActive_) {
                float captL = 0.0f;
                float captR = 0.0f;
                oscillatorBank_.processStereo(captL, captR);
                manualFreezeRecoveryOldLevel_ = (captL + captR) * 0.5f;
            } else {
                manualFreezeRecoveryOldLevel_ = 0.0f;
            }
            manualFreezeRecoverySamplesRemaining_ = manualFreezeRecoveryLengthSamples_;
            manualFreezeActive_ = false;

            // Spec B FR-016: Clear feedback buffer on freeze disengage
            feedbackBuffer_.fill(0.0f);
        }

        previousFreezeState_ = currentFreezeState;
    }

    // =========================================================================
    // M4: Harmonic Filter mask recomputation (FR-019 to FR-025, T081)
    // Recompute when filter type changes; mask is applied per-frame below.
    // =========================================================================
    {
        const float filterNorm = harmonicFilterType_.load(std::memory_order_relaxed);
        const int newFilterType = std::clamp(
            static_cast<int>(std::round(filterNorm * 4.0f)), 0, 4);

        if (newFilterType != currentFilterType_)
        {
            // Compute mask using a standard partials array with harmonicIndex = i+1.
            // Since analysis partials always follow this convention, the mask is
            // effectively a function of filterType alone and can be precomputed
            // independently of the current frame.
            std::array<Krate::DSP::Partial, Krate::DSP::kMaxPartials> stdPartials{};
            for (size_t i = 0; i < Krate::DSP::kMaxPartials; ++i)
                stdPartials[i].harmonicIndex = static_cast<int>(i) + 1;

            Krate::DSP::computeHarmonicMask(
                newFilterType, stdPartials,
                static_cast<int>(Krate::DSP::kMaxPartials), filterMask_);
            currentFilterType_ = newFilterType;
        }
    }


    // --- Spec A: Update harmonic physics smoothers ---
    warmthSmoother_.setTarget(warmth_.load(std::memory_order_relaxed));
    warmthSmoother_.advanceSamples(
        static_cast<size_t>(numSamples > 0 ? numSamples : 1));
    couplingSmoother_.setTarget(coupling_.load(std::memory_order_relaxed));
    couplingSmoother_.advanceSamples(
        static_cast<size_t>(numSamples > 0 ? numSamples : 1));
    stabilitySmoother_.setTarget(stability_.load(std::memory_order_relaxed));
    stabilitySmoother_.advanceSamples(
        static_cast<size_t>(numSamples > 0 ? numSamples : 1));
    entropySmoother_.setTarget(entropy_.load(std::memory_order_relaxed));
    entropySmoother_.advanceSamples(
        static_cast<size_t>(numSamples > 0 ? numSamples : 1));

    // --- M6: Update timbral blend smoother (FR-001, FR-005) ---
    timbralBlendSmoother_.setTarget(timbralBlend_.load(std::memory_order_relaxed));
    timbralBlendSmoother_.advanceSamples(
        static_cast<size_t>(numSamples > 0 ? numSamples : 1));
    const float smoothedTimbralBlend = timbralBlendSmoother_.getCurrentValue();

    // M6: Read modulator enable flags (used in frame loading and per-sample loop)
    const bool mod1Enabled = mod1Enable_.load(std::memory_order_relaxed) > 0.5f;
    const bool mod2Enabled = mod2Enable_.load(std::memory_order_relaxed) > 0.5f;

    // =========================================================================
    // M4: Morph interpolation + load frames into osc bank
    // When freeze active: morph between frozen (A) and live (B) frames
    // When freeze inactive: pass-through live frame (FR-016)
    // (FR-004, FR-005, FR-007, FR-010 to FR-018)
    // =========================================================================
    if (manualFreezeActive_ && noteActive_)
    {
        // Update morph position smoother (FR-017)
        morphPositionSmoother_.setTarget(
            morphPosition_.load(std::memory_order_relaxed));

        // Determine which live frame to use as State B
        Krate::DSP::HarmonicFrame liveFrame{};
        Krate::DSP::ResidualFrame liveResidualFrame{};
        if (isSidechainMode)
        {
            liveFrame = currentLiveFrame_;
            liveResidualFrame = currentLiveResidualFrame_;
        }
        else if (hasSampleAnalysis)
        {
            liveFrame = analysis->getFrame(currentFrameIndex_);
            if (!analysis->residualFrames.empty())
                liveResidualFrame = analysis->getResidualFrame(currentFrameIndex_);
        }

        // Advance smoother by block size to get smoothed value (FR-017)
        // Use advanceSamples for correct convergence at block-rate processing
        morphPositionSmoother_.advanceSamples(
            static_cast<size_t>(numSamples > 0 ? numSamples : 1));
        const float smoothedMorph = morphPositionSmoother_.getCurrentValue();

        // Morph with early-out optimization (T061)
        if (smoothedMorph < 1e-6f)
        { // NOLINT(bugprone-branch-clone) branches assign from different sources
            // Fully frozen (morph = 0.0)
            morphedFrame_ = manualFrozenFrame_;
            morphedResidualFrame_ = manualFrozenResidualFrame_;
        }
        else if (smoothedMorph > 1.0f - 1e-6f)
        {
            // Fully live (morph = 1.0)
            morphedFrame_ = liveFrame;
            morphedResidualFrame_ = liveResidualFrame;
        }
        else
        {
            // Interpolate between frozen (A) and live (B)
            morphedFrame_ = Krate::DSP::lerpHarmonicFrame(
                manualFrozenFrame_, liveFrame, smoothedMorph);
            morphedResidualFrame_ = Krate::DSP::lerpResidualFrame(
                manualFrozenResidualFrame_, liveResidualFrame, smoothedMorph);
        }

        // M6 FR-001, FR-002: Apply timbral blend (cross-synthesis)
        // Blend between pure harmonic reference and source model BEFORE filter.
        if (smoothedTimbralBlend < 1.0f - 1e-6f)
            morphedFrame_ = Krate::DSP::lerpHarmonicFrame(
                pureHarmonicFrame_, morphedFrame_, smoothedTimbralBlend);

        // M4: Apply harmonic filter mask (FR-020, FR-026, T082)
        // Filter is applied AFTER morph+blend and BEFORE oscillator bank.
        // Residual passes through unmodified (FR-027).
        if (currentFilterType_ != 0) // Skip for All-Pass (early-out)
            Krate::DSP::applyHarmonicMask(morphedFrame_, filterMask_);

        // Load morphed+filtered frame into oscillator bank
        // M6 FR-025: Apply modulator amplitude modulation before loading
        applyModulatorAmplitude(mod1Enabled, mod2Enabled);
        applyHarmonicPhysics();
        float basePitch = Krate::DSP::midiNoteToFrequency(currentMidiNote_);
        float bendRatio = Krate::DSP::semitonesToRatio(pitchBendSemitones_);
        float targetPitch = basePitch * bendRatio;
        oscillatorBank_.loadFrame(morphedFrame_, targetPitch);

        if (residualSynth_.isPrepared())
        {
            residualSynth_.loadFrame(
                morphedResidualFrame_,
                brightnessSmoother_.getCurrentValue(),
                transientEmphasisSmoother_.getCurrentValue());
        }
    }

    // For sidechain mode: if a new live frame arrived this block, load it into
    // the oscillator bank and residual synth. This happens once per process() call
    // (not per-sample) since the live pipeline produces frames at STFT hop rate.
    // FR-007: Skip when manual freeze is active (manual takes priority)
    if (!manualFreezeActive_ && isSidechainMode && currentLiveFrame_.f0 > 0.0f)
    {
        // Apply confidence-gated freeze (FR-010, FR-052) to live frames
        float recoveryThreshold = isFrozen_
            ? (kConfidenceThreshold + kConfidenceHysteresis)
            : kConfidenceThreshold;

        if (currentLiveFrame_.f0Confidence >= recoveryThreshold)
        {
            if (isFrozen_)
            {
                isFrozen_ = false;
                float captL = 0.0f;
                float captR = 0.0f;
                oscillatorBank_.processStereo(captL, captR);
                freezeRecoveryOldLevel_ = (captL + captR) * 0.5f;
                freezeRecoverySamplesRemaining_ = freezeRecoveryLengthSamples_;
            }
            lastGoodFrame_ = currentLiveFrame_;

            // M4: Store live frame as morphed (no freeze = pass-through)
            morphedFrame_ = currentLiveFrame_;
            morphedResidualFrame_ = currentLiveResidualFrame_;

            // M6 FR-001, FR-002: Apply timbral blend (cross-synthesis)
            if (smoothedTimbralBlend < 1.0f - 1e-6f)
                morphedFrame_ = Krate::DSP::lerpHarmonicFrame(
                    pureHarmonicFrame_, morphedFrame_, smoothedTimbralBlend);

            // Apply harmonic filter (FR-026, FR-027)
            if (currentFilterType_ != 0)
                Krate::DSP::applyHarmonicMask(morphedFrame_, filterMask_);

            // M6 FR-025: Apply modulator amplitude modulation
            applyModulatorAmplitude(mod1Enabled, mod2Enabled);
            applyHarmonicPhysics();

            // Calculate target pitch with pitch bend
            float basePitch = Krate::DSP::midiNoteToFrequency(currentMidiNote_);
            float bendRatio = Krate::DSP::semitonesToRatio(pitchBendSemitones_);
            float targetPitch = basePitch * bendRatio;
            oscillatorBank_.loadFrame(morphedFrame_, targetPitch);

            // Load live residual frame (FR-016: same controls as sample mode)
            if (residualSynth_.isPrepared())
            {
                residualSynth_.loadFrame(
                    morphedResidualFrame_,
                    brightnessSmoother_.getCurrentValue(),
                    transientEmphasisSmoother_.getCurrentValue());
            }
        }
        else
        {
            // FR-052: Hold last known-good frame
            isFrozen_ = true;
        }
    }


    // =========================================================================
    // M6: Evolution Engine (FR-014 to FR-023)
    // When evolution is enabled AND blend is NOT enabled, the evolution engine
    // overrides the normal morph/freeze frame selection path (FR-022).
    // =========================================================================
    const bool evolutionEnabled =
        evolutionEnable_.load(std::memory_order_relaxed) > 0.5f;
    const bool blendEnabled =
        blendEnable_.load(std::memory_order_relaxed) > 0.5f;

    // Update evolution engine parameters from smoothers (FR-023)
    if (evolutionEnabled)
    {
        // Speed: denormalize from [0,1] to [0.01, 10.0] Hz
        const float speedNorm = evolutionSpeed_.load(std::memory_order_relaxed);
        const float speedPlain = 0.01f + speedNorm * (10.0f - 0.01f);
        evolutionSpeedSmoother_.setTarget(speedPlain);

        const float depthVal = evolutionDepth_.load(std::memory_order_relaxed);
        evolutionDepthSmoother_.setTarget(depthVal);

        const float modeNorm = evolutionMode_.load(std::memory_order_relaxed);
        const int modeInt = std::clamp(static_cast<int>(std::round(modeNorm * 2.0f)), 0, 2);
        evolutionEngine_.setMode(static_cast<EvolutionMode>(modeInt));

        // Manual offset: use morphPosition as the offset (FR-021)
        const float morphPos = morphPosition_.load(std::memory_order_relaxed);
        evolutionEngine_.setManualOffset(morphPos);
    }

    // --- M6: Update stereo spread and detune spread from smoothers ---
    stereoSpreadSmoother_.setTarget(stereoSpread_.load(std::memory_order_relaxed));
    detuneSpreadSmoother_.setTarget(detuneSpread_.load(std::memory_order_relaxed));

    // --- M6: Update modulator parameters (FR-024, FR-033) ---
    if (mod1Enabled)
    {
        const float waveNorm = mod1Waveform_.load(std::memory_order_relaxed);
        mod1_.setWaveform(static_cast<ModulatorWaveform>(
            std::clamp(static_cast<int>(std::round(waveNorm * 4.0f)), 0, 4)));

        const float rateNorm = mod1Rate_.load(std::memory_order_relaxed);
        const float ratePlain = 0.01f + rateNorm * (20.0f - 0.01f);
        mod1RateSmoother_.setTarget(ratePlain);

        const float depthVal = mod1Depth_.load(std::memory_order_relaxed);
        mod1DepthSmoother_.setTarget(depthVal);

        const float rangeStartNorm = mod1RangeStart_.load(std::memory_order_relaxed);
        const float rangeEndNorm = mod1RangeEnd_.load(std::memory_order_relaxed);
        const int rangeStart = 1 + static_cast<int>(std::round(rangeStartNorm * 47.0f));
        const int rangeEnd = 1 + static_cast<int>(std::round(rangeEndNorm * 47.0f));
        mod1_.setRange(rangeStart, rangeEnd);

        const float targetNorm = mod1Target_.load(std::memory_order_relaxed);
        mod1_.setTarget(static_cast<ModulatorTarget>(
            std::clamp(static_cast<int>(std::round(targetNorm * 2.0f)), 0, 2)));
    }

    if (mod2Enabled)
    {
        const float waveNorm = mod2Waveform_.load(std::memory_order_relaxed);
        mod2_.setWaveform(static_cast<ModulatorWaveform>(
            std::clamp(static_cast<int>(std::round(waveNorm * 4.0f)), 0, 4)));

        const float rateNorm = mod2Rate_.load(std::memory_order_relaxed);
        const float ratePlain = 0.01f + rateNorm * (20.0f - 0.01f);
        mod2RateSmoother_.setTarget(ratePlain);

        const float depthVal = mod2Depth_.load(std::memory_order_relaxed);
        mod2DepthSmoother_.setTarget(depthVal);

        const float rangeStartNorm = mod2RangeStart_.load(std::memory_order_relaxed);
        const float rangeEndNorm = mod2RangeEnd_.load(std::memory_order_relaxed);
        const int rangeStart = 1 + static_cast<int>(std::round(rangeStartNorm * 47.0f));
        const int rangeEnd = 1 + static_cast<int>(std::round(rangeEndNorm * 47.0f));
        mod2_.setRange(rangeStart, rangeEnd);

        const float targetNorm = mod2Target_.load(std::memory_order_relaxed);
        mod2_.setTarget(static_cast<ModulatorTarget>(
            std::clamp(static_cast<int>(std::round(targetNorm * 2.0f)), 0, 2)));
    }

    // --- Process each sample ---
    bool hasSoundOutput = false;
    const bool hasResidual = isSidechainMode ? hasLiveResidual : hasSampleResidual;

    for (Steinberg::int32 s = 0; s < numSamples; ++s)
    {
        // --- Frame advancement (FR-047) -- only in sample mode ---
        // FR-007: Skip frame advancement when manual freeze is active
        if (!manualFreezeActive_ && !isSidechainMode && hopSizeInSamples > 0 && totalFrames > 0)
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
                float recoveryThreshold = isFrozen_
                    ? (kConfidenceThreshold + kConfidenceHysteresis)
                    : kConfidenceThreshold;

                if (frame.f0Confidence >= recoveryThreshold)
                {
                    if (isFrozen_)
                    {
                        isFrozen_ = false;
                        float captL = 0.0f;
                        float captR = 0.0f;
                        oscillatorBank_.processStereo(captL, captR);
                        freezeRecoveryOldLevel_ = (captL + captR) * 0.5f;
                        freezeRecoverySamplesRemaining_ = freezeRecoveryLengthSamples_;
                    }
                    lastGoodFrame_ = frame;

                    // M4: Store frame as morphed (no freeze = pass-through)
                    morphedFrame_ = frame;

                    // M6 FR-001, FR-002: Apply timbral blend (cross-synthesis)
                    if (smoothedTimbralBlend < 1.0f - 1e-6f)
                        morphedFrame_ = Krate::DSP::lerpHarmonicFrame(
                            pureHarmonicFrame_, morphedFrame_, smoothedTimbralBlend);

                    // Apply harmonic filter (FR-026)
                    if (currentFilterType_ != 0)
                        Krate::DSP::applyHarmonicMask(morphedFrame_, filterMask_);

                    // M6 FR-025: Apply modulator amplitude modulation
                    applyModulatorAmplitude(mod1Enabled, mod2Enabled);
                    applyHarmonicPhysics();

                    float basePitch = Krate::DSP::midiNoteToFrequency(currentMidiNote_);
                    float bendRatio = Krate::DSP::semitonesToRatio(pitchBendSemitones_);
                    float targetPitch = basePitch * bendRatio;
                    oscillatorBank_.loadFrame(morphedFrame_, targetPitch);

                    if (hasSampleResidual)
                    {
                        morphedResidualFrame_ =
                            analysis->getResidualFrame(currentFrameIndex_);
                        residualSynth_.loadFrame(
                            morphedResidualFrame_,
                            brightnessSmoother_.getCurrentValue(),
                            transientEmphasisSmoother_.getCurrentValue());
                    }
                }
                else
                {
                    isFrozen_ = true;
                }
            }
        }

        // M6: Evolution Engine per-sample advance (FR-014, FR-022)
        if (evolutionEnabled && !blendEnabled)
        {
            // Advance smoothers per sample
            float evoSpeed = evolutionSpeedSmoother_.process();
            float evoDepth = evolutionDepthSmoother_.process();
            evolutionEngine_.setSpeed(evoSpeed);
            evolutionEngine_.setDepth(evoDepth);

            evolutionEngine_.advance();

            // Load evolution-interpolated frame at each frame boundary
            // (or on first sample of block if evolution just enabled)
            // For simplicity, we load every sample (loadFrame is cheap compared to processing)
            Krate::DSP::HarmonicFrame evoFrame{};
            Krate::DSP::ResidualFrame evoResidual{};
            if (evolutionEngine_.getInterpolatedFrame(memorySlots_, evoFrame, evoResidual))
            {
                morphedFrame_ = evoFrame;
                morphedResidualFrame_ = evoResidual;

                // Apply timbral blend (cross-synthesis) to evolution output
                if (smoothedTimbralBlend < 1.0f - 1e-6f)
                    morphedFrame_ = Krate::DSP::lerpHarmonicFrame(
                        pureHarmonicFrame_, morphedFrame_, smoothedTimbralBlend);

                // Apply harmonic filter
                if (currentFilterType_ != 0)
                    Krate::DSP::applyHarmonicMask(morphedFrame_, filterMask_);

                // M6 FR-025: Apply modulator amplitude modulation
                applyModulatorAmplitude(mod1Enabled, mod2Enabled);
                applyHarmonicPhysics();

                float basePitch = Krate::DSP::midiNoteToFrequency(currentMidiNote_);
                float bendRatio = Krate::DSP::semitonesToRatio(pitchBendSemitones_);
                float targetPitch = basePitch * bendRatio;
                oscillatorBank_.loadFrame(morphedFrame_, targetPitch);

                if (residualSynth_.isPrepared())
                {
                    residualSynth_.loadFrame(
                        morphedResidualFrame_,
                        brightnessSmoother_.getCurrentValue(),
                        transientEmphasisSmoother_.getCurrentValue());
                }
            }
        }
        else
        {
            // Advance smoothers regardless to keep them tracking
            (void)evolutionSpeedSmoother_.process();
            (void)evolutionDepthSmoother_.process();
            if (evolutionEnabled)
            {
                // Evolution is enabled but blend overrides it (FR-052)
                // Still advance the engine to keep phase continuous
                evolutionEngine_.advance();
            }
        }

        // M6: Multi-Source Blending (FR-034 to FR-042, FR-052)
        // When blend is enabled, blender produces currentFrame, overriding
        // both the normal recall/freeze path and the evolution path.
        if (blendEnabled)
        {
            // Update blend weights from smoothers (FR-041)
            for (int i = 0; i < 8; ++i)
            {
                blendWeightSmootherArray_[static_cast<size_t>(i)].setTarget(
                    blendSlotWeights_[static_cast<size_t>(i)].load(std::memory_order_relaxed));
                harmonicBlender_.setSlotWeight(
                    i, blendWeightSmootherArray_[static_cast<size_t>(i)].process());
            }
            blendWeightSmootherArray_[8].setTarget(
                blendLiveWeight_.load(std::memory_order_relaxed));
            harmonicBlender_.setLiveWeight(blendWeightSmootherArray_[8].process());

            // Determine if live source is available
            bool hasLiveSource = isSidechainMode &&
                currentLiveFrame_.f0Confidence > 0.0f;

            Krate::DSP::HarmonicFrame blendFrame{};
            Krate::DSP::ResidualFrame blendResidual{};
            if (harmonicBlender_.blend(memorySlots_,
                    currentLiveFrame_, currentLiveResidualFrame_,
                    hasLiveSource, blendFrame, blendResidual))
            {
                morphedFrame_ = blendFrame;
                morphedResidualFrame_ = blendResidual;

                // Apply timbral blend (cross-synthesis) to blended output
                if (smoothedTimbralBlend < 1.0f - 1e-6f)
                    morphedFrame_ = Krate::DSP::lerpHarmonicFrame(
                        pureHarmonicFrame_, morphedFrame_, smoothedTimbralBlend);

                // Apply harmonic filter
                if (currentFilterType_ != 0)
                    Krate::DSP::applyHarmonicMask(morphedFrame_, filterMask_);

                // Apply modulator amplitude modulation
                applyModulatorAmplitude(mod1Enabled, mod2Enabled);
                applyHarmonicPhysics();

                float basePitch = Krate::DSP::midiNoteToFrequency(currentMidiNote_);
                float bendRatio = Krate::DSP::semitonesToRatio(pitchBendSemitones_);
                float targetPitch = basePitch * bendRatio;
                oscillatorBank_.loadFrame(morphedFrame_, targetPitch);

                if (residualSynth_.isPrepared())
                {
                    residualSynth_.loadFrame(
                        morphedResidualFrame_,
                        brightnessSmoother_.getCurrentValue(),
                        transientEmphasisSmoother_.getCurrentValue());
                }
            }
        }
        else
        {
            // Blend disabled: advance smoothers to keep tracking
            for (int i = 0; i < 8; ++i)
            {
                blendWeightSmootherArray_[static_cast<size_t>(i)].setTarget(
                    blendSlotWeights_[static_cast<size_t>(i)].load(std::memory_order_relaxed));
                (void)blendWeightSmootherArray_[static_cast<size_t>(i)].process();
            }
            blendWeightSmootherArray_[8].setTarget(
                blendLiveWeight_.load(std::memory_order_relaxed));
            (void)blendWeightSmootherArray_[8].process();
        }

        // M6: Update stereo spread and detune per sample (smoothed)
        oscillatorBank_.setStereoSpread(stereoSpreadSmoother_.process());
        oscillatorBank_.setDetuneSpread(detuneSpreadSmoother_.process());

        // M6: Advance harmonic modulators per sample (FR-029: free-running)
        // Apply smoothed rate and depth each sample (FR-033)
        if (mod1Enabled)
        {
            mod1_.setRate(mod1RateSmoother_.process());
            mod1_.setDepth(mod1DepthSmoother_.process());
            mod1_.advance();

            // Apply frequency multipliers on top of detune (FR-026, FR-028)
            {
                std::array<float, Krate::DSP::kMaxPartials> mult1{};
                mod1_.getFrequencyMultipliers(mult1);
                oscillatorBank_.applyExternalFrequencyMultipliers(mult1);
            }

            // Apply pan offsets (FR-027, FR-028)
            {
                std::array<float, Krate::DSP::kMaxPartials> panOff1{};
                mod1_.getPanOffsets(panOff1);
                oscillatorBank_.applyPanOffsets(panOff1);
            }
        }
        else
        {
            (void)mod1RateSmoother_.process();
            (void)mod1DepthSmoother_.process();
        }

        if (mod2Enabled)
        {
            mod2_.setRate(mod2RateSmoother_.process());
            mod2_.setDepth(mod2DepthSmoother_.process());
            mod2_.advance();

            // Apply frequency multipliers (FR-026, FR-028: additive with mod1)
            {
                std::array<float, Krate::DSP::kMaxPartials> mult2{};
                mod2_.getFrequencyMultipliers(mult2);
                oscillatorBank_.applyExternalFrequencyMultipliers(mult2);
            }

            // Apply pan offsets (FR-027, FR-028: additive with mod1)
            {
                std::array<float, Krate::DSP::kMaxPartials> panOff2{};
                mod2_.getPanOffsets(panOff2);
                oscillatorBank_.applyPanOffsets(panOff2);
            }
        }
        else
        {
            (void)mod2RateSmoother_.process();
            (void)mod2DepthSmoother_.process();
        }

        // --- Generate oscillator bank stereo output (M6: FR-007) ---
        float harmonicL = 0.0f;
        float harmonicR = 0.0f;
        oscillatorBank_.processStereo(harmonicL, harmonicR);
        float residualSample = hasResidual ? residualSynth_.process() : 0.0f;

        // M2: Mix harmonic and residual (FR-028)
        float harmLevel = harmonicLevelSmoother_.process();
        float resLevel = residualLevelSmoother_.process();
        // Advance brightness/transient smoothers per-sample (FR-025)
        (void)brightnessSmoother_.process();
        (void)transientEmphasisSmoother_.process();

        // Residual is center-panned in both channels (FR-012)
        float resContrib = residualSample * resLevel;
        float sampleL = harmonicL * harmLevel + resContrib;
        float sampleR = harmonicR * harmLevel + resContrib;

        // --- Source switch crossfade (FR-011) ---
        if (sourceCrossfadeSamplesRemaining_ > 0)
        {
            float fadeProgress = static_cast<float>(sourceCrossfadeSamplesRemaining_) /
                                 static_cast<float>(sourceCrossfadeLengthSamples_);
            sampleL = sourceCrossfadeOldLevel_ * fadeProgress +
                      sampleL * (1.0f - fadeProgress);
            sampleR = sourceCrossfadeOldLevel_ * fadeProgress +
                      sampleR * (1.0f - fadeProgress);
            sourceCrossfadeSamplesRemaining_--;
        }

        // --- Freeze recovery crossfade (FR-053) ---
        if (freezeRecoverySamplesRemaining_ > 0)
        {
            float fadeProgress = static_cast<float>(freezeRecoverySamplesRemaining_) /
                                 static_cast<float>(freezeRecoveryLengthSamples_);
            sampleL = freezeRecoveryOldLevel_ * fadeProgress +
                      sampleL * (1.0f - fadeProgress);
            sampleR = freezeRecoveryOldLevel_ * fadeProgress +
                      sampleR * (1.0f - fadeProgress);
            freezeRecoverySamplesRemaining_--;
        }

        // --- M4: Manual freeze recovery crossfade (FR-006) ---
        if (manualFreezeRecoverySamplesRemaining_ > 0)
        {
            float fadeProgress = static_cast<float>(manualFreezeRecoverySamplesRemaining_) /
                                 static_cast<float>(manualFreezeRecoveryLengthSamples_);
            sampleL = manualFreezeRecoveryOldLevel_ * fadeProgress +
                      sampleL * (1.0f - fadeProgress);
            sampleR = manualFreezeRecoveryOldLevel_ * fadeProgress +
                      sampleR * (1.0f - fadeProgress);
            manualFreezeRecoverySamplesRemaining_--;
        }

        // --- Anti-click voice steal crossfade (FR-054) ---
        if (antiClickSamplesRemaining_ > 0)
        {
            float fadeProgress = static_cast<float>(antiClickSamplesRemaining_) /
                                 static_cast<float>(antiClickLengthSamples_);
            sampleL = antiClickOldLevel_ * fadeProgress +
                      sampleL * (1.0f - fadeProgress);
            sampleR = antiClickOldLevel_ * fadeProgress +
                      sampleR * (1.0f - fadeProgress);
            antiClickSamplesRemaining_--;
        }

        // --- Velocity scaling (FR-050): applied to summed output, NOT partials ---
        sampleL *= velocityGain_;
        sampleR *= velocityGain_;

        // --- Release envelope (FR-049) ---
        if (inRelease_)
        {
            releaseGain_ *= releaseDecayCoeff_;
            sampleL *= releaseGain_;
            sampleR *= releaseGain_;

            // Check if release has faded below threshold
            if (releaseGain_ < 1e-6f)
            {
                noteActive_ = false;
                inRelease_ = false;
                releaseGain_ = 1.0f;
                oscillatorBank_.reset();
                residualSynth_.reset();
                sampleL = 0.0f;
                sampleR = 0.0f;
            }
        }

        // --- Master gain ---
        sampleL *= gain;
        sampleR *= gain;

        // --- Safety soft limiter (always on, no UI control) ---
        // Prevents output from exceeding [-1, 1] to protect speakers.
        // Uses fast Padé tanh approximant from KrateDSP Layer 0.
        sampleL = Krate::DSP::Sigmoid::tanh(sampleL);
        sampleR = Krate::DSP::Sigmoid::tanh(sampleR);

        // Write stereo output (M6: FR-007), or sum to mono (FR-013)
        if (numOutputChannels >= 2) {
            out[0][s] = sampleL;
            out[1][s] = sampleR;
        } else {
            // FR-013: mono output bus -- sum both channels
            out[0][s] = sampleL + sampleR;
        }

        if (sampleL != 0.0f || sampleR != 0.0f)
            hasSoundOutput = true;
    }

    // Spec B FR-002, FR-006, FR-014: Capture mono output into feedback buffer
    // Only active in sidechain mode (InputSource::Sidechain)
    {
        const int currentSource =
            inputSource_.load(std::memory_order_relaxed) > 0.5f ? 1 : 0;
        if (currentSource == 1)
        {
            const auto count = std::min(numSamples,
                static_cast<Steinberg::int32>(feedbackBuffer_.size()));

            if (numOutputChannels >= 2)
            {
                for (Steinberg::int32 s = 0; s < count; ++s)
                {
                    feedbackBuffer_[static_cast<size_t>(s)] =
                        (out[0][s] + out[1][s]) * 0.5f;
                }
            }
            else
            {
                for (Steinberg::int32 s = 0; s < count; ++s)
                    feedbackBuffer_[static_cast<size_t>(s)] = out[0][s];
            }

            // FR-013: Apply per-block exponential decay
            const float decayAmount =
                feedbackDecay_.load(std::memory_order_relaxed);
            if (decayAmount > 0.0f)
            {
                const float decayCoeff = std::exp(
                    -decayAmount * static_cast<float>(count)
                    / static_cast<float>(sampleRate_));
                for (Steinberg::int32 s = 0; s < count; ++s)
                    feedbackBuffer_[static_cast<size_t>(s)] *= decayCoeff;
            }
        }
    }

    if (hasSoundOutput) {
        data.outputs[0].silenceFlags = 0;
    } else {
        data.outputs[0].silenceFlags = (numOutputChannels >= 2) ? 0x3 : 0x1;
    }

    // M7: Send display data to controller (FR-048)
    // Only when producing output and processing samples (SC-008)
    if (data.numOutputs > 0 && numSamples > 0)
        sendDisplayData(data);

    return Steinberg::kResultOk;
}

// ==============================================================================
// Set Bus Arrangements (instrument: no audio inputs, stereo output only)
// ==============================================================================
Steinberg::tresult PLUGIN_API Processor::setBusArrangements(
    Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
    Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts)
{
    // Accept: 0 or 1 input (sidechain optional), 1 stereo output
    if (numOuts != 1 || outputs[0] != Steinberg::Vst::SpeakerArr::kStereo)
        return Steinberg::kResultFalse;

    if (numIns == 0)
        return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);

    if (numIns == 1 &&
        (inputs[0] == Steinberg::Vst::SpeakerArr::kStereo ||
         inputs[0] == Steinberg::Vst::SpeakerArr::kMono))
        return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);

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
// Apply Harmonic Physics (Spec A: FR-020, FR-021)
// ==============================================================================
void Processor::applyHarmonicPhysics() noexcept
{
    harmonicPhysics_.setWarmth(warmthSmoother_.getCurrentValue());
    harmonicPhysics_.setCoupling(couplingSmoother_.getCurrentValue());
    harmonicPhysics_.setStability(stabilitySmoother_.getCurrentValue());
    harmonicPhysics_.setEntropy(entropySmoother_.getCurrentValue());
    harmonicPhysics_.processFrame(morphedFrame_);
}

// ==============================================================================
// Apply Modulator Amplitude Modulation (FR-025, FR-028)
// ==============================================================================
void Processor::applyModulatorAmplitude(bool mod1Enabled, bool mod2Enabled)
{
    // FR-028: Two modulators on overlapping amplitude ranges multiply effects
    // (sequential application = multiplicative)
    if (mod1Enabled)
        mod1_.applyAmplitudeModulation(morphedFrame_);
    if (mod2Enabled)
        mod2_.applyAmplitudeModulation(morphedFrame_);
}

} // namespace Innexus
