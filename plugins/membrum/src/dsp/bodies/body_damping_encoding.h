#pragma once

// ==============================================================================
// Body damping re-encoding (fix plan T1.23, MB-074 / MB-109)
// ==============================================================================
// The stored Body Damping b1 / b3 values are RELATIVE to the law each body
// derives from Decay / Material / Size (the encoding is defined once, next to
// dampingLawFromParams in membrane_mapper.h). This header turns an ABSOLUTE
// law (b1 in s^-1, b3 in s, Hz convention) into the stored values that play it
// on a given pad, by running that pad's own body mapper with both values at
// kBodyDampingDerived to get its derived law. Used by DefaultKit, the version-5
// state read path (whose stored values were absolute) and
// tools/membrum_preset_generator.cpp, so a re-encoded kit plays what it played.
// ==============================================================================

#include "../body_model_type.h"
#include "../pad_config.h"
#include "../voice_common_params.h"
#include "bell_mapper.h"
#include "membrane_mapper.h"
#include "noise_body_mapper.h"
#include "plate_mapper.h"
#include "shell_mapper.h"

#include <algorithm>
#include <cmath>

namespace Membrum::Bodies {

/// The stored b1 / b3 values that play a given law, and whether the law fell
/// outside the encoding's range (then the nearest end is stored).
struct BodyDampingNorms
{
    float b1      = kBodyDampingDerived;
    float b3      = kBodyDampingDerived;
    bool  clipped = false;
};

/// The common parameter bundle DrumVoice::noteOn builds for a pad whose
/// effective (macro-applied) config is `effective` (pad_config_apply.h's
/// denormalisation; Material Morph's start value when the morph is on). The
/// fields no mapper reads for damping are left at their defaults.
[[nodiscard]] inline VoiceCommonParams bodyParamsFromPad(const PadConfig& effective) noexcept
{
    VoiceCommonParams p{};
    float material = effective.material;
    if (effective.morphEnabled >= 0.5f)
    {
        // MaterialMorph::process() at the trigger: the start value (an
        // exponential morph floors it at 1e-6).
        const float start = std::clamp(effective.morphStart, 0.0f, 1.0f);
        material = (effective.morphCurve >= 0.5f) ? std::max(start, 1.0e-6f) : start;
    }
    p.material    = material;
    p.size        = effective.size;
    p.decay       = effective.decay;
    p.strikePos   = effective.strikePosition;
    p.level       = effective.level;
    p.modeStretch = modeStretchFromNorm(effective.modeStretch);
    p.decaySkew   = std::clamp(effective.decaySkew, 0.0f, 1.0f) * 2.0f - 1.0f;
    p.airLoading  = effective.airLoading;
    p.modeScatter = effective.modeScatter;
    p.bodyDampingB1 = effective.bodyDampingB1;
    p.bodyDampingB3 = effective.bodyDampingB3;
    return p;
}

/// The decayTime and damping law a body's mapper gives for `params` (with the
/// stored b1 / b3 values `params` holds). Dispatches like
/// DrumVoice::mapModalBody: String falls through to Membrane.
struct BodyDampingMapping
{
    float decayTime = 0.0f;
    Krate::DSP::ModalResonatorBank::DampingLaw damping{};
};

[[nodiscard]] inline BodyDampingMapping
mapBodyDamping(const VoiceCommonParams& params, BodyModelType body) noexcept
{
    const auto pick = [](const MapperResult& r) noexcept {
        return BodyDampingMapping{.decayTime = r.decayTime, .damping = r.damping};
    };
    switch (body)
    {
    case BodyModelType::Plate:     return pick(PlateMapper::map(params, 0.0f));
    case BodyModelType::Shell:     return pick(ShellMapper::map(params, 0.0f));
    case BodyModelType::Bell:      return pick(BellMapper::map(params, 0.0f));
    case BodyModelType::NoiseBody: return pick(NoiseBodyMapper::map(params, 0.0f).modal);
    case BodyModelType::Membrane:
    case BodyModelType::String:
    case BodyModelType::kCount:
    default:                       return pick(MembraneMapper::map(params, 0.0f));
    }
}

/// The law a body derives from `params` (stored values at kBodyDampingDerived).
[[nodiscard]] inline Krate::DSP::ModalResonatorBank::DampingLaw
derivedBodyDamping(const VoiceCommonParams& params, BodyModelType body) noexcept
{
    VoiceCommonParams p = params;
    p.bodyDampingB1 = kBodyDampingDerived;
    p.bodyDampingB3 = kBodyDampingDerived;
    return mapBodyDamping(p, body).damping;
}

/// The law a pad whose effective (macro-applied) config is `effective` plays at
/// its trigger, through its own body mapper.
[[nodiscard]] inline Krate::DSP::ModalResonatorBank::DampingLaw
playedBodyDamping(const PadConfig& effective) noexcept
{
    return mapBodyDamping(bodyParamsFromPad(effective), effective.bodyModel).damping;
}

namespace BodyDampingDetail {

/// The float in [0, 1] within a few ulps of `n` whose decoded value is closest
/// to `want` (the inverse below rounds; this lands on the best stored float).
template <typename Decode>
[[nodiscard]] inline float closestNorm(float n, float want, Decode decode) noexcept
{
    float best = n;
    float bestErr = std::abs(decode(n) - want);
    for (const float dir : {2.0f, -1.0f})
    {
        float c = n;
        for (int i = 0; i < 4 && bestErr > 0.0f; ++i)
        {
            c = std::nextafter(c, dir);
            if (c < 0.0f || c > 1.0f)
                break;
            const float err = std::abs(decode(c) - want);
            if (err < bestErr)
            {
                best = c;
                bestErr = err;
            }
        }
    }
    return best;
}

} // namespace BodyDampingDetail

/// The stored values that play the absolute law (b1Abs s^-1, b3Abs s) on a
/// pad whose body derives its law from `params`.
[[nodiscard]] inline BodyDampingNorms
encodeBodyDampingFromAbsolute(const VoiceCommonParams& params, BodyModelType body,
                              float b1Abs, float b3Abs) noexcept
{
    VoiceCommonParams p = params;
    p.bodyDampingB1 = kBodyDampingDerived;
    p.bodyDampingB3 = kBodyDampingDerived;
    const auto mapped = mapBodyDamping(p, body);
    const auto derived = mapped.damping;
    // D: Decay's factor on an overridden b3 (membrane_mapper.h); the stored
    // offset is what plays b3Abs at this pad's Decay.
    const float d = bodyDampingB3DecayScale(mapped.decayTime, params.decay);
    BodyDampingNorms out;
    if (b1Abs != derived.b1)
    {
        const float raw =
            kBodyDampingDerived + std::log2(b1Abs / derived.b1) / kBodyDampingB1Octaves;
        out.clipped = out.clipped || raw < 0.0f || raw > 1.0f;
        out.b1 = BodyDampingDetail::closestNorm(
            std::clamp(raw, 0.0f, 1.0f), b1Abs, [&derived](float n) noexcept {
                return applyBodyDamping(derived, n, kBodyDampingDerived, 1.0f).b1;
            });
    }
    if (b3Abs != derived.b3)
    {
        const float raw =
            kBodyDampingDerived + (b3Abs / d - derived.b3) / kBodyDampingB3Span;
        out.clipped = out.clipped || raw > 1.0f || (raw < 0.0f && b3Abs > 0.0f);
        out.b3 = BodyDampingDetail::closestNorm(
            std::clamp(raw, 0.0f, 1.0f), b3Abs, [&derived, d](float n) noexcept {
                return applyBodyDamping(derived, kBodyDampingDerived, n, d).b3;
            });
    }
    return out;
}

/// The absolute law of the encoding used up to state version 5 and by the
/// authored DefaultKit / factory-kit values: b1 = 0.2 + n * 49.8 s^-1,
/// b3 = n * 1e-3 s, and a negative value meant "derive this coefficient".
/// Returns the stored values that play that law on the pad `effective`.
[[nodiscard]] inline BodyDampingNorms
encodeLegacyBodyDamping(const PadConfig& effective, double legacyB1, double legacyB3) noexcept
{
    const VoiceCommonParams params = bodyParamsFromPad(effective);
    const auto derived = derivedBodyDamping(params, effective.bodyModel);
    const float b1Abs = (legacyB1 < 0.0)
        ? derived.b1
        : 0.2f + std::clamp(static_cast<float>(legacyB1), 0.0f, 1.0f) * 49.8f;
    const float b3Abs = (legacyB3 < 0.0)
        ? derived.b3
        : std::clamp(static_cast<float>(legacyB3), 0.0f, 1.0f) * 1.0e-3f;
    return encodeBodyDampingFromAbsolute(params, effective.bodyModel, b1Abs, b3Abs);
}

} // namespace Membrum::Bodies
