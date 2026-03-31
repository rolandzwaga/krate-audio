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
    int currentFilterType,
    const std::array<float, Krate::DSP::kMaxPartials>& filterMask,
    float brightness,
    float transientEmphasis,
    float resonanceDecay,
    float resonanceBrightness,
    float resonanceStretch,
    float resonanceScatter,
    ExciterType exciterType,
    float impactHardness,
    float impactMass,
    float impactBrightness,
    float impactPosition,
    int resonanceType,
    float waveguideStiffness,
    float waveguidePickPosition,
    float bowPressure,
    float bowSpeed,
    float bowPosition)
{
    // FR-032: Detect retrigger (same note already playing) BEFORE overwriting midiNote.
    // On retrigger, the resonator state must NOT be reset so that existing vibration
    // persists and the mallet choke envelope can attenuate it naturally.
    const bool isRetrigger = voice.active && voice.midiNote == noteNumber;

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
    // For fresh voices (not retrigger), reset output to 0 so the attack starts
    // from silence. Without this, a stale output_ from a previous note (or from
    // being unprocessed while adsrAmount was 0) causes the envelope to start
    // mid-way instead of from zero.
    if (!isRetrigger)
        voice.adsr.reset();
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
    // FR-032: On retrigger (same note), use updateModes() to preserve resonator
    // filter states (existing vibration). On new note, use setModes() which clears
    // filter states and snaps coefficients from current frame partials.
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
        if (isRetrigger)
        {
            // Retrigger: preserve resonator state so existing vibration persists
            voice.modalResonator.updateModes(
                modeFreqs.data(), modeAmps.data(), frame.numPartials,
                resonanceDecay, resonanceBrightness, resonanceStretch, resonanceScatter);
        }
        else
        {
            // New note: clear filter states for fresh start
            voice.modalResonator.setModes(
                modeFreqs.data(), modeAmps.data(), frame.numPartials,
                resonanceDecay, resonanceBrightness, resonanceStretch, resonanceScatter);
        }
    }

    // Spec 128: Trigger impact exciter on note-on if type is Impact
    if (exciterType == ExciterType::Impact)
    {
        // Get f0 from the morphed frame for comb filter
        float f0 = voice.morphedFrame.f0;
        if (f0 <= 0.0f)
            f0 = Krate::DSP::midiNoteToFrequency(noteNumber);

        voice.impactExciter.trigger(
            velocity, impactHardness, impactMass, impactBrightness, impactPosition, f0);

        // FR-035: Mallet choke on retrigger -- velocity-dependent choke envelope
        // chokeMaxScale_ determines how much the existing resonance is damped.
        // A hard re-strike (high velocity) chokes more; gentle tap chokes less.
        constexpr float kMaxChokeBase = 4.0f;  // maximum choke multiplier
        voice.chokeMaxScale_ = 1.0f + (kMaxChokeBase - 1.0f) * velocity;
        voice.chokeEnvelope_ = 0.0f;  // start at full choke
        voice.chokeDecayScale_ = voice.chokeMaxScale_;
    }
    else if (exciterType == ExciterType::Bow)
    {
        // Spec 130: Trigger bow exciter on note-on
        voice.bowExciter.setPressure(bowPressure);
        voice.bowExciter.setSpeed(bowSpeed);
        voice.bowExciter.setPosition(bowPosition);
        voice.bowExciter.trigger(velocity);

        // No choke for bow exciter
        voice.chokeDecayScale_ = 1.0f;
        voice.chokeEnvelope_ = 1.0f;
        voice.chokeMaxScale_ = 1.0f;
    }
    else
    {
        // Residual or other exciter: no choke
        voice.chokeDecayScale_ = 1.0f;
        voice.chokeEnvelope_ = 1.0f;
        voice.chokeMaxScale_ = 1.0f;
    }

    // Spec 129: Initialize waveguide string on note-on
    voice.activeResonanceType_ = resonanceType;
    voice.waveguideString.setStiffness(waveguideStiffness);
    voice.waveguideString.setPickPosition(waveguidePickPosition);
    voice.waveguideString.setDecay(resonanceDecay);
    voice.waveguideString.setBrightness(resonanceBrightness);
    if (resonanceType == 1 && !isRetrigger)
    {
        float f0 = voice.morphedFrame.f0;
        if (f0 <= 0.0f)
            f0 = Krate::DSP::midiNoteToFrequency(noteNumber);
        voice.waveguideString.noteOn(f0, velocity);
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
    const float brightness = brightnessSmoother_.getCurrentValue();
    const float transientEmp = transientEmphasisSmoother_.getCurrentValue();

    // Spec 127: Modal resonator material parameters
    const float resDecay = resonanceDecay_.load(std::memory_order_relaxed);
    const float resBrightness = resonanceBrightness_.load(std::memory_order_relaxed);
    const float resStretch = resonanceStretch_.load(std::memory_order_relaxed);
    const float resScatter = resonanceScatter_.load(std::memory_order_relaxed);

    // Spec 128: Impact exciter parameters
    const float exciterTypeNorm = exciterType_.load(std::memory_order_relaxed);
    const auto exciterType = static_cast<ExciterType>(
        std::clamp(static_cast<int>(std::round(exciterTypeNorm * 2.0f)), 0, 2));
    const float impHardness = impactHardness_.load(std::memory_order_relaxed);
    const float impMass = impactMass_.load(std::memory_order_relaxed);
    // Brightness is stored normalized [0,1], denormalize to plain [-1,+1]
    const float impBrightnessNorm = impactBrightness_.load(std::memory_order_relaxed);
    const float impBrightness = impBrightnessNorm * 2.0f - 1.0f;
    const float impPosition = impactPosition_.load(std::memory_order_relaxed);

    // Spec 130: Bow exciter parameters
    const float bPressure = bowPressure_.load(std::memory_order_relaxed);
    const float bSpeed = bowSpeed_.load(std::memory_order_relaxed);
    const float bPosition = bowPosition_.load(std::memory_order_relaxed);

    // Spec 129: Waveguide string parameters
    const float resTypeNorm = resonanceType_.load(std::memory_order_relaxed);
    const int resType = std::clamp(static_cast<int>(std::round(resTypeNorm * 2.0f)), 0, 2);
    const float wgStiffness = waveguideStiffness_.load(std::memory_order_relaxed);
    const float wgPickPos = waveguidePickPosition_.load(std::memory_order_relaxed);

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
            currentFilterType_, filterMask_,
            brightness, transientEmp,
            resDecay, resBrightness, resStretch, resScatter,
            exciterType, impHardness, impMass, impBrightness, impPosition,
            resType, wgStiffness, wgPickPos,
            bPressure, bSpeed, bPosition);

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
                    currentFilterType_, filterMask_,
                    brightness, transientEmp,
                    resDecay, resBrightness, resStretch, resScatter,
                    exciterType, impHardness, impMass, impBrightness, impPosition,
                    resType, wgStiffness, wgPickPos,
                    bPressure, bSpeed, bPosition);
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

    // Spec 132: Route noteOn to sympathetic resonance (global, not per-voice).
    // Compute inharmonic partial frequencies: f_n = n * f0 * sqrt(1 + B * n^2)
    // B is derived from the inharmonicity parameter (0-1 -> B 0-0.001).
    {
        float f0 = Krate::DSP::midiNoteToFrequency(noteNumber);
        float inharmNorm = inharmonicityAmount_.load(std::memory_order_relaxed);
        // Map normalized 0-1 to inharmonicity coefficient B (0 to 0.001)
        // B=0 is perfect harmonics, B=0.001 is moderate piano-like inharmonicity
        float B = inharmNorm * 0.001f;

        Krate::DSP::SympatheticPartialInfo partials;
        for (int i = 0; i < Krate::DSP::kSympatheticPartialCount; ++i)
        {
            int n = i + 1;
            float fn = static_cast<float>(n) * f0 *
                       std::sqrt(1.0f + B * static_cast<float>(n * n));
            partials.frequencies[static_cast<size_t>(i)] = fn;
        }
        sympatheticResonance_.noteOn(noteId, partials);
    }
}

// ==============================================================================
// Handle Note Off (FR-049, FR-057)
// ==============================================================================
void Processor::handleNoteOff(int noteNumber, [[maybe_unused]] int32_t noteId)
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

    // Spec 132: Route noteOff to sympathetic resonance.
    // Resonators continue to ring out at their natural decay rate.
    sympatheticResonance_.noteOff(noteId);
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
