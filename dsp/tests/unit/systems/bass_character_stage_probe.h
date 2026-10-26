// ==============================================================================
// Profundum Phase 5 (specs/profundum-phase5-shape-character) - test-only probe
// ==============================================================================
// detail::BassCharacterStageProbe: the friend of BassCharacterStage (tasks.md T009,
// deviation 5: shared by >= 2 TUs, so it lives in one header to avoid an ODR clash;
// precedent systems/bloom_engine.h:1666). Read-only after T009: a missing accessor is
// recorded as a DEFECT line by the task that needs it, never added here by that task;
// T022 adds the accessors those DEFECT lines name (fracturePhase, from T015); the round-2 ruling
// (2026-10-16) adds the SC-025(a) keytracked readers (crushHoldMultiple, crushPhaseIncrement,
// fracturePhaseIncrement); round 5 adds crushSlots (SC-030 path-dependence record).
//
// Every function is static. `ch` is 0 (left) or 1 (right).
// ==============================================================================

#pragma once

#include <krate/dsp/processors/keytracked_tilt.h>
#include <krate/dsp/systems/bass_character_stage.h>

#include <algorithm>
#include <array>
#include <cstddef>

namespace Krate::DSP::detail {

struct BassCharacterStageProbe {
    using Stage = BassCharacterStage;
    using Algorithm = BassCharacterStage::Algorithm;
    using BranchOut = BassCharacterStage::BranchOut;

    /// The Rectify branch's derived values (S5.4) at its last configure().
    struct RectifyMix {
        float wet;
        float b;
        float mk;
    };

    // ---- seams (each one predictable branch in the stage) --------------------
    static void setSeamIdentityLaw(Stage& s, bool on) noexcept { s.seamIdentityLaw_ = on; }
    static void setSeamRawDry(Stage& s, bool on) noexcept { s.seamRawDry_ = on; }
    static void setSeamNaiveBaseRate(Stage& s, bool on) noexcept { s.seamNaiveBaseRate_ = on; }
    static void setSeamHardSwitch(Stage& s, bool on) noexcept { s.seamHardSwitch_ = on; }
    static void setSeamUnsmoothed(Stage& s, bool on) noexcept { s.seamUnsmoothed_ = on; }
    static void setSeamHardBypass(Stage& s, bool on) noexcept { s.seamHardBypass_ = on; }
    static void setSeamPreToneNeutral(Stage& s, bool on) noexcept { s.seamPreToneNeutral_ = on; }
    static void setSeamPostToneNeutral(Stage& s, bool on) noexcept { s.seamPostToneNeutral_ = on; }

    // ---- switch machine (S5.6) ------------------------------------------------
    [[nodiscard]] static std::size_t runningBranchCount(const Stage& s) noexcept {
        if (s.bypassed_)
            return 0;
        return s.inFlight() ? std::size_t{2} : std::size_t{1};
    }
    [[nodiscard]] static std::size_t maxRunningBranchCount(const Stage& s) noexcept { return s.maxRunningBranches_; }
    static void resetMaxRunning(Stage& s) noexcept { s.maxRunningBranches_ = 0; }
    [[nodiscard]] static Algorithm current(const Stage& s) noexcept { return s.current_; }
    [[nodiscard]] static Algorithm target(const Stage& s) noexcept { return s.target_; }
    [[nodiscard]] static Algorithm pending(const Stage& s) noexcept { return s.pending_; }
    [[nodiscard]] static bool hasPending(const Stage& s) noexcept { return s.hasPending_; }
    /// True while a transition is in flight (FR-026 pre-roll or crossfade).
    [[nodiscard]] static bool crossfading(const Stage& s) noexcept { return s.inFlight(); }
    /// True during the FR-026 pre-roll only (the crossfade proper has not started).
    [[nodiscard]] static bool prerolling(const Stage& s) noexcept { return s.prerolling_; }
    /// True during the FR-054 bypass-exit pre-roll (output still the copy).
    [[nodiscard]] static bool engagePrerolling(const Stage& s) noexcept { return s.engagePreroll_; }
    /// Samples a crossfade lasts: ceil(1 / crossfadeIncrement(kSwitchCrossfadeMs, fs)).
    [[nodiscard]] static std::size_t crossfadeLength(const Stage& s) noexcept { return s.xfadeLen_; }
    /// Samples the FR-026 / FR-054 pre-roll lasts at the stage's current f0: round(fs / f0), at least 1.
    [[nodiscard]] static std::size_t prerollLength(const Stage& s) noexcept { return s.prerollSamples(); }

    // ---- link, bypass, smoothers ----------------------------------------------
    [[nodiscard]] static bool linked(const Stage& s) noexcept { return s.linked_; }
    [[nodiscard]] static bool bypassed(const Stage& s) noexcept { return s.bypassed_; }
    /// The bypass crossfade value e (FR-054).
    [[nodiscard]] static float engage(const Stage& s) noexcept { return s.engage_.getCurrentValue(); }
    [[nodiscard]] static float smoothedAmount(const Stage& s, int ch) noexcept {
        return s.ch_[idx(ch)].amountSm.getCurrentValue();
    }
    [[nodiscard]] static float smoothedTone(const Stage& s, int ch) noexcept {
        return s.ch_[idx(ch)].toneSm.getCurrentValue();
    }

    // ---- r observer (S5.7) -----------------------------------------------------
    [[nodiscard]] static bool flippedRectify(const Stage& s) noexcept { return s.flippedRect_; }
    [[nodiscard]] static bool flippedFracture(const Stage& s) noexcept { return s.flippedFrac_; }

