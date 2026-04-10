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
};

struct MembraneMapper
{
    static constexpr int kMembraneModeCount = 16;

    [[nodiscard]] static MapperResult map(const VoiceCommonParams& params,
                                          float /*pitchHz*/) noexcept
    {
        MapperResult r{};

        // (1) Mode frequencies from size_ (FR-033)
        //     Verbatim Phase 1: f0 = 500 * pow(0.1, size)
        const float f0 = 500.0f * std::pow(0.1f, params.size);
        for (int k = 0; k < kMembraneModeCount; ++k)
            r.frequencies[k] = f0 * kMembraneRatios[static_cast<std::size_t>(k)];

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
        r.stretch    = params.material * 0.3f;

        const float baseDecayTime =
            lerp(0.15f, 0.8f, params.material) * (1.0f + 0.1f * params.size);
        r.decayTime =
            baseDecayTime * std::exp(lerp(std::log(0.3f), std::log(3.0f), params.decay));

        r.numPartials = kMembraneModeCount;
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
