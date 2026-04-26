#pragma once

// ==============================================================================
// NoiseBody -- Phase 2 (data-model.md §3.7), refactored in Phase 7 to delegate
// the noise path to the shared `NoiseLayer` component.
// ==============================================================================
// Two-layer hybrid: modal layer (shared ModalResonatorBank configured with
// plate_modes.h extended to kModeCount entries) + noise layer (delegated to
// Membrum::NoiseLayer using the raw Hz/ms values produced by NoiseBodyMapper).
// The two layers are mixed at the mapper-produced modal/noise ratio
// (FR-025 / FR-062).
//
// The Phase 7 refactor removes the previous duplicated NoiseOscillator + SVF +
// ADSR plumbing that also lived inline in DrumVoice. Behaviour is byte-
// identical to the pre-refactor inline implementation because NoiseLayer
// calls the same underlying Krate primitives with the same raw values.
//
// Mode count history (FR-062 / Phase 9 T128):
//   - Research proposal (research.md §4.6):            40 modes
//   - Phase 9 final setting:                           40 modes (restored)
// ==============================================================================

#include "../noise_layer.h"
#include "../voice_common_params.h"
#include "noise_body_mapper.h"

#include <krate/dsp/core/pattern_freeze_types.h>
#include <krate/dsp/processors/modal_resonator_bank.h>

#include <cstdint>

namespace Membrum {

struct NoiseBody
{
    // FR-062: start at 40 modes; reduce only if Phase 9 benchmark exceeds
    // the 1.25% single-voice CPU budget.
    static constexpr int kModeCount = Bodies::NoiseBodyMapper::kNoiseBodyModeCount;

    NoiseLayer noiseLayer_;
    float      modalMix_ = 0.6f;

    void prepare(double sampleRate, std::uint32_t voiceId) noexcept
    {
        noiseLayer_.prepare(sampleRate, voiceId);
        // NoiseBody is historically white noise through its bandpass; keep
        // that explicit since configureRaw() does not touch the color.
        noiseLayer_.oscillator().setColor(Krate::DSP::NoiseColor::White);
    }

    void reset(Krate::DSP::ModalResonatorBank& sharedBank) noexcept
    {
        sharedBank.reset();
        noiseLayer_.reset();
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
                            r.modal.damping,
                            r.modal.stretch,
                            r.modal.scatter);

        // Noise layer: route the mapper's raw Hz/ms/mix values into the shared
        // NoiseLayer primitive. The layer's internal gain is the noise mix.
        noiseLayer_.configureRaw(r.noiseFilterCutoffHz,
                                 r.noiseFilterResonance,
                                 r.noiseAttackMs,
                                 r.noiseDecayMs,
                                 r.noiseMix);
        noiseLayer_.trigger(/*velocity*/ 1.0f);

        modalMix_ = r.modalMix;
    }

    [[nodiscard]] float processSample(Krate::DSP::ModalResonatorBank& sharedBank,
                                      float excitation) noexcept
    {
        const float modalOut = sharedBank.processSample(excitation);
        const float noiseOut = noiseLayer_.processSample();
        return modalMix_ * modalOut + noiseOut;
    }

    [[nodiscard]] float processSampleNoSmooth(Krate::DSP::ModalResonatorBank& sharedBank,
                                              float excitation) noexcept
    {
        const float modalOut = sharedBank.processSampleNoSmooth(excitation);
        const float noiseOut = noiseLayer_.processSample();
        return modalMix_ * modalOut + noiseOut;
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
                out[offset + i] = modalMix_ * modalScratch[i]
                                + noiseLayer_.processSample();
            }
            offset    += chunk;
            remaining -= chunk;
        }
    }
};

} // namespace Membrum
