// ==============================================================================
// Processor MIDI Event Handling
// ==============================================================================

#include "processor.h"

#include "midi/midi_event_dispatcher.h"

#include <krate/dsp/core/midi_utils.h>
#include <krate/dsp/core/pitch_utils.h>
#include <krate/dsp/processors/harmonic_frame_utils.h>

#include "pluginterfaces/vst/ivstnoteexpression.h"

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
// Voice Mode Helpers
// ==============================================================================

/// Denormalize voice mode parameter to active voice count.
/// 0 = Mono (1), 0.5 = 4 Voices, 1.0 = 8 Voices
static int voiceModeToCount(float norm)
{
    int idx = std::clamp(static_cast<int>(std::round(norm * 2.0f)), 0, 2);
    constexpr int kCounts[] = {1, 4, 8};
    return kCounts[idx];
}

bool isMonoMode(float voiceModeNorm)
{
    return voiceModeToCount(voiceModeNorm) == 1;
}

// ==============================================================================
// MIDI Dispatcher Callbacks
// ==============================================================================
void Processor::onNoteOn(int16_t pitch, float velocity, int32_t noteId)
{
    handleNoteOn(static_cast<int>(pitch), velocity, noteId);
}

void Processor::onNoteOff(int16_t pitch, int32_t noteId)
{
    handleNoteOff(static_cast<int>(pitch), noteId);
}

void Processor::onPitchBend(float bipolar)
{
    float bendSemitones = Krate::DSP::pitchBendToSemitones(
        bipolar, kPitchBendRangeSemitones);
    handlePitchBend(bendSemitones);
}

void Processor::onNoteExpression(int32_t noteId, uint32_t typeId, double value)
{
    InnexusVoice* voice = findVoiceByNoteId(noteId);
    if (!voice)
        return;

    switch (typeId)
    {
    case Steinberg::Vst::NoteExpressionTypeIDs::kTuningTypeID:
        // VST3 tuning: 0.5 = center, range ±120 semitones
        voice->expressionTuning = 240.0f * (static_cast<float>(value) - 0.5f);
        break;
    case Steinberg::Vst::NoteExpressionTypeIDs::kVolumeTypeID:
        // VST3 volume: 0-1, mapped to 0-4x gain
        voice->expressionVolume = static_cast<float>(value) * 4.0f;
        break;
    case Steinberg::Vst::NoteExpressionTypeIDs::kPanTypeID:
        voice->expressionPan = static_cast<float>(value);
        break;
    case Steinberg::Vst::NoteExpressionTypeIDs::kBrightnessTypeID:
        voice->expressionBrightness = static_cast<float>(value);
        break;
    default:
        break;
    }
}

// ==============================================================================
// Find Voice by NoteId
// ==============================================================================
InnexusVoice* Processor::findVoiceByNoteId(int32_t noteId)
{
    if (noteId < 0)
        return nullptr;

    const float modeNorm = voiceMode_.load(std::memory_order_relaxed);
    const int maxVoices = voiceModeToCount(modeNorm);

    for (int i = 0; i < maxVoices; ++i)
    {
        if (voices_[static_cast<size_t>(i)].active &&
            voices_[static_cast<size_t>(i)].noteId == noteId)
            return &voices_[static_cast<size_t>(i)];
    }
    return nullptr;
}

// ==============================================================================
// Get Active Voice Count
// ==============================================================================
int Processor::getActiveVoiceCount() const
{
    const float modeNorm = voiceMode_.load(std::memory_order_relaxed);
    const int maxVoices = voiceModeToCount(modeNorm);
    int count = 0;
    for (int i = 0; i < maxVoices; ++i)
    {
        if (voices_[static_cast<size_t>(i)].active)
            ++count;
    }
    return count;
}

