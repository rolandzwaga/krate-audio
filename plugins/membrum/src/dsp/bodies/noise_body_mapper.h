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
    //   6. Post-Phase-9: 32 modes = 4 clean AVX2 SIMD iters, zero scalar tail.
    //   7. Crash redesign (CRASH-REDESIGN-PLAN.md Phase 5): 64 modes. A crash's
    //      identity is a DENSE inharmonic cloud that fuses into wash; 32 modes
    //      over 0.7-20 kHz leave ~600 Hz spacing -> individually resolvable ->
    //      a pitched chime (defect D6 / AC-4). 64 halves the spacing so the
    //      upper cluster reads as colored noise. 64 = 8 clean AVX2 iters (zero
    //      scalar tail) and stays within the bank's kMaxModes (96).
    //      FeedbackExciter forces the per-sample SLOW path (smoothCoefficients
    //      every sample); to keep that path affordable, DrumVoice caps NoiseBody
    //      at kFeedbackModeCap (32) modes via VoiceCommonParams::maxModes when
    //      the Feedback exciter is active (a drone/FX combo where wash density
    //      matters least).
    //
    // Note: `smoothCoefficients()` is called ONCE per block on the SIMD
    // fast path (fine) but ONCE PER SAMPLE on the slow path (expensive).
    // The slow path is only hit when FeedbackExciter is active.
    static constexpr int kNoiseBodyModeCount = 64;

    /// Cap applied on the FeedbackExciter slow path (per-sample smoothing).
    static constexpr int kFeedbackModeCap = 32;

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

        int numModes = std::min(
            kNoiseBodyModeCount,
            Krate::DSP::ModalResonatorBank::kMaxModes);
        // Feedback slow-path cap (Phase 5): keep the per-sample smoothing path
        // affordable. 0 = no cap.
        if (params.maxModes > 0)
            numModes = std::min(numModes, params.maxModes);

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

        // Per-mode amplitude tilt for Decay Skew (audit M-5): boost high modes
        // by ratio^(-decaySkew) so the skew re-balances the modal spectrum
        // rather than only nudging global decay. decaySkew == 0 is bit-identical
        // (guarded), preserving the default-off path.
        if (params.decaySkew != 0.0f)
        {
            for (int k = 0; k < numModes; ++k)
            {
                const float ratio = kPlateRatios[k];
                if (ratio > 0.0f)
                    out.modal.amplitudes[k] *= std::clamp(
                        std::exp(-params.decaySkew * std::log(ratio)),
                        1.0f / kDecaySkewMaxModeTilt, kDecaySkewMaxModeTilt);
            }
        }

        out.modal.numPartials = numModes;
        out.modal.scatter     = std::clamp(params.modeScatter, 0.0f, 1.0f);
        out.modal.damping     = dampingLawFromParams(
            params, out.modal.decayTime, out.modal.brightness);

        // ----- Noise layer -----
        // Centered around the bright metallic region so a cymbal has
        // recognizable hiss content; material shifts the cutoff up.
        out.noiseFilterCutoffHz = std::clamp(
            1500.0f + 5000.0f * params.material, 200.0f, 15000.0f);
        out.noiseFilterResonance = 0.6f + 0.4f * params.material;

        // Noise envelope. For SHORT-decay bodies (hats/toms, decay <= 0.5) this
        // is the original fixed formula (bit-identical). For LONG-decay bodies
        // (cymbals) the wash is blended toward the modal lowest-mode T60 so the
        // sizzle bed rings for the full crash tail instead of dying in ~90 ms
        // under a multi-second modal ring (CRASH-REDESIGN-PLAN.md Phase 2 / T5).
        out.noiseAttackMs = 0.5f;
        const float shortDecayMs = 60.0f *
            std::exp(lerp(std::log(0.3f), std::log(3.0f), params.decay));
        // Lowest-mode (fundamental) T60 from the damping law populated above.
        const float rateLow = out.modal.damping.b1 +
            out.modal.damping.b3 * f0 * f0;
        const float t60LowMs = 6.9078f / std::max(rateLow, 0.2f) * 1000.0f;
        const float longDecayMs = t60LowMs * 0.55f;
        // Blend gate: 0 for decay <= 0.5 (short formula wins, hat-safe), rising
        // to 1 at decay = 1.0 so only genuinely long instruments get the wash.
        const float washBlend = std::clamp((params.decay - 0.5f) * 2.0f, 0.0f, 1.0f);
        out.noiseDecayMs = std::clamp(
            lerp(shortDecayMs, longDecayMs, washBlend), 20.0f, 3000.0f);

        out.modalMix = 0.6f;
        // Brighter / more-metallic bodies carry more of their tail in the noise
        // wash (a cymbal's sizzle), darker bodies less (Phase 2 / T5). Default
        // material 0.5 gives 0.45, close to the legacy 0.4.
        out.noiseMix = lerp(0.35f, 0.55f, params.material);

        return out;
    }

private:
    static float lerp(float a, float b, float t) noexcept
    {
        return a + (b - a) * t;
    }
};

} // namespace Membrum::Bodies
