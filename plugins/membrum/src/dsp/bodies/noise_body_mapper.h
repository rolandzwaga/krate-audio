#pragma once

// ==============================================================================
// NoiseBodyMapper -- Hybrid modal + noise body helper (data-model.md §4)
// ==============================================================================
// Produces:
//   - A MapperResult configured with plate ratios (extended table) at
//     kNoiseBodyModeCount entries. This drives the shared ModalResonatorBank
//     inside NoiseBody.
//   - Noise-layer parameters (SVF cutoff, resonance, envelope times, mix).
//
//   Size          -> fundamental (FR-032): f0 = 1500 * 0.1^size
//   Strike pos    -> per-mode amplitude via computePlateAmplitude
//   Material      -> metallic bright damping, noise filter cutoff bias
//   Decay         -> decayTime multiplier + noise envelope scale
//   modeStretch   -> forwarded to bank
//   decaySkew     -> scalar-bias
// ==============================================================================

#include "../voice_common_params.h"
#include "membrane_mapper.h"  // for MapperResult
#include "plate_modes.h"

#include <krate/dsp/processors/modal_resonator_bank.h>

#include <algorithm>
#include <cmath>

namespace Membrum::Bodies {

struct NoiseBodyMapper
{
    // FR-062 / Phase 9 T128: 32 modes (AVX2-aligned, post-Phase 9 tuning).
    //
    // History:
    //   1. Initial research.md §4.6 target: 40 modes.
    //   2. Pre-SIMD Phase 9 measurement @ 40 modes: ~8.7% CPU for
    //      Impulse + NoiseBody + TS + UN on the scalar per-sample path.
    //   3. plan.md §SIMD Emergency Fallback landed: DrumVoice block hot path
    //      routes through BodyBank::processBlock → NoiseBody::processBlock →
    //      ModalResonatorBank::processBlock (SIMD via Highway kernel).
    //      40 modes on the SIMD fast path measured under budget for all
    //      five non-Feedback exciters.
    //   4. BUT: FeedbackExciter requires strict per-sample body-feedback
    //      semantics (research.md §3) and uses DrumVoice's SLOW path —
    //      NoiseBody::processSample → sharedBank.processSample per sample.
    //      At 40 modes the slow path pays `smoothCoefficients()` every
    //      sample (~3 FLOPs × 40 modes = 120 FLOPs/sample overhead) on
    //      top of the core resonator math. Feedback+NoiseBody+TS+UN
    //      measured 1.43% — 14% over the 1.25% budget.
    //   5. Interim attempt: 30 modes. REGRESSED to ~5% CPU because
    //      `processModalBankSampleSIMD` (Highway kernel) runs 3 full AVX2
    //      SIMD iters (24 modes) + a SCALAR TAIL of 6 modes per sample,
    //      and the 6-mode scalar tail dominated the per-sample cost on
    //      the FAST path for the other 5 exciters. Lesson: mode count must
    //      be a multiple of the widest SIMD lane on the target ISA (AVX2=8).
    //   6. Final: 32 modes = 4 clean AVX2 SIMD iters, zero scalar tail,
    //      and 20% lower slow-path `smoothCoefficients()` overhead vs 40.
    //      This is the best tradeoff for both hot paths simultaneously.
    //
    // Note: `smoothCoefficients()` is called ONCE per block on the SIMD
    // fast path (fine) but ONCE PER SAMPLE on the slow path (expensive).
    // The slow path is only hit when FeedbackExciter is active.
    static constexpr int kNoiseBodyModeCount = 32;

    struct Result
    {
        MapperResult modal{};                 // fed to sharedBank.setModes
        float noiseFilterCutoffHz  = 2000.0f;
        float noiseFilterResonance = 0.7f;
        float noiseAttackMs        = 1.0f;
        float noiseDecayMs         = 120.0f;
        float modalMix             = 0.6f;
        float noiseMix             = 0.4f;
    };

    [[nodiscard]] static Result map(const VoiceCommonParams& params,
                                    float /*pitchHz*/) noexcept
    {
        Result out{};

        // ----- Modal layer ----- (plate-ratio extended to kNoiseBodyModeCount)
        const float f0 = 1500.0f * std::pow(0.1f, params.size);

        const int numModes = std::min(
            kNoiseBodyModeCount,
            Krate::DSP::ModalResonatorBank::kMaxModes);

        for (int k = 0; k < numModes; ++k)
            out.modal.frequencies[k] = f0 * kPlateRatios[k];

        for (int k = 0; k < numModes; ++k)
        {
            float a = std::abs(computePlateAmplitude(k, params.strikePos));
            a = std::max(a, 0.03f);
            out.modal.amplitudes[k] = a;
        }

        // Metallic, shimmering - cymbal/hi-hat palette
        out.modal.brightness = 0.7f + 0.3f * params.material;

        const float stretchNorm = std::clamp(
            (params.modeStretch - 0.5f) * (1.0f / 1.5f), 0.0f, 1.0f);
        out.modal.stretch = stretchNorm;

        // Kept modest so the 10x Decay knob stays below the bank's 5 s clamp.
        const float baseDecayTime =
            lerp(0.2f, 1.0f, params.material) * (1.0f + 0.1f * params.size);
        const float skewBias = 1.0f - 0.3f * params.decaySkew;
        out.modal.decayTime =
            baseDecayTime *
            std::exp(lerp(std::log(0.3f), std::log(3.0f), params.decay)) *
            skewBias;

        out.modal.numPartials = numModes;
        out.modal.scatter     = 0.0f;
        out.modal.damping     = dampingLawFromParams(
            params, out.modal.decayTime, out.modal.brightness);

        // ----- Noise layer -----
        // Centered around the bright metallic region so a cymbal has
        // recognizable hiss content; material shifts the cutoff up.
        out.noiseFilterCutoffHz = std::clamp(
            1500.0f + 5000.0f * params.material, 200.0f, 15000.0f);
        out.noiseFilterResonance = 0.6f + 0.4f * params.material;

        // Noise envelope scales with Decay parameter.
        out.noiseAttackMs = 0.5f;
        out.noiseDecayMs  = 60.0f *
            std::exp(lerp(std::log(0.3f), std::log(3.0f), params.decay));

        out.modalMix = 0.6f;
        out.noiseMix = 0.4f;

        return out;
    }

private:
    static float lerp(float a, float b, float t) noexcept
    {
        return a + (b - a) * t;
    }
};

} // namespace Membrum::Bodies
