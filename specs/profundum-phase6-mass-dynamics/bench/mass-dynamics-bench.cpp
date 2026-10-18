// Profundum Phase 6, task T001 (plan R-1, S12.4, S16 step 1): throwaway CPU microbench of the Mass dynamics chain,
// written BEFORE any production header exists. It includes the real reused KrateDSP headers and prototypes the new
// pieces locally (anonymous namespace, Bench prefix) exactly as plan S3-S7 specify:
//   BenchPeriodPeak  - plan S3 (port of harmonic_fold.h:403-427: 4x true-peak estimate + 8-segment ring max)
//   BenchShaper      - plan S4.3 steps 1-7 (sanitise, held detector, fast EF / slow EF ticked /8 + interpolation,
//                      c = clamp(gainToDb(f / s) / 12, 0, 1), two 10 ms OnePoleSmoothers, one dbToGain)
//   BenchCompressor  - plan S5.4 / S5.5 (sanitise, per-channel Biquad HP at 2 f0, held true-peak max, one
//                      DynamicsProcessor in Peak mode with the sidechain off, getCurrentGainReduction() + 5 ms
//                      makeup smoother, control step every 32 samples with the integer release-rule ring and
//                      setReleaseTime only when changed)
//   BenchMassLaw     - plan S7 (six rows, applyModCurve, one SlewLimiter step per 32 samples)
//
// Cells (48 kHz, host block 64, stereo DISTINCT L / R: a 64-partial 1/n saw at E1, -12 dBFS RMS, R offset by 0.37
// period - the SC-017 setup of plan S13.3):
//   1 shaper      shaper alone, A = +12 / S = -6 dB, duration 40 ms
//   2 compressor  compressor alone, amount-1 row of the S5.1 seed, key E1
//   3 total       shaper -> compressor + one BenchMassLaw step and six-row evaluation per 32 samples (the SC-017 cell)
//   4 dc_limiter  DCBlocker x2 + TruePeakLimiter (recorded for Phase 9)
//   5 linked      cell 3 with L == R, one channel processed and copied (recorded only: SC-017 is a distinct pair)
//   6 silence     one second of digital silence through cell 3, after the signal (R-8, recorded)
// Output: "cell <name> ns_per_sample <ns> pct_core <pct>" with pct = ns * 48000 / 1e7 (ns per STEREO sample).
//
// Compile-time variants (R-1 fallbacks 2 and 3):
//   -DP6_DYN_TICK4  the DynamicsProcessor is prepared at fs / 4 and fed the held value once every 4 samples,
//                   its GR held between ticks
//   -DP6_TP2X       both true-peak estimators at 2x instead of 4x
// Flags: --quick (0.2 s per cell, smoke only), --seconds <s>.
// Compile: see run-bench.sh (clang++ -std=c++20 -O2 -ffast-math -march=x86-64-v2 -DNDEBUG -Idsp/include).

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/modulation_curves.h>
#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/polyphase_halfband_oversampler.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/processors/dynamics_processor.h>
#include <krate/dsp/processors/envelope_follower.h>
#include <krate/dsp/processors/true_peak_limiter.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include <xmmintrin.h>

namespace {

using namespace Krate::DSP;

constexpr double kBenchFs = 48000.0;
constexpr std::size_t kBenchBlock = 64;
constexpr std::size_t kBenchInterval = 32;   // FR-049 control interval
constexpr double kBenchE1Hz = 41.20344461410875;
constexpr int kBenchPartials = 64;

#if defined(P6_TP2X)
constexpr std::size_t kBenchTpFactor = 2;
#else
constexpr std::size_t kBenchTpFactor = 4;
#endif

#if defined(P6_DYN_TICK4)
constexpr std::size_t kBenchDynTick = 4;
#else
constexpr std::size_t kBenchDynTick = 1;
#endif

volatile float gSink = 0.0f;

[[nodiscard]] inline float benchSanitize(float v) noexcept { return detail::isFinite(v) ? v : 0.0f; }

// ------------------------------------------------------------------------------------------------------------------
// BenchPeriodPeak (plan S3)
// ------------------------------------------------------------------------------------------------------------------
class BenchPeriodPeak {
public:
    static constexpr std::size_t kSegments = 8;
    static constexpr float kMinHoldMs = 1.0f;
    static constexpr float kMaxHoldMs = 125.0f;

