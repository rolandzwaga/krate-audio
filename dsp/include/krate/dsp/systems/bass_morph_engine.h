// ==============================================================================
// Layer 3: System - Profundum Phase 8 (specs/profundum-phase8-bass-morph)
// ==============================================================================
// BassMorphEngine (position ramp, Anchor curves, morph laws, selection machine).
//   Serves (T003): FR-010 - FR-016, FR-020 - FR-022, FR-035, FR-050 - FR-052; SC-001, SC-002,
//          SC-003(a)(c), SC-014, SC-021(a), SC-022.
//   Serves (T004): FR-030 - FR-037, FR-040, FR-053 - FR-055, FR-061, FR-082; SC-003(b), SC-004(c), SC-004(d) engine,
//          SC-005 engine, SC-013, SC-019(b), SC-020, SC-021(b), SC-024 engine.
//   Serves (T005): FR-062, FR-080, FR-081 (restore side); SC-006(a), SC-007(c)(d), SC-011 (plan S4.8, S4.9).
//   Encodes: plan S4.1 - S4.7; C-2 (one selection per governing-amount group), C-3 (p_a's target = c_m(ramped p),
//            every interval), C-4 (every law in double, rounded once, clamped to the endpoints), C-5 (prepare ends
//            with reset()), C-6 (resolution cached behind dirty_), C-7 (sub-threshold targets are +0; curve()
//            flushes; restoreRuntime rejects -0 and sub-threshold positions), C-14 (endpoint(undeclared) reads A);
//            tasks.md deviation 11 (public static curve()).
//   Spec:  specs/profundum-phase8-bass-morph/spec.md
//   Plan:  specs/profundum-phase8-bass-morph/plan.md
//   Tasks: specs/profundum-phase8-bass-morph/tasks.md
//
// Control domain only: no audio buffer, no process* member (FR-010, FR-038). No component's processing method
// or setter is called here. Real-time safe: every method is noexcept, allocation-, lock-, exception- and
// I/O-free; all storage is fixed-size members.
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>          // L0: kDenormalThreshold, detail::isFinite, detail::flushDenormal
#include <krate/dsp/primitives/smoother.h>    // L1: SlewLimiter
#include <krate/dsp/systems/morph_state.h>    // L3 peer: MorphState, kFieldTable, forEachMorphLeaf

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <tuple>
#include <type_traits>
#include <utility>

namespace Krate::DSP {

/// FR-010: the global Bass Morph engine. Holds two MorphState endpoints, a rate-limited morph position p and the
/// Anchor position p_a, and resolves one MorphState per 32-sample control interval. One instance per instrument;
/// every voice reads the same resolved() (FR-016).
class BassMorphEngine {
public:
    enum class Endpoint : std::uint8_t { A, B };                    // FR-012
    enum class AnchorMorphMode : std::uint8_t { Lock, Blend, Punch };   // FR-014
    using Law = MorphState::Law;                                    // plan S1 C-1 re-exports
    using Axis = MorphState::Axis;
    using SelectionGroup = MorphState::SelectionGroup;

    /// FR-015: one interval's output.
    struct Resolved {
        MorphState state{};
        float position = 0.0f;          // ramped p
        float anchorPosition = 0.0f;    // ramped p_a
        float anchorMass = MassLaw::kNeutralMass;   // FR-054: Mass resolved at p_a
    };

    static constexpr std::size_t kControlInterval = 32;
    static_assert(kControlInterval == ProfundumCore::kControlInterval && kControlInterval == MassLaw::kControlInterval
                  && kControlInterval == BassStereoField::kControlInterval);
    static constexpr double kMinSampleRate = 22050.0;
    static constexpr double kMaxSampleRate = 192000.0;
    static constexpr float kMinPositionRampMs = 5.0f;        // FR-021 (Q3)
    static constexpr float kMaxPositionRampMs = 2000.0f;
    static constexpr float kDefaultPositionRampMs = 40.0f;   // FR-020 (D-6)
    static constexpr float kThetaUp = 0.52f;                 // FR-034 (D-1, Q1)
    static constexpr float kThetaDown = 0.48f;
    static constexpr int kPunchExponent = 3;                 // FR-051 (D-4, Q6)
    static constexpr AnchorMorphMode kDefaultAnchorMorphMode = AnchorMorphMode::Blend;   // FR-014 (D-7)
    static constexpr const auto& kLawTable = detail::morph_state_io::kFieldTable;        // plan S1 C-1
    static constexpr std::uint8_t kMorphRuntimeFormatVersion = 1;   // FR-062 (plan S4.8)
    static constexpr std::size_t kMorphRuntimeBytes = 20;

