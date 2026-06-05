#pragma once

// ==============================================================================
// FrictionExciter -- Phase 2 (data-model.md §2.4, FR-013)
// ==============================================================================
// Transient friction (struck-bow) exciter built on Krate::DSP::BowExciter.
// Phase 2 uses BowExciter in transient mode only: a short ADSR envelope ramps
// the bow pressure up and releases within ≤ 50 ms. Sustained bowing is out of
// scope (deferred to Phase 4).
//
// Velocity mapping (FR-013):
//   bow pressure = lerp(0.1, 0.5, velocity)
//   bow speed    = lerp(0.2, 0.8, velocity)
//
// Envelope (A=1ms, D=40ms, S=0, R=5ms) → total duration ≈ 46 ms ≤ 50 ms.
// The bow auto-releases when the envelope reaches its decay stage's sustain
// level (0) — we explicitly gate off at the end of the envelope's decay so
// the bow naturally stops oscillating.
// ==============================================================================

#include <krate/dsp/primitives/adsr_envelope.h>
#include <krate/dsp/primitives/svf.h>
#include <krate/dsp/processors/bow_exciter.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Membrum {

struct FrictionExciter
{
    void prepare(double sampleRate, std::uint32_t /*voiceId*/) noexcept
    {
        sampleRate_ = sampleRate;
        core_.prepare(sampleRate);
        bowEnvelope_.prepare(static_cast<float>(sampleRate));
        // Transient envelope: A=1ms, D=40ms, S=0, R=5ms → ≤50 ms total (FR-013).
        bowEnvelope_.setAttack(1.0f);
        bowEnvelope_.setDecay(40.0f);
        bowEnvelope_.setSustain(0.0f);
        bowEnvelope_.setRelease(5.0f);
        // Auto-release timer: 41 ms ≈ end of decay → triggers release.
        autoReleaseSamples_ = static_cast<int>(0.041f * static_cast<float>(sampleRate));
        // Bandpass post-filter, velocity-driven cutoff for FR-016/SC-004.
        bodyFilter_.prepare(sampleRate);
        bodyFilter_.setMode(Krate::DSP::SVFMode::Bandpass);
        bodyFilter_.setResonance(1.5f);
        bodyFilter_.setCutoff(800.0f);
        bodyFilter_.snapToTarget();
        reset();
    }

    void reset() noexcept
    {
        core_.reset();
        bowEnvelope_.reset();
        bodyFilter_.reset();
        sampleCounter_ = 0;
        active_        = false;
    }

    /// Set the Friction Pressure from a normalized [0,1] host value
    /// (plugin_ids.h kExciterFrictionPressureId). It adds to the velocity-
    /// derived bow-pressure baseline: at 0 the bow is velocity-driven exactly
    /// as before; higher values press harder. Applied on the next trigger().
    void setPressure(float normalized) noexcept
    {
        pressureAmount_ = std::clamp(normalized, 0.0f, 1.0f);
    }

    void trigger(float velocity) noexcept
    {
        velocity = std::clamp(velocity, 0.0f, 1.0f);

        // FR-013: velocity → pressure, speed. The Friction Pressure parameter
        // adds an extra bow-pressure bias on top of the velocity baseline
        // (amount=0 -> legacy behaviour, amount=1 -> +0.5 heavier bowing).
        core_.setPressure(
            std::clamp(0.1f + (0.5f - 0.1f) * velocity + pressureAmount_ * 0.5f,
                       0.0f, 1.0f));
        core_.setSpeed(0.2f + (0.8f - 0.2f) * velocity);
        core_.setPosition(0.13f); // default bridge-ish position

        // FR-016/SC-004: velocity drives spectral centroid via bandpass cutoff.
        const float cutoff = 400.0f * std::pow(20.0f, velocity); // 400 → 8000 Hz
        bodyFilter_.setCutoff(cutoff);
        bodyFilter_.snapToTarget();
        bodyFilter_.reset();

        // Retrigger-safe: reset envelope and sample counter before gating.
        bowEnvelope_.reset();
        bowEnvelope_.gate(true);
        sampleCounter_ = 0;
        core_.trigger(velocity);
        active_ = true;
    }

    void release() noexcept
    {
        bowEnvelope_.gate(false);
        core_.release();
    }

    [[nodiscard]] float process(float /*bodyFeedback*/) noexcept
    {
        if (!active_)
            return 0.0f;

        // FR-013 "auto-releases bow at envelope completion": once the attack +
        // decay stages have elapsed (~41 ms), fire gate(false) to enter release.
        if (sampleCounter_ == autoReleaseSamples_)
        {
            bowEnvelope_.gate(false);
        }
        ++sampleCounter_;

        const float envValue = bowEnvelope_.process();
        core_.setEnvelopeValue(envValue);

        // BowExciter consumes the body-feedback velocity normally; Phase 2 runs
        // the transient mode with zero feedback so the stick-slip signature
        // still emerges from the internal rosin jitter and friction junction.
        const float raw = core_.process(0.0f);
        const float out = bodyFilter_.process(raw);

        if (!bowEnvelope_.isActive())
        {
            active_ = false;
        }
        return out;
    }

    [[nodiscard]] bool isActive() const noexcept
    {
        return active_ && bowEnvelope_.isActive();
    }

private:
    Krate::DSP::BowExciter   core_{};
    Krate::DSP::ADSREnvelope bowEnvelope_{};
    Krate::DSP::SVF          bodyFilter_{};
    double sampleRate_         = 44100.0;
    int    sampleCounter_      = 0;
    int    autoReleaseSamples_ = 0;
    // User Friction Pressure bias [0,1]; 0 = legacy velocity-only behaviour.
    float  pressureAmount_     = 0.0f;
    bool   active_             = false;
};

} // namespace Membrum
