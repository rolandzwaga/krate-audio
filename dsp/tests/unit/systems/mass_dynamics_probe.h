// ==============================================================================
// Profundum Phase 6 (specs/profundum-phase6-mass-dynamics) - test-only probe
// ==============================================================================
// detail::MassDynamicsProbe: the friend of BassCompressor (and, from T010, of MassDynamics).
// tasks.md T007 writes the BassCompressor half; T010 appends the MassDynamics accessors; read-only
// after (tasks.md file map). Shared by several TUs, so it lives in one header (ODR; precedent
// dsp/tests/unit/systems/bass_character_stage_probe.h).
//
// Every function is static. The switches are the plan S5.1 probe fields: at their defaults the
// production path is untouched.
// ==============================================================================

#pragma once

#include <krate/dsp/systems/mass_dynamics.h>

#include <array>

namespace Krate::DSP::detail {

struct MassDynamicsProbe {
    // ---- BassCompressor (T007) ------------------------------------------------
    /// Fixed release in ms (SC-007 positive control); 0 = the D-5 rule. Takes effect at the next
    /// 32-sample control step (or at reset()).
    static void setFixedReleaseMs(BassCompressor& c, float ms) noexcept { c.probeFixedReleaseMs_ = ms; }
    /// Detector high-pass bypass (SC-006(c) positive control).
    static void setHpBypass(BassCompressor& c, bool on) noexcept { c.probeHpBypass_ = on; }
    /// The struck rectified feed (SC-006(e) negative control): max(|L|, |R|) of the unfiltered
    /// channels into the DynamicsProcessor's internal sidechain at the corner (applied at the next
    /// control step).
    static void setRectifiedFeed(BassCompressor& c, bool on) noexcept { c.probeRectifiedFeed_ = on; }
    /// Replace kAmountTable in the amount mapping (nullptr = kAmountTable). Applied at the next
    /// control step. The table must outlive its use.
    static void overrideAmountTable(BassCompressor& c, const std::array<BassCompressor::AmountRow, 5>* table) noexcept {
        c.probeTableOverride_ = table;
    }

    /// The DynamicsProcessor's parameters in effect (dynamics_processor.h getters).
    [[nodiscard]] static float threshold(const BassCompressor& c) noexcept { return c.dyn_.getThreshold(); }
    [[nodiscard]] static float ratio(const BassCompressor& c) noexcept { return c.dyn_.getRatio(); }
    [[nodiscard]] static float knee(const BassCompressor& c) noexcept { return c.dyn_.getKneeWidth(); }
    /// The latched makeup target (the makeup smoother's target), dB.
    [[nodiscard]] static float makeupTargetDb(const BassCompressor& c) noexcept { return c.makeupTargetDb_; }
    [[nodiscard]] static float releaseTimeInDynamics(const BassCompressor& c) noexcept { return c.dyn_.getReleaseTime(); }

    // ---- MassDynamics (T010; tasks.md deviation 4) ---------------------------------
    /// Non-const access to the composed stages (the chain's positive / negative controls; with the
    /// BassCompressor switches above, e.g. setFixedReleaseMs(compressor(md), 275.0f)).
    [[nodiscard]] static TransientShaper& shaper(MassDynamics& m) noexcept { return m.shaper_; }
    [[nodiscard]] static BassCompressor& compressor(MassDynamics& m) noexcept { return m.comp_; }
};

}  // namespace Krate::DSP::detail
