#pragma once

// ==============================================================================
// MembraneMapper -- Phase 1 Bessel membrane mapping (data-model.md §4)
// ==============================================================================
// Extracts Phase 1's inline DrumVoice::noteOn mapping verbatim into a stateless
// helper. FR-031: bit-identical output to Phase 1's inline code for the same
// input (material, size, decay, strikePos). pitchHz is accepted for API
// uniformity but ignored here — Phase 1's f0 is derived from `size` alone.
// ==============================================================================

#include "../membrane_modes.h"
#include "../voice_common_params.h"

#include <krate/dsp/processors/modal_resonator_bank.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace Membrum::Bodies {

struct MapperResult
{
    static constexpr int kMaxModes = Krate::DSP::ModalResonatorBank::kMaxModes;

    float frequencies[kMaxModes]{};
    float amplitudes[kMaxModes]{};
    int   numPartials = 0;
    float decayTime   = 0.0f;
    float brightness  = 0.0f;
    float stretch     = 0.0f;
    float scatter     = 0.0f;

    // Phase 8A: explicit per-mode damping law. Always populated by the mapper:
    // when VoiceCommonParams.bodyDampingB1/B3 are sentinels (-1.0f), this is
    // derived from decayTime/brightness (legacy bit-identical path); when
    // non-sentinel, this takes precedence over decayTime/brightness and
    // directly drives ModalResonatorBank::setModes.
    Krate::DSP::ModalResonatorBank::DampingLaw damping{};
};

/// Build a DampingLaw from the mapper's legacy decayTime/brightness pair,
/// letting explicit VoiceCommonParams.bodyDamping{B1,B3} overrides take
/// precedence when they are not the sentinel value (-1.0f).
/// Denormalisation for the override path:
///   b1 = 0.2 + norm * 49.8     -> [0.2, 50.0] s^-1
///   b3 = norm * 4.0e-4         -> [0, 4e-4] s * rad^-2 (~10 x legacy kMaxB3).
///   b3 range widened vs. plan value of 8e-5 after empirical testing: at
///   the plan's ceiling the above-1-kHz band only lost ~2 dB, which is at
///   the edge of audibility. 4e-4 yields ~10 dB drop in the high band
///   while still leaving below-1-kHz modes largely intact -- the plan's
///   "metallic ring -> wood thump" perceptual contract.
[[nodiscard]] inline Krate::DSP::ModalResonatorBank::DampingLaw
dampingLawFromParams(const VoiceCommonParams& params,
                     float legacyDecayTime,
                     float legacyBrightness) noexcept
{
    auto law = Krate::DSP::ModalResonatorBank::dampingLawFromLegacy(
        legacyDecayTime, legacyBrightness);
    if (params.bodyDampingB1 >= 0.0f) {
        const float n = std::clamp(params.bodyDampingB1, 0.0f, 1.0f);
        law.b1 = 0.2f + n * 49.8f;
    }
    if (params.bodyDampingB3 >= 0.0f) {
        const float n = std::clamp(params.bodyDampingB3, 0.0f, 1.0f);
        law.b3 = n * 1.0e-3f;
    }
    return law;
}

struct MembraneMapper
{
    // Phase 8B: mode count 16 -> 48. Aligned to AVX2 8-lane kernel
    // (48 / 8 = 6 clean iters). Matches Chromaphone 3's "High" preset.
    static constexpr int kMembraneModeCount = 48;

