// ==============================================================================
// Layer 2: DSP Processor - Profundum Phase 5 (specs/profundum-phase5-shape-character)
// ==============================================================================
// KeytrackedTilt: first-order keytracked tilt behind every Tone and BITE's
// emphasis (plan S1 C-1, C-13, S6.1; tasks T004, deviation 11).
//   Carries: FR-055, SC-028 (a)(b).
//   Spec:  specs/profundum-phase5-shape-character/spec.md
//   Plan:  specs/profundum-phase5-shape-character/plan.md
//
// Law (plan S6.1):
//   lp = OnePoleLP[fc = pivotMultiple * f0](x)   (a = exp(-2 pi fc / fs), clamps
//                                                  fc at 0.495 * rate)
//   y  = gL * lp + gH * (x - lp)
//   G  = (2 t - 1) * maxShelfDb, gL = 10^(-G/20) * nu, gH = 10^(G/20) * nu,
//   nu = sqrt(2 / (gL^2 + gH^2))   (unity magnitude at the pivot, analog split)
//
// - Tone 0.5: gL = gH = 1, so y = lp + (x - lp) is flat within float rounding.
// - The response depends on f / (pivotMultiple * f0) only, so it is keytracked.
// - The matched-pole one-pole is not power-complementary: fully tilted, the
//   pivot misses unity by ~0.017 dB at C2 and ~0.035 dB at C3 (192 kHz, k 2,
//   +-12 dB; plan C-25). Tone 0.5 is exact.
// - The 10^x pair is recomputed only when setTone receives a changed value
//   (bit compare) or configure() changes the depth.
//
// Arguments are not sanitised: non-finite or out-of-range f0 / tone / depth are
// the owner's job (BassCharacterStage and ShapeMorph sanitise before calling).
// Tone is expected in [0, 1].
//
// Real-time safe: no allocation, no locks, no exceptions. Mono (one channel).
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/one_pole.h>

#include <bit>
#include <cmath>
#include <cstdint>

namespace Krate::DSP {

class KeytrackedTilt {
public:
    /// Prepare at the rate this tilt runs at; clears the filter state.
    void prepare(double processRate) noexcept {
        lp_.prepare(processRate);
        lp_.setCutoff(k_ * f0_);
        lp_.reset();
    }

    /// Pivot at pivotMultiple * f0; maxShelfDb is the shelf depth at Tone 0 / 1.
    void configure(float pivotMultiple, float maxShelfDb) noexcept {
        k_ = pivotMultiple;
        maxDb_ = maxShelfDb;
        lp_.setCutoff(k_ * f0_);
        updateGains();
    }

    /// Tone in [0, 1]; 0.5 is flat. Recomputes the gains only on a changed value.
    void setTone(float tone) noexcept {
        if (std::bit_cast<std::uint32_t>(tone) == std::bit_cast<std::uint32_t>(tone_))
            return;
        tone_ = tone;
        updateGains();
    }

    /// Re-keys the pivot: lp cutoff = pivotMultiple * f0 (OnePoleLP clamps at 0.495 * rate).
    void setFundamentalHz(float f0) noexcept {
        f0_ = f0;
        lp_.setCutoff(k_ * f0_);
    }

    void reset() noexcept { lp_.reset(); }

    [[nodiscard]] float process(float x) noexcept {
        const float lp = lp_.process(x);
        return gL_ * lp + gH_ * (x - lp);
    }

    /// pivotMultiple * f0 as last set (before OnePoleLP's clamp).
    [[nodiscard]] float pivotHz() const noexcept { return k_ * f0_; }

    /// The last Tone value set (0.5 by default).
    [[nodiscard]] float tone() const noexcept { return tone_; }

private:
    /// Profundum Phase 9 T032: float exp2 pair instead of two double pows. The Character stage hands
    /// this tilt its per-sample smoothed Tone, so under a moving Tone the recompute ran per sample per
    /// channel (5.8 % of the engine's samples in the P-1 profile); the gains were float already.
    /// ShapeMorph::computeInverse evaluates the same expressions in the same order (C-23 exact inverse).
    void updateGains() noexcept {
        const float g = (2.0f * tone_ - 1.0f) * maxDb_;
        const float lo = std::exp2(-g * kLog2TenOver20);
        const float hi = std::exp2(g * kLog2TenOver20);
        const float nu = std::sqrt(2.0f / (lo * lo + hi * hi));
        gL_ = lo * nu;
        gH_ = hi * nu;
    }

public:
    /// log2(10) / 20: 10^(dB / 20) = 2^(dB · kLog2TenOver20). Shared with ShapeMorph::computeInverse.
    static constexpr float kLog2TenOver20 = 0.16609640474436811f;

private:

    OnePoleLP lp_;
    float k_ = 2.0f;
    float maxDb_ = 0.0f;
    float tone_ = 0.5f;
    float f0_ = 65.40639f; // C2 until the owner sets f0 (not ruled; owners always set it)
    float gL_ = 1.0f;
    float gH_ = 1.0f;
};

} // namespace Krate::DSP
