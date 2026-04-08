#pragma once

// ==============================================================================
// DrumVoice -- Single drum voice for Membrum Phase 1
// ==============================================================================
// Signal path: ImpactExciter -> ModalResonatorBank (16 modes) -> ADSREnvelope
// Implements FR-030 through FR-039.
// ==============================================================================

#include "membrane_modes.h"

#include <krate/dsp/primitives/adsr_envelope.h>
#include <krate/dsp/processors/impact_exciter.h>
#include <krate/dsp/processors/modal_resonator_bank.h>

#include <algorithm>
#include <cmath>

namespace Membrum {

class DrumVoice
{
public:
    DrumVoice() = default;

    // ------------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------------

    /// Prepare all sub-components for the given sample rate.
    void prepare(double sampleRate) noexcept
    {
        exciter_.prepare(sampleRate, 0);
        modalBank_.prepare(sampleRate);
        ampEnvelope_.prepare(static_cast<float>(sampleRate));

        // FR-038: ADSR defaults for membrane drum
        ampEnvelope_.setAttack(0.0f);   // instant attack
        ampEnvelope_.setDecay(200.0f);  // 200ms decay
        ampEnvelope_.setSustain(0.0f);  // no sustain
        ampEnvelope_.setRelease(300.0f); // 300ms release

        // Enable velocity scaling so amplitude follows velocity
        ampEnvelope_.setVelocityScaling(true);
    }

    // ------------------------------------------------------------------
    // Note control
    // ------------------------------------------------------------------

    /// Trigger the drum voice with the given normalized velocity [0,1].
    /// Computes all mode parameters from current cached values.
    void noteOn(float velocity) noexcept
    {
        // (1) Compute mode frequencies from size_ (FR-033)
        float f0 = 500.0f * std::pow(0.1f, size_);
        float freqs[16];
        for (int k = 0; k < 16; ++k)
            freqs[k] = f0 * kMembraneRatios[static_cast<size_t>(k)];

        // (2) Compute per-mode amplitudes from strike position (FR-035)
        float r_over_a = strikePos_ * 0.9f;
        float amps[16];
        for (int k = 0; k < 16; ++k)
        {
            int m = kMembraneBesselOrder[static_cast<size_t>(k)];
            float jmn = kMembraneBesselZeros[static_cast<size_t>(k)];
            amps[k] = std::abs(evaluateBesselJ(m, jmn * r_over_a));
        }

        // (3) Compute material-derived parameters (FR-032, FR-033, FR-034)
        float brightness = material_;
        float stretch = material_ * 0.3f;
        float baseDecayTime = lerp(0.15f, 0.8f, material_) * (1.0f + 0.1f * size_);
        float decayTime = baseDecayTime * std::exp(lerp(std::log(0.3f), std::log(3.0f), decay_));

        // (4) Set modes on modal bank (clears filter state for new note)
        modalBank_.setModes(freqs, amps, 16, decayTime, brightness, stretch, 0.0f);

        // (5) Compute exciter parameters from velocity (FR-037)
        float hardness = lerp(0.3f, 0.8f, velocity);
        float excBrightness = lerp(0.15f, 0.4f, velocity);

        // (6) Trigger exciter: mass=0.3, position=0, f0=0 (comb disabled Phase 1)
        exciter_.trigger(velocity, hardness, 0.3f, excBrightness, 0.0f, 0.0f);

        // (7) Set velocity and gate on envelope
        ampEnvelope_.setVelocity(velocity);
        ampEnvelope_.gate(true);

        active_ = true;
    }

    /// Release the voice (trigger ADSR release phase, no abrupt cut).
    void noteOff() noexcept
    {
        ampEnvelope_.gate(false);
    }

    // ------------------------------------------------------------------
    // Processing
    // ------------------------------------------------------------------

    /// Process one sample. Returns mono output.
    [[nodiscard]] float process() noexcept
    {
        // Early-out for silent voice (FR-039)
        if (!ampEnvelope_.isActive())
        {
            active_ = false;
            return 0.0f;
        }

        float exc = exciter_.process(0.0f);
        float body = modalBank_.processSample(exc);
        float env = ampEnvelope_.process();

        return body * env * level_;
    }

    /// Returns true if the voice is producing output.
    [[nodiscard]] bool isActive() const noexcept
    {
        return ampEnvelope_.isActive();
    }

    // ------------------------------------------------------------------
    // Parameter setters (cache + live update)
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

    void setLevel(float v) noexcept
    {
        level_ = v;
    }

private:
    /// Linear interpolation helper.
    static float lerp(float a, float b, float t) noexcept
    {
        return a + (b - a) * t;
    }

    /// Recompute and update modal bank parameters without clearing filter state.
    /// Uses updateModes() to preserve resonator state mid-note.
    void updateModalParameters() noexcept
    {
        float f0 = 500.0f * std::pow(0.1f, size_);
        float freqs[16];
        for (int k = 0; k < 16; ++k)
            freqs[k] = f0 * kMembraneRatios[static_cast<size_t>(k)];

        float r_over_a = strikePos_ * 0.9f;
        float amps[16];
        for (int k = 0; k < 16; ++k)
        {
            int m = kMembraneBesselOrder[static_cast<size_t>(k)];
            float jmn = kMembraneBesselZeros[static_cast<size_t>(k)];
            amps[k] = std::abs(evaluateBesselJ(m, jmn * r_over_a));
        }

        float brightness = material_;
        float stretch = material_ * 0.3f;
        float baseDecayTime = lerp(0.15f, 0.8f, material_) * (1.0f + 0.1f * size_);
        float decayTime = baseDecayTime * std::exp(lerp(std::log(0.3f), std::log(3.0f), decay_));

        // updateModes preserves filter state (no reset)
        modalBank_.updateModes(freqs, amps, 16, decayTime, brightness, stretch, 0.0f);
    }

    // Sub-components
    Krate::DSP::ImpactExciter exciter_;
    Krate::DSP::ModalResonatorBank modalBank_;
    Krate::DSP::ADSREnvelope ampEnvelope_;

    // Cached parameters (normalized 0-1)
    float material_ = 0.5f;
    float size_ = 0.5f;
    float decay_ = 0.3f;
    float strikePos_ = 0.3f;
    float level_ = 0.8f;

    // Voice state
    bool active_ = false;
};

} // namespace Membrum