    void prepare(double sampleRate, float holdMs) noexcept {
        sampleRate_ = sampleRate;
        for (auto& e : est_) e.os.setFactor(kBenchTpFactor);
        holdMs_ = std::clamp(holdMs, kMinHoldMs, kMaxHoldMs);
        segLen_ = segLenFor(holdMs_);
        pendingSegLen_ = segLen_;
        reset();
    }

    void reset() noexcept {
        for (auto& e : est_) {
            e.os.reset();
            e.buf.fill(0.0f);
            e.prev1 = 0.0f;
            e.prev2 = 0.0f;
        }
        ring_.fill(0.0f);
        ringMax_ = segMax_ = held_ = 0.0f;
        segCount_ = ringPos_ = 0;
        segLen_ = pendingSegLen_;
    }

    void setHoldMs(float holdMs) noexcept {
        if (!detail::isFinite(holdMs)) return;
        holdMs_ = std::clamp(holdMs, kMinHoldMs, kMaxHoldMs);
        pendingSegLen_ = segLenFor(holdMs_);
    }

    [[nodiscard]] float process(float x) noexcept { return pushHeld(truePeak(est_[0], x)); }
    [[nodiscard]] float process(float xL, float xR) noexcept {
        return pushHeld(std::max(truePeak(est_[0], xL), truePeak(est_[1], xR)));
    }
    [[nodiscard]] float held() const noexcept { return held_; }

private:
    struct Estimator {
        PolyphaseHalfbandOversampler os;
        std::array<float, PolyphaseHalfbandOversampler::kMaxFactor> buf{};
        float prev1 = 0.0f;
        float prev2 = 0.0f;
    };

    [[nodiscard]] std::size_t segLenFor(float holdMs) const noexcept {
        const double holdSamples = std::round(static_cast<double>(holdMs) * 1.0e-3 * sampleRate_);
        return std::max<std::size_t>(
            1, static_cast<std::size_t>(std::ceil(holdSamples / static_cast<double>(kSegments))));
    }

    // harmonic_fold.h:403-417, verbatim in structure.
    [[nodiscard]] static float truePeak(Estimator& e, float x) noexcept {
        float d = std::abs(x);
        e.os.upsample(x, e.buf.data());
        for (std::size_t i = 0; i < kBenchTpFactor; ++i) {
            const float d0 = std::abs(e.buf[i]);
            const float a = e.prev2;
            const float b = e.prev1;
            d = std::max(d, d0);
            if (b >= a && b > d0) {
                const float offset = 0.5f * (a - d0) / (a - 2.0f * b + d0);
                d = std::max(d, b - 0.25f * (a - d0) * offset);
            }
            e.prev2 = b;
            e.prev1 = d0;
        }
        return d;
    }

    // harmonic_fold.h:418-427 plus the pending segLen and the max(segMax, ringMax) output (plan S3).
    [[nodiscard]] float pushHeld(float d) noexcept {
        segMax_ = std::max(segMax_, d);
        if (++segCount_ >= segLen_) {
            ring_[ringPos_] = segMax_;
            ringPos_ = (ringPos_ + 1) & (kSegments - 1);
            segMax_ = 0.0f;
            segCount_ = 0;
            segLen_ = pendingSegLen_;
            ringMax_ = 0.0f;
            for (const float r : ring_) ringMax_ = std::max(ringMax_, r);
        }
        held_ = std::max(segMax_, ringMax_);
        return held_;
    }

    std::array<Estimator, 2> est_{};
    std::array<float, kSegments> ring_{};
    float ringMax_ = 0.0f;
    float segMax_ = 0.0f;
    float held_ = 0.0f;
    std::size_t segCount_ = 0;
    std::size_t ringPos_ = 0;
    std::size_t segLen_ = 1;
    std::size_t pendingSegLen_ = 1;
    double sampleRate_ = kBenchFs;
    float holdMs_ = 50.0f;
};

// ------------------------------------------------------------------------------------------------------------------
// BenchShaper (plan S4)
// ------------------------------------------------------------------------------------------------------------------
class BenchShaper {
public:
    static constexpr float kDifferentialRangeDb = 12.0f;
    static constexpr float kHoldMs = 50.0f;
    static constexpr float kFastAttackMs = 0.5f;
    static constexpr float kFastReleaseMs = 40.0f;
    static constexpr float kSlowReleaseMs = 60.0f;
    static constexpr std::size_t kSlowTickSamples = 8;
    static constexpr float kGainSmoothMs = 10.0f;
    static constexpr float kSilenceFloorLin = 1e-7f;
    struct DurationPoint {
        float durationMs;
        float slowAttackArgMs;
    };
    static constexpr std::array<DurationPoint, 6> kDurationTable{{
        {.durationMs = 5.0f, .slowAttackArgMs = 7.308f},
        {.durationMs = 10.0f, .slowAttackArgMs = 15.351f},
        {.durationMs = 20.0f, .slowAttackArgMs = 33.314f},
        {.durationMs = 40.0f, .slowAttackArgMs = 67.690f},
        {.durationMs = 100.0f, .slowAttackArgMs = 175.324f},
        {.durationMs = 250.0f, .slowAttackArgMs = 444.323f}}};