    /// The log2(x + o) caches of the two default endpoints (plan S1 C-4), so an endpoint never set reads correctly.
    BassMorphEngine() noexcept {
        refreshLogCache(a_, logA_);
        refreshLogCache(b_, logB_);
    }

    // ---- lifecycle (FR-011, plan S4.3) ----

    /// Clamps fs to [22 050, 192 000] (a non-finite rate reads as kMinSampleRate), configures both rate limiters at
    /// the interval rate fs / 32, then reset() (plan S1 C-5). Endpoints, target, mode and ramp time are preserved.
    void prepare(double sampleRate) noexcept {
        double fs = detail::isFinite(sampleRate) ? sampleRate : kMinSampleRate;
        fs = std::clamp(fs, kMinSampleRate, kMaxSampleRate);
        intervalRate_ = static_cast<float>(fs / static_cast<double>(kControlInterval));
        configureRate();
        prepared_ = true;
        reset();
    }

    /// Snaps p to its target and p_a to c_m(target), applies the FR-034 selection rule at the snapped positions
    /// (plan S4.5: inside the [kThetaDown, kThetaUp] band the selection is kept), then resolves.
    void reset() noexcept {
        posSlew_.snapTo(target_);
        anchorSlew_.snapTo(curve(mode_, target_));
        static_cast<void>(updateSelections(posSlew_.getCurrentValue(), anchorSlew_.getCurrentValue()));
        dirty_ = true;
        resolve();
    }

    // ---- endpoints (FR-012, plan S1 C-14) ----

    /// Stores @p s as endpoint @p e. False, nothing written, for an undeclared @p e or an invalid @p s; storing a
    /// bitwise-identical state is a no-op that returns true. Otherwise refreshes that endpoint's log2(x + o) cache
    /// (plan S1 C-4; 11 std::log2 calls, outside the interval path) and marks the resolution dirty: the change
    /// appears at the next advanceInterval(), never ramped by the engine (FR-061).
    bool setEndpoint(Endpoint e, const MorphState& s) noexcept {
        if (e != Endpoint::A && e != Endpoint::B) {
            return false;
        }
        if (!isValidMorphState(s)) {
            return false;
        }
        MorphState& dst = (e == Endpoint::B) ? b_ : a_;
        if (detail::morph_state_io::leafBitsEqual(s, dst)) {
            return true;
        }
        dst = s;
        refreshLogCache(dst, (e == Endpoint::B) ? logB_ : logA_);
        dirty_ = true;
        return true;
    }

    /// The stored snapshot; an undeclared @p e reads A (plan S1 C-14).
    [[nodiscard]] const MorphState& endpoint(Endpoint e) const noexcept {
        return (e == Endpoint::B) ? b_ : a_;
    }

    // ---- position, ramp, Anchor mode (FR-013, FR-020 - FR-022, FR-050, FR-051; plan S4.4) ----

    /// Non-finite -> keeps the previous target (filtered before the SlewLimiter, whose setTarget snaps NaN to 0).
    /// Every p < kDenormalThreshold (1e-15f: -0, negatives, denormals, tiny normals) becomes +0 and p >= 1 becomes 1
    /// (plan S1 C-7), so SlewLimiter::process()'s flush never moves a landed value (FR-022).
    void setPosition(float p) noexcept {
        if (!detail::isFinite(p)) {
            return;
        }
        p = (p < kDenormalThreshold) ? 0.0f : (p >= 1.0f ? 1.0f : p);
        target_ = p;
        posSlew_.setTarget(p);
    }

