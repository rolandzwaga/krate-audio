#pragma once

// ==============================================================================
// NoiseBody -- Phase 2 (data-model.md §3.7)
// ==============================================================================
// Two-layer hybrid: modal layer (shared ModalResonatorBank configured with
// plate_modes.h extended to kModeCount entries) + noise layer (NoiseOscillator
// -> SVF bandpass + ADSR envelope). The two layers are mixed at the default
// 0.6 modal / 0.4 noise ratio (FR-025 / FR-062).
//
// Mode count history (FR-062 / Phase 9 T128):
//   - Research proposal (research.md §4.6):            40 modes
//   - Phase 9 initial measurement @ 40 modes (Impulse + NB + TS + UZ on the
//     per-sample scalar path): ~8.70% CPU — ~7× over the 1.25% per-voice
//     budget. Reducing the mode count alone was insufficient; see below.
//   - Phase 9 SIMD emergency fallback (plan.md §SIMD Emergency Fallback /
//     FR-071): DrumVoice's hot path was refactored to use
//     BodyBank::processBlock, which now routes the modal layer through
//     ModalResonatorBank::processBlock (already SIMD-accelerated via the
//     Highway kernel processModalBankSampleSIMD). With the block-rate body
//     path active, the scalar-per-sample hot spot is eliminated.
//   - Phase 9 final setting:                           40 modes (restored)
//
// The block-rate body path is the direct execution of plan.md's emergency
// fallback clause. The earlier mode-count reduction (40 -> 20) was a patch
// on the wrong symptom; the real hotspot was per-sample variant dispatch +
// per-sample scalar modal MACs, both now removed.
// ==============================================================================

#include "../voice_common_params.h"
#include "noise_body_mapper.h"

#include <krate/dsp/core/pattern_freeze_types.h>
#include <krate/dsp/primitives/adsr_envelope.h>
#include <krate/dsp/primitives/noise_oscillator.h>
#include <krate/dsp/primitives/svf.h>
#include <krate/dsp/processors/modal_resonator_bank.h>

#include <cstdint>

namespace Membrum {

struct NoiseBody
{
    // FR-062: start at 40 modes; reduce only if Phase 9 benchmark exceeds
    // the 1.25% single-voice CPU budget.
    static constexpr int kModeCount = Bodies::NoiseBodyMapper::kNoiseBodyModeCount;

    Krate::DSP::NoiseOscillator noise_;
    Krate::DSP::SVF             noiseFilter_;
    Krate::DSP::ADSREnvelope    noiseEnvelope_;

    float modalMix_ = 0.6f;
    float noiseMix_ = 0.4f;

    void prepare(double sampleRate, std::uint32_t voiceId) noexcept
    {
        noise_.prepare(sampleRate);
        noise_.setSeed(voiceId + 1u);  // per-voice determinism
        noise_.setColor(Krate::DSP::NoiseColor::White);

        noiseFilter_.prepare(sampleRate);
        noiseFilter_.setMode(Krate::DSP::SVFMode::Bandpass);
        noiseFilter_.setCutoff(2000.0f);
        noiseFilter_.setResonance(0.7f);

        noiseEnvelope_.prepare(static_cast<float>(sampleRate));
        noiseEnvelope_.setAttack(1.0f);
        noiseEnvelope_.setDecay(120.0f);
        noiseEnvelope_.setSustain(0.0f);
        noiseEnvelope_.setRelease(60.0f);
    }

    void reset(Krate::DSP::ModalResonatorBank& sharedBank) noexcept
    {
        sharedBank.reset();
        noise_.reset();
        noiseFilter_.reset();
        noiseEnvelope_.reset();
    }

    void configureForNoteOn(Krate::DSP::ModalResonatorBank& sharedBank,
                            const VoiceCommonParams& params,
                            float pitchHz) noexcept
    {
        const auto r = Bodies::NoiseBodyMapper::map(params, pitchHz);

        // Modal layer
        sharedBank.setModes(r.modal.frequencies,
                            r.modal.amplitudes,
                            r.modal.numPartials,
                            r.modal.decayTime,
                            r.modal.brightness,
                            r.modal.stretch,
                            r.modal.scatter);

        // Noise layer: SVF bandpass with per-note cutoff bias.
        noiseFilter_.setCutoff(r.noiseFilterCutoffHz);
        noiseFilter_.setResonance(r.noiseFilterResonance);

        // Noise envelope
        noiseEnvelope_.setAttack(r.noiseAttackMs);
        noiseEnvelope_.setDecay(r.noiseDecayMs);
        noiseEnvelope_.setSustain(0.0f);
        noiseEnvelope_.reset();
        noiseEnvelope_.gate(true);   // trigger immediately

        modalMix_ = r.modalMix;
        noiseMix_ = r.noiseMix;
    }

    [[nodiscard]] float processSample(Krate::DSP::ModalResonatorBank& sharedBank,
                                      float excitation) noexcept
    {
        const float modalOut = sharedBank.processSample(excitation);

        const float nSample  = noise_.process();
        const float filtered = noiseFilter_.process(nSample);
        const float env      = noiseEnvelope_.process();
        const float noiseOut = env * filtered;

        return modalMix_ * modalOut + noiseMix_ * noiseOut;
    }

    // Block-rate fast path (Phase 9 SIMD emergency fallback / plan.md §SIMD).
    // Modal layer runs through the SIMD-accelerated ModalResonatorBank::processBlock
    // into a stack scratch buffer, then the noise layer is mixed per-sample.
    void processBlock(Krate::DSP::ModalResonatorBank& sharedBank,
                      const float* excitation,
                      float* out,
                      int numSamples) noexcept
    {
        // Stack scratch for the modal layer. The DrumVoice caps block size
        // at kMaxBlockSize so this is always large enough.
        static constexpr int kMaxBlockSize = 2048;
        float modalScratch[kMaxBlockSize];

        int remaining = numSamples;
        int offset    = 0;
        while (remaining > 0)
        {
            const int chunk = remaining < kMaxBlockSize ? remaining : kMaxBlockSize;
            sharedBank.processBlock(excitation + offset, modalScratch, chunk);
            for (int i = 0; i < chunk; ++i)
            {
                const float nSample  = noise_.process();
                const float filtered = noiseFilter_.process(nSample);
                const float env      = noiseEnvelope_.process();
                const float noiseOut = env * filtered;
                out[offset + i] = modalMix_ * modalScratch[i] + noiseMix_ * noiseOut;
            }
            offset    += chunk;
            remaining -= chunk;
        }
    }
};

} // namespace Membrum
