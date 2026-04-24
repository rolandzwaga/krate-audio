#pragma once

// ==============================================================================
// ShellMapper -- Free-free beam body mapping helper (data-model.md §4)
// ==============================================================================
//   Size          -> fundamental frequency (FR-032):  f0 = 1500 * 0.1^size
//   Strike pos    -> per-mode amplitude via computeShellAmplitude (FR-034)
//   Material      -> metallic damping defaults (FR-033, long sustain)
//   Decay         -> decayTime multiplier
//
// NAMING CAVEAT: the user-facing label for this body is "Shell", but the
// underlying physics is a free-free Euler-Bernoulli bar (glockenspiel-style
// partial series 1.000, 2.757, 5.404, 8.933, 13.344, 18.637 -- roots of
// cos(beta*L)*cosh(beta*L) = 1; see Fletcher & Rossing, free-free bar).
// It is NOT a cylindrical shell model (which would need axial + circum-
// ferential modes). Additionally, the strike-position amplitude uses
// sin(k*pi*x) which is the simply-supported beam's mode shape, not the
// free-free beam's cosh/sinh combination. This is a deliberate character
// approximation, documented here so future readers do not treat the
// module as a drum-shell physical model.
// ==============================================================================

#include "../voice_common_params.h"
#include "membrane_mapper.h"  // for MapperResult
#include "shell_modes.h"

#include <krate/dsp/processors/modal_resonator_bank.h>

#include <algorithm>
#include <cmath>

namespace Membrum::Bodies {

struct ShellMapper
{
    static constexpr int kModeCount = kShellModeCount;  // 12

    [[nodiscard]] static MapperResult map(const VoiceCommonParams& params,
                                          float /*pitchHz*/) noexcept
    {
        MapperResult r{};

        // Fundamental (shells sit higher than plates) — FR-032
        const float f0 = 1500.0f * std::pow(0.1f, params.size);

        for (int k = 0; k < kModeCount; ++k)
            r.frequencies[k] = f0 * kShellRatios[k];

        for (int k = 0; k < kModeCount; ++k)
        {
            float a = std::abs(computeShellAmplitude(k, params.strikePos));
            a = std::max(a, 0.05f);
            r.amplitudes[k] = a;
        }

        // Metallic free-free beam: keep b3*f^2 damping low (= brightness high)
        // so the Decay knob retains meaningful RT60 range across high modes.
        r.brightness = 0.85f + 0.15f * params.material;

        const float stretchNorm = std::clamp(
            (params.modeStretch - 0.5f) * (1.0f / 1.5f), 0.0f, 1.0f);
        r.stretch = stretchNorm;

        // Metallic beam but kept modest so the 10x Decay knob scale stays
        // below the bank's internal 5 s clamp.
        const float baseDecayTime =
            lerp(0.15f, 0.8f, params.material) * (1.0f + 0.1f * params.size);

        const float skewBias = 1.0f - 0.3f * params.decaySkew;
        r.decayTime =
            baseDecayTime *
            std::exp(lerp(std::log(0.3f), std::log(3.0f), params.decay)) *
            skewBias;

        r.numPartials = kModeCount;
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
