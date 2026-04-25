// ==============================================================================
// MacroMapper -- Implementation (Phase 6)
// ==============================================================================

#include "processor/macro_mapper.h"

#include <algorithm>
#include <cmath>

namespace Membrum {

namespace {

[[nodiscard]] inline float clamp01(float v) noexcept
{
    return std::clamp(v, 0.0f, 1.0f);
}

// Exponential, symmetric around macro=0.5. At macro=0.5 returns 0 exactly.
// span scales the magnitude. For "inverted" (higher macro -> smaller target),
// caller negates.
[[nodiscard]] inline float expDelta(float macro, float span) noexcept
{
    // (exp2f((macro - 0.5f) * 2.0f) - 1.0f) is in roughly [-0.75, 1.0]; we
    // scale by span so the magnitude at extremes stays bounded.
    const float e = std::exp2f((macro - 0.5f) * 2.0f) - 1.0f;
    return e * span;
}

[[nodiscard]] inline float linDelta(float macro, float span) noexcept
{
    return (macro - 0.5f) * span;
}

} // namespace

void MacroMapper::prepare(const RegisteredDefaultsTable& defaults) noexcept
{
    defaults_ = defaults;
    invalidateCache();
    prepared_ = true;
}

void MacroMapper::invalidateCache() noexcept
{
    // PadCache default-initialises to 0.5 (neutral) for each macro.
    for (auto& c : cache_)
        c = PadCache{};
}

bool MacroMapper::isDirty(int padIndex, const PadConfig& padConfig) const noexcept
{
    if (padIndex < 0 || padIndex >= kNumPads)
        return false;
    const auto& c = cache_[static_cast<std::size_t>(padIndex)];
    return c.tightness  != padConfig.macroTightness  ||
           c.brightness != padConfig.macroBrightness ||
           c.bodySize   != padConfig.macroBodySize   ||
           c.punch      != padConfig.macroPunch      ||
           c.complexity != padConfig.macroComplexity;
}

void MacroMapper::apply(int padIndex, PadConfig& padConfig) noexcept
{
    if (!prepared_)
        return;
    if (padIndex < 0 || padIndex >= kNumPads)
        return;

    auto& c = cache_[static_cast<std::size_t>(padIndex)];
    const bool dirty =
        c.tightness  != padConfig.macroTightness  ||
        c.brightness != padConfig.macroBrightness ||
        c.bodySize   != padConfig.macroBodySize   ||
        c.punch      != padConfig.macroPunch      ||
        c.complexity != padConfig.macroComplexity;
    if (!dirty)
        return;

    // Incremental: each applier computes (newDelta - oldDelta) using the
    // cached previous macro value, so only the *change* is layered onto
    // the per-pad config. Cache is initialised at 0.5 so a first apply
    // with a preset's neutral macro values produces zero adjustment,
    // preserving every loaded per-pad parameter.
    applyTightness(padConfig, c);
    applyBrightness(padConfig, c);
    applyBodySize(padConfig, c);
    applyPunch(padConfig, c);
    applyComplexity(padConfig, c);

    c.tightness  = padConfig.macroTightness;
    c.brightness = padConfig.macroBrightness;
    c.bodySize   = padConfig.macroBodySize;
    c.punch      = padConfig.macroPunch;
    c.complexity = padConfig.macroComplexity;
}

void MacroMapper::reapplyAll(std::array<PadConfig, kNumPads>& pads) noexcept
{
    invalidateCache();
    for (int p = 0; p < kNumPads; ++p)
        apply(p, pads[static_cast<std::size_t>(p)]);
}

void MacroMapper::syncCacheFromCfg(int padIndex,
                                   const PadConfig& padConfig) noexcept
{
    if (padIndex < 0 || padIndex >= kNumPads)
        return;
    auto& c = cache_[static_cast<std::size_t>(padIndex)];
    c.tightness  = padConfig.macroTightness;
    c.brightness = padConfig.macroBrightness;
    c.bodySize   = padConfig.macroBodySize;
    c.punch      = padConfig.macroPunch;
    c.complexity = padConfig.macroComplexity;
}

// ------------------------------------------------------------------------------
// Per-macro appliers: each one layers (newDelta - oldDelta) onto the relevant
// per-pad fields, where oldDelta is computed from the cached previous macro
// value. This preserves preset values when a macro arrives at neutral (0.5):
// newDelta(0.5) - oldDelta(0.5) = 0, so the loaded per-pad config is left
// alone. The macros remain independent and commutative because each one only
// touches a disjoint set of fields.
// ------------------------------------------------------------------------------

void MacroMapper::applyTightness(PadConfig& cfg, const PadCache& cache) noexcept
{
    const float m    = clamp01(cfg.macroTightness);
    const float mOld = cache.tightness;

    cfg.material = clamp01(cfg.material
        + (linDelta(m, MacroCurves::kTightnessMaterialSpan)
         - linDelta(mOld, MacroCurves::kTightnessMaterialSpan)));

    // decay: exponential, INVERTED (higher macro -> shorter decay)
    cfg.decay = clamp01(cfg.decay
        - (expDelta(m, MacroCurves::kTightnessDecaySpan)
         - expDelta(mOld, MacroCurves::kTightnessDecaySpan)));

    cfg.decaySkew = clamp01(cfg.decaySkew
        + (linDelta(m, MacroCurves::kTightnessDecaySkewSpan)
         - linDelta(mOld, MacroCurves::kTightnessDecaySkewSpan)));
}

void MacroMapper::applyBrightness(PadConfig& cfg, const PadCache& cache) noexcept
{
    const float m    = clamp01(cfg.macroBrightness);
    const float mOld = cache.brightness;

    cfg.tsFilterCutoff = clamp01(cfg.tsFilterCutoff
        + (linDelta(m, MacroCurves::kBrightnessCutoffSpan)
         - linDelta(mOld, MacroCurves::kBrightnessCutoffSpan)));

    cfg.modeInjectAmount = clamp01(cfg.modeInjectAmount
        + (linDelta(m, MacroCurves::kBrightnessModeInjectSpan)
         - linDelta(mOld, MacroCurves::kBrightnessModeInjectSpan)));
}

void MacroMapper::applyBodySize(PadConfig& cfg, const PadCache& cache) noexcept
{
    const float m    = clamp01(cfg.macroBodySize);
    const float mOld = cache.bodySize;

    cfg.size = clamp01(cfg.size
        + (linDelta(m, MacroCurves::kBodySizeSizeSpan)
         - linDelta(mOld, MacroCurves::kBodySizeSizeSpan)));

    cfg.modeStretch = clamp01(cfg.modeStretch
        + (linDelta(m, MacroCurves::kBodySizeStretchSpan)
         - linDelta(mOld, MacroCurves::kBodySizeStretchSpan)));

    // decay: Body Size also nudges envelope scale (small additive layer
    // on top of Tightness's decay contribution).
    cfg.decay = clamp01(cfg.decay
        + (linDelta(m, MacroCurves::kBodySizeDecayScaleSpan)
         - linDelta(mOld, MacroCurves::kBodySizeDecayScaleSpan)));
}

void MacroMapper::applyPunch(PadConfig& cfg, const PadCache& cache) noexcept
{
    const float m    = clamp01(cfg.macroPunch);
    const float mOld = cache.punch;

    cfg.tsPitchEnvStart = clamp01(cfg.tsPitchEnvStart
        + (expDelta(m, MacroCurves::kPunchPitchEnvDepthSpan)
         - expDelta(mOld, MacroCurves::kPunchPitchEnvDepthSpan)));

    // tsPitchEnvTime: inverse-linear (higher macro -> faster env)
    cfg.tsPitchEnvTime = clamp01(cfg.tsPitchEnvTime
        - (linDelta(m, MacroCurves::kPunchPitchEnvTimeSpan)
         - linDelta(mOld, MacroCurves::kPunchPitchEnvTimeSpan)));

    // Per-exciter attack target (Phase 6 uses noiseBurstDuration as the
    // generic attack proxy when the active exciter is not friction/feedback).
    cfg.noiseBurstDuration = clamp01(cfg.noiseBurstDuration
        - (linDelta(m, MacroCurves::kPunchExciterAttackSpan)
         - linDelta(mOld, MacroCurves::kPunchExciterAttackSpan)));
}

void MacroMapper::applyComplexity(PadConfig& cfg, const PadCache& cache) noexcept
{
    const float m    = clamp01(cfg.macroComplexity);
    const float mOld = cache.complexity;

    cfg.couplingAmount = clamp01(cfg.couplingAmount
        + (linDelta(m, MacroCurves::kComplexityCouplingSpan)
         - linDelta(mOld, MacroCurves::kComplexityCouplingSpan)));

    cfg.nonlinearCoupling = clamp01(cfg.nonlinearCoupling
        + (linDelta(m, MacroCurves::kComplexityNonlinearSpan)
         - linDelta(mOld, MacroCurves::kComplexityNonlinearSpan)));

    // Complexity also nudges modeInjectAmount (proxy for partial count) on
    // top of Brightness's contribution.
    cfg.modeInjectAmount = clamp01(cfg.modeInjectAmount
        + (linDelta(m, MacroCurves::kComplexityModeInjectSpan)
         - linDelta(mOld, MacroCurves::kComplexityModeInjectSpan)));
}

} // namespace Membrum