    void prepare(double sampleRate) noexcept {
        const auto fs = static_cast<float>(sampleRate);
        det_.prepare(sampleRate, kHoldMs);
        fast_.prepare(sampleRate, 0);
        fast_.setMode(DetectionMode::Amplitude);
        fast_.setAttackTime(kFastAttackMs);
        fast_.setReleaseTime(kFastReleaseMs);
        slow_.prepare(sampleRate, 0);
        slow_.setMode(DetectionMode::Amplitude);
        slow_.setReleaseTime(kSlowReleaseMs / static_cast<float>(kSlowTickSamples));
        attackS_.configure(kGainSmoothMs, fs);
        sustainS_.configure(kGainSmoothMs, fs);
        setTransientDurationMs(durationMs_);
        reset();
    }

    void reset() noexcept {
        det_.reset();
        fast_.reset();
        slow_.reset();
        slowPrev_ = slowCur_ = 0.0f;
        tickPhase_ = 0;
        attackS_.snapTo(attackDb_);
        sustainS_.snapTo(sustainDb_);
    }

    void setAttackGainDb(float dB) noexcept {
        if (!detail::isFinite(dB)) return;
        attackDb_ = std::clamp(dB, -12.0f, 12.0f);
        attackS_.setTarget(attackDb_);
    }
    void setSustainGainDb(float dB) noexcept {
        if (!detail::isFinite(dB)) return;
        sustainDb_ = std::clamp(dB, -12.0f, 12.0f);
        sustainS_.setTarget(sustainDb_);
    }

    // Log-log interpolation of the duration table, then one slow_.setAttackTime (one constexprExp).
    void setTransientDurationMs(float ms) noexcept {
        if (!detail::isFinite(ms)) return;
        durationMs_ = std::clamp(ms, 5.0f, 250.0f);
        std::size_t k = 0;
        while (k + 2 < kDurationTable.size() && durationMs_ > kDurationTable[k + 1].durationMs) ++k;
        const auto& p0 = kDurationTable[k];
        const auto& p1 = kDurationTable[k + 1];
        const float t = (std::log(durationMs_) - std::log(p0.durationMs)) /
                        (std::log(p1.durationMs) - std::log(p0.durationMs));
        const float logArg = std::log(p0.slowAttackArgMs) + t * (std::log(p1.slowAttackArgMs) -
                                                                 std::log(p0.slowAttackArgMs));
        slow_.setAttackTime(std::exp(logArg));
    }

    // In place; right == left -> one channel.
    void processBlock(float* left, float* right, std::size_t numSamples) noexcept {
        const bool mono = (right == left);
        for (std::size_t n = 0; n < numSamples; ++n) {
            const float xL = benchSanitize(left[n]);
            const float xR = mono ? xL : benchSanitize(right[n]);
            const float m = mono ? det_.process(xL) : det_.process(xL, xR);
            const float f = fast_.processSample(m);
            if (tickPhase_ == 0) {
                slowPrev_ = slowCur_;
                slowCur_ = slow_.processSample(m);
            }
            const float s = slowPrev_ + (slowCur_ - slowPrev_) *
                                            (static_cast<float>(tickPhase_) / static_cast<float>(kSlowTickSamples));
            tickPhase_ = (tickPhase_ + 1) & (kSlowTickSamples - 1);
            float c = 0.0f;
            if (s <= kSilenceFloorLin)
                c = (f > kSilenceFloorLin) ? 1.0f : 0.0f;
            else
                c = std::clamp(gainToDb(f / s) / kDifferentialRangeDb, 0.0f, 1.0f);
            const float a = attackS_.process();
            const float su = sustainS_.process();
            const float gainDb = a * c + su * (1.0f - c);
            const float g = dbToGain(gainDb);
            lastGainDb_ = gainDb;
            left[n] = xL * g;
            if (!mono) right[n] = xR * g;
        }
    }

