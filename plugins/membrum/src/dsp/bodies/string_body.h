#pragma once

// ==============================================================================
// StringBody -- Phase 2 stub (data-model.md §3.6)
// ==============================================================================
// Owns its own Krate::DSP::WaveguideString (ignores sharedBank). Phase 4
// wires up pitch, decay, brightness and pick position from the mapper.
// Phase 2.A: structurally complete, silent output.
// ==============================================================================

#include "../voice_common_params.h"

#include <krate/dsp/processors/modal_resonator_bank.h>
#include <krate/dsp/processors/waveguide_string.h>

#include <cstdint>

namespace Membrum {

struct StringBody
{
    Krate::DSP::WaveguideString string_;

    void prepare(double sampleRate, std::uint32_t voiceId) noexcept
    {
        string_.prepare(sampleRate);
        string_.prepareVoice(voiceId);
    }

    void reset(Krate::DSP::ModalResonatorBank& /*sharedBank*/) noexcept
    {
        // WaveguideString does not expose a public reset() — re-preparing the
        // per-voice state happens in configureForNoteOn() in Phase 4.
    }

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
