#pragma once

// ==============================================================================
// Macro offsets -- the five per-pad macros as offsets on stored base values
// ==============================================================================
// Fix plan T1.22 (MB-073, MB-081, MB-173, MB-084). A pad's stored PadConfig
// holds BASE values and its five macros (Tightness, Brightness, Body Size,
// Punch, Complexity). A macro never modifies a stored field: at note-on (and
// wherever the engine reads a macro target) the effective config is
//
//     effective field = clamp01(base field + sum of the macro deltas)
//
// so a macro sweep that returns to its start restores the sound exactly, the
// controller's per-pad parameters (base fields and macro values) describe the
// whole state, and every save path reproduces what plays.
//
// Curves and spans are the Phase 6 ones (spec 141 data-model.md section 3),
// with one change: Punch scales an EXISTING pitch envelope (a multiplicative
// factor on its time, the start delta only while the envelope is on) and never
// turns one on or off, so Punch below neutral no longer retunes an
// un-enveloped pad to a 20 Hz fundamental (MB-081).
//
// A macro at exactly 0.5 contributes nothing by a branch, not by arithmetic
// that cancels (the macOS leg builds with -ffast-math), so a pad whose macros
// are all neutral plays its stored fields bit for bit.
// ==============================================================================

#include "pad_config.h"

#include <algorithm>
#include <cmath>

namespace Membrum {

namespace MacroCurves {
    // Tightness
    inline constexpr float kTightnessMaterialSpan    = 0.30f;
    inline constexpr float kTightnessDecaySpan       = 0.25f;
    inline constexpr float kTightnessDecaySkewSpan   = 1.00f;

    // Brightness
    inline constexpr float kBrightnessCutoffSpan     = 0.40f;
    inline constexpr float kBrightnessModeInjectSpan = 0.30f;

    // Body Size
    inline constexpr float kBodySizeSizeSpan         = 0.40f;
    inline constexpr float kBodySizeStretchSpan      = 0.20f;
    inline constexpr float kBodySizeDecayScaleSpan   = 0.20f;

    // Punch. The pitch-envelope time is scaled by
    // 2^((0.5 - punch) * 2 * kPunchPitchEnvTimeOctaves): Punch 1 halves the
    // time (a faster drop), Punch 0 doubles it.
    inline constexpr float kPunchPitchEnvDepthSpan   = 0.50f;
    inline constexpr float kPunchPitchEnvTimeOctaves = 1.00f;
    inline constexpr float kPunchExciterAttackSpan   = 0.30f;

    // Complexity
    inline constexpr float kComplexityCouplingSpan   = 0.50f;
    inline constexpr float kComplexityNonlinearSpan  = 0.30f;
    inline constexpr float kComplexityModeInjectSpan = 0.30f;

    /// Linear, symmetric around macro 0.5.
    [[nodiscard]] inline float linDelta(float macro, float span) noexcept
    {
        return (macro - 0.5f) * span;
    }

    /// Exponential around macro 0.5: (2^((macro - 0.5) * 2) - 1) * span, in
    /// roughly [-0.5, 1.0] x span.
    [[nodiscard]] inline float expDelta(float macro, float span) noexcept
    {
        return (std::exp2f((macro - 0.5f) * 2.0f) - 1.0f) * span;
    }

