// ==============================================================================
// Layer 2: DSP Processor - Profundum Phase 4 (specs/profundum-phase4-drop-note-behaviour)
// ==============================================================================
// DropEnvelope: the per-voice Drop pitch envelope over the latched DropCurve.
// Includes Layer 0 only.
//   Spec:  specs/profundum-phase4-drop-note-behaviour/spec.md (FR-001-FR-010)
//   Plan:  specs/profundum-phase4-drop-note-behaviour/plan.md (S3.4, S3.6, S11.1)
//   Carries: SC-001-SC-005, SC-024(a), SC-017, SC-019.
//
// Output d[n] in semitones above the note. Parameters are latched at trigger
// (FR-003, D-4). The per-sample kernel is counter-based (no accumulation of u),
// evaluates the hoisted cubic of the current piece in Horner form and the
// exponential terms through one geometric recurrence per piece, seeded with
// std::exp at the piece's first sample (plan S3.4, T005 lever (2)).
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/drop_curve.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate::DSP {

class DropEnvelope {
public:
    static constexpr float kMaxAmountSemitones = 48.0f;
    static constexpr float kMinTimeMs = 1.0f;
    static constexpr float kMaxTimeMs = 4000.0f;
    static constexpr double kMinSampleRate = 22050.0;
    static constexpr double kMaxSampleRate = 192000.0;
    static constexpr float kDefaultAmountSemitones = 0.0f;
    static constexpr float kDefaultTimeMs = 100.0f;
    static constexpr float kDefaultShape = 0.0f;
    static constexpr float kDefaultRebound = 0.0f;

    // Aliases of the Layer 0 definitions (plan S1 C-12); not second definitions.
    static constexpr double kExpRate = DropCurve::kExpRate;
    static constexpr double kLandingFilletFraction = DropCurve::kLandingFilletFraction;
    static constexpr double kOnsetFilletFraction = DropCurve::kOnsetFilletFraction;
    static constexpr double kElasticOvershootFraction = DropCurve::kElasticOvershootFraction;
    static constexpr double kElasticDecay = DropCurve::kElasticDecay;
    static constexpr double kElasticPeakRatio = DropCurve::kElasticPeakRatio;
    static constexpr double kElasticSettleFactor = DropCurve::kElasticSettleFactor;
    static constexpr double kElasticLandingSlope = DropCurve::kElasticLandingSlope;
    static constexpr double kMaxReboundFraction = DropCurve::kMaxReboundFraction;
    static constexpr double kReboundSettleFactor = DropCurve::kReboundSettleFactor;
    static constexpr double kDropBridgeMs = DropCurve::kDropBridgeMs;
    static constexpr double kBrakeSemitones = DropCurve::kBrakeSemitones;

    /// Clamp the rate to [22050, 192000], then reset(). A non-finite rate is a no-op
    /// (tasks.md deviation 7).
    void prepare(double sampleRate) noexcept {
        if (!detail::isFinite(sampleRate))
            return;
        sampleRate_ = std::clamp(sampleRate, kMinSampleRate, kMaxSampleRate);
        prepared_ = true;
        reset();
    }

    /// d = d' = 0, idle; parameters kept (FR-002).
    void reset() noexcept {
        if (!prepared_)
            return;
        curve_ = DropCurve{};
        counter_ = 0;
        piece_ = 0;
        seeded_ = false;
        active_ = false;
        current_ = 0.0f;
        expDown_ = 0.0;
        expUp_ = 0.0;
        ratioDown_ = 1.0;
        ratioUp_ = 1.0;
    }

    /// Setter rule (FR-003, SC-017): non-finite keeps the previous value; subnormal (and signed
    /// zero) -> +0.0f; then clamp. Latched at the next trigger (D-4).
    void setAmount(float semitones) noexcept {
        if (prepared_)
            amount_ = sanitize(semitones, 0.0f, kMaxAmountSemitones, amount_);
    }
    void setTime(float ms) noexcept {
        if (prepared_)
            timeMs_ = sanitize(ms, kMinTimeMs, kMaxTimeMs, timeMs_);
    }
    void setShape(float s) noexcept {
        if (prepared_)
            shape_ = sanitize(s, 0.0f, 1.0f, shape_);
    }
    void setRebound(float r) noexcept {
        if (prepared_)
            rebound_ = sanitize(r, 0.0f, 1.0f, rebound_);
    }

    /// Latch the parameters and plan the curve; effective at the next process() (FR-007).
    /// snap: d jumps to A. Bridged: brake -> rise -> onset fillet from the running curve's value
    /// and slope at the next sample position (idle: (0, 0)).
    void trigger(bool snap) noexcept {
        if (!prepared_)
            return;
        double dNow = 0.0;
        double slopeNow = 0.0;
        if (!snap && active_) {
            const auto n = static_cast<double>(counter_);
            dNow = curve_.valueAt(n);
            slopeNow = curve_.slopeAt(n);
        }
        curve_ = makeDropCurve(static_cast<double>(amount_), static_cast<double>(timeMs_), static_cast<double>(shape_),
                               static_cast<double>(rebound_), sampleRate_, !snap, dNow, slopeNow);
        ratioDown_ = std::exp(-curve_.expRate);
        ratioUp_ = std::exp(curve_.expRate);
        counter_ = 0;
        piece_ = 0;
        seeded_ = false;
        active_ = true;
    }

    /// d[n] in semitones, then advance one sample (FR-008). Exactly 0.0f and idle from endSample.
    [[nodiscard]] float process() noexcept {
        if (!prepared_)
            return 0.0f;
        float d = 0.0f;
        run(&d, 1);
        return d;
    }

    /// Write n samples. A null pointer or n = 0 is a no-op with no state change (FR-008).
    void processBlock(float* out, std::size_t n) noexcept {
        if (out == nullptr || n == 0)
            return;
        if (!prepared_) {
            for (std::size_t i = 0; i < n; ++i)
                out[i] = 0.0f;
            return;
        }
        run(out, n);
    }

    /// False once d has settled at exactly 0.0f and the envelope is idle (FR-008).
    [[nodiscard]] bool isActive() const noexcept { return active_; }

    /// d at the last processed sample (FR-008).
    [[nodiscard]] float currentOffset() const noexcept { return current_; }

    /// The latched curve of the last trigger (FR-056 hook, plan S1 C-1).
    [[nodiscard]] const DropCurve& plannedCurve() const noexcept { return curve_; }

    /// Integral of (2^{d/12} - 1) dt over the latched curve, in seconds (plan S1 C-2). Computed on
    /// call by fixed-panel quadrature, never on the process() path.
    [[nodiscard]] double phaseIntegralSeconds() const noexcept {
        if (!prepared_)
            return 0.0;
        return curve_.integrate([](double, double d) { return std::exp2(d / 12.0) - 1.0; }) / sampleRate_;
    }

private:
    /// n samples of d (prepared). The running state is held in locals for the loop (SC-019: stores
    /// to out cannot alias them) and written back once. SC-019 (T023 epoch 7): the samples are
    /// processed in spans that stay inside one piece, so the piece test, the end test and the
    /// exponential-term test run once per span, not per sample; the arithmetic per sample is
    /// unchanged (t + j < end <=> j < ceil(end - t) for integer j, and end - t is exact).
    void run(float* out, std::size_t n) noexcept {
        bool active = active_;
        std::uint32_t counter = counter_;
        int piece = piece_;
        bool seeded = seeded_;
        double expDown = expDown_;
        double expUp = expUp_;
        float current = current_;
        const double ratioDown = ratioDown_;
        const double ratioUp = ratioUp_;
        const double endSample = curve_.endSample;
        std::size_t i = 0;
        while (i < n) {
            if (!active) {
                for (; i < n; ++i)
                    out[i] = 0.0f;
                current = 0.0f;
                break;
            }
            const auto t0 = static_cast<double>(counter);
            if (!(t0 < endSample)) {
                active = false;  // exactly 0.0f and idle from endSample
                continue;
            }
            while (!(t0 < curve_.pieces[static_cast<std::size_t>(piece)].end)) {
                ++piece;
                seeded = false;
            }
            const DropCurve::Piece p = curve_.pieces[static_cast<std::size_t>(piece)];
            const std::size_t left = n - i;
            const double room = std::ceil(p.end - t0);  // >= 1: samples of this piece from t0
            const std::size_t m = room < static_cast<double>(left) ? static_cast<std::size_t>(room) : left;
            float* const o = out + i;
            if (p.eDown != 0.0 || p.eUp != 0.0) {
                if (!seeded) {
                    expDown = curve_.expDownAt(t0);
                    expUp = curve_.expUpAt(t0);
                    seeded = true;
                }
                for (std::size_t j = 0; j < m; ++j) {
                    const auto t = static_cast<double>(counter + static_cast<std::uint32_t>(j));
                    const double tau = (t - p.start) * p.invLen;
                    double d = ((p.c3 * tau + p.c2) * tau + p.c1) * tau + p.c0;
                    if (p.eDown != 0.0)
                        d += p.eDown * expDown;
                    if (p.eUp != 0.0)
                        d += p.eUp * expUp;
                    expDown *= ratioDown;
                    expUp *= ratioUp;
                    o[j] = static_cast<float>(d);
                }
            } else {
                for (std::size_t j = 0; j < m; ++j) {
                    const auto t = static_cast<double>(counter + static_cast<std::uint32_t>(j));
                    const double tau = (t - p.start) * p.invLen;
                    o[j] = static_cast<float>(((p.c3 * tau + p.c2) * tau + p.c1) * tau + p.c0);
                }
            }
            counter += static_cast<std::uint32_t>(m);
            i += m;
            current = o[m - 1];
        }
        active_ = active;
        counter_ = counter;
        piece_ = piece;
        seeded_ = seeded;
        expDown_ = expDown;
        expUp_ = expUp;
        current_ = current;
    }

    [[nodiscard]] static float sanitize(float v, float lo, float hi, float previous) noexcept {
        if (!detail::isFinite(v))
            return previous;
        if ((detail::opaqueFloatBits(v) & 0x7F800000u) == 0u)
            v = 0.0f;  // subnormal or signed zero -> +0 (bit test: immune to fast-math and FTZ/DAZ)
        return std::clamp(v, lo, hi);
    }

    DropCurve curve_{};
    double sampleRate_ = 48000.0;
    double expDown_ = 0.0;
    double expUp_ = 0.0;
    double ratioDown_ = 1.0;
    double ratioUp_ = 1.0;
    std::uint32_t counter_ = 0;
    int piece_ = 0;
    float amount_ = kDefaultAmountSemitones;
    float timeMs_ = kDefaultTimeMs;
    float shape_ = kDefaultShape;
    float rebound_ = kDefaultRebound;
    float current_ = 0.0f;
    bool prepared_ = false;
    bool active_ = false;
    bool seeded_ = false;
};

}  // namespace Krate::DSP