// ==============================================================================
// Initialize Voice for Note-On (shared logic)
// ==============================================================================
static void initVoiceForNoteOn(
    InnexusVoice& voice,
    int noteNumber,
    float velocity,
    int32_t noteId,
    float pitchBendSemitones,
    const SampleAnalysis* analysis,
    bool isSidechainMode,
    const Krate::DSP::HarmonicFrame& currentLiveFrame,
    const Krate::DSP::ResidualFrame& currentLiveResidualFrame,
    float timbralBlend,
    int currentFilterType,
    const std::array<float, Krate::DSP::kMaxPartials>& filterMask,
    const Krate::DSP::HarmonicFrame& pureHarmonicFrame,
    float brightness,
    float transientEmphasis,
    float resonanceDecay,
    float resonanceBrightness,
    float resonanceStretch,
    float resonanceScatter)
{
    // Set new note state
    voice.midiNote = noteNumber;
    voice.noteId = noteId;
    voice.active = true;
    voice.inRelease = false;
    voice.inAdsrRelease = false;
    voice.releaseGain = 1.0f;
    voice.isFrozen = false;
    voice.freezeRecoverySamplesRemaining = 0;
    voice.freezeRecoveryOldLevel = 0.0f;

    // Reset expression values for new note
    voice.expressionTuning = 0.0f;
    voice.expressionVolume = 1.0f;
    voice.expressionPan = 0.5f;
    voice.expressionBrightness = 0.5f;

    // FR-050: Velocity scales global amplitude, NOT partial amplitudes
    voice.velocityGain = velocity;

    // Spec 124 FR-012: Gate ADSR envelope on note-on (hard retrigger)
    voice.adsr.gate(true);

    // Start from first frame (FR-047)
    voice.currentFrameIndex = 0;
    voice.frameSampleCounter = 0;

    // Calculate target pitch with pitch bend
    float basePitch = Krate::DSP::midiNoteToFrequency(noteNumber);
    float bendRatio = Krate::DSP::semitonesToRatio(pitchBendSemitones);
    float targetPitch = basePitch * bendRatio;

    // Reset oscillator bank for new note
    voice.oscillatorBank.reset();

    if (isSidechainMode)
    {
        if (currentLiveFrame.f0 > 0.0f)
        {
            voice.lastGoodFrame = currentLiveFrame;
            voice.morphedFrame = currentLiveFrame;
            voice.morphedResidualFrame = currentLiveResidualFrame;

            // Apply timbral blend (cross-synthesis)
            if (timbralBlend < 1.0f - 1e-6f)
                voice.morphedFrame = Krate::DSP::lerpHarmonicFrame(
                    pureHarmonicFrame, voice.morphedFrame, timbralBlend);

            // Apply harmonic filter
            if (currentFilterType != 0)
                Krate::DSP::applyHarmonicMask(voice.morphedFrame, filterMask);

            voice.oscillatorBank.loadFrame(voice.morphedFrame, targetPitch);

            if (voice.residualSynth.isPrepared())
            {
                voice.residualSynth.reset();
                voice.residualSynth.loadFrame(
                    voice.morphedResidualFrame, brightness, transientEmphasis);
            }
        }
    }
    else
    {
        // Sample mode: load first frame from analysis
        const auto& frame = analysis->getFrame(0);
        voice.lastGoodFrame = frame;
        voice.morphedFrame = frame;

        // Apply timbral blend (cross-synthesis)
        if (timbralBlend < 1.0f - 1e-6f)
            voice.morphedFrame = Krate::DSP::lerpHarmonicFrame(
                pureHarmonicFrame, voice.morphedFrame, timbralBlend);

        // Apply harmonic filter
        if (currentFilterType != 0)
            Krate::DSP::applyHarmonicMask(voice.morphedFrame, filterMask);

        voice.oscillatorBank.loadFrame(voice.morphedFrame, targetPitch);

        // Load initial residual frame
        if (analysis && !analysis->residualFrames.empty() &&
            voice.residualSynth.isPrepared())
        {
            voice.morphedResidualFrame = analysis->getResidualFrame(0);
            voice.residualSynth.reset();
            voice.residualSynth.loadFrame(
                voice.morphedResidualFrame, brightness, transientEmphasis);
        }
    }

    // Spec 127 FR-018: Initialize modal resonator on note-on
    // Clears filter states and snaps coefficients from current frame partials
    {
        const auto& frame = voice.morphedFrame;
        float pitchRatio = (frame.f0 > 0.0f) ? targetPitch / frame.f0 : 1.0f;
        std::array<float, Krate::DSP::kMaxPartials> modeFreqs{};
        std::array<float, Krate::DSP::kMaxPartials> modeAmps{};
        for (int k = 0; k < frame.numPartials; ++k)
        {
            modeFreqs[static_cast<size_t>(k)] =
                frame.partials[static_cast<size_t>(k)].frequency * pitchRatio;
            modeAmps[static_cast<size_t>(k)] =
                frame.partials[static_cast<size_t>(k)].amplitude;
        }
        voice.modalResonator.setModes(
            modeFreqs.data(), modeAmps.data(), frame.numPartials,
            resonanceDecay, resonanceBrightness, resonanceStretch, resonanceScatter);
    }
}

