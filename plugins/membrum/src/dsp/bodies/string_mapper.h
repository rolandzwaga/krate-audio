#pragma once

// ==============================================================================
// StringMapper -- WaveguideString body mapping helper (data-model.md §4)
// ==============================================================================
// Unlike modal bodies, StringMapper does NOT target ModalResonatorBank.
// Instead it produces a small struct consumed by WaveguideString setters in
// StringBody::configureForNoteOn.
//
//   Size          -> frequency (FR-032):  f0 = 800 * 0.1^size
//   Strike pos    -> pickPosition (forwarded directly, FR-023)
//   Material      -> brightness / decay (FR-033)
//   Decay         -> decayTime multiplier
// ==============================================================================

#include "../voice_common_params.h"

#include <algorithm>
#include <cmath>

namespace Membrum::Bodies {

struct StringMapperResult
{
    float frequencyHz = 440.0f;
    float decayTime   = 1.0f;
    float brightness  = 0.5f;
    float pickPosition = 0.3f;
};

struct StringMapper
{
    [[nodiscard]] static StringMapperResult map(
        const VoiceCommonParams& params,
        float /*pitchHz*/) noexcept
    {
        StringMapperResult r{};

        // Size -> fundamental (FR-032).
        r.frequencyHz = 800.0f * std::pow(0.1f, params.size);
        r.frequencyHz = std::clamp(r.frequencyHz, 20.0f, 8000.0f);

        // Material -> brightness of the lossy filter in the waveguide.
        // WaveguideString's loop filter uses S = brightness * 0.5 inside
        //   H(z) = rho * [(1-S) + S*z^-1]
        // which is a one-zero low-pass whose damping INCREASES with S. That
        // means a higher "brightness_" value makes the string sound DARKER
        // in the tail. For FR-033 (high modes decay faster at low Material,
        // more even at high Material) we need the opposite semantics at the
        // body-mapper level, so invert here: material=1 -> brightness_=0 =>
        // flat loop gain => all harmonics survive equally; material=0 ->
        // brightness_=1 => strongest low-pass => high harmonics decay fast.
        r.brightness = std::clamp(1.0f - params.material, 0.0f, 1.0f);

        // Decay -> waveguide decay time (seconds). Strings sustain.
        const float base = lerp(0.5f, 4.0f, params.material);
        r.decayTime = base *
            std::exp(lerp(std::log(0.3f), std::log(3.0f), params.decay));

        // Strike Position -> pick position, directly (FR-034 for strings).
        r.pickPosition = std::clamp(params.strikePos, 0.02f, 0.98f);

        return r;
    }

private:
    static float lerp(float a, float b, float t) noexcept
    {
        return a + (b - a) * t;
    }
};

} // namespace Membrum::Bodies