    /// Punch's multiplicative factor on the pitch-envelope time.
    [[nodiscard]] inline float punchTimeFactor(float macro) noexcept
    {
        return std::exp2f((0.5f - macro) * 2.0f * kPunchPitchEnvTimeOctaves);
    }
} // namespace MacroCurves

namespace MacroOffsetsDetail {

/// The summed additive delta of every macro target, plus Punch's time factor.
/// A field whose delta is exactly 0 (every macro that drives it at 0.5) is
/// left untouched.
struct FieldDeltas
{
    float material           = 0.0f;
    float decay              = 0.0f;
    float decaySkew          = 0.0f;
    float tsFilterCutoff     = 0.0f;
    float modeInjectAmount   = 0.0f;
    float size               = 0.0f;
    float modeStretch        = 0.0f;
    float tsPitchEnvStart    = 0.0f;
    float tsPitchEnvTimeMul  = 1.0f;
    float noiseBurstDuration = 0.0f;
    float couplingAmount     = 0.0f;
    float nonlinearCoupling  = 0.0f;
    bool  any                = false;
};

[[nodiscard]] inline float clampMacro(float m) noexcept
{
    return std::clamp(m, 0.0f, 1.0f);
}

/// `pitchEnvOn`: the pad's pitch envelope is active (time > 0). Punch's
/// pitch-envelope deltas apply only then.
[[nodiscard]] inline FieldDeltas fieldDeltas(const PadConfig& cfg, bool pitchEnvOn) noexcept
{
    using namespace MacroCurves;
    FieldDeltas d;

    const float tight = clampMacro(cfg.macroTightness);
    if (tight != 0.5f)
    {
        d.material  += linDelta(tight, kTightnessMaterialSpan);
        d.decay     -= expDelta(tight, kTightnessDecaySpan);   // inverted: tighter = shorter
        d.decaySkew += linDelta(tight, kTightnessDecaySkewSpan);
        d.any = true;
    }

    const float bright = clampMacro(cfg.macroBrightness);
    if (bright != 0.5f)
    {
        d.tsFilterCutoff   += linDelta(bright, kBrightnessCutoffSpan);
        d.modeInjectAmount += linDelta(bright, kBrightnessModeInjectSpan);
        d.any = true;
    }

    const float bodySize = clampMacro(cfg.macroBodySize);
    if (bodySize != 0.5f)
    {
        d.size        += linDelta(bodySize, kBodySizeSizeSpan);
        d.modeStretch += linDelta(bodySize, kBodySizeStretchSpan);
        d.decay       += linDelta(bodySize, kBodySizeDecayScaleSpan);
        d.any = true;
    }

    const float punch = clampMacro(cfg.macroPunch);
    if (punch != 0.5f)
    {
        if (pitchEnvOn)
        {
            d.tsPitchEnvStart  += expDelta(punch, kPunchPitchEnvDepthSpan);
            d.tsPitchEnvTimeMul = punchTimeFactor(punch);
        }
        // Generic attack proxy: higher Punch -> shorter burst.
        d.noiseBurstDuration -= linDelta(punch, kPunchExciterAttackSpan);
        d.any = true;
    }

    const float complexity = clampMacro(cfg.macroComplexity);
    if (complexity != 0.5f)
    {
        d.couplingAmount    += linDelta(complexity, kComplexityCouplingSpan);
        d.nonlinearCoupling += linDelta(complexity, kComplexityNonlinearSpan);
        d.modeInjectAmount  += linDelta(complexity, kComplexityModeInjectSpan);
        d.any = true;
    }
    return d;
}

/// Moves `base` (inside [0, 1]) by up to a few ulps until `apply(base)`, the
/// forward map, lands exactly on `want`: base = effective - delta can be one
/// ulp off once the delta is added back. If no nearby float lands on it, the
/// nearest (`base`) is kept.
template <typename Forward>
[[nodiscard]] inline float exactBase(float base, float want, Forward apply) noexcept
{
    float b = base;
    for (int i = 0; i < 4; ++i)
    {
        const float got = apply(b);
        if (got == want)
            return b;
        const float next = std::nextafter(b, got < want ? 2.0f : -1.0f);
        if (next < 0.0f || next > 1.0f)
            break;
        b = next;
    }
    return base;
}

} // namespace MacroOffsetsDetail

/// The config the engine plays: `base` with every macro's delta applied.
/// Stored fields are base values and are never modified by a macro.
[[nodiscard]] inline PadConfig applyMacroOffsets(const PadConfig& base) noexcept
{
    const auto d = MacroOffsetsDetail::fieldDeltas(base, base.tsPitchEnvTime > 0.0f);
    if (!d.any)
        return base;

    PadConfig e = base;
    const auto add = [](float& field, float delta) noexcept {
        if (delta != 0.0f)
            field = std::clamp(field + delta, 0.0f, 1.0f);
    };
    add(e.material, d.material);
    add(e.decay, d.decay);
    add(e.decaySkew, d.decaySkew);
    add(e.tsFilterCutoff, d.tsFilterCutoff);
    add(e.modeInjectAmount, d.modeInjectAmount);
    add(e.size, d.size);
    add(e.modeStretch, d.modeStretch);
    add(e.tsPitchEnvStart, d.tsPitchEnvStart);
    add(e.noiseBurstDuration, d.noiseBurstDuration);
    add(e.couplingAmount, d.couplingAmount);
    add(e.nonlinearCoupling, d.nonlinearCoupling);
    // Scaling keeps a positive time positive, so Punch never switches the
    // envelope on or off.
    if (d.tsPitchEnvTimeMul != 1.0f)
        e.tsPitchEnvTime = std::clamp(base.tsPitchEnvTime * d.tsPitchEnvTimeMul, 0.0f, 1.0f);
    return e;
}

/// The inverse of applyMacroOffsets: the base values that play `effective` at
/// its own macro positions, `base = clamp01(effective - delta)` per target
/// field (Punch's time factor divides). Used to re-encode a version-5 state,
/// whose stored fields are what it played, and the factory kits. When
/// `clampedFields` is given it receives the number of fields where the clamp
/// acted (that field cannot be reproduced at that macro position).
[[nodiscard]] inline PadConfig baseFromEffective(const PadConfig& effective,
                                                 int* clampedFields = nullptr) noexcept
{
    const auto d = MacroOffsetsDetail::fieldDeltas(effective, effective.tsPitchEnvTime > 0.0f);
    int clamped = 0;
    PadConfig b = effective;
    if (d.any)
    {
        const auto sub = [&clamped](float& field, float delta) noexcept {
            if (delta == 0.0f)
                return;
            const float raw = field - delta;
            if (raw < 0.0f || raw > 1.0f)
            {
                ++clamped;
                field = std::clamp(raw, 0.0f, 1.0f);
                return;
            }
            // The base whose offset lands exactly on the effective value where
            // one exists (otherwise one ulp off), so a re-encoded kit plays
            // its old field.
            field = MacroOffsetsDetail::exactBase(raw, field, [delta](float b) noexcept {
                return std::clamp(b + delta, 0.0f, 1.0f);
            });
        };
        sub(b.material, d.material);
        sub(b.decay, d.decay);
        sub(b.decaySkew, d.decaySkew);
        sub(b.tsFilterCutoff, d.tsFilterCutoff);
        sub(b.modeInjectAmount, d.modeInjectAmount);
        sub(b.size, d.size);
        sub(b.modeStretch, d.modeStretch);
        sub(b.tsPitchEnvStart, d.tsPitchEnvStart);
        sub(b.noiseBurstDuration, d.noiseBurstDuration);
        sub(b.couplingAmount, d.couplingAmount);
        sub(b.nonlinearCoupling, d.nonlinearCoupling);
        if (d.tsPitchEnvTimeMul != 1.0f)
        {
            const float mul = d.tsPitchEnvTimeMul;
            const float raw = effective.tsPitchEnvTime / mul;
            if (raw > 1.0f)
            {
                ++clamped;
                b.tsPitchEnvTime = 1.0f;
            }
            else
            {
                b.tsPitchEnvTime = MacroOffsetsDetail::exactBase(
                    raw, effective.tsPitchEnvTime, [mul](float t) noexcept {
                        return std::clamp(t * mul, 0.0f, 1.0f);
                    });
            }
        }
    }
    if (clampedFields != nullptr)
        *clampedFields = clamped;
    return b;
}

} // namespace Membrum