    [[nodiscard]] float position() const noexcept { return posSlew_.getCurrentValue(); }
    [[nodiscard]] float targetPosition() const noexcept { return target_; }
    [[nodiscard]] float anchorPosition() const noexcept { return anchorSlew_.getCurrentValue(); }

    /// FR-021: non-finite -> keeps the previous value; clamps to [5, 2000] ms. Applies to the rate from the next
    /// interval without moving p (SlewLimiter::configure touches neither current nor target).
    void setPositionRampMs(float ms) noexcept {
        if (!detail::isFinite(ms)) {
            return;
        }
        rampMs_ = std::clamp(ms, kMinPositionRampMs, kMaxPositionRampMs);
        if (prepared_) {
            configureRate();
        }
    }

    [[nodiscard]] float positionRampMs() const noexcept { return rampMs_; }

    /// FR-014: an undeclared value is ignored. p_a re-targets at the next interval (FR-051: never a step).
    void setAnchorMorphMode(AnchorMorphMode m) noexcept {
        if (m != AnchorMorphMode::Lock && m != AnchorMorphMode::Blend && m != AnchorMorphMode::Punch) {
            return;
        }
        mode_ = m;
    }

    [[nodiscard]] AnchorMorphMode anchorMorphMode() const noexcept { return mode_; }

    /// Group g's current selection; SelectionGroup::None reads A without indexing (plan S4.2).
    [[nodiscard]] Endpoint selection(SelectionGroup g) const noexcept {
        if (g == SelectionGroup::None) {
            return Endpoint::A;
        }
        return selections_[MorphState::groupIndex(g)];
    }

    /// FR-051 (tasks.md deviation 11): Lock c = 0, Blend c = p, Punch c = p^3, returned through
    /// detail::flushDenormal (plan S1 C-7), so a Punch value below 1e-15 is +0. c(0) = 0, c(1) = 1 (Blend, Punch),
    /// c monotone, c(p) <= p on [0, 1], c(0.5) = 0.125 exactly in Punch.
    [[nodiscard]] static float curve(AnchorMorphMode m, float p) noexcept {
        static_assert(kPunchExponent == 3, "curve() spells p^kPunchExponent as p * p * p");
        float c = p;
        if (m == AnchorMorphMode::Lock) {
            c = 0.0f;
        } else if (m == AnchorMorphMode::Punch) {
            c = p * p * p;
        }
        return detail::flushDenormal(c);
    }

    // ---- resolution (FR-015, plan S4.4) ----

    /// Advances p and p_a by one 32-sample interval, updates the per-group selections (plan S4.5) and resolves.
    /// p_a's target is c_m(the ramped p after this interval's step) (plan S1 C-3). Resolution is cached (plan S1
    /// C-6): it re-runs only when an endpoint, p, p_a (bitwise) or a selection changed. Before prepare(): performs
    /// the reset() snap (no ramp) and resolves (FR-011).
    const Resolved& advanceInterval() noexcept {
        if (!prepared_) {
            reset();
            return resolved_;
        }
        const float pOld = posSlew_.getCurrentValue();
        const float paOld = anchorSlew_.getCurrentValue();
        const float p = posSlew_.process();
        anchorSlew_.setTarget(curve(mode_, p));
        const float pa = anchorSlew_.process();
        if (!sameBits(p, pOld) || !sameBits(pa, paOld)) {
            dirty_ = true;
        }
        if (updateSelections(p, pa)) {
            dirty_ = true;
        }
        if (dirty_) {
            resolve();
        }
        return resolved_;
    }

    [[nodiscard]] const Resolved& resolved() const noexcept { return resolved_; }

    // ---- runtime round trip (FR-062, plan S4.8, S1 C-2, C-7) ----

