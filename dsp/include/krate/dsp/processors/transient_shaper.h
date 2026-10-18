// ==============================================================================
// Layer 2: DSP Processor - Profundum Phase 6 (specs/profundum-phase6-mass-dynamics)
// ==============================================================================
// TransientShaper: bass-safe attack / sustain shaper with one gain for both channels.
//   Spec:  specs/profundum-phase6-mass-dynamics/spec.md (FR-001 - FR-009, FR-040 - FR-042,
//          FR-047, FR-049; SC-008, SC-009, SC-011, SC-012/013/016/018/019/021 shaper arms, SC-025)
//   Plan:  specs/profundum-phase6-mass-dynamics/plan.md (S4, S1 C-5, C-10, S8.1, S12.2)
//   Tests: dsp/tests/unit/processors/transient_shaper_test.cpp (tag [transient-shaper]),
//          dsp/tests/unit/processors/transient_shaper_nonfinite_test.cpp (SC-019 shaper arm)
//
// Includes Layer 0, Layer 1 and the peer Layer 2 headers envelope_follower.h and
// period_peak_detector.h (precedent: dynamics_processor.h includes envelope_follower.h).
//
// Signal path per sample (plan S4.3):
//   1. sanitise: a non-finite sample becomes 0.0f (FR-007, detail::isFinite survives -ffast-math);
//   2. m = PeriodPeakDetector held true-peak of max(|L|, |R|) over a 50 ms hold (FR-004, FR-005);
//   3. f = fast EnvelopeFollower (Amplitude, 0.5 ms / 40 ms);
//   4. s = slow EnvelopeFollower (Amplitude), ticked once per kSlowTickSamples samples and linearly
//      interpolated between ticks (C-5);
//   5. c = clamp((f_dB - s_dB) / K, 0, 1), K = 12 dB (FR-008);
//   6. gain_dB = A_s * c + S_s * (1 - c), A_s / S_s the 10 ms smoothed attack / sustain gains;
//   7. L *= g, R *= g with the one g = dbToGain(gain_dB).
//
// C-5 (why the slow follower is ticked /8): the transient time of a step onset is
// t_3dB = 0.438 * tau with tau = T / 2pi (envelope_follower.h coefficient), so 250 ms needs a
// 3.59 s attack while EnvelopeFollower clamps its attack at 500 ms. Ticking the slow follower once
// per 8 samples makes its time argument real time / 8 (real range 0.8 ms ... 4 s). Its release
// argument is therefore kSlowReleaseMs / 8 (7.5 ms = 60 ms real). The tick phase is state: reset by
// reset() / prepare() and carried across processBlock calls, so the shaper is partition-invariant
// (FR-049, SC-018).
//
// S4.4 (why the law behaves): on a steady note m is constant to the true-peak estimate's precision,
// both followers converge to it, f / s -> 1, c -> 0 and the gain is the sustain gain (SC-008(a));
// the followers never see the waveform, so nothing rides individual cycles (SC-009). On an onset
// from silence s ~ 0 while f > 0, so c = 1 and the full attack gain applies (SC-008(b)(e)); the gain
// is a convex combination of A and S, so it never exceeds max(A, S). On a decay the slow release
// (60 ms) is >= the fast release (40 ms), so f <= s and c = 0: no false boost on decays. Recorded
// limitation (not gated): a legato note at the same level, or a re-attack inside the 50 ms hold, is
// not a transient for a level-based shaper.
//
// Contract: fixed size, zero heap, every method noexcept and allocation-free (FR-040). Before
// prepare(), with a null channel or with 0 samples, processBlock is a no-op (FR-047). Setters clamp
// to the D-3 ranges; a non-finite argument keeps the previous value (SC-025). Latency 0.
// FTZ / DAZ is a precondition (PeriodPeakDetector's oversampler is not flushed).
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/processors/envelope_follower.h>
#include <krate/dsp/processors/period_peak_detector.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace Krate::DSP {

namespace detail {
struct TransientShaperProbe;

/// Per-sample dB conversions of the Phase 6 dynamics (FR-045, 2026-10-18): the Layer-0 `gainToDb` /
/// `dbToGain` evaluate their constexpr series at run time (about 24 ns each under MSVC, six per
/// sample pair across the shaper and the compressor, about 60 % of the SC-017 cost). These are the
/// same functions through `std::log2` / `std::exp2`, equal within float rounding; the callers pass
/// finite arguments (a positive ratio; a bounded dB value).
inline constexpr float kDbPerOctave = 6.02059991327962f;      // 20 * log10(2)
inline constexpr float kOctavesPerDb = 0.166096404744368f;    // log2(10) / 20
[[nodiscard]] inline float gainToDbFast(float ratio) noexcept { return kDbPerOctave * std::log2(ratio); }
[[nodiscard]] inline float dbToGainFast(float dB) noexcept { return std::exp2(dB * kOctavesPerDb); }
}  // namespace detail

