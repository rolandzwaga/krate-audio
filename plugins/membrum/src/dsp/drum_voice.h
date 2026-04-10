#pragma once

// ==============================================================================
// DrumVoice -- Phase 2 refactored single drum voice
// ==============================================================================
// Signal path:
//   ExciterBank -> BodyBank -> ToneShaper -> UnnaturalZone::NonlinearCoupling
//   -> amp envelope -> level
//
// Phase 2.A default configuration (Impulse + Membrane + bypassed tone shaper +
// bypassed unnatural zone) is bit-identical to Phase 1's inline
// ImpactExciter + ModalResonatorBank path (FR-007, FR-031, FR-095).
// ==============================================================================

#include "body_bank.h"
#include "bodies/membrane_mapper.h"
#include "exciter_bank.h"
#include "exciter_type.h"
#include "body_model_type.h"
#include "tone_shaper.h"
#include "unnatural/unnatural_zone.h"
#include "voice_common_params.h"

#include <krate/dsp/primitives/adsr_envelope.h>
#include <krate/dsp/processors/modal_resonator_bank.h>

#include <cstdint>

namespace Membrum {

class DrumVoice
{
public:
    DrumVoice() = default;

    // ------------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------------

    /// Phase 1-compatible overload (voiceId defaulted to 0).
    void prepare(double sampleRate) noexcept
    {
        prepare(sampleRate, 0u);
    }

    void prepare(double sampleRate, std::uint32_t voiceId) noexcept
    {
        sampleRate_ = sampleRate;
        voiceId_    = voiceId;

        exciterBank_.prepare(sampleRate, voiceId);
        bodyBank_.prepare(sampleRate, voiceId);
        toneShaper_.prepare(sampleRate);
        unnaturalZone_.prepare(sampleRate, voiceId);

        ampEnvelope_.prepare(static_cast<float>(sampleRate));
        // FR-038: ADSR defaults for membrane drum (Phase 1 carry-over)
        ampEnvelope_.setAttack(0.0f);
        ampEnvelope_.setDecay(200.0f);
        ampEnvelope_.setSustain(0.0f);
        ampEnvelope_.setRelease(300.0f);
        ampEnvelope_.setVelocityScaling(true);
    }

    // ------------------------------------------------------------------
    // Note control
    // ------------------------------------------------------------------

    void noteOn(float velocity) noexcept
    {
        // Push cached parameters into the common bundle.
        params_.material   = material_;
        params_.size       = size_;
        params_.decay      = decay_;
        params_.strikePos  = strikePos_;
        params_.level      = level_;
        params_.modeStretch = unnaturalZone_.getModeStretch();
        params_.decaySkew   = unnaturalZone_.getDecaySkew();

        // Control-plane query (Phase 2.A stub returns the configured start Hz).
        const float pitchHz = toneShaper_.processPitchEnvelope();

        // Configure the body for this note (applies deferred body-model swap).
        bodyBank_.configureForNoteOn(params_, pitchHz);

        // Tone shaper + unnatural zone lifecycle hooks.
        toneShaper_.noteOn(velocity);

        // Trigger exciter (applies deferred exciter-type swap).
        exciterBank_.trigger(velocity);

        ampEnvelope_.setVelocity(velocity);
        ampEnvelope_.gate(true);
        active_ = true;
    }

    void noteOff() noexcept
    {
        exciterBank_.release();
        toneShaper_.noteOff();
        ampEnvelope_.gate(false);
    }

    // ------------------------------------------------------------------
    // Processing
    // ------------------------------------------------------------------

    [[nodiscard]] float process() noexcept
    {
        if (!ampEnvelope_.isActive())
        {
            active_ = false;
            return 0.0f;
        }

        // Exciter takes the body's last output as feedback (only FeedbackExciter
        // uses it; all other backends ignore it).
        const float exc  = exciterBank_.process(bodyBank_.getLastOutput());
        const float body = bodyBank_.processSample(exc);
        const float shaped = toneShaper_.processSample(body);
        const float coupled = unnaturalZone_.nonlinearCoupling.processSample(shaped);
        const float env  = ampEnvelope_.process();
        return coupled * env * level_;
    }

    [[nodiscard]] bool isActive() const noexcept
    {
        return ampEnvelope_.isActive();
    }

    // ------------------------------------------------------------------
    // Phase 1 parameter setters (unchanged API; FR-007)
    // ------------------------------------------------------------------

    void setMaterial(float v) noexcept
    {
        material_ = v;
        if (active_)
            updateModalParameters();
    }

    void setSize(float v) noexcept
    {
        size_ = v;
        if (active_)
            updateModalParameters();
    }

    void setDecay(float v) noexcept
    {
        decay_ = v;
        if (active_)
            updateModalParameters();
    }

    void setStrikePosition(float v) noexcept
    {
        strikePos_ = v;
        if (active_)
            updateModalParameters();
    }

    void setLevel(float v) noexcept { level_ = v; }

    // ------------------------------------------------------------------
    // Phase 2 setters
    // ------------------------------------------------------------------

    void setExciterType(ExciterType type) noexcept
    {
        exciterBank_.setExciterType(type);
    }

    void setBodyModel(BodyModelType type) noexcept
    {
        bodyBank_.setBodyModel(type);
    }

    [[nodiscard]] ToneShaper& toneShaper() noexcept { return toneShaper_; }
    [[nodiscard]] UnnaturalZone& unnaturalZone() noexcept { return unnaturalZone_; }

    // Exposed for testing and state helpers
    [[nodiscard]] const ExciterBank& exciterBank() const noexcept { return exciterBank_; }
    [[nodiscard]] const BodyBank& bodyBank() const noexcept { return bodyBank_; }

private:
    /// Recompute body mapping without clearing filter state (Phase 1 behavior).
    ///
    /// Only the Membrane body supports mid-note parameter updates in Phase 2.A.
    /// All other body types are stubs that do not produce audio, so their
    /// "live update" path reduces to a no-op.
    void updateModalParameters() noexcept
    {
        if (bodyBank_.getCurrentType() != BodyModelType::Membrane)
            return;

        VoiceCommonParams p{};
        p.material   = material_;
        p.size       = size_;
        p.decay      = decay_;
        p.strikePos  = strikePos_;
        p.level      = level_;
        p.modeStretch = unnaturalZone_.getModeStretch();
        p.decaySkew   = unnaturalZone_.getDecaySkew();

        const auto r = Bodies::MembraneMapper::map(p, /*pitchHz*/ 0.0f);
        bodyBank_.getSharedBank().updateModes(
            r.frequencies, r.amplitudes, r.numPartials,
            r.decayTime, r.brightness, r.stretch, r.scatter);
    }

    // Sub-components
    ExciterBank                exciterBank_;
    BodyBank                   bodyBank_;
    ToneShaper                 toneShaper_;
    UnnaturalZone              unnaturalZone_;
    Krate::DSP::ADSREnvelope   ampEnvelope_;

    // Reusable parameter bundle (populated on every noteOn to avoid alloc).
    VoiceCommonParams params_{};

    // Cached Phase 1 parameters (normalized 0-1)
    float material_  = 0.5f;
    float size_      = 0.5f;
    float decay_     = 0.3f;
    float strikePos_ = 0.3f;
    float level_     = 0.8f;

    // Voice identity
    std::uint32_t voiceId_    = 0;
    double        sampleRate_ = 0.0;

    // State
    bool active_ = false;
};

} // namespace Membrum