    [[nodiscard]] float currentGainDb() const noexcept { return lastGainDb_; }

private:
    BenchPeriodPeak det_;
    EnvelopeFollower fast_;
    EnvelopeFollower slow_;
    float slowPrev_ = 0.0f;
    float slowCur_ = 0.0f;
    std::size_t tickPhase_ = 0;
    OnePoleSmoother attackS_;
    OnePoleSmoother sustainS_;
    float attackDb_ = 6.0f;
    float sustainDb_ = 0.0f;
    float durationMs_ = 40.0f;
    float lastGainDb_ = 0.0f;
};

// ------------------------------------------------------------------------------------------------------------------
// BenchCompressor (plan S5)
// ------------------------------------------------------------------------------------------------------------------
class BenchCompressor {
public:
    static constexpr float kMinCornerHz = 20.0f;
    static constexpr float kMaxCornerHz = 500.0f;
    static constexpr float kFallbackCornerHz = 80.0f;
    static constexpr float kCornerSmoothMs = 20.0f;
    static constexpr float kAttackMs = 10.0f;
    static constexpr float kMinReleaseMs = 50.0f;
    static constexpr float kMaxReleaseMs = 500.0f;
    static constexpr float kRuleThresholdDb = 1.0f;
    static constexpr std::size_t kMaxRuleIntervals = 1800;
    static constexpr float kMakeupSmoothMs = 5.0f;
    struct AmountRow {
        float amount;
        float thresholdDb;
        float slope;
        float kneeDb;
        float makeupDb;
    };
    static constexpr std::array<AmountRow, 5> kAmountTable{{
        {.amount = 0.00f, .thresholdDb = 0.0f, .slope = 0.00f, .kneeDb = 0.0f, .makeupDb = 0.0f},
        {.amount = 0.25f, .thresholdDb = -12.0f, .slope = 0.25f, .kneeDb = 6.0f, .makeupDb = 0.0f},
        {.amount = 0.50f, .thresholdDb = -16.0f, .slope = 0.40f, .kneeDb = 6.0f, .makeupDb = 0.0f},
        {.amount = 0.75f, .thresholdDb = -19.0f, .slope = 0.55f, .kneeDb = 6.0f, .makeupDb = 0.0f},
        {.amount = 1.00f, .thresholdDb = -22.0f, .slope = 0.67f, .kneeDb = 6.0f, .makeupDb = 0.0f}}};

    void prepare(double sampleRate, std::size_t maxBlockSize) noexcept {
        sampleRate_ = sampleRate;
        dyn_.prepare(sampleRate / static_cast<double>(kBenchDynTick), maxBlockSize);
        dyn_.setDetectionMode(DynamicsDetectionMode::Peak);
        dyn_.setSidechainEnabled(false);
        dyn_.setLookahead(0.0f);
        dyn_.setAttackTime(kAttackMs);
        dyn_.setReleaseTime(kMinReleaseMs);
        dyn_.setMakeupGain(0.0f);
        dyn_.setAutoMakeup(false);
        const auto fs = static_cast<float>(sampleRate);
        for (auto& h : hp_) h.configure(FilterType::Highpass, kFallbackCornerHz, kButterworthQ, 0.0f, fs);
        det_.prepare(sampleRate, 2000.0f / kFallbackCornerHz);
        cornerLog2S_.configure(kCornerSmoothMs, static_cast<float>(sampleRate / 32.0));
        makeupS_.configure(kMakeupSmoothMs, fs);
        ruleIntervals_ = std::clamp<std::size_t>(
            static_cast<std::size_t>(std::lround(0.3 * sampleRate / 32.0)), 1, kMaxRuleIntervals);
        latch();
        reset();
    }

    void reset() noexcept {
        dyn_.reset();
        releaseMs_ = kMinReleaseMs;
        dyn_.setReleaseTime(releaseMs_);
        cornerHz_ = std::exp2(cornerTargetLog2_);
        const auto fs = static_cast<float>(sampleRate_);
        for (auto& h : hp_) {
            h.reset();
            h.configure(FilterType::Highpass, cornerHz_, kButterworthQ, 0.0f, fs);
        }
        det_.reset();
        det_.setHoldMs(2000.0f / cornerHz_);
        ruleRing_.fill(0);
        ruleSum_ = 0;
        ruleCount_ = 0;
        rulePos_ = 0;
        intervalPhase_ = 0;
        cornerLog2S_.snapTo(cornerTargetLog2_);
        makeupS_.snapTo(makeupTargetDb_);
        gainDb_ = 0.0f;
        gr_ = 0.0f;
    }

