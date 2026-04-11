#pragma once

// ==============================================================================
// BellMapper -- Church-bell Chladni body mapping helper (data-model.md §4)
// ==============================================================================
//   Size          -> fundamental (nominal partial) (FR-032):
//                     f0_nominal = 800 * 0.1^size
//   Strike pos    -> per-mode amplitude via computeBellAmplitude (FR-034)
//   Material      -> very-metallic damping defaults (FR-024):
//                     low b1 (long sustain), very low b3 (long upper partials)
//   Decay         -> decayTime multiplier
// ==============================================================================

#include "../voice_common_params.h"
#include "bell_modes.h"
#include "membrane_mapper.h"  // for MapperResult

#include <krate/dsp/processors/modal_resonator_bank.h>

#include <algorithm>
#include <cmath>

namespace Membrum::Bodies {

struct BellMapper
{
    static constexpr int kModeCount = kBellModeCount;  // 16

    [[nodiscard]] static MapperResult map(const VoiceCommonParams& params,
                                          float /*pitchHz*/) noexcept
    {
        MapperResult r{};

        // Size -> nominal partial fundamental (FR-032).
        // The ratio table already expresses partials relative to nominal
        // (nominal is at index 4 with ratio 1.000), so f0_nominal*kBellRatios[k]
        // places the hum at 0.25*f0 and the upper partials above.
        const float f0Nominal = 800.0f * std::pow(0.1f, params.size);

        for (int k = 0; k < kModeCount; ++k)
            r.frequencies[k] = f0Nominal * kBellRatios[k];

        for (int k = 0; k < kModeCount; ++k)
        {
            float a = computeBellAmplitude(k, params.strikePos);
            a = std::max(a, 0.05f);
            r.amplitudes[k] = a;
        }

        // Very metallic — brightness mapped high so b3 (high-freq damping)
        // is very low, giving the long upper-partial tails characteristic of
        // church bells (FR-024).
        r.brightness = 0.7f + 0.3f * params.material;

        const float stretchNorm = std::clamp(
            (params.modeStretch - 0.5f) * (1.0f / 1.5f), 0.0f, 1.0f);
        r.stretch = stretchNorm;

        // Bells ring the longest of all modal bodies; still kept below the
        // bank's internal 5 s clamp at the max end of the Decay sweep.
        const float baseDecayTime =
            lerp(0.4f, 1.4f, params.material) * (1.0f + 0.1f * params.size);

        const float skewBias = 1.0f - 0.3f * params.decaySkew;
        r.decayTime =
            baseDecayTime *
            std::exp(lerp(std::log(0.3f), std::log(3.0f), params.decay)) *
            skewBias;

        r.numPartials = kModeCount;
        r.scatter     = 0.0f;
        return r;
    }

private:
    static float lerp(float a, float b, float t) noexcept
    {
        return a + (b - a) * t;
    }
};

} // namespace Membrum::Bodies
