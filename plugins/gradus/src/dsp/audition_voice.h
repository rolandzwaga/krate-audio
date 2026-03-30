#pragma once

// ==============================================================================
// AuditionVoice — Minimal monophonic synth for pattern auditioning
// ==============================================================================
// Simple: PolyBlepOscillator + linear ADSR + gain.
// Not a production synth — just so users can hear arp patterns.
// ==============================================================================

#include <krate/dsp/primitives/polyblep_oscillator.h>

#include <algorithm>
#include <cmath>

namespace Gradus {

class AuditionVoice {
public:
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
        osc_.prepare(sampleRate);
        osc_.setWaveform(Krate::DSP::OscWaveform::Sine);
    }

    void setWaveform(int type) noexcept {
        switch (type) {
            case 0: osc_.setWaveform(Krate::DSP::OscWaveform::Sine); break;
            case 1: osc_.setWaveform(Krate::DSP::OscWaveform::Sawtooth); break;
            case 2: osc_.setWaveform(Krate::DSP::OscWaveform::Square); break;
            default: osc_.setWaveform(Krate::DSP::OscWaveform::Sine); break;
        }
    }

    void setDecay(float ms) noexcept {
        decayMs_ = std::max(1.0f, ms);
    }

    void setVolume(float vol) noexcept {
        volume_ = std::clamp(vol, 0.0f, 1.0f);
    }

    void noteOn(uint8_t pitch, uint8_t velocity) noexcept {
        float freq = 440.0f * std::pow(2.0f, (static_cast<float>(pitch) - 69.0f) / 12.0f);
        osc_.setFrequency(freq);
        velocity_ = static_cast<float>(velocity) / 127.0f;
        envPhase_ = EnvPhase::Attack;
        envLevel_ = 0.0f;
        active_ = true;
    }

    void noteOff() noexcept {
        if (active_) {
            envPhase_ = EnvPhase::Release;
        }
    }

    void processBlock(float* outL, float* outR, size_t numSamples) noexcept {
        if (!active_) return;

        constexpr float kAttackMs = 2.0f;
        constexpr float kSustainLevel = 0.6f;
        constexpr float kReleaseMs = 30.0f;

        const float attackInc = 1.0f / (kAttackMs * 0.001f * static_cast<float>(sampleRate_));
        const float decayInc = (1.0f - kSustainLevel) /
            (decayMs_ * 0.001f * static_cast<float>(sampleRate_));
        const float releaseInc = envLevel_ /
            std::max(1.0f, kReleaseMs * 0.001f * static_cast<float>(sampleRate_));

        for (size_t i = 0; i < numSamples; ++i) {
            // Advance envelope
            switch (envPhase_) {
                case EnvPhase::Attack:
                    envLevel_ += attackInc;
                    if (envLevel_ >= 1.0f) {
                        envLevel_ = 1.0f;
                        envPhase_ = EnvPhase::Decay;
                    }
                    break;
                case EnvPhase::Decay:
                    envLevel_ -= decayInc;
                    if (envLevel_ <= kSustainLevel) {
                        envLevel_ = kSustainLevel;
                        envPhase_ = EnvPhase::Sustain;
                    }
                    break;
                case EnvPhase::Sustain:
                    // Hold at sustain level
                    break;
                case EnvPhase::Release:
                    envLevel_ -= releaseInc;
                    if (envLevel_ <= 0.0f) {
                        envLevel_ = 0.0f;
                        active_ = false;
                        return; // Stop processing
                    }
                    break;
            }

            float sample = osc_.process() * envLevel_ * velocity_ * volume_;
            outL[i] += sample;
            outR[i] += sample;
        }
    }

    [[nodiscard]] bool isActive() const noexcept { return active_; }

private:
    enum class EnvPhase { Attack, Decay, Sustain, Release };

    Krate::DSP::PolyBlepOscillator osc_;
    double sampleRate_ = 44100.0;
    float velocity_ = 0.0f;
    float volume_ = 0.7f;
    float decayMs_ = 200.0f;
    float envLevel_ = 0.0f;
    EnvPhase envPhase_ = EnvPhase::Release;
    bool active_ = false;
};

} // namespace Gradus
