// ==============================================================================
// Processor MIDI Event Handling
// ==============================================================================

#include "processor.h"

#include "midi/midi_event_dispatcher.h"

#include <krate/dsp/core/midi_utils.h>
#include <krate/dsp/core/pitch_utils.h>
#include <krate/dsp/processors/harmonic_frame_utils.h>

#include <algorithm>
#include <cmath>

namespace Innexus {

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

    const bool isSidechainMode =
        inputSource_.load(std::memory_order_relaxed) > 0.5f;

    // FR-055: No sample loaded (sample mode) or no live frame (sidechain mode) -> no sound
    if (!isSidechainMode && (!analysis || analysis->frames.empty()))
        return;

    // FR-054: Monophonic -- if note already active, capture current output
    // for anti-click crossfade (20ms)
    if (noteActive_ && !inRelease_)
    {
        // Capture last output sample before resetting for crossfade
        {
            float captL = 0.0f;
            float captR = 0.0f;
            oscillatorBank_.processStereo(captL, captR);
            antiClickOldLevel_ = (captL + captR) * 0.5f;
        }
        antiClickSamplesRemaining_ = antiClickLengthSamples_;
    }

    // Set new note state
    currentMidiNote_ = noteNumber;
    noteActive_ = true;
    inRelease_ = false;
    inAdsrRelease_ = false;
    releaseGain_ = 1.0f;
    isFrozen_ = false;
    freezeRecoverySamplesRemaining_ = 0;
    freezeRecoveryOldLevel_ = 0.0f;

    // FR-050: Velocity scales global amplitude, NOT partial amplitudes
    velocityGain_ = velocity; // VST3 velocity is already 0.0-1.0

    // Spec 124 FR-012: Gate ADSR envelope on note-on (hard retrigger)
    adsr_.gate(true);

    // Start from first frame (FR-047)
    currentFrameIndex_ = 0;
    frameSampleCounter_ = 0;

    // Calculate target pitch with pitch bend
    float basePitch = Krate::DSP::midiNoteToFrequency(noteNumber);
    float bendRatio = Krate::DSP::semitonesToRatio(pitchBendSemitones_);
    float targetPitch = basePitch * bendRatio;

    // Reset oscillator bank for new note
    oscillatorBank_.reset();

    if (isSidechainMode)
    {
        // Load current live frame if available
        if (currentLiveFrame_.f0 > 0.0f)
        {
            lastGoodFrame_ = currentLiveFrame_;

            morphedFrame_ = currentLiveFrame_;
            morphedResidualFrame_ = currentLiveResidualFrame_;

            // M6 FR-001, FR-002: Apply timbral blend (cross-synthesis)
            {
                const float blend = timbralBlendSmoother_.getCurrentValue();
                if (blend < 1.0f - 1e-6f)
                    morphedFrame_ = Krate::DSP::lerpHarmonicFrame(
                        pureHarmonicFrame_, morphedFrame_, blend);
            }

            // M4: Apply harmonic filter (FR-026)
            if (currentFilterType_ != 0)
                Krate::DSP::applyHarmonicMask(morphedFrame_, filterMask_);

            // M6 FR-025: Apply modulator amplitude modulation
            {
                const bool m1On = mod1Enable_.load(std::memory_order_relaxed) > 0.5f;
                const bool m2On = mod2Enable_.load(std::memory_order_relaxed) > 0.5f;
                applyModulatorAmplitude(m1On, m2On);
            }
            applyHarmonicPhysics();

            oscillatorBank_.loadFrame(morphedFrame_, targetPitch);

            // Load live residual frame
            if (residualSynth_.isPrepared())
            {
                residualSynth_.reset();
                residualSynth_.loadFrame(
                    morphedResidualFrame_,
                    brightnessSmoother_.getCurrentValue(),
                    transientEmphasisSmoother_.getCurrentValue());
            }
        }
    }
    else
    {
        // Sample mode: load first frame from analysis
        const auto& frame = analysis->getFrame(0);
        lastGoodFrame_ = frame;

        morphedFrame_ = frame;

        // M6 FR-001, FR-002: Apply timbral blend (cross-synthesis)
        {
            const float blend = timbralBlendSmoother_.getCurrentValue();
            if (blend < 1.0f - 1e-6f)
                morphedFrame_ = Krate::DSP::lerpHarmonicFrame(
                    pureHarmonicFrame_, morphedFrame_, blend);
        }

        // M4: Apply harmonic filter (FR-026)
        if (currentFilterType_ != 0)
            Krate::DSP::applyHarmonicMask(morphedFrame_, filterMask_);

        // M6 FR-025: Apply modulator amplitude modulation
        {
            const bool m1On = mod1Enable_.load(std::memory_order_relaxed) > 0.5f;
            const bool m2On = mod2Enable_.load(std::memory_order_relaxed) > 0.5f;
            applyModulatorAmplitude(m1On, m2On);
        }
        applyHarmonicPhysics();

        oscillatorBank_.loadFrame(morphedFrame_, targetPitch);

        // M2: Load initial residual frame (FR-017)
        if (analysis && !analysis->residualFrames.empty() && residualSynth_.isPrepared())
        {
            morphedResidualFrame_ = analysis->getResidualFrame(0);
            residualSynth_.reset();
            residualSynth_.loadFrame(
                morphedResidualFrame_,
                brightnessSmoother_.getCurrentValue(),
                transientEmphasisSmoother_.getCurrentValue());
        }
    }
}

// ==============================================================================
// Handle Note Off (FR-049, FR-057)
// ==============================================================================
void Processor::handleNoteOff()
{
    if (!noteActive_ || inRelease_)
        return;

    // Spec 124 FR-012: Gate ADSR envelope off on note-off
    adsr_.gate(false);

    // When ADSR Amount > 0, the ADSR release envelope handles the fade-out.
    // Skip the old FR-049 release to avoid cutting the note prematurely.
    const float adsrAmount = adsrAmount_.load(std::memory_order_relaxed);
    if (adsrAmount <= 0.0f)
    {
        // No ADSR active — use the old FR-049 release envelope
        inRelease_ = true;
        updateReleaseDecayCoeff();
    }
    else
    {
        // ADSR handles release — mark that we're in ADSR release mode
        inAdsrRelease_ = true;
    }
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

} // namespace Innexus