    [[nodiscard]] static MapperResult map(const VoiceCommonParams& params,
                                          float /*pitchHz*/) noexcept
    {
        MapperResult r{};

        // (1) Mode frequencies from size_ (FR-033)
        //     Verbatim Phase 1: f0 = 500 * pow(0.1, size)
        //     Phase 8C: apply Rossing air-loading depression per tabulated
        //     curve. airLoading = 0 recovers Phase-1 ratios exactly.
        const float f0 = 500.0f * std::pow(0.1f, params.size);
        const float airLoading = std::clamp(params.airLoading, 0.0f, 1.0f);
        for (int k = 0; k < kMembraneModeCount; ++k)
        {
            const float ratio = kMembraneRatios[static_cast<std::size_t>(k)];
            const float depression =
                airLoading * kAirLoadingCurve[static_cast<std::size_t>(k)];
            r.frequencies[k] = f0 * ratio * (1.0f - depression);
        }

        // (2) Per-mode amplitudes from strike position (FR-035)
        const float r_over_a = params.strikePos * 0.9f;
        for (int k = 0; k < kMembraneModeCount; ++k)
        {
            const int   m   = kMembraneBesselOrder[static_cast<std::size_t>(k)];
            const float jmn = kMembraneBesselZeros[static_cast<std::size_t>(k)];
            r.amplitudes[k] = std::abs(evaluateBesselJ(m, jmn * r_over_a));
        }

        // (3) Material-derived parameters (FR-032, FR-033, FR-034)
        r.brightness = params.material;

        // (4) Mode Stretch (FR-050): combine material's inherent stretch with
        //     the UnnaturalZone modeStretch parameter. The bank's stretch takes
        //     [0,1]; modeStretch is in [0.5, 2.0] where 1.0 is physical.
        //     When modeStretch==1.0, the extra contribution is 0 and the mapper
        //     is bit-identical to the Phase 1 code path (FR-055 default-off
        //     guarantee).
        const float stretchFromStretchParam =
            std::clamp((params.modeStretch - 0.5f) * (1.0f / 1.5f), 0.0f, 1.0f);
        // Baseline (Phase 1): stretch = material * 0.3. When modeStretch is
        // at unity (1.0), stretchFromStretchParam evaluates to
        // (1.0 - 0.5) / 1.5 = 0.3333, so we subtract a matching baseline and
        // add the material-derived term back to preserve FR-031 bit-identity
        // for Phase 1 default patches.
        constexpr float kUnityStretchNorm = (1.0f - 0.5f) / 1.5f;
        const float stretchDelta = stretchFromStretchParam - kUnityStretchNorm;
        r.stretch = std::clamp(params.material * 0.3f + stretchDelta, 0.0f, 1.0f);

        // (5) Decay Skew (FR-051, research.md §9): scalar-bias approximation.
        //     decaySkew == 0 preserves the Phase 1 calculation exactly.
        //     NOTE: The scalar-bias approach attenuates global decay time
        //     rather than applying a per-mode inversion. See research.md §9
        //     for the fallback plan (per-block updateModes) if the US6-2
        //     inversion test cannot be satisfied with this approximation.
        const float skewBias = 1.0f - 0.3f * params.decaySkew;

        const float baseDecayTime =
            lerp(0.15f, 0.8f, params.material) * (1.0f + 0.1f * params.size);
        r.decayTime =
            baseDecayTime * std::exp(lerp(std::log(0.3f), std::log(3.0f), params.decay)) *
            skewBias;

        // (6) Per-mode amplitude boost for Decay Skew inversion (research.md §9
        //     "more-accurate" option). When decaySkew < 0, boost high-mode
        //     amplitudes by exp(-decaySkew * log(ratio)) = ratio^(-decaySkew)
        //     so mode 7 starts substantially louder than mode 0, shifting
        //     perceived t60 ordering. When decaySkew == 0, this factor is 1.0
        //     for every mode (bit-identical to Phase 1).
        if (params.decaySkew != 0.0f)
        {
            for (int k = 0; k < kMembraneModeCount; ++k)
            {
                const float ratio = kMembraneRatios[static_cast<std::size_t>(k)];
                if (ratio > 0.0f)
                {
                    const float exponent = -params.decaySkew * std::log(ratio);
                    r.amplitudes[k] *= std::exp(exponent);
                }
            }
        }

        r.numPartials = kMembraneModeCount;
        r.scatter     = std::clamp(params.modeScatter, 0.0f, 1.0f);
        r.damping     = dampingLawFromParams(params, r.decayTime, r.brightness);
        return r;
    }

private:
    static float lerp(float a, float b, float t) noexcept
    {
        return a + (b - a) * t;
    }
};

} // namespace Membrum::Bodies