    /// Writes the version-1 runtime blob (plan S4.8): version, target, ramped p, ramped p_a (float bit patterns),
    /// Anchor morph mode, ramp ms, the main-group selection mask (bit groupIndex(g): Character 0, ImageMode 1,
    /// FieldMotion 2, Motion 3) and the Anchor selection (0 = A, 1 = B).
    /// @return kMorphRuntimeBytes; 0, writing nothing, if @p dest is null or @p capacity < kMorphRuntimeBytes.
    [[nodiscard]] std::size_t serializeRuntime(std::byte* dest, std::size_t capacity) const noexcept {
        if (dest == nullptr || capacity < kMorphRuntimeBytes) {
            return 0;
        }
        const std::uint8_t version = kMorphRuntimeFormatVersion;
        const float target = target_;
        const float p = posSlew_.getCurrentValue();
        const float pa = anchorSlew_.getCurrentValue();
        const auto mode = static_cast<std::uint8_t>(mode_);
        const float ramp = rampMs_;
        std::uint8_t mask = 0;
        for (const SelectionGroup g : kSelectionGroups) {
            if (g != SelectionGroup::Anchor && selections_[MorphState::groupIndex(g)] == Endpoint::B) {
                mask = static_cast<std::uint8_t>(mask | (1u << MorphState::groupIndex(g)));
            }
        }
        const std::uint8_t anchorSel =
            selections_[MorphState::groupIndex(SelectionGroup::Anchor)] == Endpoint::B ? std::uint8_t{1}
                                                                                         : std::uint8_t{0};
        std::memcpy(dest + kRtVersionOffset, &version, sizeof(version));
        std::memcpy(dest + kRtTargetOffset, &target, sizeof(target));
        std::memcpy(dest + kRtPositionOffset, &p, sizeof(p));
        std::memcpy(dest + kRtAnchorPositionOffset, &pa, sizeof(pa));
        std::memcpy(dest + kRtModeOffset, &mode, sizeof(mode));
        std::memcpy(dest + kRtRampOffset, &ramp, sizeof(ramp));
        std::memcpy(dest + kRtMainMaskOffset, &mask, sizeof(mask));
        std::memcpy(dest + kRtAnchorSelectionOffset, &anchorSel, sizeof(anchorSel));
        return kMorphRuntimeBytes;
    }

