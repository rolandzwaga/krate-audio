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

    // Phase 8A: per-mode damping law, always populated by the mapper through
    // dampingLawFromParams: the law derived from decayTime/brightness, scaled
    // and offset by VoiceCommonParams.bodyDampingB1/B3 (0.5 = the derived law
    // exactly). Drives ModalResonatorBank::setModes.
    Krate::DSP::ModalResonatorBank::DampingLaw damping{};
};

/// Upper bound (and reciprocal lower bound) on the per-mode Decay-Skew
/// amplitude tilt factor `ratio^(-decaySkew)`. Without a bound, bodies whose
/// frequency-ratio tables span a wide range (Shell reaches ~466, Plate ~65,
/// NoiseBody ~52) would boost their top mode by 1-2 orders of magnitude at high
/// |decaySkew|, collapsing the bank's unit-peak-normalised spectrum onto a
/// single partial and defeating Size/Material/StrikePos. 8.0 (~+18 dB)
/// approximates the Membrane body's natural tilt depth (max ratio ~8.16) and
/// keeps Decay Skew a spectral-balance control rather than a mode selector.
inline constexpr float kDecaySkewMaxModeTilt = 8.0f;

// ==============================================================================
// Body damping B1 / B3 encoding (fix plan T1.23, MB-074 / MB-029 / MB-109 /
// MB-216). The ONE definition of the per-pad Body Damping b1 / b3 values:
// PadConfig, VoiceCommonParams, DrumVoice, the controller (registration and
// display), DefaultKit, the state codec and tools/membrum_preset_generator.cpp
// refer here instead of restating it.
//
// Units are the bank's (ModalResonatorBank::DampingLaw): the decay rate of a
// mode at f Hz is b1 + b3 * f^2 in s^-1, so b1 is in s^-1 and b3 in s with f in
// Hz (not rad/s; a per-rad^2 figure would be (2 pi)^2 ~= 39.5x smaller).
//
// The stored value n in [0, 1] is RELATIVE to the law the body derives from
// Decay, Material, Size and Decay Skew (b1d = 1 / decayTime in [0.2, 100] s^-1,
// b3d = (1 - brightness) * 4e-5 s, ModalResonatorBank::dampingLawFromLegacy):
//
//   b1 = max(0.2, b1d * 2^(12 * (n1 - 0.5)))         x1/64 .. x64 of the derived b1
//   b3 = max(0, b3d + (n3 - 0.5) * 5e-3) * D          -2.5e-3 .. +2.5e-3 s around it,
//        D = b1d(Decay) / b1d(Decay 0.5)               scaled by Decay like b1
//
// D is the user's ruling (a') of 2026-11-24: Decay scales the override's whole
// b3, so it moves both damping terms the same way and stays live on a pad whose
// b3 is overridden. Every modal mapper's decayTime is k * 0.3 * 10^Decay, so
// D = b1d / b1d(decayTime * 10^(0.5 - Decay)), each b1d through
// dampingLawFromLegacy's [0.01, 5] s clamp: D = 10^(0.5 - Decay) inside it.
// D is 1 at Decay 0.5; a re-encode divides it out, so a stored law plays at
// the Decay it was stored with.
//
// n = 0.5 is the derived law EXACTLY, produced by a branch (the macOS leg builds
// with -ffast-math, so arithmetic that cancels is not exact). There is no hidden
// "derive" sentinel any more: every stored value is a parameter value the knob
// can show, and Decay, Material and the macros stay live under any override.
// The b1 floor 0.2 s^-1 is the derived law's own (decayTime <= 5 s).
// The +-6 octaves keep a 0.01 knob step at 2^0.12 = 1.087x (under 10 % on the
// ring time) and cover every absolute b1 the factory kits and DefaultKit were
// authored with. The stored b3 part is b3Abs / D - b3d, so a b3 range of
// +-2.5e-3 s holds every factory-kit and DefaultKit value (the largest needs
// 1.12e-3 s) and any former absolute b3 in [0, 1e-3] s up to Decay 0.9, and
// below b3Abs 7.9e-4 s at Decay 1 (D >= 10^-0.5). A 0.01 step is 5e-5 s x D,
// which keeps the step under 10 % of the ring time on the default kit's
// explicit-b3 pads (a range wide enough for every former value at Decay 1,
// +-3.16e-3 s, made a 0.01 step move the floor tom's decay by 10.2 %). The
// generator and the version-5 read path re-encode the absolute values through
// Bodies::encodeBodyDampingFromAbsolute (body_damping_encoding.h); the
// generator fails on a value out of range, the read path stores the nearest end.
// ==============================================================================

// The stored value that plays the derived law exactly (the default) is
// Membrum::kBodyDampingDerived = 0.5 (voice_common_params.h, the lowest header
// PadConfig and VoiceCommonParams share).
/// b1 spans +-kBodyDampingB1Octaves / 2 octaves around the derived b1.
inline constexpr float kBodyDampingB1Octaves = 12.0f;
/// b3 spans +-kBodyDampingB3Span / 2 seconds around the derived b3.
inline constexpr float kBodyDampingB3Span    = 5.0e-3f;
/// b1 never goes below the derived law's own floor (1 / 5 s).
inline constexpr float kBodyDampingB1Floor   = 0.2f;

