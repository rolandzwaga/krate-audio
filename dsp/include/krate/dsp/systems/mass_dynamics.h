// ==============================================================================
// Layer 3: System
// Profundum Phase 6 (specs/profundum-phase6-mass-dynamics)
// ==============================================================================
// BassCompressor (FR-010-FR-019; SC-006, SC-007, SC-022, SC-024, compressor arms of SC-012/013/016/018/019/021) and MassDynamics (FR-013, FR-015, FR-041; SC-012-SC-018). Filled by T007 (BassCompressor) and T010 (MassDynamics).
//   Spec:  specs/profundum-phase6-mass-dynamics/spec.md
//   Plan:  specs/profundum-phase6-mass-dynamics/plan.md (S1 C-6, C-8, C-10, C-11; S5; S6)
//   Tests: dsp/tests/unit/systems/mass_compressor_test.cpp (tag [mass-dynamics]) and the chain TUs
//
// Includes Layer 0 - Layer 2 only (no L3 include). Fixed size, zero heap after prepare, every
// method noexcept. FTZ / DAZ is a precondition (PeriodPeakDetector's oversampler sections are not
// flushed).
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/processors/dynamics_processor.h>
#include <krate/dsp/processors/period_peak_detector.h>
#include <krate/dsp/processors/transient_shaper.h>   // peer-lower L2 (MassDynamics, T010)

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate::DSP {

namespace detail {
struct MassDynamicsProbe;
}  // namespace detail

// =============================================================================
// BassCompressor (plan S5)
// =============================================================================
/// The bass compressor composition (FR-010 - FR-019).
///
/// Detection (FR-011 form (b), ratified D-6; plan C-6): each channel is high-passed by its own
/// RBJ `Biquad` at the keytracked corner (2·f0, FR-018) on SIGNED samples, then the
/// `PeriodPeakDetector` takes the true-peak of each HP output, d = max(tp_L, tp_R), held for one
/// period of the key f0 (hold = 2000 / corner ms). The held value drives ONE shared
/// `DynamicsProcessor` in Peak detection with its internal sidechain off (FR-013); its own audio
/// output is discarded and its smoothed `getCurrentGainReduction()` plus the smoothed makeup is the
/// one gain applied to both channels.
///
/// Release (D-5, FR-012): release = 50 + 450·f ms, f = the fraction of the last 300 ms spent beyond
/// 1 dB of smoothed GR, from integer per-interval counts, updated once per 32-sample interval
/// (FR-049) and driven through `setReleaseTime` only when it changed.
///
/// Cost note (plan S5.5): `DynamicsProcessor::processSample` computes and discards its own output
/// (two `dbToGain`, dynamics_processor.h:206-211). That waste cannot be removed without a header
/// change (FR-044); it is part of the SC-017 measurement (T001 measured it, R-1). The per-sample
/// conversions this header owns use `detail::dbToGainFast` (transient_shaper.h; FR-045, 2026-10-18).
class BassCompressor {
public:
    static constexpr std::size_t kControlInterval = 32;                  // FR-049 (== ProfundumCore::kControlInterval)
    static constexpr float kMinCornerHz = 20.0f;                         // FR-018 (== DynamicsProcessor::kMinSidechainHz)
    static constexpr float kMaxCornerHz = 500.0f;                        // FR-018 (== DynamicsProcessor::kMaxSidechainHz)
    static constexpr float kFallbackCornerHz = 80.0f;                    // C-10: "no voice" corner
    static constexpr float kCornerSmoothMs = 20.0f;                      // C-10: OnePoleSmoother on log2(corner), at fs/32
    static constexpr float kAttackMs = 10.0f;                            // C-10 (== DynamicsProcessor::kDefaultAttackMs)
    static constexpr float kMinReleaseMs = 50.0f;                        // D-5
    static constexpr float kMaxReleaseMs = 500.0f;                       // D-5
    static constexpr float kRuleWindowMs = 300.0f;                       // D-5
    static constexpr float kRuleThresholdDb = 1.0f;                      // D-5 (1 dB ratified)
    static constexpr std::size_t kMaxRuleIntervals = 1800;               // 300 ms / 32 samples at 192 kHz
    static constexpr float kMakeupSmoothMs = 5.0f;                       // C-10 (matches the GR smoother)
    static constexpr double kMinSampleRate = 22050.0;                    // as AnchorOscillator
    static constexpr double kMaxSampleRate = 192000.0;

    /// One row of the compression-amount table (FR-016). slope = 1 − 1/ratio.
    struct AmountRow {
        float amount;
        float thresholdDb;
        float slope;
        float kneeDb;
        float makeupDb;
    };

    // SEED (plan S8.3); the amount-table harvest (T019) replaces thresholds and makeup.
    // clang-format off
    static constexpr std::array<AmountRow, 5> kAmountTable{{
        // HARVEST-BEGIN amount-table
        // harvested 2026-10-18 from evidence/amount-table.txt (plan S8)
        {.amount = 0.0f, .thresholdDb = 0.0f, .slope = 0.0f, .kneeDb = 0.0f, .makeupDb = 0.0f},
        {.amount = 0.25f, .thresholdDb = -2.75390625f, .slope = 0.25f, .kneeDb = 6.0f, .makeupDb = 0.248968944f},
        {.amount = 0.5f, .thresholdDb = -5.5078125f, .slope = 0.400000006f, .kneeDb = 6.0f, .makeupDb = 1.28469789f},
        {.amount = 0.75f, .thresholdDb = -8.26171875f, .slope = 0.550000012f, .kneeDb = 6.0f, .makeupDb = 3.28110814f},
        {.amount = 1.0f, .thresholdDb = -11.015625f, .slope = 0.670000017f, .kneeDb = 6.0f, .makeupDb = 5.84210396f},
        // HARVEST-END amount-table
    }};
    // clang-format on

    // -------------------------------------------------------------------------
    // Lifecycle (plan S5.2, FR-047)
    // -------------------------------------------------------------------------

    /// Allocation-free: the DynamicsProcessor's look-ahead stays 0 (dynamics_processor.h:429-437,
    /// FR-014), so its prepare never allocates. The sample rate is clamped to [22050, 192000].
    void prepare(double sampleRate, std::size_t maxBlockSize) noexcept {
        const double fs = std::clamp(detail::isFinite(sampleRate) ? sampleRate : 48000.0, kMinSampleRate,
                                     kMaxSampleRate);
        sampleRate_ = fs;

        // 1. The shared gain computer: Peak detection, internal sidechain off, zero look-ahead
        //    (never non-zero, FR-014), constant attack, release from the rule, makeup applied by
        //    this composition (FR-016), auto makeup off.
        dyn_.prepare(fs, maxBlockSize);
        dyn_.setDetectionMode(DynamicsDetectionMode::Peak);
        dyn_.setSidechainEnabled(false);
        dyn_.setLookahead(0.0f);
        dyn_.setAttackTime(kAttackMs);
        dyn_.setReleaseTime(kMinReleaseMs);
        dyn_.setMakeupGain(0.0f);
        dyn_.setAutoMakeup(false);

        // 2. The detector high-passes at the fallback corner (reset() reconfigures at the latched one).
        for (auto& hp : hp_)
            hp.configure(FilterType::Highpass, kFallbackCornerHz, kButterworthQ, 0.0f, static_cast<float>(fs));

        // 3. The held true-peak detector: one period of a 40 Hz key (2000 / 80 Hz = 25 ms).
        det_.prepare(fs, 2000.0f / kFallbackCornerHz);

        // 4. Smoothers: the corner at the interval rate, the makeup per sample. configure() takes a
        //    float rate (smoother.h:160).
        cornerLog2S_.configure(kCornerSmoothMs, static_cast<float>(fs / static_cast<double>(kControlInterval)));
        makeupS_.configure(kMakeupSmoothMs, static_cast<float>(fs));

        // 5. Release-rule window length in intervals: 413 / 450 / 900 / 1800 at 44.1 / 48 / 96 / 192 kHz.
        const auto intervals = static_cast<std::size_t>(std::llround(
            static_cast<double>(kRuleWindowMs) * 0.001 * fs / static_cast<double>(kControlInterval)));
        ruleIntervals_ = std::clamp<std::size_t>(intervals, 1, kMaxRuleIntervals);

        // 6. Apply the pending amount and key, then reset().
        prepared_ = true;
        latchControls();
        reset();
    }

    /// FR-047; allocation-free (SC-016). Clears the detector, filter, rule and gain-computer state
    /// and snaps the smoothers to the latched targets (plan S5.2 steps 1-7).
    void reset() noexcept {
        // 1. Gain computer, and the release for f = 0 (the ring is cleared below).
        dyn_.reset();
        releaseMs_ = (probeFixedReleaseMs_ > 0.0f) ? probeFixedReleaseMs_ : kMinReleaseMs;
        dyn_.setReleaseTime(releaseMs_);
        fraction_ = 0.0f;

        // 6 (first, so steps 2-3 configure at the latched corner): smoothers snap to the latched targets.
        cornerLog2S_.snapTo(cornerTargetLog2_);
        cornerHz_ = std::exp2(cornerTargetLog2_);
        makeupS_.snapTo(makeupTargetDb_);

        // 2-3. Filters and detector at the latched corner. setHoldMs before det_.reset() so the
        //      segment length takes effect at once (period_peak_detector.h reset()).
        for (auto& hp : hp_)
            hp.reset();
        configureDetector();
        det_.reset();

        // 4. Release rule. rulePos_ = 0 matters across a re-prepare: ruleIntervals_ changes with the
        //    rate, and a rulePos_ >= the new length would leave stale entries inside the window.
        ruleRing_.fill(0);
        ruleSum_ = 0;
        ruleCount_ = 0;
        rulePos_ = 0;

        // 5. Grid.
        intervalPhase_ = 0;

        // 7. Taps.
        gainDb_ = 0.0f;
        detectorLevel_ = 0.0f;
    }

    // -------------------------------------------------------------------------
    // Controls (latched at the next 32-sample boundary, FR-049 / C-11)
    // -------------------------------------------------------------------------

    /// FR-016: non-finite keeps the previous value; clamped to [0, 1]. Before prepare the value is
    /// stored and prepare applies it.
    void setAmount(float amount01) noexcept {
        if (!detail::isFinite(amount01))
            return;
        pendingAmount_ = std::clamp(amount01, 0.0f, 1.0f);
    }

    /// FR-018: the f0 of the lowest sounding voice; f0 <= 0 or non-finite = "no voice" (corner =
    /// kFallbackCornerHz). The corner target is log2(clamp(2·f0, 20, 500)).
    void setDetectorKeyHz(float f0Hz) noexcept {
        pendingKeyHz_ = (detail::isFinite(f0Hz) && f0Hz > 0.0f) ? f0Hz : 0.0f;
    }

    // -------------------------------------------------------------------------
    // Processing (plan S5.5)
    // -------------------------------------------------------------------------

    /// In place. Mono when right == left (single-channel detector, only L written). A call before
    /// prepare, with a null channel or with numSamples == 0 is a no-op: nothing is written and no
    /// state advances (FR-047).
    void processBlock(float* left, float* right, std::size_t numSamples) noexcept {
        if (!prepared_ || left == nullptr || right == nullptr || numSamples == 0)
            return;
        const bool mono = (right == left);
        for (std::size_t i = 0; i < numSamples; ++i) {
            // 1. Sanitise (FR-017): one substituted 0.0f for the audio path and the detection path,
            //    so the DynamicsProcessor's own NaN / Inf branch is never reached.
            const float xL = detail::isFinite(left[i]) ? left[i] : 0.0f;
            const float xR = mono ? xL : (detail::isFinite(right[i]) ? right[i] : 0.0f);

            // 2. Control step on the absolute 32-sample grid.
            if (intervalPhase_ == 0)
                controlStep();

            // 3. Detection, FR-011 (b): high-pass on signed samples, then the held true-peak of
            //    max(|HP L|, |HP R|). The probe's rectified feed is the struck form (unfiltered
            //    max(|L|, |R|) into the internal sidechain), kept only for SC-006(e).
            float d = 0.0f;
            if (probeRectifiedFeed_) {
                d = mono ? std::abs(xL) : std::max(std::abs(xL), std::abs(xR));
            } else {
                const float hL = probeHpBypass_ ? xL : hp_[0].process(xL);
                if (mono) {
                    d = det_.process(hL);
                } else {
                    const float hR = probeHpBypass_ ? xR : hp_[1].process(xR);
                    d = det_.process(hL, hR);
                }
            }
            detectorLevel_ = d;

            // 4. Gain computer (its audio output is unused, FR-013). gr <= 0, already 5 ms smoothed.
            (void)dyn_.processSample(d);
            const float gr = dyn_.getCurrentGainReduction();
            if (gr < -kRuleThresholdDb)
                ++ruleCount_;

            // 5. One gain for both channels. gainDb_ == 0 is the hard bypass (FR-016 / SC-013): gr is
            //    exactly +0.0f there (flushDenormal(-0.0f) returns the literal 0.0f, db_utils.h:263).
            const float mk = makeupS_.process();
            gainDb_ = gr + mk;
            if (gainDb_ == 0.0f) {
                left[i] = xL;
                if (!mono)
                    right[i] = xR;
            } else {
                const float g = detail::dbToGainFast(gainDb_);
                left[i] = xL * g;
                if (!mono)
                    right[i] = xR * g;
            }

            // 6. Grid.
            intervalPhase_ = (intervalPhase_ + 1) % kControlInterval;
        }
    }

    /// FR-014: no look-ahead, no latency.
    [[nodiscard]] static constexpr std::size_t latencySamples() noexcept { return 0; }

    // -------------------------------------------------------------------------
    // Taps
    // -------------------------------------------------------------------------

    /// Applied gain of the last sample (GR + makeup), dB.
    [[nodiscard]] float currentGainDb() const noexcept { return gainDb_; }
    /// DynamicsProcessor::getCurrentGainReduction() (0 = none, negative = reduction).
    [[nodiscard]] float currentGainReductionDb() const noexcept { return dyn_.getCurrentGainReduction(); }
    /// The D-5 rule output in effect (or the probe's fixed value).
    [[nodiscard]] float currentReleaseMs() const noexcept { return releaseMs_; }
    /// f in [0, 1]: the fraction of the last 300 ms spent beyond 1 dB of smoothed GR.
    [[nodiscard]] float programFraction() const noexcept { return fraction_; }
    /// The smoothed corner in effect, Hz.
    [[nodiscard]] float detectorCornerHz() const noexcept { return cornerHz_; }
    /// The held detection value fed to the DynamicsProcessor (linear).
    [[nodiscard]] float detectorLevel() const noexcept { return detectorLevel_; }
    /// The latched amount in effect.
    [[nodiscard]] float amount() const noexcept { return amount_; }

    /// True when every state value is finite.
    [[nodiscard]] bool stateFinite() const noexcept {
        for (const auto& hp : hp_) {
            if (!detail::isFinite(hp.getZ1()) || !detail::isFinite(hp.getZ2()))
                return false;
        }
        return det_.stateFinite() && detail::isFinite(cornerLog2S_.getCurrentValue()) &&
               detail::isFinite(makeupS_.getCurrentValue()) && detail::isFinite(dyn_.getCurrentGainReduction()) &&
               detail::isFinite(gainDb_) && detail::isFinite(detectorLevel_) && detail::isFinite(releaseMs_) &&
               detail::isFinite(cornerHz_);
    }

private:
    friend struct detail::MassDynamicsProbe;

    [[nodiscard]] static std::uint32_t bitsOf(float x) noexcept { return std::bit_cast<std::uint32_t>(x); }

    /// FR-016 / plan S5.3: the bracketing rows, linear in t. Amount 0 returns row 0 exactly (slope
    /// 0 -> ratio exactly 1.0f, makeup 0.0f); a row's own amount returns that row exactly.
    [[nodiscard]] static AmountRow interpolateRow(const std::array<AmountRow, 5>& table, float a) noexcept {
        if (a <= table.front().amount)
            return table.front();
        for (std::size_t i = 0; i + 1 < table.size(); ++i) {
            const AmountRow& lo = table[i];
            const AmountRow& hi = table[i + 1];
            if (a > hi.amount)
                continue;
            if (a == hi.amount)
                return hi;
            const float t = (a - lo.amount) / (hi.amount - lo.amount);
            return AmountRow{.amount = a,
                             .thresholdDb = lo.thresholdDb + t * (hi.thresholdDb - lo.thresholdDb),
                             .slope = lo.slope + t * (hi.slope - lo.slope),
                             .kneeDb = lo.kneeDb + t * (hi.kneeDb - lo.kneeDb),
                             .makeupDb = lo.makeupDb + t * (hi.makeupDb - lo.makeupDb)};
        }
        return table.back();
    }

    /// Corner target for a key (FR-018): log2(clamp(2·f0, 20, 500)), or log2(80) for "no voice".
    [[nodiscard]] static float cornerTargetLog2For(float keyHz) noexcept {
        if (!(detail::isFinite(keyHz) && keyHz > 0.0f))
            return std::log2(kFallbackCornerHz);
        return std::log2(std::clamp(2.0f * keyHz, kMinCornerHz, kMaxCornerHz));
    }

    /// Latch the pending amount and key (plan S5.4 step 1). Each DynamicsProcessor setter is called
    /// only when its value changed bitwise; the probe table, when set, replaces kAmountTable.
    void latchControls() noexcept {
        amount_ = pendingAmount_;
        const std::array<AmountRow, 5>& table = (probeTableOverride_ != nullptr) ? *probeTableOverride_ : kAmountTable;
        const AmountRow row = interpolateRow(table, amount_);
        const float ratio = 1.0f / (1.0f - row.slope);
        if (bitsOf(row.thresholdDb) != bitsOf(dyn_.getThreshold()))
            dyn_.setThreshold(row.thresholdDb);
        if (bitsOf(ratio) != bitsOf(dyn_.getRatio()))
            dyn_.setRatio(ratio);
        if (bitsOf(row.kneeDb) != bitsOf(dyn_.getKneeWidth()))
            dyn_.setKneeWidth(row.kneeDb);
        if (bitsOf(row.makeupDb) != bitsOf(makeupTargetDb_)) {
            makeupTargetDb_ = row.makeupDb;
            makeupS_.setTarget(makeupTargetDb_);
        }
        cornerTargetLog2_ = cornerTargetLog2For(pendingKeyHz_);
    }

    /// Both HP biquads and the detector hold at cornerHz_ (hold = one period of the key f0 when
    /// unclamped, C-6). In the probe's rectified-feed mode the internal sidechain follows the corner.
    void configureDetector() noexcept {
        const auto fs = static_cast<float>(sampleRate_);
        for (auto& hp : hp_)
            hp.configure(FilterType::Highpass, cornerHz_, kButterworthQ, 0.0f, fs);
        det_.setHoldMs(2000.0f / cornerHz_);
        if (probeRectifiedFeed_)
            dyn_.setSidechainCutoff(cornerHz_);
    }

    /// Plan S5.4, at intervalPhase_ == 0 (FR-012, FR-018, FR-049).
    void controlStep() noexcept {
        // 1. Latch.
        latchControls();

        // 2. Corner: smoothed in log2 at the interval rate; reconfigure only on a bitwise change.
        cornerLog2S_.setTarget(cornerTargetLog2_);
        const float corner = std::exp2(cornerLog2S_.process());
        if (bitsOf(corner) != bitsOf(cornerHz_)) {
            cornerHz_ = corner;
            configureDetector();
        }
        // Probe-only (SC-006(e)): the struck rectified feed uses the internal sidechain at the corner.
        if (dyn_.isSidechainEnabled() != probeRectifiedFeed_) {
            dyn_.setSidechainEnabled(probeRectifiedFeed_);
            if (probeRectifiedFeed_)
                dyn_.setSidechainCutoff(cornerHz_);
        }

        // 3. Release rule (D-5): integer counts, so f is exact and deterministic.
        ruleSum_ = ruleSum_ + ruleCount_ - ruleRing_[rulePos_];
        ruleRing_[rulePos_] = static_cast<std::uint8_t>(ruleCount_);
        rulePos_ = (rulePos_ + 1) % ruleIntervals_;
        ruleCount_ = 0;
        fraction_ = static_cast<float>(ruleSum_) / static_cast<float>(ruleIntervals_ * kControlInterval);
        const float release = (probeFixedReleaseMs_ > 0.0f)
                                  ? probeFixedReleaseMs_
                                  : kMinReleaseMs + (kMaxReleaseMs - kMinReleaseMs) * fraction_;
        if (bitsOf(release) != bitsOf(releaseMs_)) {
            releaseMs_ = release;
            dyn_.setReleaseTime(releaseMs_);   // one exp per change, inside SC-017
        }
    }

    DynamicsProcessor dyn_;
    std::array<Biquad, 2> hp_{};
    PeriodPeakDetector det_;
    OnePoleSmoother cornerLog2S_;
    OnePoleSmoother makeupS_;
    std::array<std::uint8_t, kMaxRuleIntervals> ruleRing_{};
    std::uint32_t ruleSum_ = 0;
    std::uint32_t ruleCount_ = 0;
    std::size_t rulePos_ = 0;
    std::size_t ruleIntervals_ = 450;
    std::size_t intervalPhase_ = 0;
    float pendingAmount_ = 0.0f;
    float amount_ = 0.0f;
    float pendingKeyHz_ = 0.0f;
    float cornerHz_ = kFallbackCornerHz;
    float cornerTargetLog2_ = 6.32192809f;   // log2(kFallbackCornerHz)
    float makeupTargetDb_ = 0.0f;
    float releaseMs_ = kMinReleaseMs;
    float fraction_ = 0.0f;
    float gainDb_ = 0.0f;
    float detectorLevel_ = 0.0f;
    double sampleRate_ = 48000.0;
    bool prepared_ = false;
    // Probe-only switches (positive / negative controls), written only by detail::MassDynamicsProbe.
    // At their defaults the production path is untouched (precedent bass_character_stage.h:71, 513).
    float probeFixedReleaseMs_ = 0.0f;                               // > 0: fixed release (SC-007 control)
    bool probeHpBypass_ = false;                                     // SC-006(c)
    bool probeRectifiedFeed_ = false;                                // SC-006(e)
    const std::array<AmountRow, 5>* probeTableOverride_ = nullptr;   // amount harvest
};

// ---- compile-time checks on the table and the constants (plan S5.1, S5.3) -------------------
static_assert(
    [] {
        constexpr std::array<float, 5> kAmounts{0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
        const auto& t = BassCompressor::kAmountTable;
        for (std::size_t i = 0; i < t.size(); ++i) {
            if (t[i].amount != kAmounts[i] || !(t[i].slope < 1.0f))
                return false;
        }
        for (std::size_t i = 0; i + 1 < t.size(); ++i) {
            if (t[i + 1].thresholdDb > t[i].thresholdDb || t[i + 1].slope < t[i].slope ||
                t[i + 1].kneeDb < t[i].kneeDb || t[i + 1].makeupDb < t[i].makeupDb)
                return false;
        }
        return t[0].slope == 0.0f && t[0].makeupDb == 0.0f;
    }(),
    "kAmountTable: amounts {0, .25, .5, .75, 1}; threshold non-increasing; slope, knee, makeup "
    "non-decreasing; row 0 slope 0 and makeup 0; every slope < 1");
static_assert(BassCompressor::kMaxRuleIntervals >=
                  static_cast<std::size_t>(0.3 * 192000.0 / static_cast<double>(BassCompressor::kControlInterval) + 0.5),
              "the release-rule ring must hold 300 ms of intervals at 192 kHz");
static_assert(BassCompressor::kMinCornerHz == DynamicsProcessor::kMinSidechainHz);
static_assert(BassCompressor::kMaxCornerHz == DynamicsProcessor::kMaxSidechainHz);
static_assert(BassCompressor::kAttackMs == DynamicsProcessor::kDefaultAttackMs);
static_assert(BassCompressor::kControlInterval <= 255, "a per-interval rule count must fit the uint8_t ring");

// =============================================================================
// MassDynamics (plan S6; C-8, C-11)
// =============================================================================
/// The Mass dynamics composition (FR-015): TransientShaper -> BassCompressor, in that fixed order,
/// on one absolute 32-sample control grid (FR-049, plan C-11).
///
/// `processBlock` splits every call into runs that end on the grid. At each run start with
/// intervalPhase_ == 0 it latches the pending transient duration into the shaper, then runs the
/// shaper and the compressor over the run. Both are per-sample and the compressor reads only the
/// shaper's output, so run-wise order equals sample-wise order. The compressor keeps its own
/// intervalPhase_, reset by the same prepare / reset and advanced by the same samples, so the two
/// grids are aligned by construction (C-11): a setter issued anywhere inside an interval latches at
/// the next boundary of the absolute sample counter, whatever the host partition (SC-018).
///
/// One gain per stage for both channels (FR-013, FR-041): the shaper and the compressor each apply
/// a single gain to L and R. Mono when right == left. Latency 0 (FR-014). Fixed size, zero heap,
/// every method noexcept (FR-040); FTZ / DAZ is a precondition (see the file banner).
class MassDynamics {
public:
    static constexpr std::size_t kControlInterval = 32;   // FR-049 (== BassCompressor::kControlInterval)
    /// FR-021 output trim (user ruling 2026-10-17, spec Clarifications "Group 8 harvest ruling" G8-5): the
    /// Mass law's seventh target, a dB gain applied after the compressor; smoothed like the makeup (5 ms).
    static constexpr float kTrimSmoothMs = 5.0f;
    static constexpr float kMaxOutputTrimDb = 12.0f;

    // -------------------------------------------------------------------------
    // Lifecycle (plan S6, FR-047)
    // -------------------------------------------------------------------------

    /// Prepares both stages at sampleRate (each applies its stored setter values), then reset().
    void prepare(double sampleRate, std::size_t maxBlockSize) noexcept {
        shaper_.prepare(sampleRate, maxBlockSize);
        comp_.prepare(sampleRate, maxBlockSize);
        trimGainS_.configure(kTrimSmoothMs, static_cast<float>(sampleRate));
        reset();
        prepared_ = true;
    }

    /// FR-047; allocation-free (SC-016). Latches the pending duration, clears both stages and the grid.
    void reset() noexcept {
        shaper_.setTransientDurationMs(pendingDurationMs_);
        shaper_.reset();
        comp_.reset();
        trimGainS_.snapTo(trimGainTarget_);
        intervalPhase_ = 0;
    }

    // -------------------------------------------------------------------------
    // Global Mass targets (FR-020): latched at the next 32-sample boundary
    // -------------------------------------------------------------------------

    /// Non-finite keeps the previous value; clamped to the shaper's [5, 250] ms (D-3).
    void setTransientDurationMs(float ms) noexcept {
        if (!detail::isFinite(ms))
            return;
        pendingDurationMs_ = std::clamp(ms, TransientShaper::kMinDurationMs, TransientShaper::kMaxDurationMs);
    }

    /// -> BassCompressor::setAmount (latched by the compressor on the shared grid).
    void setCompressionAmount(float amount01) noexcept { comp_.setAmount(amount01); }

    /// -> BassCompressor::setDetectorKeyHz (FR-018 handoff; f0 <= 0 or non-finite = "no voice").
    void setDetectorKeyHz(float f0Hz) noexcept { comp_.setDetectorKeyHz(f0Hz); }

    /// FR-021 output trim (G8-5): non-finite keeps the previous value; clamped to +/-kMaxOutputTrimDb. The
    /// linear gain is smoothed over kTrimSmoothMs; 0 dB is exactly 1.0f (SC-023 bit-exact neutrality).
    void setOutputTrimDb(float dB) noexcept {
        if (!detail::isFinite(dB))
            return;
        const float t = std::clamp(dB, -kMaxOutputTrimDb, kMaxOutputTrimDb);
        trimGainTarget_ = (t == 0.0f) ? 1.0f : dbToGain(t);
        trimGainS_.setTarget(trimGainTarget_);
    }

    // -------------------------------------------------------------------------
    // User-facing shaper gains (FR-009): immediate, 10 ms smoothed inside the shaper
    // -------------------------------------------------------------------------

    void setTransientAttackDb(float dB) noexcept { shaper_.setAttackGainDb(dB); }
    void setTransientSustainDb(float dB) noexcept { shaper_.setSustainGainDb(dB); }

    // -------------------------------------------------------------------------
    // Processing (plan S6, FR-015, FR-049)
    // -------------------------------------------------------------------------

    /// In place. Mono when right == left. Before prepare, with a null channel or with 0 samples, a
    /// no-op: the buffers are untouched and no phase advances (FR-047).
    void processBlock(float* left, float* right, std::size_t numSamples) noexcept {
        if (!prepared_ || left == nullptr || right == nullptr || numSamples == 0)
            return;
        std::size_t pos = 0;
        while (pos < numSamples) {
            // Latch on the absolute grid (plan C-11). right + pos == left + pos in mono.
            if (intervalPhase_ == 0)
                shaper_.setTransientDurationMs(pendingDurationMs_);
            const std::size_t len = std::min(kControlInterval - intervalPhase_, numSamples - pos);
            // FR-015: shaper -> compressor, fixed.
            shaper_.processBlock(left + pos, right + pos, len);
            comp_.processBlock(left + pos, right + pos, len);
            // FR-021 output trim (G8-5): one smoothed gain for both channels, after the compressor.
            for (std::size_t i = 0; i < len; ++i) {
                const float g = trimGainS_.process();
                left[pos + i] *= g;
                if (right != left)
                    right[pos + i] *= g;
            }
            intervalPhase_ = (intervalPhase_ + len) % kControlInterval;
            pos += len;
        }
    }

    /// FR-014: neither stage has look-ahead.
    [[nodiscard]] static constexpr std::size_t latencySamples() noexcept { return 0; }

    // -------------------------------------------------------------------------
    // Taps
    // -------------------------------------------------------------------------

    [[nodiscard]] const TransientShaper& shaper() const noexcept { return shaper_; }
    [[nodiscard]] const BassCompressor& compressor() const noexcept { return comp_; }

    /// True when every state value of both stages is finite (SC-019).
    [[nodiscard]] bool stateFinite() const noexcept {
        return shaper_.stateFinite() && comp_.stateFinite() && detail::isFinite(trimGainS_.getCurrentValue());
    }

    /// The smoothed FR-021 trim gain now applied (linear; 1.0f at 0 dB).
    [[nodiscard]] float currentOutputTrimGain() const noexcept { return trimGainS_.getCurrentValue(); }

private:
    // tasks.md deviation 4: the chain's positive / negative controls need the composed stages.
    friend struct detail::MassDynamicsProbe;

    TransientShaper shaper_;
    BassCompressor comp_;
    OnePoleSmoother trimGainS_{1.0f};   // FR-021 output trim (G8-5)
    float trimGainTarget_ = 1.0f;
    float pendingDurationMs_ = TransientShaper::kDefaultDurationMs;
    std::size_t intervalPhase_ = 0;
    bool prepared_ = false;
};

static_assert(MassDynamics::kControlInterval == BassCompressor::kControlInterval,
              "MassDynamics and BassCompressor share one control grid (plan C-11)");
static_assert(MassDynamics::latencySamples() == 0 && BassCompressor::latencySamples() == 0 &&
                  TransientShaper::latencySamples() == 0,
              "FR-014: the Mass dynamics adds no latency");

}  // namespace Krate::DSP
