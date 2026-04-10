#pragma once

// ==============================================================================
// MembraneBody -- Phase 2 (data-model.md §3.2)
// ==============================================================================
// Thin stateless wrapper that configures the shared ModalResonatorBank using
// Phase 1's Bessel membrane mapping (see MembraneMapper) and delegates per-
// sample processing to the bank.
// ==============================================================================

#include "../voice_common_params.h"
#include "membrane_mapper.h"

#include <krate/dsp/processors/modal_resonator_bank.h>

#include <cstdint>

namespace Membrum {

struct MembraneBody
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
        const auto result = Bodies::MembraneMapper::map(params, pitchHz);
        sharedBank.setModes(result.frequencies,
                            result.amplitudes,
                            result.numPartials,
                            result.decayTime,
                            result.brightness,
                            result.stretch,
                            result.scatter);
    }

    [[nodiscard]] float processSample(Krate::DSP::ModalResonatorBank& sharedBank,
                                      float excitation) noexcept
    {
        return sharedBank.processSample(excitation);
    }

    // Block-rate fast path (Phase 9 SIMD emergency fallback / plan.md §SIMD).
    // Delegates to ModalResonatorBank::processBlock, which uses the Highway
    // SIMD kernel (processModalBankSampleSIMD) for the per-mode inner loop.
    void processBlock(Krate::DSP::ModalResonatorBank& sharedBank,
                      const float* excitation,
                      float* out,
                      int numSamples) noexcept
    {
        sharedBank.processBlock(excitation, out, numSamples);
    }
};

} // namespace Membrum
