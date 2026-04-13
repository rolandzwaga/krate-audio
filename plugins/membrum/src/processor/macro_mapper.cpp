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

    applyTightness(padConfig);
    applyBrightness(padConfig);
    applyBodySize(padConfig);
    applyPunch(padConfig);
    applyComplexity(padConfig);

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

// ------------------------------------------------------------------------------
// Per-macro appliers: each recomputes the target fields from the registered
// default + the macro delta. Other macros' effects are accumulated via
// superposition (all five macros are independent and commutative; the target
// field set for each macro is disjoint at the field level for Phase 6).
// ------------------------------------------------------------------------------

void MacroMapper::applyTightness(PadConfig& cfg) noexcept
{
    const float m = clamp01(cfg.macroTightness);

    // material: linear delta
    const float materialBase = defaults_.byOffset[kPadMaterial];
    cfg.material = clamp01(materialBase +
                           linDelta(m, MacroCurves::kTightnessMaterialSpan));

    // decay: exponential, INVERTED (higher macro -> shorter decay)
    const float decayBase = defaults_.byOffset[kPadDecay];
    cfg.decay = clamp01(decayBase -
                        expDelta(m, MacroCurves::kTightnessDecaySpan));

    // decaySkew: linear full-span
    const float skewBase = defaults_.byOffset[kPadDecaySkew];
    cfg.decaySkew = clamp01(skewBase +
                            linDelta(m, MacroCurves::kTightnessDecaySkewSpan));
}

void MacroMapper::applyBrightness(PadConfig& cfg) noexcept
{
    const float m = clamp01(cfg.macroBrightness);

    // tsFilterCutoff: exponential (in log-Hz), here linear in normalised units
    const float cutoffBase = defaults_.byOffset[kPadTSFilterCutoff];
    cfg.tsFilterCutoff = clamp01(cutoffBase +
                                 linDelta(m, MacroCurves::kBrightnessCutoffSpan));

    // modeInjectAmount: linear
    const float mijBase = defaults_.byOffset[kPadModeInjectAmount];
    cfg.modeInjectAmount = clamp01(mijBase +
                                   linDelta(m, MacroCurves::kBrightnessModeInjectSpan));
}

void MacroMapper::applyBodySize(PadConfig& cfg) noexcept
{
    const float m = clamp01(cfg.macroBodySize);

    const float sizeBase = defaults_.byOffset[kPadSize];
    cfg.size = clamp01(sizeBase +
                       linDelta(m, MacroCurves::kBodySizeSizeSpan));

    const float stretchBase = defaults_.byOffset[kPadModeStretch];
    cfg.modeStretch = clamp01(stretchBase +
                              linDelta(m, MacroCurves::kBodySizeStretchSpan));

    // decay: Body Size also affects envelope scale (small additive)
    cfg.decay = clamp01(cfg.decay +
                        linDelta(m, MacroCurves::kBodySizeDecayScaleSpan));
}

void MacroMapper::applyPunch(PadConfig& cfg) noexcept
{
    const float m = clamp01(cfg.macroPunch);

    // tsPitchEnvStart: exponential depth
    const float startBase = defaults_.byOffset[kPadTSPitchEnvStart];
    cfg.tsPitchEnvStart = clamp01(startBase +
                                  expDelta(m, MacroCurves::kPunchPitchEnvDepthSpan));

    // tsPitchEnvTime: inverse-exponential (higher macro -> faster env)
    const float timeBase = defaults_.byOffset[kPadTSPitchEnvTime];
    cfg.tsPitchEnvTime = clamp01(timeBase -
                                 linDelta(m, MacroCurves::kPunchPitchEnvTimeSpan));

    // Per-exciter attack target (Phase 6 uses noiseBurstDuration as the
    // generic attack proxy when the active exciter is not friction/feedback).
    // Inverse linear: higher macro -> shorter attack.
    const float nbdBase = defaults_.byOffset[kPadNoiseBurstDuration];
    cfg.noiseBurstDuration = clamp01(nbdBase -
                                     linDelta(m, MacroCurves::kPunchExciterAttackSpan));
}

void MacroMapper::applyComplexity(PadConfig& cfg) noexcept
{
    const float m = clamp01(cfg.macroComplexity);

    const float couplingBase = defaults_.byOffset[kPadCouplingAmount];
    cfg.couplingAmount = clamp01(couplingBase +
                                 linDelta(m, MacroCurves::kComplexityCouplingSpan));

    const float nonlinBase = defaults_.byOffset[kPadNonlinearCoupling];
    cfg.nonlinearCoupling = clamp01(nonlinBase +
                                    linDelta(m, MacroCurves::kComplexityNonlinearSpan));

    // Complexity also adds to modeInjectAmount (proxy for partial count)
    cfg.modeInjectAmount = clamp01(cfg.modeInjectAmount +
                                   linDelta(m, MacroCurves::kComplexityModeInjectSpan));
}

} // namespace Membrum