    /// Restores a blob written by serializeRuntime. Decodes into locals; returns false and changes NOTHING if
    /// @p src is null, @p size < kMorphRuntimeBytes, the version is not kMorphRuntimeFormatVersion, a position
    /// (target, p, p_a) is non-finite, outside [0, 1], -0 or 0 < |x| < kDenormalThreshold (plan S1 C-7: the engine
    /// never holds one), the ramp is non-finite or outside [5, 2000] ms, the mode byte is > Punch, the main mask is
    /// > 0x0F, the Anchor byte is > 1, or a stored selection contradicts the plan S4.5 rule at the stored position
    /// for the CURRENT endpoints (call setEndpoint first; inside the hysteresis band either side is accepted).
    /// On success installs ramp, mode, target, p, p_a and the selections and resolves; no advanceInterval(), no
    /// reset(). The next advanceInterval() re-targets p_a at c_m(p) before stepping (plan S1 C-3), so a restored
    /// engine tracks the original bitwise.
    [[nodiscard]] bool restoreRuntime(const std::byte* src, std::size_t size) noexcept {
        if (src == nullptr || size < kMorphRuntimeBytes) {
            return false;
        }
        std::uint8_t version = 0;
        float target = 0.0f;
        float p = 0.0f;
        float pa = 0.0f;
        std::uint8_t modeByte = 0;
        float ramp = 0.0f;
        std::uint8_t mask = 0;
        std::uint8_t anchorSel = 0;
        std::memcpy(&version, src + kRtVersionOffset, sizeof(version));
        std::memcpy(&target, src + kRtTargetOffset, sizeof(target));
        std::memcpy(&p, src + kRtPositionOffset, sizeof(p));
        std::memcpy(&pa, src + kRtAnchorPositionOffset, sizeof(pa));
        std::memcpy(&modeByte, src + kRtModeOffset, sizeof(modeByte));
        std::memcpy(&ramp, src + kRtRampOffset, sizeof(ramp));
        std::memcpy(&mask, src + kRtMainMaskOffset, sizeof(mask));
        std::memcpy(&anchorSel, src + kRtAnchorSelectionOffset, sizeof(anchorSel));

        if (version != kMorphRuntimeFormatVersion) {
            return false;
        }
        if (!isRuntimePosition(target) || !isRuntimePosition(p) || !isRuntimePosition(pa)) {
            return false;
        }
        if (!detail::isFinite(ramp) || ramp < kMinPositionRampMs || ramp > kMaxPositionRampMs) {
            return false;
        }
        if (modeByte > static_cast<std::uint8_t>(AnchorMorphMode::Punch)) {
            return false;
        }
        if (mask > kRtMainMaskMax || anchorSel > 1u) {
            return false;
        }
        std::array<Endpoint, MorphState::kNumSelectionGroups> sel{};
        for (const SelectionGroup g : kSelectionGroups) {
            const std::size_t i = MorphState::groupIndex(g);
            const bool isB = (g == SelectionGroup::Anchor) ? anchorSel == 1u
                                                           : ((static_cast<unsigned>(mask) >> i) & 1u) != 0u;
            sel[i] = isB ? Endpoint::B : Endpoint::A;
            const float q = (g == SelectionGroup::Anchor) ? pa : p;
            if (ruleSelection(q, governingAmount(a_, g), governingAmount(b_, g), sel[i]) != sel[i]) {
                return false;   // contradicts plan S4.5 for the current endpoints
            }
        }

        rampMs_ = ramp;
        if (prepared_) {
            configureRate();
        }
        mode_ = static_cast<AnchorMorphMode>(modeByte);
        target_ = target;
        posSlew_.snapTo(p);
        posSlew_.setTarget(target_);
        anchorSlew_.snapTo(pa);
        selections_ = sel;
        dirty_ = true;
        resolve();
        return true;
    }

private:
    // Plan S4.8 runtime layout, version 1.
    static constexpr std::size_t kRtVersionOffset = 0;
    static constexpr std::size_t kRtTargetOffset = 1;
    static constexpr std::size_t kRtPositionOffset = 5;
    static constexpr std::size_t kRtAnchorPositionOffset = 9;
    static constexpr std::size_t kRtModeOffset = 13;
    static constexpr std::size_t kRtRampOffset = 14;
    static constexpr std::size_t kRtMainMaskOffset = 18;
    static constexpr std::size_t kRtAnchorSelectionOffset = 19;
    static constexpr std::uint8_t kRtMainMaskMax = 0x0F;   // four main groups
    static_assert(kRtAnchorSelectionOffset + 1 == kMorphRuntimeBytes);
    static_assert(MorphState::groupIndex(SelectionGroup::Anchor) == 4
                      && MorphState::groupIndex(SelectionGroup::Motion) == 3,
                  "the main mask holds bits 0 - 3; the Anchor selection is its own byte");

    /// Plan S4.8 / C-7: a storable position is +0 or in [kDenormalThreshold, 1] (rejects non-finite, negatives,
    /// -0 and 0 < |x| < 1e-15). The +0 test is on the bit pattern, so it holds under fast-math and DAZ.
    [[nodiscard]] static bool isRuntimePosition(float x) noexcept {
        if (!detail::isFinite(x)) {
            return false;
        }
        if (std::bit_cast<std::uint32_t>(x) == 0u) {
            return true;   // +0
        }
        return x >= kDenormalThreshold && x <= 1.0f;
    }

    [[nodiscard]] static bool sameBits(float x, float y) noexcept {
        return std::bit_cast<std::uint32_t>(x) == std::bit_cast<std::uint32_t>(y);
    }

    /// Per-interval step = (1 / rampMs) / (intervalRate * 0.001) = 32 000 / (rampMs * fs) = Delta p_max (FR-020).
    void configureRate() noexcept {
        posSlew_.configure(1.0f / rampMs_, intervalRate_);
        anchorSlew_.configure(1.0f / rampMs_, intervalRate_);
    }