class TransientShaper {
public:
    // ---- D-3 ranges and FR-009 defaults ----
    static constexpr float kMinGainDb = -12.0f;
    static constexpr float kMaxGainDb = 12.0f;
    static constexpr float kMinDurationMs = 5.0f;
    static constexpr float kMaxDurationMs = 250.0f;
    static constexpr float kDefaultAttackDb = 6.0f;
    static constexpr float kDefaultSustainDb = 0.0f;
    static constexpr float kDefaultDurationMs = 40.0f;  // C-10: AnchorOscillator punchTimeMs
    // ---- FR-008 law ----
    static constexpr float kDifferentialRangeDb = 12.0f;  // K (ratified Q5)
    // ---- detector design (S4.3, C-5, C-10) ----
    static constexpr float kHoldMs = 50.0f;  // >= the E0 period (48.54 ms)
    static constexpr float kFastAttackMs = 0.5f;
    static constexpr float kFastReleaseMs = 40.0f;
    static constexpr float kSlowReleaseMs = 60.0f;  // real time; argument kSlowReleaseMs / kSlowTickSamples
    static constexpr std::size_t kSlowTickSamples = 8;
    static constexpr float kGainSmoothMs = 10.0f;
    static constexpr float kSilenceFloorLin = 1e-7f;  // -140 dB

    static_assert((kSlowTickSamples & (kSlowTickSamples - 1)) == 0, "kSlowTickSamples must be a power of two");

    // ---- FR-008 duration calibration: duration (ms) -> slow EF attack argument (ms), log-log ----
    struct DurationPoint {
        float durationMs;
        float slowAttackArgMs;
    };
    // SEED from the model (plan S12.2; the 7.5 / 15 / 30 / 60 / 150 ms entries are log-log
    // interpolations of their neighbours, tasks.md deviation 7). Harvested and replaced by
    // TransientShaper_DurationTableHarvest (tasks.md T019, evidence/duration-table.txt).
    static constexpr std::array<DurationPoint, 11> kDurationTable{{
        // HARVEST-BEGIN duration-table
        // harvested 2026-10-18 from evidence/duration-table.txt (plan S8)
        {.durationMs = 5.0f, .slowAttackArgMs = 7.30807829f},
        {.durationMs = 7.5f, .slowAttackArgMs = 11.7715015f},
        {.durationMs = 10.0f, .slowAttackArgMs = 15.3191643f},
        {.durationMs = 15.0f, .slowAttackArgMs = 24.3181133f},
        {.durationMs = 20.0f, .slowAttackArgMs = 33.2880974f},
        {.durationMs = 30.0f, .slowAttackArgMs = 49.6755791f},
        {.durationMs = 40.0f, .slowAttackArgMs = 67.6638641f},
        {.durationMs = 60.0f, .slowAttackArgMs = 103.588699f},
        {.durationMs = 100.0f, .slowAttackArgMs = 175.304916f},
        {.durationMs = 150.0f, .slowAttackArgMs = 264.961334f},
        {.durationMs = 250.0f, .slowAttackArgMs = 444.156616f},
        // HARVEST-END duration-table
    }};

    // =========================================================================
    // Lifecycle (FR-001, FR-047)
    // =========================================================================

    /// Allocation-free. maxBlockSize is unused (per-sample core). Applies the stored setter values
    /// and calls reset().
    void prepare(double sampleRate, std::size_t maxBlockSize) noexcept {
        (void)maxBlockSize;
        det_.prepare(sampleRate, kHoldMs);

        fast_.prepare(sampleRate, 0);
        fast_.setMode(DetectionMode::Amplitude);
        fast_.setAttackTime(kFastAttackMs);
        fast_.setReleaseTime(kFastReleaseMs);

        slow_.prepare(sampleRate, 0);
        slow_.setMode(DetectionMode::Amplitude);
        slow_.setAttackTime(slowAttackArgFor(durationMs_));
        slow_.setReleaseTime(kSlowReleaseMs / static_cast<float>(kSlowTickSamples));

        attackS_.configure(kGainSmoothMs, static_cast<float>(sampleRate));
        sustainS_.configure(kGainSmoothMs, static_cast<float>(sampleRate));
        attackS_.setTarget(attackDb_);
        sustainS_.setTarget(sustainDb_);

        prepared_ = true;
        reset();
    }

    /// Clears the detector, both followers and the tick phase; snaps the gain smoothers to their
    /// targets (FR-047). No-op before prepare().
    void reset() noexcept {
        if (!prepared_)
            return;
        det_.reset();
        fast_.reset();
        slow_.reset();
        slowPrev_ = 0.0f;
        slowCur_ = 0.0f;
        tickPhase_ = 0;
        attackS_.snapToTarget();
        sustainS_.snapToTarget();
        lastGainDb_ = 0.0f;
        lastC_ = 0.0f;
        lastFast_ = 0.0f;
        lastSlow_ = 0.0f;
        lastA_ = attackS_.getCurrentValue();
        lastS_ = sustainS_.getCurrentValue();
    }