    void setAmount(float amount01) noexcept {
        if (!detail::isFinite(amount01)) return;
        pendingAmount_ = std::clamp(amount01, 0.0f, 1.0f);
    }
    void setDetectorKeyHz(float f0Hz) noexcept { pendingKeyHz_ = detail::isFinite(f0Hz) ? f0Hz : 0.0f; }

    void processBlock(float* left, float* right, std::size_t numSamples) noexcept {
        const bool mono = (right == left);
        for (std::size_t n = 0; n < numSamples; ++n) {
            const float xL = benchSanitize(left[n]);
            const float xR = mono ? xL : benchSanitize(right[n]);
            if (intervalPhase_ == 0) controlStep();
            const float hL = hp_[0].process(xL);
            const float held = mono ? det_.process(hL) : det_.process(hL, hp_[1].process(xR));
            if constexpr (kBenchDynTick == 1) {
                (void)dyn_.processSample(held);
                gr_ = dyn_.getCurrentGainReduction();
            } else {
                if ((intervalPhase_ & (kBenchDynTick - 1)) == 0) {
                    (void)dyn_.processSample(held);
                    gr_ = dyn_.getCurrentGainReduction();
                }
            }
            if (gr_ < -kRuleThresholdDb) ++ruleCount_;
            const float mk = makeupS_.process();
            gainDb_ = gr_ + mk;
            if (gainDb_ != 0.0f) {
                const float g = dbToGain(gainDb_);
                left[n] = xL * g;
                if (!mono) right[n] = xR * g;
            } else {
                left[n] = xL;
                if (!mono) right[n] = xR;
            }
            intervalPhase_ = (intervalPhase_ + 1) % kBenchInterval;
        }
    }

    [[nodiscard]] float currentGainDb() const noexcept { return gainDb_; }

private:
    // S5.4 step 1 + S5.3: latch the pending amount and key; apply the row only on a bitwise change.
    void latch() noexcept {
        amount_ = pendingAmount_;
        std::size_t i = 0;
        while (i + 2 < kAmountTable.size() && amount_ > kAmountTable[i + 1].amount) ++i;
        const auto& r0 = kAmountTable[i];
        const auto& r1 = kAmountTable[i + 1];
        const float t = (amount_ - r0.amount) / (r1.amount - r0.amount);
        const float thr = r0.thresholdDb + t * (r1.thresholdDb - r0.thresholdDb);
        const float slope = r0.slope + t * (r1.slope - r0.slope);
        const float knee = r0.kneeDb + t * (r1.kneeDb - r0.kneeDb);
        const float makeup = r0.makeupDb + t * (r1.makeupDb - r0.makeupDb);
        const float ratio = 1.0f / (1.0f - slope);
        if (thr != thrApplied_) { dyn_.setThreshold(thr); thrApplied_ = thr; }
        if (ratio != ratioApplied_) { dyn_.setRatio(ratio); ratioApplied_ = ratio; }
        if (knee != kneeApplied_) { dyn_.setKneeWidth(knee); kneeApplied_ = knee; }
        if (makeup != makeupTargetDb_) { makeupTargetDb_ = makeup; makeupS_.setTarget(makeup); }
        const bool noVoice = !(pendingKeyHz_ > 0.0f);
        cornerTargetLog2_ = noVoice ? std::log2(kFallbackCornerHz)
                                    : std::log2(std::clamp(2.0f * pendingKeyHz_, kMinCornerHz, kMaxCornerHz));
        cornerLog2S_.setTarget(cornerTargetLog2_);
    }