    // ---- tone filters (FR-055, SC-028) ----------------------------------------
    [[nodiscard]] static bool hasPreTilt(Algorithm a) noexcept {
        return a == Algorithm::Tube || a == Algorithm::Bite;
    }
    /// The branch's pre tilt; algorithms without one (hasPreTilt false) return a neutral default tilt.
    [[nodiscard]] static const KeytrackedTilt& preTilt(const Stage& s, Algorithm a, int ch) noexcept {
        const auto& c = s.ch_[idx(ch)];
        if (a == Algorithm::Tube)
            return c.tube.preTilt;
        if (a == Algorithm::Bite)
            return c.bite.preTilt;
        return neutralTilt();
    }
    [[nodiscard]] static const KeytrackedTilt& postTilt(const Stage& s, Algorithm a, int ch) noexcept {
        return Stage::visitBranch(s.ch_[idx(ch)], a,
                                  [](const auto& br) noexcept -> const KeytrackedTilt& { return br.postTilt; });
    }

    // ---- direct branch drive (C-19, SC-027, SC-028) ---------------------------
    /// Configures the branch with channel ch's current smoothed Amount / Tone, then runs one
    /// F·fs sample. y is BEFORE the post tilt (C-23: the post tilt runs at the base rate).
    [[nodiscard]] static BranchOut runBranch(Stage& s, Algorithm a, int ch, float u) noexcept {
        auto& c = s.ch_[idx(ch)];
        const float amount = c.amountSm.getCurrentValue();
        const float tone = c.toneSm.getCurrentValue();
        return Stage::visitBranch(c, a, [amount, tone, u](auto& br) noexcept -> BranchOut {
            br.configure(amount, tone);
            return br.process(u);
        });
    }
    /// Resets the branch and re-keys it with the stage's f0 (what start() does).
    static void resetBranch(Stage& s, Algorithm a, int ch) noexcept { s.startBranch(s.ch_[idx(ch)], a); }
    /// The branch's lin coefficient c_b at its last configure() (S2, S5.4).
    [[nodiscard]] static float branchLinearCoefficient(const Stage& s, Algorithm a, int ch) noexcept {
        return Stage::visitBranch(s.ch_[idx(ch)], a,
                                  [](const auto& br) noexcept -> float { return br.linearCoefficient(); });
    }
    [[nodiscard]] static RectifyMix rectifyMix(const Stage& s, int ch) noexcept {
        const auto& r = s.ch_[idx(ch)].rect;
        return {.wet = r.wet, .b = r.b, .mk = r.mk};
    }

    // ---- Crush and Tube counters / settings (T011, T014) ----------------------
    [[nodiscard]] static std::size_t crushMaxOccupancy(const Stage& s) noexcept { return s.crushMaxOccupancy_; }
    [[nodiscard]] static std::size_t crushRetirements(const Stage& s) noexcept { return s.crushRetirements_; }
    [[nodiscard]] static float tubeSaturationAmount(const Stage& s, int ch) noexcept {
        return s.ch_[idx(ch)].tube.stage.getSaturationAmount();
    }

    // ---- Fracture phasor (T015 DEFECT; added by T022) -------------------------
    /// The Fracture branch's ÷2 phasor φ ∈ [0, 2) in periods of f0: the value the next F-sample uses.
    [[nodiscard]] static double fracturePhase(const Stage& s, int ch) noexcept { return s.ch_[idx(ch)].frac.phase; }

    // ---- keytracked members (SC-025(a), round-2 ruling 2026-10-16) -------------
    /// Crush's effective hold multiple m_eff = max(1, min(m(A), F·fs/(4·f0))), as process() computes it.
    [[nodiscard]] static double crushHoldMultiple(const Stage& s, int ch) noexcept {
        const auto& c = s.ch_[idx(ch)].crush;
        return std::max(1.0, std::min(c.mAmount, c.mCap));
    }
    /// Crush's hold-phasor increment f0 / (F·fs), cycles per F-sample.
    [[nodiscard]] static double crushPhaseIncrement(const Stage& s, int ch) noexcept {
        return s.ch_[idx(ch)].crush.inc;
    }
    /// Crush hold engine e (0 or 1) of channel ch: its multiple m, the per-slot hysteresis memory (level q_ref,
    /// held input v_ref, valid) and one quantiser step in the held value's units (SC-030 path-dependence record,
    /// round-5 ruling 2026-10-16).
    struct CrushSlots {
        int m = 0;
        float step = 0.0f;
        std::array<float, Stage::kCrushMaxSlots> level{};
        std::array<float, Stage::kCrushMaxSlots> input{};
        std::array<bool, Stage::kCrushMaxSlots> valid{};
    };
    [[nodiscard]] static CrushSlots crushSlots(const Stage& s, int ch, std::size_t e) noexcept {
        const auto& c = s.ch_[idx(ch)].crush;
        const auto& h = c.engine[e];
        return CrushSlots{.m = h.m, .step = c.qStep, .level = h.slotLevel, .input = h.slotInput, .valid = h.slotValid};
    }
    /// Fracture's ÷2-phasor increment Δφ = f0 / (F·fs), periods of f0 per F-sample.
    [[nodiscard]] static double fracturePhaseIncrement(const Stage& s, int ch) noexcept {
        return s.ch_[idx(ch)].frac.inc;
    }

private:
    [[nodiscard]] static std::size_t idx(int ch) noexcept { return ch == 0 ? std::size_t{0} : std::size_t{1}; }
    [[nodiscard]] static const KeytrackedTilt& neutralTilt() noexcept {
        static const KeytrackedTilt kNeutral{};
        return kNeutral;
    }
};

}  // namespace Krate::DSP::detail