// ==============================================================================
// Handle Note On (FR-048, FR-054: Mono last-note-priority / Poly via allocator)
// ==============================================================================
void Processor::handleNoteOn(int noteNumber, float velocity, int32_t noteId)
{
    const SampleAnalysis* analysis =
        currentAnalysis_.load(std::memory_order_acquire);

    const bool isSidechainMode =
        inputSource_.load(std::memory_order_relaxed) > 0.5f;

    // FR-055: No sample loaded (sample mode) or no live frame (sidechain mode) -> no sound
    if (!isSidechainMode && (!analysis || analysis->frames.empty()))
        return;

    const float modeNorm = voiceMode_.load(std::memory_order_relaxed);
    const int maxVoices = voiceModeToCount(modeNorm);
    const float blend = timbralBlendSmoother_.getCurrentValue();
    const float brightness = brightnessSmoother_.getCurrentValue();
    const float transientEmp = transientEmphasisSmoother_.getCurrentValue();

    // Spec 127: Modal resonator material parameters
    const float resDecay = resonanceDecay_.load(std::memory_order_relaxed);
    const float resBrightness = resonanceBrightness_.load(std::memory_order_relaxed);
    const float resStretch = resonanceStretch_.load(std::memory_order_relaxed);
    const float resScatter = resonanceScatter_.load(std::memory_order_relaxed);

    if (maxVoices == 1)
    {
        // === MONO MODE: last-note-priority (original behavior) ===
        auto& voice = voices_[0];

        // FR-054: if note already active, capture current output for anti-click crossfade
        if (voice.active && !voice.inRelease)
        {
            float captL = 0.0f;
            float captR = 0.0f;
            voice.oscillatorBank.processStereo(captL, captR);
            voice.antiClickOldLevel = (captL + captR) * 0.5f;
            voice.antiClickSamplesRemaining = voice.antiClickLengthSamples;
        }

        initVoiceForNoteOn(
            voice, noteNumber, velocity, noteId,
            voice.pitchBendSemitones,
            analysis, isSidechainMode,
            currentLiveFrame_, currentLiveResidualFrame_,
            blend, currentFilterType_, filterMask_, pureHarmonicFrame_,
            brightness, transientEmp,
            resDecay, resBrightness, resStretch, resScatter);

        // Apply modulators (mono mode uses processor-level modulators)
        const bool m1On = mod1Enable_.load(std::memory_order_relaxed) > 0.5f;
        const bool m2On = mod2Enable_.load(std::memory_order_relaxed) > 0.5f;
        applyModulatorAmplitude(m1On, m2On);
        applyHarmonicPhysics();

        // Reload frame after modulation
        float basePitch = Krate::DSP::midiNoteToFrequency(noteNumber);
        float bendRatio = Krate::DSP::semitonesToRatio(
            voice.pitchBendSemitones + voice.expressionTuning);
        voice.oscillatorBank.loadFrame(voice.morphedFrame, basePitch * bendRatio);
    }
    else
    {
        // === POLY MODE: use VoiceAllocator ===
        (void)voiceAllocator_.setVoiceCount(static_cast<size_t>(maxVoices));

        auto events = voiceAllocator_.noteOn(
            static_cast<uint8_t>(noteNumber),
            static_cast<uint8_t>(std::clamp(
                static_cast<int>(velocity * 127.0f + 0.5f), 1, 127)));

        for (const auto& evt : events)
        {
            auto& voice = voices_[evt.voiceIndex];

            switch (evt.type)
            {
            case Krate::DSP::VoiceEvent::Type::NoteOn:
            {
                initVoiceForNoteOn(
                    voice, noteNumber, velocity, noteId,
                    0.0f, // fresh pitch bend for new voice
                    analysis, isSidechainMode,
                    currentLiveFrame_, currentLiveResidualFrame_,
                    blend, currentFilterType_, filterMask_, pureHarmonicFrame_,
                    brightness, transientEmp,
                    resDecay, resBrightness, resStretch, resScatter);
                break;
            }
            case Krate::DSP::VoiceEvent::Type::NoteOff:
            {
                // Soft steal: release old note
                voice.adsr.gate(false);
                const float adsrAmount = adsrAmount_.load(std::memory_order_relaxed);
                if (adsrAmount <= 0.0f)
                {
                    voice.inRelease = true;
                    updateReleaseDecayCoeff(voice);
                }
                else
                {
                    voice.inAdsrRelease = true;
                }
                break;
            }
            case Krate::DSP::VoiceEvent::Type::Steal:
            {
                // Hard steal: anti-click crossfade then restart
                if (voice.active)
                {
                    float captL = 0.0f;
                    float captR = 0.0f;
                    voice.oscillatorBank.processStereo(captL, captR);
                    voice.antiClickOldLevel = (captL + captR) * 0.5f;
                    voice.antiClickSamplesRemaining = voice.antiClickLengthSamples;
                }
                break;
            }
            }
        }
    }
}