    void controlStep() noexcept {
        latch();
        // S5.4 step 2: corner, reconfigure only on a bitwise change.
        const float c2 = cornerLog2S_.process();
        const float corner = std::exp2(c2);
        if (corner != cornerHz_) {
            cornerHz_ = corner;
            const auto fs = static_cast<float>(sampleRate_);
            for (auto& h : hp_) h.configure(FilterType::Highpass, cornerHz_, kButterworthQ, 0.0f, fs);
            det_.setHoldMs(2000.0f / cornerHz_);
        }
        // S5.4 step 3: D-5 release rule from integer counts.
        ruleSum_ += ruleCount_;
        ruleSum_ -= ruleRing_[rulePos_];
        ruleRing_[rulePos_] = static_cast<std::uint8_t>(ruleCount_);
        rulePos_ = (rulePos_ + 1) % ruleIntervals_;
        ruleCount_ = 0;
        const float f = static_cast<float>(ruleSum_) / static_cast<float>(ruleIntervals_ * kBenchInterval);
        const float rel = kMinReleaseMs + (kMaxReleaseMs - kMinReleaseMs) * f;
        if (rel != releaseMs_) {
            releaseMs_ = rel;
            dyn_.setReleaseTime(releaseMs_);
            ++releaseSets_;
        }
    }

    DynamicsProcessor dyn_;
    std::array<Biquad, 2> hp_{};
    BenchPeriodPeak det_;
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
    float cornerTargetLog2_ = 6.32192809f;
    float makeupTargetDb_ = 0.0f;
    float thrApplied_ = 1.0e9f;
    float ratioApplied_ = -1.0f;
    float kneeApplied_ = -1.0f;
    float releaseMs_ = kMinReleaseMs;
    float gainDb_ = 0.0f;
    float gr_ = 0.0f;
    double sampleRate_ = kBenchFs;

public:
    std::uint64_t releaseSets_ = 0;   // bench diagnostic: setReleaseTime calls
};

// ------------------------------------------------------------------------------------------------------------------
// BenchMassLaw (plan S7; S8.2 seed rows with the C-7 (i) Fold / compression amounts)
// ------------------------------------------------------------------------------------------------------------------
class BenchMassLaw {
public:
    static constexpr std::size_t kNumTargets = 6;
    static constexpr float kMassSweepMs = 40.0f;
    struct Row {
        float lo;
        float hi;
        float amountDown;
        float amountUp;
        ModCurve curve;
    };
    static constexpr std::array<Row, kNumTargets> kRows{{
        {.lo = 0.0f, .hi = 1.0f, .amountDown = -0.5f, .amountUp = -0.5f, .curve = ModCurve::Linear},     // Depth
        {.lo = 0.0f, .hi = 1.0f, .amountDown = 0.5f, .amountUp = 0.4f, .curve = ModCurve::Linear},       // Body
        {.lo = 5.0f, .hi = 250.0f, .amountDown = 30.0f, .amountUp = 110.0f, .curve = ModCurve::Linear},  // Duration
        {.lo = 0.0f, .hi = 100.0f, .amountDown = 50.0f, .amountUp = 60.0f, .curve = ModCurve::Linear},   // Fold
        {.lo = 0.0f, .hi = 1.0f, .amountDown = 0.5f, .amountUp = 1.0f, .curve = ModCurve::Linear},       // Compression
        {.lo = 0.0f, .hi = 1.0f, .amountDown = 0.5f, .amountUp = 0.5f, .curve = ModCurve::Linear}}};     // Anchor
    using Targets = std::array<float, kNumTargets>;

    [[nodiscard]] static float evaluateRow(const Row& row, float mass, float base) noexcept {
        const float m = std::clamp(detail::isFinite(mass) ? mass : 50.0f, 0.0f, 100.0f);
        const float u = (m - 50.0f) * 0.02f;
        const float s = applyModCurve(row.curve, std::abs(u));
        return std::clamp(base + (u >= 0.0f ? row.amountUp * s : -row.amountDown * s), row.lo, row.hi);
    }
    [[nodiscard]] Targets evaluate(float mass) const noexcept {
        Targets t{};
        for (std::size_t i = 0; i < kNumTargets; ++i) t[i] = evaluateRow(kRows[i], mass, base_[i]);
        return t;
    }