/// The factor a stored b1 value applies to the derived b1 (1 at exactly 0.5).
[[nodiscard]] inline float bodyDampingB1Factor(float n) noexcept
{
    const float c = std::clamp(n, 0.0f, 1.0f);
    if (c == kBodyDampingDerived)
        return 1.0f;
    return std::exp2(kBodyDampingB1Octaves * (c - kBodyDampingDerived));
}

/// The offset (s) a stored b3 value adds to the derived b3 (0 at exactly 0.5).
[[nodiscard]] inline float bodyDampingB3Offset(float n) noexcept
{
    const float c = std::clamp(n, 0.0f, 1.0f);
    if (c == kBodyDampingDerived)
        return 0.0f;
    return (c - kBodyDampingDerived) * kBodyDampingB3Span;
}

/// The Decay at which an overridden b3 plays its stored value unscaled (D = 1).
inline constexpr float kBodyDampingB3RefDecay = 0.5f;

/// D, Decay's factor on an overridden b3 (see the encoding above): the derived
/// b1 at `decay` over the derived b1 at kBodyDampingB3RefDecay, for a body whose
/// mapper gave `decayTime` at `decay`.
[[nodiscard]] inline float bodyDampingB3DecayScale(float decayTime, float decay) noexcept
{
    using Bank = Krate::DSP::ModalResonatorBank;
    const float refDecayTime =
        decayTime * std::pow(10.0f, kBodyDampingB3RefDecay - decay);
    return Bank::dampingLawFromLegacy(decayTime, 1.0f).b1 /
           Bank::dampingLawFromLegacy(refDecayTime, 1.0f).b1;
}

/// Apply stored b1 / b3 values to a derived law; `b3DecayScale` is D
/// (bodyDampingB3DecayScale), used only when b3 is overridden.
[[nodiscard]] inline Krate::DSP::ModalResonatorBank::DampingLaw
applyBodyDamping(Krate::DSP::ModalResonatorBank::DampingLaw derived,
                 float b1Norm, float b3Norm, float b3DecayScale) noexcept
{
    auto law = derived;
    if (std::clamp(b1Norm, 0.0f, 1.0f) != kBodyDampingDerived)
        law.b1 = std::max(kBodyDampingB1Floor, derived.b1 * bodyDampingB1Factor(b1Norm));
    if (std::clamp(b3Norm, 0.0f, 1.0f) != kBodyDampingDerived)
        law.b3 = std::max(0.0f, derived.b3 + bodyDampingB3Offset(b3Norm)) * b3DecayScale;
    return law;
}

/// Build the body's DampingLaw: the law derived from the mapper's
/// decayTime / brightness pair, scaled and offset by the stored
/// VoiceCommonParams.bodyDamping{B1,B3} values (see the encoding above).
[[nodiscard]] inline Krate::DSP::ModalResonatorBank::DampingLaw
dampingLawFromParams(const VoiceCommonParams& params,
                     float legacyDecayTime,
                     float legacyBrightness) noexcept
{
    const float b3DecayScale =
        (std::clamp(params.bodyDampingB3, 0.0f, 1.0f) != kBodyDampingDerived)
            ? bodyDampingB3DecayScale(legacyDecayTime, params.decay)
            : 1.0f;
    return applyBodyDamping(
        Krate::DSP::ModalResonatorBank::dampingLawFromLegacy(legacyDecayTime, legacyBrightness),
        params.bodyDampingB1, params.bodyDampingB3, b3DecayScale);
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
        //     Phase 8C: interpolate each mode's frequency from its pure
        //     Bessel ratio (airLoading = 0) toward the Rossing target
        //     (airLoading = 1). See kAirLoadingTargetScale in
        //     membrane_modes.h for the full derivation. airLoading = 0
        //     recovers Phase-1 ratios exactly (scale = 1.0 for every k).
        const float f0 = 500.0f * std::pow(0.1f, params.size);
        const float airLoading = std::clamp(params.airLoading, 0.0f, 1.0f);
        for (int k = 0; k < kMembraneModeCount; ++k)
        {
            const float ratio = kMembraneRatios[static_cast<std::size_t>(k)];
            const float targetScale =
                kAirLoadingTargetScale[static_cast<std::size_t>(k)];
            const float scale = 1.0f + airLoading * (targetScale - 1.0f);
            r.frequencies[k] = f0 * ratio * scale;
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
                    const float tilt = std::clamp(
                        std::exp(-params.decaySkew * std::log(ratio)),
                        1.0f / kDecaySkewMaxModeTilt, kDecaySkewMaxModeTilt);
                    r.amplitudes[k] *= tilt;
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