    /// FR-034's governing amount of group g (plan S4.5): the continuous field whose value 0 makes that side of the
    /// group's discrete fields silent. Callers pass g != None (they iterate kSelectionGroups).
    [[nodiscard]] static float governingAmount(const MorphState& s, SelectionGroup g) noexcept {
        switch (g) {
            case SelectionGroup::Character: return s.characterAmount;
            case SelectionGroup::ImageMode: return s.field.spread;
            case SelectionGroup::FieldMotion: return s.field.motionDepth;
            case SelectionGroup::Motion: return s.motion.amount;   // [-1, 1]; 0 routes nothing
            case SelectionGroup::Anchor: return s.anchor.level;
            case SelectionGroup::None: break;
        }
        return 0.0f;
    }

    /// FR-034 selection rule of one group (plan S4.5, S1 C-2) at position q with governing amounts gA, gB:
    /// rule 1 identity (q <= 0 -> A, q >= 1 -> B); rule 2 (exactly one governing amount is 0 -> the other side,
    /// for every q in (0, 1)); rule 3 hysteresis (q >= kThetaUp -> B, q <= kThetaDown -> A, else @p kept,
    /// including the side rule 2 had forced). "== 0.0f" matches +/-0; a negative motion.amount is non-zero.
    /// restoreRuntime accepts a stored selection s iff ruleSelection(q, gA, gB, s) == s (plan S4.8).
    [[nodiscard]] static Endpoint ruleSelection(float q, float gA, float gB, Endpoint kept) noexcept {
        if (q <= 0.0f) {
            return Endpoint::A;   // rule 1
        }
        if (q >= 1.0f) {
            return Endpoint::B;
        }
        if (gA == 0.0f && gB != 0.0f) {
            return Endpoint::B;   // rule 2
        }
        if (gB == 0.0f && gA != 0.0f) {
            return Endpoint::A;
        }
        if (q >= kThetaUp) {
            return Endpoint::B;   // rule 3
        }
        if (q <= kThetaDown) {
            return Endpoint::A;
        }
        return kept;   // inside the band
    }

    /// Applies ruleSelection per group, at q = p for the four main groups and q = p_a for Anchor.
    /// @return true if any group's selection changed.
    bool updateSelections(float p, float pa) noexcept {
        bool changed = false;
        for (const SelectionGroup g : kSelectionGroups) {
            const float q = (g == SelectionGroup::Anchor) ? pa : p;
            Endpoint& sel = selections_[MorphState::groupIndex(g)];
            const Endpoint before = sel;
            sel = ruleSelection(q, governingAmount(a_, g), governingAmount(b_, g), sel);
            changed = changed || sel != before;
        }
        return changed;
    }

    /// log2(x + o) of every Logarithmic leaf of @p s, in double (plan S1 C-4); the other slots are left alone.
    static void refreshLogCache(const MorphState& s, std::array<double, kMorphLeafFieldCount>& cache) noexcept {
        namespace io = detail::morph_state_io;
        io::forEachMorphLeaf(
            [&](auto leaf, [[maybe_unused]] const auto& x) {
                constexpr std::size_t i = decltype(leaf)::value;
                constexpr MorphState::FieldInfo row = io::kFieldTable[i];
                if constexpr (row.law == Law::Logarithmic) {
                    cache[i] = std::log2(static_cast<double>(x) + static_cast<double>(row.logOffset));
                }
            },
            s);
    }

