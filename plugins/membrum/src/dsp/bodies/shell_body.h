#pragma once

// ==============================================================================
// ShellBody -- Phase 2 (data-model.md §3.4)
// ==============================================================================
// Stateless wrapper: configures the shared bank from ShellMapper. 12 modes
// (FR-022).
// ==============================================================================

#include "../voice_common_params.h"
#include "shell_mapper.h"

#include <krate/dsp/processors/modal_resonator_bank.h>

#include <cstdint>

namespace Membrum {

struct ShellBody
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
        const auto r = Bodies::ShellMapper::map(params, pitchHz);
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
    //
    // Shell has only 12 modes, which is awkward for wide SIMD (AVX2 lane 8
    // produces 1 SIMD iter + 4 scalar tail). Rather than pay the Highway
    // dynamic-dispatch per-sample overhead of the pure SIMD processBlock,
    // we use the **scalar** block overload `processBlock(in, out, n, decayScale)`
    // at decayScale=1.0f. That overload smooths coefficients ONCE per block
    // (instead of per sample) and loops `processSampleCore` inline — so we
    // get the per-sample scalar path's cache/inlining behavior while
    // amortizing `smoothCoefficients()` (3 FLOPs × numModes per sample →
    // 3 FLOPs × numModes per block), which is a ~30% reduction in Shell's
    // per-sample cost. The variant dispatch is still only one std::visit
    // per block (from BodyBank::processBlock).
    void processBlock(Krate::DSP::ModalResonatorBank& sharedBank,
                      const float* excitation,
                      float* out,
                      int numSamples) noexcept
    {
        sharedBank.processBlock(excitation, out, numSamples, 1.0f);
    }
};

} // namespace Membrum