// ==============================================================================
// Handle Note Off (FR-049, FR-057)
// ==============================================================================
void Processor::handleNoteOff(int noteNumber, int32_t noteId)
{
    const float modeNorm = voiceMode_.load(std::memory_order_relaxed);
    const int maxVoices = voiceModeToCount(modeNorm);

    if (maxVoices == 1)
    {
        // === MONO MODE: original behavior ===
        auto& voice = voices_[0];
        if (!voice.active || voice.inRelease)
            return;

        voice.adsr.gate(false);

        const float adsrAmount = adsrAmount_.load(std::memory_order_relaxed);
        if (adsrAmount <= 0.0f)
        {
            voice.inRelease = true;
            updateReleaseDecayCoeff(voice);
        }
        else
        {
            voice.inAdsrRelease = true;
        }
    }
    else
    {
        // === POLY MODE: release via VoiceAllocator ===
        auto events = voiceAllocator_.noteOff(static_cast<uint8_t>(noteNumber));

        for (const auto& evt : events)
        {
            if (evt.type != Krate::DSP::VoiceEvent::Type::NoteOff)
                continue;

            auto& voice = voices_[evt.voiceIndex];
            if (!voice.active || voice.inRelease)
                continue;

            voice.adsr.gate(false);

            const float adsrAmount = adsrAmount_.load(std::memory_order_relaxed);
            if (adsrAmount <= 0.0f)
            {
                voice.inRelease = true;
                updateReleaseDecayCoeff(voice);
            }
            else
            {
                voice.inAdsrRelease = true;
            }
        }
    }
}

// ==============================================================================
// Handle Pitch Bend (FR-051, T084)
// ==============================================================================
void Processor::handlePitchBend(float bendSemitones)
{
    // In mono mode, apply to voice 0 (original behavior)
    // In poly mode, apply to all active voices (global pitch bend)
    // Per-note pitch bend is handled via NoteExpression (Phase 3)
    const float modeNorm = voiceMode_.load(std::memory_order_relaxed);
    const int maxVoices = voiceModeToCount(modeNorm);

    for (int i = 0; i < maxVoices; ++i)
    {
        auto& voice = voices_[static_cast<size_t>(i)];
        voice.pitchBendSemitones = bendSemitones;

        if (!voice.active || voice.midiNote < 0)
            continue;

        float basePitch = Krate::DSP::midiNoteToFrequency(voice.midiNote);
        float bendRatio = Krate::DSP::semitonesToRatio(
            voice.pitchBendSemitones + voice.expressionTuning);
        float targetPitch = basePitch * bendRatio;

        voice.oscillatorBank.setTargetPitch(targetPitch);
    }
}

// ==============================================================================
// Update Release Decay Coefficient (FR-049, FR-057)
// ==============================================================================
void Processor::updateReleaseDecayCoeff(InnexusVoice& voice)
{
    float userReleaseMs = releaseTimeMs_.load(std::memory_order_relaxed);

    // FR-057: Enforce minimum 20ms anti-click fade
    float effectiveReleaseMs = std::max(userReleaseMs, kMinAntiClickMs);

    // Exponential decay: A(t) = exp(-t/tau)
    float tauSamples = effectiveReleaseMs * 0.001f * static_cast<float>(sampleRate_);
    voice.releaseDecayCoeff = std::exp(-1.0f / tauSamples);
}

} // namespace Innexus