    /// Plan S4.6: per leaf with q = p_a for the Anchor block and p otherwise. Stepped: the group's selected
    /// endpoint's value. Continuous: identity by selection (q == 0 or bitwise-equal endpoints -> A's value, q == 1
    /// -> B's; FR-035, never arithmetic), else the row's law in double (plan S1 C-4), rounded to float once and
    /// clamped to [min, max] of the endpoints (FR-036). Equal-power evaluates the FR-033 radicand
    /// (1 - t) m_A^2 + t m_B^2 regrouped as m_A^2 + t (m_B^2 - m_A^2), so every rounded operation is monotone in t
    /// (FR-037). anchorMass is Mass resolved linearly at p_a with the same identity branches (FR-054).
    void resolve() noexcept {
        namespace io = detail::morph_state_io;
        const float p = posSlew_.getCurrentValue();
        const float pa = anchorSlew_.getCurrentValue();
        io::forEachMorphLeaf(
            [&](auto leaf, auto& out, const auto& xa, const auto& xb) {
                constexpr std::size_t i = decltype(leaf)::value;
                constexpr MorphState::FieldInfo row = io::kFieldTable[i];
                float q = p;
                if constexpr (row.anchorBlock) {
                    q = pa;
                }
                if constexpr (row.law == Law::Stepped) {
                    out = (selections_[MorphState::groupIndex(row.group)] == Endpoint::A) ? xa : xb;
                } else {
                    if (q == 0.0f || sameBits(xa, xb)) {
                        out = xa;
                    } else if (q == 1.0f) {
                        out = xb;
                    } else {
                        const double t = static_cast<double>(q);
                        const double da = static_cast<double>(xa);
                        const double db = static_cast<double>(xb);
                        double y = da;
                        if constexpr (row.law == Law::Linear) {
                            y = da + t * (db - da);   // FR-031
                        } else if constexpr (row.law == Law::EqualPower) {
                            const double a2 = da * da;
                            y = std::sqrt(a2 + t * (db * db - a2));   // FR-033
                        } else {
                            y = std::exp2(logA_[i] + t * (logB_[i] - logA_[i]))
                                - static_cast<double>(row.logOffset);   // FR-032
                        }
                        out = std::clamp(static_cast<float>(y), std::min(xa, xb), std::max(xa, xb));   // FR-036
                    }
                }
            },
            resolved_.state, std::as_const(a_), std::as_const(b_));

        resolved_.position = p;
        resolved_.anchorPosition = pa;
        if (pa == 0.0f || sameBits(a_.mass, b_.mass)) {
            resolved_.anchorMass = a_.mass;
        } else if (pa == 1.0f) {
            resolved_.anchorMass = b_.mass;
        } else {
            const double ma = static_cast<double>(a_.mass);
            const double mb = static_cast<double>(b_.mass);
            const double y = ma + static_cast<double>(pa) * (mb - ma);
            resolved_.anchorMass =
                std::clamp(static_cast<float>(y), std::min(a_.mass, b_.mass), std::max(a_.mass, b_.mass));
        }
        dirty_ = false;
    }

    static constexpr std::array<SelectionGroup, MorphState::kNumSelectionGroups> kSelectionGroups{
        SelectionGroup::Character, SelectionGroup::ImageMode, SelectionGroup::FieldMotion, SelectionGroup::Motion,
        SelectionGroup::Anchor};

    MorphState a_{};
    MorphState b_{};
    // log2(x + o) per Logarithmic leaf (plan S1 C-4), refreshed by the constructor and setEndpoint; other slots 0.
    std::array<double, kMorphLeafFieldCount> logA_{};
    std::array<double, kMorphLeafFieldCount> logB_{};
    SlewLimiter posSlew_{};
    SlewLimiter anchorSlew_{};
    float target_ = 0.0f;
    float rampMs_ = kDefaultPositionRampMs;
    float intervalRate_ = 48000.0f / static_cast<float>(kControlInterval);   // replaced by prepare()
    AnchorMorphMode mode_ = kDefaultAnchorMorphMode;
    std::array<Endpoint, MorphState::kNumSelectionGroups> selections_{};    // per group (plan S1 C-2), start A
    Resolved resolved_{};
    bool prepared_ = false;
    bool dirty_ = true;

    static_assert(MorphState::groupIndex(SelectionGroup::Anchor) < std::tuple_size_v<decltype(selections_)>);
};

}  // namespace Krate::DSP
