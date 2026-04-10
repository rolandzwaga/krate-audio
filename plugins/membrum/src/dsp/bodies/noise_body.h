#pragma once

// ==============================================================================
// NoiseBody -- Phase 2 stub (data-model.md §3.7)
// ==============================================================================
// Structurally-complete stub. Phase 2.A is silent. Phase 4 replaces this with
// the two-layer (modal + filtered noise burst) implementation.
// ==============================================================================

#include "../voice_common_params.h"

#include <krate/dsp/processors/modal_resonator_bank.h>

#include <cstdint>

namespace Membrum {

struct NoiseBody
{
    static constexpr int kModeCount = 40;  // FR-062 starting mode count

    float modalMix_ = 0.6f;
    float noiseMix_ = 0.4f;

    void prepare(double, std::uint32_t) noexcept {}
    void reset(Krate::DSP::ModalResonatorBank& sharedBank) noexcept { sharedBank.reset(); }

    void configureForNoteOn(Krate::DSP::ModalResonatorBank& /*sharedBank*/,
                            const VoiceCommonParams& /*params*/,
                            float /*pitchHz*/) noexcept {}

    [[nodiscard]] float processSample(Krate::DSP::ModalResonatorBank& /*sharedBank*/,
                                      float /*excitation*/) noexcept
    {
        return 0.0f;
    }
};

} // namespace Membrum
