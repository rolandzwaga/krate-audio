#pragma once

// ==============================================================================
// BellBody -- Phase 2 stub (data-model.md §3.5)
// ==============================================================================

#include "../voice_common_params.h"

#include <krate/dsp/processors/modal_resonator_bank.h>

#include <cstdint>

namespace Membrum {

struct BellBody
{
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
