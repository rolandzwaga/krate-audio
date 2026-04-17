#pragma once

// ==============================================================================
// PlateMapper -- Kirchhoff square-plate body mapping helper (data-model.md §4)
// ==============================================================================
// Converts VoiceCommonParams + body-specific state into the arguments for
// Krate::DSP::ModalResonatorBank::setModes(). Stateless, pure function, no
// allocation.
//
//   Size          -> fundamental frequency (FR-032):  f0 = 800 * 0.1^size
//   Strike pos    -> per-mode amplitude via computePlateAmplitude (FR-034)
//   Material      -> brightness & damping profile    (FR-033)
//   Decay         -> decayTime multiplier
//   modeStretch   -> stretch scalar (forwarded to bank)
//   decaySkew     -> research.md §9 scalar-bias approximation
// ==============================================================================

#include "../voice_common_params.h"
#include "membrane_mapper.h"  // for MapperResult
#include "plate_modes.h"

#include <krate/dsp/processors/modal_resonator_bank.h>

#include <algorithm>
#include <cmath>

namespace Membrum::Bodies {

struct PlateMapper
{
    static constexpr int kModeCount = kPlateModeCount;  // 16

    [[nodiscard]] static MapperResult map(const VoiceCommonParams& params,
                                          float /*pitchHz*/) noexcept
    {
        MapperResult r{};

        // (1) Fundamental from Size (FR-032)
        const float f0 = 800.0f * std::pow(0.1f, params.size);

        // (2) Mode frequencies = f0 * kPlateRatios (first 16)
        for (int k = 0; k < kModeCount; ++k)
            r.frequencies[k] = f0 * kPlateRatios[k];

        // (3) Per-mode amplitudes from Strike Position (FR-034)
        for (int k = 0; k < kModeCount; ++k)
        {
            float a = std::abs(computePlateAmplitude(k, params.strikePos));
            // Ensure every mode has a non-zero contribution so SC-002 can
            // actually measure the ratios. Clamp the floor to 0.05.
            a = std::max(a, 0.05f);
            r.amplitudes[k] = a;
        }

        // (4) Material-derived parameters (metallic, bright)
        r.brightness = 0.5f + 0.5f * params.material;  // plates are metallic

        // modeStretch routes through as the 'stretch' scalar. The bank's
        // internal stretch uses [0,1] so we map [0.5, 2.0] -> [0, 1].
        const float stretchNorm = std::clamp(
            (params.modeStretch - 0.5f) * (1.0f / 1.5f), 0.0f, 1.0f);
        r.stretch = stretchNorm;

        // (5) Decay time (plates ring long)
        const float baseDecayTime =
            lerp(0.4f, 2.0f, params.material) * (1.0f + 0.1f * params.size);

        // Decay skew scalar-bias approximation (research.md §9).
        const float skewBias = 1.0f - 0.3f * params.decaySkew;
        r.decayTime =
            baseDecayTime *
            std::exp(lerp(std::log(0.3f), std::log(3.0f), params.decay)) *
            skewBias;

        r.numPartials = kModeCount;
        r.scatter     = 0.0f;
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
