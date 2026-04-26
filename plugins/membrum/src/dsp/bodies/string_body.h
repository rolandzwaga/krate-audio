#pragma once

// ==============================================================================
// StringBody -- Phase 2 (data-model.md §3.6)
// ==============================================================================
// Owns a Krate::DSP::WaveguideString. Ignores the shared ModalResonatorBank
// entirely (FR-023, first body model to break out of the shared bank).
//
// The waveguide lifecycle used here is:
//   configureForNoteOn()
//     -> setFrequency / setDecay / setBrightness / setPickPosition
//     -> noteOn(f0, velocity)  (initializes delay-line lengths + loop state)
// Phase 2 uses a fixed velocity=1.0 inside noteOn; the audible output is
// driven by the exciter through processSample(excitation) which calls
// waveguide_.process(excitation). The exciter already embeds user velocity.
// ==============================================================================

#include "../voice_common_params.h"
#include "string_mapper.h"

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
        string_.silence();
    }

    void configureForNoteOn(Krate::DSP::ModalResonatorBank& /*sharedBank*/,
                            const VoiceCommonParams& params,
                            float pitchHz) noexcept
    {
        const auto r = Bodies::StringMapper::map(params, pitchHz);

        string_.setFrequency(r.frequencyHz);
        string_.setDecay(r.decayTime);
        string_.setBrightness(r.brightness);
        string_.setPickPosition(r.pickPosition);

        // Initialize delay lines and loop state at the target frequency.
        // The excitation noise burst it writes is fine; once the exciter
        // starts feeding samples through processSample, that transient
        // plus the external excitation drives the waveguide.
        string_.noteOn(r.frequencyHz, 1.0f);
    }

    // IMPORTANT: IGNORES sharedBank (FR-023, shared-bank isolation contract).
    [[nodiscard]] float processSample(
        Krate::DSP::ModalResonatorBank& /*sharedBank*/,
        float excitation) noexcept
    {
        return string_.process(excitation);
    }

    /// String body doesn't use the modal bank, so the no-smooth variant is
    /// just the same per-sample call. Symmetric API for the slow-path caller.
    [[nodiscard]] float processSampleNoSmooth(
        Krate::DSP::ModalResonatorBank& /*sharedBank*/,
        float excitation) noexcept
    {
        return string_.process(excitation);
    }

    // Block-rate entry point (Phase 9 SIMD emergency fallback / plan.md §SIMD).
    // WaveguideString has no SIMD block entry, so we loop per-sample. The
    // real win here is that the outer per-sample chain (unnatural + tone
    // shaper + env) can still be block-free of std::variant dispatch.
    void processBlock(Krate::DSP::ModalResonatorBank& /*sharedBank*/,
                      const float* excitation,
                      float* out,
                      int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
            out[i] = string_.process(excitation[i]);
    }
};

} // namespace Membrum
