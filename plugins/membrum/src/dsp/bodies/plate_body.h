#pragma once

// ==============================================================================
// PlateBody -- Phase 2 stub (data-model.md §3.3)
// ==============================================================================
// Structurally-complete stub. Phase 4 adds Kirchhoff plate mode mapping.
// ==============================================================================

#include "../voice_common_params.h"

#include <krate/dsp/processors/modal_resonator_bank.h>

#include <cstdint>

namespace Membrum {

struct PlateBody
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
