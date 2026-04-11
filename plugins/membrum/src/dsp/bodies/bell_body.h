#pragma once

// ==============================================================================
// BellBody -- Phase 2 (data-model.md §3.5)
// ==============================================================================
// Stateless wrapper: configures the shared bank from BellMapper. 16 modes
// (FR-024). Metallic defaults produce long-ringing partials.
// ==============================================================================

#include "../voice_common_params.h"
#include "bell_mapper.h"

#include <krate/dsp/processors/modal_resonator_bank.h>

#include <cstdint>

namespace Membrum {

struct BellBody
{
    void prepare(double /*sampleRate*/, std::uint32_t /*voiceId*/) noexcept {}

    void reset(Krate::DSP::ModalResonatorBank& sharedBank) noexcept
    {
        sharedBank.reset();
    }

    void configureForNoteOn(Krate::DSP::ModalResonatorBank& sharedBank,
                            const VoiceCommonParams& params,
                            float pitchHz) noexcept
    {
        const auto r = Bodies::BellMapper::map(params, pitchHz);
        sharedBank.setModes(r.frequencies,
                            r.amplitudes,
                            r.numPartials,
                            r.decayTime,
                            r.brightness,
                            r.stretch,
                            r.scatter);
    }

    [[nodiscard]] float processSample(Krate::DSP::ModalResonatorBank& sharedBank,
                                      float excitation) noexcept
    {
        return sharedBank.processSample(excitation);
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