    void prepare(double sampleRate) noexcept {
        slew_.configure(100.0f / kMassSweepMs, static_cast<float>(sampleRate / 32.0));
        slew_.snapTo(pendingMass_);
    }
    void setMass(float mass) noexcept {
        if (!detail::isFinite(mass)) return;
        pendingMass_ = std::clamp(mass, 0.0f, 100.0f);
        slew_.setTarget(pendingMass_);
    }
    [[nodiscard]] float advanceInterval() noexcept { return slew_.process(); }

private:
    SlewLimiter slew_;
    float pendingMass_ = 50.0f;
    Targets base_{0.5f, 0.5f, 40.0f, 0.0f, 0.0f, 0.5f};
};

// ------------------------------------------------------------------------------------------------------------------
// Input: 64-partial 1/n saw at E1, -12 dBFS RMS, R offset by 0.37 period; 103 whole periods (wrap error < 0.03 sample)
// ------------------------------------------------------------------------------------------------------------------
std::vector<float> gInL;
std::vector<float> gInR;

void makeInput() {
    const double period = kBenchFs / kBenchE1Hz;
    const auto len = static_cast<std::size_t>(std::lround(103.0 * period));
    gInL.assign(len, 0.0f);
    gInR.assign(len, 0.0f);
    double ms = 0.0;
    for (int k = 1; k <= kBenchPartials; ++k) ms += 0.5 / (static_cast<double>(k) * k);
    const double scale = std::pow(10.0, -12.0 / 20.0) / std::sqrt(ms);
    constexpr double kTwoPi = 6.283185307179586;
    for (std::size_t n = 0; n < len; ++n) {
        const double ph = kTwoPi * kBenchE1Hz * static_cast<double>(n) / kBenchFs;
        double l = 0.0, r = 0.0;
        for (int k = 1; k <= kBenchPartials; ++k) {
            l += std::sin(k * ph) / k;
            r += std::sin(k * (ph + kTwoPi * 0.37)) / k;
        }
        gInL[n] = static_cast<float>(l * scale);
        gInR[n] = static_cast<float>(r * scale);
    }
}

// ------------------------------------------------------------------------------------------------------------------
// The chain under test (cell 3 composition: MassDynamics run-splitting on the 32-sample grid + MassLaw per interval)
// ------------------------------------------------------------------------------------------------------------------
struct BenchChain {
    BenchShaper shaper;
    BenchCompressor comp;
    BenchMassLaw law;
    BenchMassLaw::Targets lastTargets{};

    void prepare() {
        shaper.setAttackGainDb(12.0f);
        shaper.setSustainGainDb(-6.0f);
        shaper.prepare(kBenchFs);
        shaper.setTransientDurationMs(40.0f);
        comp.setAmount(1.0f);
        comp.setDetectorKeyHz(static_cast<float>(kBenchE1Hz));
        comp.prepare(kBenchFs, kBenchBlock);
        law.setMass(75.0f);
        law.prepare(kBenchFs);
    }