    // =========================================================================
    // Setters (FR-001, FR-009, SC-025): clamp; non-finite keeps the previous value; valid before
    // prepare (stored, applied by prepare).
    // =========================================================================

    void setAttackGainDb(float dB) noexcept {
        if (!detail::isFinite(dB))
            return;
        attackDb_ = std::clamp(dB, kMinGainDb, kMaxGainDb);
        attackS_.setTarget(attackDb_);
    }

    void setSustainGainDb(float dB) noexcept {
        if (!detail::isFinite(dB))
            return;
        sustainDb_ = std::clamp(dB, kMinGainDb, kMaxGainDb);
        sustainS_.setTarget(sustainDb_);
    }

    /// Interpolates kDurationTable in log-log and sets the slow follower's attack argument (one
    /// constexprExp in EnvelopeFollower::setAttackTime; MassDynamics calls this at most once per
    /// control interval).
    void setTransientDurationMs(float ms) noexcept {
        if (!detail::isFinite(ms))
            return;
        const float clamped = std::clamp(ms, kMinDurationMs, kMaxDurationMs);
        if (clamped == durationMs_)
            return;   // unchanged: the follower already holds this argument (prepare re-applies it)
        durationMs_ = clamped;
        slow_.setAttackTime(slowAttackArgFor(durationMs_));
    }

    // =========================================================================
    // Processing (FR-001 - FR-008, S4.3)
    // =========================================================================

    /// In place. `right == left` processes one channel.
    void processBlock(float* left, float* right, std::size_t numSamples) noexcept {
        if (!prepared_ || left == nullptr || right == nullptr || numSamples == 0)
            return;
        const bool mono = (right == left);
        constexpr float kInvTicks = 1.0f / static_cast<float>(kSlowTickSamples);

        for (std::size_t n = 0; n < numSamples; ++n) {
            // 1. Sanitise (FR-007): every later step sees only xL / xR.
            const float xL = detail::isFinite(left[n]) ? left[n] : 0.0f;
            const float xR = mono ? xL : (detail::isFinite(right[n]) ? right[n] : 0.0f);

            // 2. Detection (FR-004, FR-005). Probe branch: SC-009's negative control.
            float m = 0.0f;
            if (probeHoldBypass_)
                m = mono ? std::abs(xL) : std::max(std::abs(xL), std::abs(xR));
            else
                m = mono ? det_.process(xL) : det_.process(xL, xR);

            // 3. Fast follower.
            const float f = fast_.processSample(m);

            // 4. Slow follower, ticked once per kSlowTickSamples and interpolated (C-5).
            if (tickPhase_ == 0) {
                slowPrev_ = slowCur_;
                slowCur_ = slow_.processSample(m);
            }
            const float s = slowPrev_ + (slowCur_ - slowPrev_) * (static_cast<float>(tickPhase_) * kInvTicks);
            tickPhase_ = (tickPhase_ + 1) & (kSlowTickSamples - 1);

            // 5. c (FR-008): one log of a ratio per sample.
            float c = 0.0f;
            if (s <= kSilenceFloorLin)
                c = (f > kSilenceFloorLin) ? 1.0f : 0.0f;
            else
                c = std::clamp(detail::gainToDbFast(f / s) / kDifferentialRangeDb, 0.0f, 1.0f);

            // 6. Gain. Probe branch: SC-008(d)'s positive control (raw targets, smoothers advanced).
            const float aSmoothed = attackS_.process();
            const float sSmoothed = sustainS_.process();
            const float a = probeGainSmootherBypass_ ? attackDb_ : aSmoothed;
            const float su = probeGainSmootherBypass_ ? sustainDb_ : sSmoothed;
            const float gainDb = a * c + su * (1.0f - c);
            const float g = (gainDb == 0.0f) ? 1.0f : detail::dbToGainFast(gainDb);  // 0 dB -> exactly 1 (SC-013)

            // 7. Output: one gain on both channels (FR-005, SC-012).
            left[n] = xL * g;
            if (!mono)
                right[n] = xR * g;

            lastGainDb_ = gainDb;
            lastC_ = c;
            lastFast_ = f;
            lastSlow_ = s;
            lastA_ = a;
            lastS_ = su;
        }
    }

    [[nodiscard]] static constexpr std::size_t latencySamples() noexcept { return 0; }

    // =========================================================================
    // FR-006 diagnostic taps (read-only, allocation-free)
    // =========================================================================

