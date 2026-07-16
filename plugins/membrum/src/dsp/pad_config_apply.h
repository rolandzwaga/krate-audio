// ==============================================================================
// pad_config_apply.h -- the ONE PadConfig -> DrumVoice application path
// ==============================================================================
// Shared by the production VoicePool (plugins/membrum/src/voice_pool/) and the
// offline fitter/render harness (tools/membrum-fit/src/refinement/). Extracted
// per 06-orchestralKit-fix-plan.md D1: the harness used to carry a private,
// partial copy of this mapping that silently dropped the noise/click layers,
// body-damping overrides, airLoading, modeInject, wireCoupling, secondary
// shell, and the entire material-morph block -- so every offline render-based
// verdict on those axes was measured against a voice that never had them
// applied. One helper, zero drift: any new PadConfig field is either forwarded
// here (both consumers get it) or nowhere.
//
// All denormalisation conventions in this file ARE the product definition of
// the preset format's normalized fields. Do not "fix" a mapping here without a
// preset-migration plan -- every shipped .vstpreset depends on it.
// ==============================================================================

#pragma once

#include "drum_voice.h"
#include "pad_config.h"
#include "tone_shaper.h"

#include <algorithm>
#include <cmath>

namespace Membrum {

inline void applyPadConfigToVoice(DrumVoice& v, const PadConfig& cfg) noexcept
{
    v.setExciterType(cfg.exciterType);
    v.setBodyModel(cfg.bodyModel);
    v.setMaterial(cfg.material);
    v.setSize(cfg.size);
    v.setDecay(cfg.decay);
    v.setStrikePosition(cfg.strikePosition);
    v.setLevel(cfg.level);

    // Phase 7: parallel noise layer
    v.setNoiseLayerMix(cfg.noiseLayerMix);
    v.setNoiseLayerCutoff(cfg.noiseLayerCutoff);
    v.setNoiseLayerResonance(cfg.noiseLayerResonance);
    v.setNoiseLayerDecay(cfg.noiseLayerDecay);
    v.setNoiseLayerColor(cfg.noiseLayerColor);
    v.setNoiseLayerGain(cfg.noiseLayerGain);
    // Wire coupling: buzz-follows-body depth.
    v.setWireCoupling(cfg.wireCoupling);
    // Phase 7: always-on click transient
    v.setClickLayerMix(cfg.clickLayerMix);
    v.setClickLayerContactMs(cfg.clickLayerContactMs);
    v.setClickLayerBrightness(cfg.clickLayerBrightness);
    // Phase 7 bug-fix: finally plumb PadConfig::noiseBurstDuration (normalized)
    // through to the NoiseBurstExciter. Previously stored and ignored.
    v.setNoiseBurstContactMs(cfg.noiseBurstDuration);
    // Audit findings 3-5: plumb the remaining secondary exciter params that
    // were stored in PadConfig but never forwarded to a voice (dead knobs).
    v.setFMRatio(cfg.fmRatio);
    v.setFeedbackAmount(cfg.feedbackAmount);
    v.setFrictionPressure(cfg.frictionPressure);
    // Phase 8A: per-mode damping law overrides (sentinel -1.0f preserves
    // legacy brightness-derived behaviour).
    v.setBodyDampingB1(cfg.bodyDampingB1);
    v.setBodyDampingB3(cfg.bodyDampingB3);
    // Phase 8C: air-loading + per-mode scatter.
    v.setAirLoading(cfg.airLoading);
    v.setModeScatter(cfg.modeScatter);
    // Phase 8D: head <-> shell coupling.
    v.setCouplingStrength(cfg.couplingStrength);
    v.setSecondaryEnabled(cfg.secondaryEnabled);
    v.setSecondarySize(cfg.secondarySize);
    v.setSecondaryMaterial(cfg.secondaryMaterial);
    // Phase 8E: nonlinear tension modulation.
    v.setTensionModAmt(cfg.tensionModAmt);

    // ---- Tone Shaper -------------------------------------------------------
    {
        const int filterTypeIdx =
            std::clamp(static_cast<int>(cfg.tsFilterType * 3.0f), 0, 2);
        v.toneShaper().setFilterType(
            static_cast<ToneShaperFilterType>(filterTypeIdx));
    }
    v.toneShaper().setFilterCutoff(
        20.0f * std::pow(1000.0f, std::clamp(cfg.tsFilterCutoff, 0.0f, 1.0f)));
    v.toneShaper().setFilterResonance(cfg.tsFilterResonance);
    v.toneShaper().setFilterEnvAmount(cfg.tsFilterEnvAmount * 2.0f - 1.0f);
    // Filter envelope time scaling: cubic decode (norm^3 * maxMs) to match the
    // ADSRDisplay's drag encoding. See processor.cpp at the matching apply
    // site for the full rationale (display/processor round-trip alignment).
    {
        const float aN = std::clamp(cfg.tsFilterEnvAttack,  0.0f, 1.0f);
        const float dN = std::clamp(cfg.tsFilterEnvDecay,   0.0f, 1.0f);
        const float rN = std::clamp(cfg.tsFilterEnvRelease, 0.0f, 1.0f);
        v.toneShaper().setFilterEnvAttackMs (aN * aN * aN * 500.0f);
        v.toneShaper().setFilterEnvDecayMs  (dN * dN * dN * 2000.0f);
        v.toneShaper().setFilterEnvSustain  (cfg.tsFilterEnvSustain);
        v.toneShaper().setFilterEnvReleaseMs(rN * rN * rN * 2000.0f);
    }
    v.toneShaper().setDriveAmount(cfg.tsDriveAmount);
    v.toneShaper().setFoldAmount(cfg.tsFoldAmount);
    v.toneShaper().setPitchEnvStartHz(
        20.0f * std::pow(100.0f,
                         std::clamp(cfg.tsPitchEnvStart, 0.0f, 1.0f)));
    v.toneShaper().setPitchEnvEndHz(
        20.0f * std::pow(100.0f,
                         std::clamp(cfg.tsPitchEnvEnd,   0.0f, 1.0f)));
    v.toneShaper().setPitchEnvTimeMs(cfg.tsPitchEnvTime * 500.0f);
    // Phase 10: continuous curve mapping. Norm 0.5 -> linear; 0 -> -1 (fast
    // initial drop); 1 -> +1 (slow start, fast end).
    v.toneShaper().setPitchEnvCurveAmount(
        2.0f * std::clamp(cfg.tsPitchEnvCurve, 0.0f, 1.0f) - 1.0f);
    // Phase 10: knee + middle breakpoint.
    v.toneShaper().setPitchEnvKneeEnabled(cfg.tsPitchEnvKneeEnabled >= 0.5f);
    v.toneShaper().setPitchEnvMidHz(
        20.0f * std::pow(100.0f,
                         std::clamp(cfg.tsPitchEnvMidPitch, 0.0f, 1.0f)));
    v.toneShaper().setPitchEnvMidFraction(
        std::clamp(cfg.tsPitchEnvMidFraction, 0.0f, 1.0f));
    v.toneShaper().setPitchEnvCurve2Amount(
        2.0f * std::clamp(cfg.tsPitchEnvCurve2, 0.0f, 1.0f) - 1.0f);

    // ---- Unnatural Zone ----------------------------------------------------
    // modeStretch is normalised over [0.5, 2.0] (1.0 = physical / Phase 1
    // bit-identity); decaySkew is bipolar [-1, +1] from the [0, 1] norm.
    v.unnaturalZone().setModeStretch(
        0.5f + std::clamp(cfg.modeStretch, 0.0f, 1.0f) * 1.5f);
    v.unnaturalZone().setDecaySkew(
        std::clamp(cfg.decaySkew, 0.0f, 1.0f) * 2.0f - 1.0f);
    v.unnaturalZone().modeInject.setAmount(
        std::clamp(cfg.modeInjectAmount, 0.0f, 1.0f));
    v.unnaturalZone().nonlinearCoupling.setAmount(
        std::clamp(cfg.nonlinearCoupling, 0.0f, 1.0f));

    // ---- Material Morph ----------------------------------------------------
    v.unnaturalZone().materialMorph.setEnabled(cfg.morphEnabled >= 0.5f);
    v.unnaturalZone().materialMorph.setStart(
        std::clamp(cfg.morphStart, 0.0f, 1.0f));
    v.unnaturalZone().materialMorph.setEnd(
        std::clamp(cfg.morphEnd,   0.0f, 1.0f));
    // Morph duration: linear norm over [10, 2000] ms (default 0.095477 -> 200 ms).
    v.unnaturalZone().materialMorph.setDurationMs(
        10.0f + std::clamp(cfg.morphDuration, 0.0f, 1.0f) * 1990.0f);
    v.unnaturalZone().materialMorph.setCurve(cfg.morphCurve >= 0.5f);
}

}  // namespace Membrum