    // processBlock of a host block, split into runs ending on the 32-sample grid (host block 64 = 2 runs).
    void processBlock(float* l, float* r, std::size_t n) noexcept {
        for (std::size_t off = 0; off < n; off += kBenchInterval) {
            const std::size_t run = std::min(kBenchInterval, n - off);
            const float m = law.advanceInterval();
            lastTargets = law.evaluate(m);
            shaper.setTransientDurationMs(40.0f);   // MassDynamics latches the pending duration at each interval
            shaper.processBlock(l + off, r == l ? l + off : r + off, run);
            comp.processBlock(l + off, r == l ? l + off : r + off, run);
        }
    }
};

enum class CellKind { Shaper, Compressor, Total, DcLimiter, Linked };

struct BenchBuffers {
    std::array<float, kBenchBlock> l{};
    std::array<float, kBenchBlock> r{};
    std::size_t pos = 0;
    void fill(bool linked) {
        for (std::size_t i = 0; i < kBenchBlock; ++i) {
            l[i] = gInL[pos];
            r[i] = linked ? gInL[pos] : gInR[pos];
            if (++pos >= gInL.size()) pos = 0;
        }
    }
};

double runCell(CellKind kind, double seconds) {
    auto chain = std::make_unique<BenchChain>();
    chain->prepare();
    auto shaper = std::make_unique<BenchShaper>();
    shaper->setAttackGainDb(12.0f);
    shaper->setSustainGainDb(-6.0f);
    shaper->prepare(kBenchFs);
    shaper->setTransientDurationMs(40.0f);
    auto comp = std::make_unique<BenchCompressor>();
    comp->setAmount(1.0f);
    comp->setDetectorKeyHz(static_cast<float>(kBenchE1Hz));
    comp->prepare(kBenchFs, kBenchBlock);
    std::array<DCBlocker, 2> dc{};
    for (auto& d : dc) d.prepare(kBenchFs);
    auto lim = std::make_unique<TruePeakLimiter>();
    lim->prepare(kBenchFs, kBenchBlock);

    BenchBuffers buf;
    double sum = 0.0;
    std::uint64_t samples = 0;
    auto block = [&] {
        const bool linked = (kind == CellKind::Linked);
        buf.fill(linked);
        float* l = buf.l.data();
        float* r = buf.r.data();
        switch (kind) {
            case CellKind::Shaper: shaper->processBlock(l, r, kBenchBlock); break;
            case CellKind::Compressor: comp->processBlock(l, r, kBenchBlock); break;
            case CellKind::Total: chain->processBlock(l, r, kBenchBlock); break;
            case CellKind::DcLimiter:
                for (std::size_t i = 0; i < kBenchBlock; ++i) {
                    l[i] = dc[0].process(l[i]);
                    r[i] = dc[1].process(r[i]);
                }
                lim->processBlock(l, r, static_cast<int>(kBenchBlock));
                break;
            case CellKind::Linked:
                chain->processBlock(l, l, kBenchBlock);
                std::memcpy(r, l, kBenchBlock * sizeof(float));
                break;
        }
        sum += static_cast<double>(l[kBenchBlock - 1]) + static_cast<double>(r[kBenchBlock - 1]);
        samples += kBenchBlock;
    };
    for (int w = 0; w < 750; ++w) block();   // 1 s warm-up
    samples = 0;
    const auto t0 = std::chrono::steady_clock::now();
    double elapsed = 0.0;
    do {
        for (int w = 0; w < 64; ++w) block();
        elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    } while (elapsed < seconds);
    gSink = gSink + static_cast<float>(sum) + chain->lastTargets[2] + shaper->currentGainDb() + comp->currentGainDb();
    return elapsed * 1.0e9 / static_cast<double>(samples);
}

// Cell 6 (R-8): 1 s of signal (untimed) then exactly 1 s of digital silence (timed) through the cell 3 chain,
// repeated until `seconds` of timed silence have elapsed (at least once).
double runSilenceCell(double seconds) {
    auto chain = std::make_unique<BenchChain>();
    chain->prepare();
    BenchBuffers buf;
    constexpr int kBlocksPerSecond = 750;
    double timed = 0.0;
    std::uint64_t samples = 0;
    double sum = 0.0;
    do {
        for (int w = 0; w < kBlocksPerSecond; ++w) {
            buf.fill(false);
            chain->processBlock(buf.l.data(), buf.r.data(), kBenchBlock);
        }
        const auto t0 = std::chrono::steady_clock::now();
        for (int w = 0; w < kBlocksPerSecond; ++w) {
            buf.l.fill(0.0f);
            buf.r.fill(0.0f);
            chain->processBlock(buf.l.data(), buf.r.data(), kBenchBlock);
            sum += static_cast<double>(buf.l[kBenchBlock - 1]);
        }
        timed += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        samples += static_cast<std::uint64_t>(kBlocksPerSecond) * kBenchBlock;
    } while (timed < seconds);
    gSink = gSink + static_cast<float>(sum);
    return timed * 1.0e9 / static_cast<double>(samples);
}

double pctCore(double ns) { return ns * 48000.0 / 1.0e7; }

}  // namespace

int main(int argc, char** argv) {
    _mm_setcsr(_mm_getcsr() | 0x8040u);   // FTZ | DAZ (host precondition, plan R-8)
    double seconds = 3.0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--quick") == 0) seconds = 0.2;
        else if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) seconds = std::atof(argv[++i]);
    }
    makeInput();
    std::printf("P6BENCH fs=48000 block=%zu interval=%zu tp_factor=%zu dyn_tick=%zu seconds_per_cell=%.2f\n",
                kBenchBlock, kBenchInterval, kBenchTpFactor, kBenchDynTick, seconds);

    struct Cell {
        const char* name;
        CellKind kind;
    };
    constexpr std::array<Cell, 5> kCells{{{"shaper", CellKind::Shaper},
                                          {"compressor", CellKind::Compressor},
                                          {"total", CellKind::Total},
                                          {"dc_limiter", CellKind::DcLimiter},
                                          {"linked", CellKind::Linked}}};
    for (const auto& c : kCells) {
        const double ns = runCell(c.kind, seconds);
        std::printf("cell %s ns_per_sample %.2f pct_core %.4f\n", c.name, ns, pctCore(ns));
        std::fflush(stdout);
    }
    const double nsSil = runSilenceCell(seconds);
    std::printf("cell silence ns_per_sample %.2f pct_core %.4f\n", nsSil, pctCore(nsSil));
    std::printf("sink %g\n", static_cast<double>(gSink));
    return 0;
}