    [[nodiscard]] float currentGainDb() const noexcept { return lastGainDb_; }       // gain of the last sample
    [[nodiscard]] float transientFraction() const noexcept { return lastC_; }        // c of the last sample
    [[nodiscard]] float fastEnvelope() const noexcept { return lastFast_; }          // f of the last sample
    [[nodiscard]] float slowEnvelope() const noexcept { return lastSlow_; }          // interpolated s of the last sample
    [[nodiscard]] float smoothedAttackDb() const noexcept { return lastA_; }         // A_s of the last sample (SC-008(f))
    [[nodiscard]] float smoothedSustainDb() const noexcept { return lastS_; }        // S_s of the last sample (SC-008(f))
    [[nodiscard]] float attackGainDb() const noexcept { return attackDb_; }          // sanitised targets
    [[nodiscard]] float sustainGainDb() const noexcept { return sustainDb_; }
    [[nodiscard]] float transientDurationMs() const noexcept { return durationMs_; }

    /// True when every state value is finite (SC-019).
    [[nodiscard]] bool stateFinite() const noexcept {
        return det_.stateFinite() && detail::isFinite(fast_.getCurrentValue()) &&
               detail::isFinite(slow_.getCurrentValue()) && detail::isFinite(slowPrev_) &&
               detail::isFinite(slowCur_) && detail::isFinite(attackS_.getCurrentValue()) &&
               detail::isFinite(sustainS_.getCurrentValue()) && detail::isFinite(lastGainDb_) &&
               detail::isFinite(lastC_) && detail::isFinite(lastFast_) && detail::isFinite(lastSlow_);
    }

private:
    friend struct detail::TransientShaperProbe;

    /// log(arg) = lerp(log arg_k, log arg_{k+1}, (log d - log d_k) / (log d_{k+1} - log d_k)).
    [[nodiscard]] static float slowAttackArgFor(float durationMs) noexcept {
        const float d = std::clamp(durationMs, kMinDurationMs, kMaxDurationMs);
        std::size_t k = 0;
        while (k + 2 < kDurationTable.size() && d > kDurationTable[k + 1].durationMs)
            ++k;
        const DurationPoint& p0 = kDurationTable[k];
        const DurationPoint& p1 = kDurationTable[k + 1];
        const float ld0 = std::log(p0.durationMs);
        const float t = std::clamp((std::log(d) - ld0) / (std::log(p1.durationMs) - ld0), 0.0f, 1.0f);
        const float la0 = std::log(p0.slowAttackArgMs);
        return std::exp(la0 + t * (std::log(p1.slowAttackArgMs) - la0));
    }

    PeriodPeakDetector det_;
    EnvelopeFollower fast_;
    EnvelopeFollower slow_;
    float slowPrev_ = 0.0f;
    float slowCur_ = 0.0f;
    std::size_t tickPhase_ = 0;
    OnePoleSmoother attackS_{kDefaultAttackDb};
    OnePoleSmoother sustainS_{kDefaultSustainDb};
    // sanitised targets
    float attackDb_ = kDefaultAttackDb;
    float sustainDb_ = kDefaultSustainDb;
    float durationMs_ = kDefaultDurationMs;
    // taps
    float lastGainDb_ = 0.0f;
    float lastC_ = 0.0f;
    float lastFast_ = 0.0f;
    float lastSlow_ = 0.0f;
    float lastA_ = 0.0f;
    float lastS_ = 0.0f;
    bool prepared_ = false;
    // probe-only switches (detail::TransientShaperProbe); default leaves the production path untouched
    bool probeHoldBypass_ = false;
    bool probeGainSmootherBypass_ = false;
};

// ---- kDurationTable invariants (tasks.md T006) ----
static_assert(
    [] {
        const auto& t = TransientShaper::kDurationTable;
        for (std::size_t i = 1; i < t.size(); ++i) {
            if (!(t[i].durationMs > t[i - 1].durationMs) || !(t[i].slowAttackArgMs > t[i - 1].slowAttackArgMs))
                return false;
        }
        return true;
    }(),
    "kDurationTable: both columns strictly increasing");
static_assert(TransientShaper::kDurationTable.front().durationMs == TransientShaper::kMinDurationMs,
              "kDurationTable starts at kMinDurationMs");
static_assert(TransientShaper::kDurationTable.back().durationMs == TransientShaper::kMaxDurationMs,
              "kDurationTable ends at kMaxDurationMs");
static_assert(
    [] {
        for (const auto& p : TransientShaper::kDurationTable) {
            if (p.slowAttackArgMs < EnvelopeFollower::kMinAttackMs || p.slowAttackArgMs > EnvelopeFollower::kMaxAttackMs)
                return false;
        }
        return true;
    }(),
    "kDurationTable: every slow argument inside EnvelopeFollower's attack range");

}  // namespace Krate::DSP
