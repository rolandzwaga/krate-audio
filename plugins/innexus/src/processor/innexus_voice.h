#pragma once

// ==============================================================================
// InnexusVoice - Per-voice state for polyphonic rendering
// ==============================================================================
// Phase 1: Single-voice struct extracted from Processor.
// Phase 2: Array of voices with VoiceAllocator.
//
// Contains all state that must be independent per voice:
// - Oscillator bank & residual synthesizer (DSP engines)
// - ADSR envelope & amount smoother
// - Note identity, velocity, pitch bend
// - Frame tracking & sustain loop region
// - Release envelope, anti-click crossfade
// - Confidence-gated freeze & spectral decay
// - Per-voice copies of morphed frames
// ==============================================================================

#include "dsp/spectral_decay_envelope.h"

#include <krate/dsp/processors/harmonic_oscillator_bank.h>
#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/processors/residual_synthesizer.h>
#include <krate/dsp/processors/residual_types.h>
#include <krate/dsp/primitives/adsr_envelope.h>
#include <krate/dsp/primitives/smoother.h>

#include <cmath>
#include <cstdint>

namespace Innexus {

struct InnexusVoice
{
    // =========================================================================
    // Identity (Phase 2: MPE noteId routing)
    // =========================================================================
    int32_t noteId = -1;       ///< VST3 noteId for NoteExpression routing

    // =========================================================================
    // DSP Engines
    // =========================================================================
    Krate::DSP::HarmonicOscillatorBank oscillatorBank;
    Krate::DSP::ResidualSynthesizer residualSynth;

    // ADSR Envelope (Spec 124)
    Krate::DSP::ADSREnvelope adsr;
    Krate::DSP::OnePoleSmoother adsrAmountSmoother;

    // =========================================================================
    // Voice State
    // =========================================================================
    bool active = false;
    int midiNote = -1;
    float velocityGain = 1.0f;
    float pitchBendSemitones = 0.0f;

    // =========================================================================
    // Frame Advancement (FR-047)
    // =========================================================================
    size_t currentFrameIndex = 0;
    size_t frameSampleCounter = 0;

    // Sustain loop region (Spec 124)
    int sustainLoopStart = 0;
    int sustainLoopEnd = 0;

    // =========================================================================
    // Per-voice Morphed Frame Copies
    // =========================================================================
    Krate::DSP::HarmonicFrame morphedFrame{};
    Krate::DSP::ResidualFrame morphedResidualFrame{};

    // =========================================================================
    // Release Envelope (FR-049, FR-057)
    // =========================================================================
    float releaseGain = 1.0f;
    bool inRelease = false;
    bool inAdsrRelease = false;
    float releaseDecayCoeff = 0.0f;

    // =========================================================================
    // Anti-Click Voice Steal Crossfade (FR-054)
    // =========================================================================
    int antiClickSamplesRemaining = 0;
    int antiClickLengthSamples = 0;
    float antiClickOldLevel = 0.0f;

    // =========================================================================
    // Confidence-Gated Freeze (FR-052, FR-053)
    // =========================================================================
    Krate::DSP::HarmonicFrame lastGoodFrame{};
    bool isFrozen = false;
    int freezeRecoverySamplesRemaining = 0;
    int freezeRecoveryLengthSamples = 0;
    float freezeRecoveryOldLevel = 0.0f;
    SpectralDecayEnvelope spectralDecay;

    // =========================================================================
    // NoteExpression values (Phase 3: MPE per-voice expression)
    // =========================================================================
    float expressionTuning = 0.0f;      ///< semitones offset (kTuningTypeID)
    float expressionVolume = 1.0f;       ///< gain multiplier (kVolumeTypeID)
    float expressionPan = 0.5f;          ///< stereo position (kPanTypeID)
    float expressionBrightness = 0.5f;   ///< timbral blend (kBrightnessTypeID)

    // =========================================================================
    // Methods
    // =========================================================================

    /// Prepare voice for processing at the given sample rate.
    void prepare(double sampleRate)
    {
        oscillatorBank.prepare(sampleRate);
        adsr.prepare(static_cast<float>(sampleRate));
        adsr.reset();
        adsr.setRetriggerMode(Krate::DSP::RetriggerMode::Hard);
    }

    /// Reset all voice state to idle defaults.
    void reset()
    {
        active = false;
        midiNote = -1;
        noteId = -1;
        velocityGain = 1.0f;
        pitchBendSemitones = 0.0f;
        currentFrameIndex = 0;
        frameSampleCounter = 0;
        releaseGain = 1.0f;
        inRelease = false;
        inAdsrRelease = false;
        antiClickSamplesRemaining = 0;
        antiClickOldLevel = 0.0f;
        isFrozen = false;
        freezeRecoverySamplesRemaining = 0;
        freezeRecoveryOldLevel = 0.0f;
        oscillatorBank.reset();
        residualSynth.reset();

        // Reset expression values
        expressionTuning = 0.0f;
        expressionVolume = 1.0f;
        expressionPan = 0.5f;
        expressionBrightness = 0.5f;
    }

    /// Check if the voice has finished playing (release complete).
    bool isFinished() const
    {
        if (!active)
            return true;

        // Voice finishes when release envelope fades out
        // or ADSR reaches Idle stage
        if (inRelease && releaseGain < 1e-6f)
            return true;
        if (inAdsrRelease &&
            adsr.getStage() == Krate::DSP::ADSRStage::Idle)
            return true;

        return false;
    }
};

} // namespace Innexus
