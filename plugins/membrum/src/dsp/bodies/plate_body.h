#pragma once

// ==============================================================================
// PlateBody -- Phase 2 (data-model.md §3.3)
// ==============================================================================
// Stateless wrapper: delegates to the shared ModalResonatorBank via setModes
// at noteOn (FR-026), then per-sample processing is a thin pass-through to
// the shared bank. 16 modes by default (FR-021).
// ==============================================================================

#include "../voice_common_params.h"
#include "plate_mapper.h"

#include <krate/dsp/processors/modal_resonator_bank.h>

#include <cstdint>

namespace Membrum {

struct PlateBody
{
    void prepare(double /*sampleRate*/, std::uint32_t /*voiceId*/) noexcept
    {
        // sharedBank_ is owned by BodyBank and prepared separately.
    }

    void reset(Krate::DSP::ModalResonatorBank& sharedBank) noexcept
    {
        sharedBank.reset();
    }

    void configureForNoteOn(Krate::DSP::ModalResonatorBank& sharedBank,
                            const VoiceCommonParams& params,
                            float pitchHz) noexcept
    {
        const auto r = Bodies::PlateMapper::map(params, pitchHz);
        sharedBank.setModes(r.frequencies,
                            r.amplitudes,
                            r.numPartials,
                            r.damping,
                            r.stretch,
                            r.scatter);
    }

    [[nodiscard]] float processSample(Krate::DSP::ModalResonatorBank& sharedBank,
                                      float excitation) noexcept
    {
        return sharedBank.processSample(excitation);
    }

    [[nodiscard]] float processSampleNoSmooth(Krate::DSP::ModalResonatorBank& sharedBank,
                                              float excitation) noexcept
    {
        return sharedBank.processSampleNoSmooth(excitation);
    }

    // Block-rate fast path (Phase 9 SIMD emergency fallback / plan.md §SIMD).
    void processBlock(Krate::DSP::ModalResonatorBank& sharedBank,
                      const float* excitation,
                      float* out,
                      int numSamples) noexcept
    {
        sharedBank.processBlock(excitation, out, numSamples);
    }
};

} // namespace Membrum
