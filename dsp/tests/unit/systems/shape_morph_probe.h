// ==============================================================================
// Test probe: ShapeMorph (Profundum Phase 5, specs/profundum-phase5-shape-character)
// ==============================================================================
// tasks.md T010 (deviation 5): the one definition of detail::ShapeMorphProbe, shared by
// shape_morph_test.cpp, shape_morph_spectral_test.cpp and the chain / nonfinite / perf TUs
// (defining it in two TUs with different bodies would be an ODR violation). Befriended by
// ShapeMorph (systems/shape_morph.h). Read-only after T010: a missing accessor is recorded
// as a DEFECT: line in the needing task's run log.
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/systems/shape_morph.h>

#include <array>

namespace Krate::DSP::detail {

struct ShapeMorphProbe {
    // ---- seams (plan S6.2 / tasks T010; default off) ----

    /// SHAPE snaps: setShape (and turning the seam on) jumps the smoother to its target.
    static void setSeamUnsmoothed(ShapeMorph& m, bool on) noexcept {
        m.seamUnsmoothed_ = on;
        if (on)
            m.shapeSm_.snapToTarget();
    }

    /// The weights become the nearest regime only (s < 0.25 ROUND, s <= 0.75 HOLLOW, else BITE).
    static void setSeamHardRegime(ShapeMorph& m, bool on) noexcept { m.seamHardRegime_ = on; }

    /// kLoudnessTableDb is not applied (gain 1). Forces a re-key so the next sample uses it.
    static void setSeamLoudnessBypassed(ShapeMorph& m, bool on) noexcept {
        m.seamLoudnessBypassed_ = on;
        m.needsRekey_ = true;
        if (on)
            m.loudnessGain_ = 1.0f;
    }

    // ---- observers ----

    /// {w_R, w_H, w_B} at the current smoothed SHAPE (plan S6.3; the hard-regime seam applies).
    [[nodiscard]] static std::array<float, 3> weights(const ShapeMorph& m) noexcept {
        const ShapeMorph::Weights w = m.weightsAt(m.shapeSm_.getCurrentValue());
        return {w.round, w.hollow, w.bite};
    }

    /// Regimes running at the last processed sample.
    [[nodiscard]] static int runningRegimeCount(const ShapeMorph& m) noexcept { return m.runningRegimes_; }

    /// Largest running-regime count since prepare / reset / resetMaxRunning.
    [[nodiscard]] static int maxRunningRegimeCount(const ShapeMorph& m) noexcept { return m.maxRunningRegimes_; }

    static void resetMaxRunning(ShapeMorph& m) noexcept { m.maxRunningRegimes_ = m.runningRegimes_; }

    /// True while channel 0 runs alone and is copied (mono link, plan S2).
    [[nodiscard]] static bool linked(const ShapeMorph& m) noexcept { return m.linked_; }

    /// The current engage value e (0 = bypass copy, 1 = processed).
    [[nodiscard]] static float engage(const ShapeMorph& m) noexcept { return m.engage_.getCurrentValue(); }

    /// The current smoothed SHAPE s.
    [[nodiscard]] static float smoothedShape(const ShapeMorph& m) noexcept { return m.shapeSm_.getCurrentValue(); }

    /// Every processed channel's path / ADAA / tilt / SVF state (read through ShapeMorph's
    /// per-regime output mirrors and the components' own stateFinite) is finite, and at least
    /// one regime mirror is nonzero (C-22: the path ran warm while disabled).
    [[nodiscard]] static bool regimeStateWarm(const ShapeMorph& m) noexcept {
        if (!m.stateFinite())
            return false;
        const std::size_t active = m.linked_ ? std::size_t{1} : std::size_t{2};
        bool anyNonzero = false;
        for (std::size_t c = 0; c < active; ++c) {
            const auto& ch = m.channels_[c];
            const std::array<float, 5> mirrors{ch.roundAdaaOut, ch.roundOut, ch.hollowOut, ch.biteTiltOut,
                                               ch.biteOut};
            for (const float v : mirrors) {
                if (!isFinite(v))
                    return false;
                if (v != 0.0f)
                    anyNonzero = true;
            }
        }
        return anyNonzero;
    }

    // ---- keytracked members (SC-025(a), round-2 ruling 2026-10-16) ----------------------
    /// Channel 0's keyed (s, f0) members and which regimes are running (only running regimes are
    /// keyed, so only those are comparable): ROUND's low-pass cutoff, HOLLOW's notch and peak
    /// centres, BITE's emphasis-tilt pivot and the emphasis inverse's pole coefficient.
    struct Keyed {
        bool round = false;
        bool hollow = false;
        bool bite = false;
        float roundCutoffHz = 0.0f;
        float hollowNotchHz = 0.0f;
        float hollowPeakHz = 0.0f;
        float bitePivotHz = 0.0f;
        float biteInverseA = 0.0f;
    };
    [[nodiscard]] static Keyed keyed(const ShapeMorph& m) noexcept {
        const auto& ch = m.channels_[0];
        return Keyed{.round = m.roundOn_,
                     .hollow = m.hollowOn_,
                     .bite = m.biteOn_,
                     .roundCutoffHz = ch.roundLp.getCutoff(),
                     .hollowNotchHz = ch.hollowNotch.getCutoff(),
                     .hollowPeakHz = ch.hollowBand.getCutoff(),
                     .bitePivotHz = ch.biteTilt.pivotHz(),
                     .biteInverseA = m.inverseA_};
    }
};

} // namespace Krate::DSP::detail
