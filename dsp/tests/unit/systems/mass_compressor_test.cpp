// ==============================================================================
// Profundum Phase 6 (specs/profundum-phase6-mass-dynamics)
// BassCompressor standalone: SC-007, SC-012/013/016/018/021 compressor arms, SC-024(a)(b)(c)(e)
// ==============================================================================
// tasks.md T007 (plan S5, C-6, C-10, C-11, C-13). Tags [systems][profundum][mass-dynamics].
// Fixtures: ../processors/transient_shaper_test_helpers.h (kSawAnchor / sine, notes, ramps) and,
// through it, ../processors/harmonic_fold_test_helpers.h.
//
// This TU is compiled in the shipping FP mode (not -fno-fast-math): non-finite arguments are built
// from bit patterns through a volatile sink, never from std::numeric_limits, and are never compared.
// No bit-exact float goldens: every bit-exact comparison is between two renders of this binary.
// ==============================================================================

#include "mass_dynamics_probe.h"

#include "../processors/transient_shaper_test_helpers.h"

// allocation_detector.h ONLY: the global operator new/delete replacements are included from exactly
// one TU of dsp_systems_tests (selectable_oscillator_test.cpp).
#include <allocation_detector.h>

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/systems/mass_dynamics.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <utility>
#include <vector>

using namespace Krate::DSP;
namespace tst = Krate::DSP::TransientShaperTest;
using Probe = Krate::DSP::detail::MassDynamicsProbe;

namespace {

constexpr double kFs = 48000.0;
constexpr std::size_t kCI = BassCompressor::kControlInterval;
constexpr std::size_t kMaxBlock = 512;

/// SC-007 recovery start (plan S1 C-13). RULED 2026-10-17 (user, plan S1 C-13; spec SC-007 row): the
/// recovery starts at the last sample within 0.1 dB of the event's peak GR (the plateau end). The
/// Peak reading (first sample at the peak GR) is printed beside it, not gated.
enum class RecoveryFrom : std::uint8_t { Peak, PlateauEnd };
constexpr RecoveryFrom kRecoveryFrom = RecoveryFrom::PlateauEnd;

/// A float from its bit pattern through a volatile sink (fast-math safe).
float floatFromBits(std::uint32_t bits) noexcept {
    volatile std::uint32_t sink = bits;
    const std::uint32_t read = sink;
    return std::bit_cast<float>(read);
}
float quietNaN() noexcept { return floatFromBits(0x7FC00000u); }
float posInfF() noexcept { return floatFromBits(0x7F800000u); }
float negInfF() noexcept { return floatFromBits(0xFF800000u); }

bool bitEqual(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

std::vector<float> sawTone(double f0, double fs, double seconds) {
    return tst::steadyTone(tst::Tone::Saw, f0, fs, seconds);
}

/// The DynamicsProcessor's amount-1 threshold (the last table row).
float amountOneThresholdDb() { return BassCompressor::kAmountTable.back().thresholdDb; }

/// max makeup over the table, as a linear gain.
float maxMakeupGain() {
    float mk = 0.0f;
    for (const auto& r : BassCompressor::kAmountTable)
        mk = std::max(mk, r.makeupDb);
    return dbToGain(mk);
}

/// One host-block render, in place, with optional events applied at absolute sample positions
/// (a block is split at an event, as a host with sample-accurate automation does).
struct Event {
    std::size_t at;
    std::function<void(BassCompressor&)> apply;
};

void renderPartitioned(BassCompressor& c, float* L, float* R, std::size_t n, std::size_t block,
                       const std::vector<Event>& events = {}) {
    REQUIRE(block >= 1);
    std::size_t pos = 0;
    std::size_t e = 0;
    while (pos < n) {
        while (e < events.size() && events[e].at <= pos) {
            events[e].apply(c);
            ++e;
        }
        std::size_t len = std::min(block, n - pos);
        if (e < events.size() && events[e].at < pos + len)
            len = events[e].at - pos;
        c.processBlock(L + pos, R + pos, len);
        pos += len;
    }
}

/// SC-007 calibration: the scale that puts the detector tap (mean dB over the last 0.5 s of a
/// 1.5 s render) at the amount-1 threshold, kSawAnchor at f0, key = f0.
struct Calibration {
    double scale;
    double detectorDb;
    double thresholdDb;
};

Calibration calibrateToThreshold(double fs, double f0) {
    BassCompressor c;
    c.setAmount(1.0f);
    c.setDetectorKeyHz(static_cast<float>(f0));
    c.prepare(fs, kMaxBlock);
    std::vector<float> L = sawTone(f0, fs, 1.5);
    std::vector<float> R = L;
    const std::size_t from = tst::secondsToSamples(fs, 1.0);
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t n = 0; n < L.size(); ++n) {
        c.processBlock(&L[n], &R[n], 1);
        if (n >= from) {
            sum += 20.0 * std::log10(std::max(static_cast<double>(c.detectorLevel()), 1e-30));
            ++count;
        }
    }
    REQUIRE(count > 0);
    const double d1 = sum / static_cast<double>(count);
    const double thr = static_cast<double>(Probe::threshold(c));
    REQUIRE(thr == static_cast<double>(amountOneThresholdDb()));
    return {.scale = std::pow(10.0, (thr - d1) / 20.0), .detectorDb = d1, .thresholdDb = thr};
}

/// SC-007 event render: 2 s of base, a +12 dB event (0.5 ms raised-cosine edges) lasting
/// `eventSeconds`, then 2 s of base. Recovery from the event's peak GR p: start = first sample with
/// GR == p (Peak) or the last within 0.1 dB of p (PlateauEnd); end = first later sample with
/// GR >= p / 2.
struct Recovery {
    double peakGrDb;
    double fromPeakMs;
    double fromPlateauEndMs;
    double plateauDetectorDb;   // mean detector dB over the event's plateau (the 1 s event only)
};

Recovery eventRecovery(double fs, double f0, double scale, double eventSeconds, float fixedReleaseMs) {
    const std::size_t pre = tst::secondsToSamples(fs, 2.0);
    const std::size_t dur = tst::secondsToSamples(fs, eventSeconds);
    const std::size_t ramp = tst::rampSamples(fs);
    const std::size_t post = tst::secondsToSamples(fs, 2.0);
    REQUIRE(dur > ramp);
    const std::size_t total = pre + dur + ramp + post;
    const std::vector<float> x = sawTone(f0, fs, static_cast<double>(total) / fs);
    REQUIRE(x.size() == total);

    const double G = std::pow(10.0, 12.0 / 20.0);   // x 3.98107
    std::vector<float> L(total, 0.0f);
    for (std::size_t n = 0; n < total; ++n) {
        double env = 1.0;
        if (n >= pre && n < pre + ramp)
            env = 1.0 + (G - 1.0) * tst::rampWeight(n - pre, ramp);
        else if (n >= pre + ramp && n < pre + dur)
            env = G;
        else if (n >= pre + dur && n < pre + dur + ramp)
            env = G - (G - 1.0) * tst::rampWeight(n - pre - dur, ramp);
        L[n] = static_cast<float>(scale * env * static_cast<double>(x[n]));
    }
    std::vector<float> R = L;

    BassCompressor c;
    c.setAmount(1.0f);
    c.setDetectorKeyHz(static_cast<float>(f0));
    Probe::setFixedReleaseMs(c, fixedReleaseMs);
    c.prepare(fs, kMaxBlock);
    std::vector<float> gr(total, 0.0f);
    std::vector<float> det(total, 0.0f);
    for (std::size_t n = 0; n < total; ++n) {
        c.processBlock(&L[n], &R[n], 1);
        gr[n] = c.currentGainReductionDb();
        det[n] = c.detectorLevel();
    }

    float p = 0.0f;
    for (std::size_t n = pre; n < total; ++n)
        p = std::min(p, gr[n]);
    REQUIRE(p < -1.0f);   // the event engaged the compressor

    std::size_t firstPeak = total;
    std::size_t plateauEnd = total;
    for (std::size_t n = pre; n < total; ++n) {
        if (firstPeak == total && gr[n] == p)
            firstPeak = n;
        if (static_cast<double>(gr[n]) <= static_cast<double>(p) + 0.1)
            plateauEnd = n;
    }
    REQUIRE(firstPeak < total);
    REQUIRE(plateauEnd < total);

    const double half = static_cast<double>(p) / 2.0;
    const auto recoveredAfter = [&](std::size_t start) {
        for (std::size_t m = start + 1; m < total; ++m) {
            if (static_cast<double>(gr[m]) >= half)
                return m;
        }
        return total;
    };
    const std::size_t endPeak = recoveredAfter(firstPeak);
    const std::size_t endPlateau = recoveredAfter(plateauEnd);
    REQUIRE(endPeak < total);
    REQUIRE(endPlateau < total);

    double detSum = 0.0;
    std::size_t detCount = 0;
    const std::size_t detFrom = pre + ramp + tst::secondsToSamples(fs, 0.1);
    for (std::size_t n = detFrom; n < pre + dur; ++n) {
        detSum += 20.0 * std::log10(std::max(static_cast<double>(det[n]), 1e-30));
        ++detCount;
    }
    const double plateauDb = (detCount > 0) ? detSum / static_cast<double>(detCount) : 0.0;

    return {.peakGrDb = static_cast<double>(p),
            .fromPeakMs = static_cast<double>(endPeak - firstPeak) * 1000.0 / fs,
            .fromPlateauEndMs = static_cast<double>(endPlateau - plateauEnd) * 1000.0 / fs,
            .plateauDetectorDb = plateauDb};
}

double gatedReading(const Recovery& r) {
    return (kRecoveryFrom == RecoveryFrom::PlateauEnd) ? r.fromPlateauEndMs : r.fromPeakMs;
}

struct NoteCase {
    const char* name;
    double hz;
};

/// SC-024(b): one interval's corner step against the OnePoleSmoother's allowance at the interval
/// rate: (1 − exp(−5000 / (20 · fs/32))) · |T − prev| + 1e-6. The smoother also snaps to its target
/// once within kCompletionThreshold (smoother.h:55, 199-201); that landing step (< 1e-4 log2 units)
/// is what "the smoother allows" there and is admitted as jump + 1e-6.
struct CornerStepCheck {
    double coeff;
    std::size_t violations = 0;
    double maxExcess = 0.0;
    std::size_t steps = 0;

    explicit CornerStepCheck(double fs)
        : coeff(std::exp(-5000.0 / (static_cast<double>(BassCompressor::kCornerSmoothMs) * fs /
                                    static_cast<double>(kCI)))) {}

    void step(double prevHz, double curHz, float keyHz) {
        const double prev = std::log2(prevHz);
        const double cur = std::log2(curHz);
        const double target = std::log2(std::clamp(2.0 * static_cast<double>(keyHz),
                                                   static_cast<double>(BassCompressor::kMinCornerHz),
                                                   static_cast<double>(BassCompressor::kMaxCornerHz)));
        const double jump = std::abs(target - prev);
        double allowed = (1.0 - coeff) * jump + 1e-6;
        if (jump < static_cast<double>(kCompletionThreshold) + 1e-6)
            allowed = std::max(allowed, jump + 1e-6);
        const double s = std::abs(cur - prev);
        ++steps;
        if (s > allowed) {
            ++violations;
            maxExcess = std::max(maxExcess, s - allowed);
        }
    }
};

/// Process `intervals` 32-sample blocks of silence.
void runSilentIntervals(BassCompressor& c, std::size_t intervals) {
    std::array<float, kCI> L{};
    std::array<float, kCI> R{};
    for (std::size_t k = 0; k < intervals; ++k) {
        L.fill(0.0f);
        R.fill(0.0f);
        c.processBlock(L.data(), R.data(), kCI);
    }
}

std::size_t intervalsIn(double fs, double seconds) {
    return static_cast<std::size_t>(std::ceil(seconds * fs / static_cast<double>(kCI)));
}

}  // namespace

// =============================================================================
// 1. FR-016, FR-014: the amount mapping
// =============================================================================
TEST_CASE("MassCompressor_AmountMapping", "[systems][profundum][mass-dynamics]") {
    STATIC_REQUIRE(BassCompressor::latencySamples() == 0);
    const auto& t = BassCompressor::kAmountTable;

    BassCompressor c;
    c.prepare(kFs, kMaxBlock);

    SECTION("amount 0 is the hard bypass row") {
        c.setAmount(0.0f);
        runSilentIntervals(c, 1);
        REQUIRE(Probe::ratio(c) == 1.0f);
        REQUIRE(Probe::makeupTargetDb(c) == 0.0f);
    }

    SECTION("amount 1 is the last row") {
        c.setAmount(1.0f);
        runSilentIntervals(c, 1);
        const double expRatio = 1.0 / (1.0 - static_cast<double>(t.back().slope));
        REQUIRE(std::abs(static_cast<double>(Probe::threshold(c)) - static_cast<double>(t.back().thresholdDb)) <= 1e-5);
        REQUIRE(std::abs(static_cast<double>(Probe::ratio(c)) - expRatio) <= 1e-5);
        REQUIRE(std::abs(static_cast<double>(Probe::knee(c)) - static_cast<double>(t.back().kneeDb)) <= 1e-5);
        REQUIRE(std::abs(static_cast<double>(Probe::makeupTargetDb(c)) - static_cast<double>(t.back().makeupDb)) <= 1e-5);
    }

    SECTION("amount 0.6 interpolates rows 0.5 and 0.75 linearly") {
        c.setAmount(0.6f);
        runSilentIntervals(c, 1);
        const auto& lo = t[2];
        const auto& hi = t[3];
        REQUIRE(lo.amount == 0.5f);
        REQUIRE(hi.amount == 0.75f);
        const double u = (0.6 - 0.5) / 0.25;
        const auto lerp = [u](float a, float b) {
            return static_cast<double>(a) + u * (static_cast<double>(b) - static_cast<double>(a));
        };
        const double expRatio = 1.0 / (1.0 - lerp(lo.slope, hi.slope));
        std::printf("MassCompressor_AmountMapping amount 0.6: threshold %.6g (exp %.6g) ratio %.6g (exp %.6g) "
                    "knee %.6g (exp %.6g) makeup %.6g (exp %.6g)\n",
                    static_cast<double>(Probe::threshold(c)), lerp(lo.thresholdDb, hi.thresholdDb),
                    static_cast<double>(Probe::ratio(c)), expRatio, static_cast<double>(Probe::knee(c)),
                    lerp(lo.kneeDb, hi.kneeDb), static_cast<double>(Probe::makeupTargetDb(c)),
                    lerp(lo.makeupDb, hi.makeupDb));
        REQUIRE(std::abs(static_cast<double>(Probe::threshold(c)) - lerp(lo.thresholdDb, hi.thresholdDb)) <= 1e-5);
        REQUIRE(std::abs(static_cast<double>(Probe::ratio(c)) - expRatio) <= 1e-5);
        REQUIRE(std::abs(static_cast<double>(Probe::knee(c)) - lerp(lo.kneeDb, hi.kneeDb)) <= 1e-5);
        REQUIRE(std::abs(static_cast<double>(Probe::makeupTargetDb(c)) - lerp(lo.makeupDb, hi.makeupDb)) <= 1e-5);
    }

    SECTION("monotone over 101 amounts") {
        float prevThr = 0.0f;
        float prevRatio = 0.0f;
        float prevKnee = 0.0f;
        float prevMk = 0.0f;
        std::size_t breaks = 0;
        for (int k = 0; k <= 100; ++k) {
            c.setAmount(static_cast<float>(k) / 100.0f);
            runSilentIntervals(c, 1);
            const float thr = Probe::threshold(c);
            const float ratio = Probe::ratio(c);
            const float knee = Probe::knee(c);
            const float mk = Probe::makeupTargetDb(c);
            if (k > 0 && (thr > prevThr || ratio < prevRatio || knee < prevKnee || mk < prevMk)) {
                ++breaks;
                std::printf("MassCompressor_AmountMapping monotonicity break at amount %d/100\n", k);
            }
            prevThr = thr;
            prevRatio = ratio;
            prevKnee = knee;
            prevMk = mk;
        }
        REQUIRE(breaks == 0);
    }

    SECTION("non-finite keeps the previous value; out of range clamps") {
        c.setAmount(0.4f);
        runSilentIntervals(c, 1);
        REQUIRE(c.amount() == 0.4f);
        c.setAmount(quietNaN());
        c.setAmount(posInfF());
        c.setAmount(negInfF());
        runSilentIntervals(c, 1);
        REQUIRE(c.amount() == 0.4f);
        c.setAmount(-1.0f);
        runSilentIntervals(c, 1);
        REQUIRE(c.amount() == 0.0f);
        c.setAmount(2.0f);
        runSilentIntervals(c, 1);
        REQUIRE(c.amount() == 1.0f);
    }

    SECTION("a mid-interval setAmount is latched at the next 32-sample boundary") {
        c.setAmount(0.0f);
        runSilentIntervals(c, 1);
        REQUIRE(Probe::ratio(c) == 1.0f);
        std::array<float, kCI> L{};
        std::array<float, kCI> R{};
        c.processBlock(L.data(), R.data(), 10);
        c.setAmount(1.0f);
        c.processBlock(L.data(), R.data(), kCI - 10);   // the interval ends; no boundary crossed yet
        REQUIRE(Probe::ratio(c) == 1.0f);
        REQUIRE(c.amount() == 0.0f);
        c.processBlock(L.data(), R.data(), 1);          // the next boundary
        REQUIRE(Probe::ratio(c) > 1.0f);
        REQUIRE(c.amount() == 1.0f);
    }
}

// =============================================================================
// 2. SC-013 compressor arm: transparent at amount 0
// =============================================================================
TEST_CASE("MassCompressor_TransparentAtAmountZero", "[systems][profundum][mass-dynamics]") {
    const std::array<NoteCase, 2> notes{{{"E0", tst::kE0Hz}, {"C3", tst::kC3Hz}}};
    const std::array<tst::Tone, 2> tones{tst::Tone::Saw, tst::Tone::Sine};
    for (const auto& note : notes) {
        for (const auto tone : tones) {
            const std::vector<float> x = tst::steadyTone(tone, note.hz, kFs, 2.0);
            std::vector<float> inR(x.size(), 0.0f);
            for (std::size_t n = 0; n < x.size(); ++n)
                inR[n] = 0.5f * x[n];
            std::vector<float> L = x;
            std::vector<float> R = inR;

            BassCompressor c;
            c.setAmount(0.0f);
            c.setDetectorKeyHz(static_cast<float>(note.hz));
            c.prepare(kFs, kMaxBlock);
            const std::size_t from = tst::secondsToSamples(kFs, 0.1);
            std::size_t nonZeroGain = 0;
            for (std::size_t pos = 0; pos < L.size(); pos += kCI) {
                const std::size_t len = std::min(kCI, L.size() - pos);
                c.processBlock(&L[pos], &R[pos], len);
                if (pos >= from && c.currentGainDb() != 0.0f)
                    ++nonZeroGain;
            }
            const bool lEqual = std::memcmp(L.data() + from, x.data() + from, (L.size() - from) * sizeof(float)) == 0;
            const bool rEqual = std::memcmp(R.data() + from, inR.data() + from, (R.size() - from) * sizeof(float)) == 0;
            std::printf("MassCompressor_TransparentAtAmountZero %s %s: L %s, R %s, nonzero-gain intervals %zu\n",
                        note.name, tone == tst::Tone::Saw ? "kSawAnchor" : "sine", lEqual ? "bit-equal" : "DIFFERS",
                        rEqual ? "bit-equal" : "DIFFERS", nonZeroGain);
            REQUIRE(lEqual);
            REQUIRE(rEqual);
            REQUIRE(nonZeroGain == 0);
            REQUIRE(c.currentGainDb() == 0.0f);
        }
    }
}

// =============================================================================
// 3. SC-007 law tap (D-5): release = 50 + 450 · f
// =============================================================================
TEST_CASE("MassCompressor_ReleaseRuleLaw", "[systems][profundum][mass-dynamics]") {
    const double f0 = tst::kE1Hz;
    const Calibration cal = calibrateToThreshold(kFs, f0);
    const double driveGain = cal.scale * std::pow(10.0, 12.0 / 20.0);
    std::printf("MassCompressor_ReleaseRuleLaw: detector %.6g dB at unit scale, threshold %.6g dB, scale %.6g\n",
                cal.detectorDb, cal.thresholdDb, cal.scale);

    const std::vector<float> x = sawTone(f0, kFs, 7.0);
    std::size_t pos = 0;
    std::array<float, kCI> L{};
    std::array<float, kCI> R{};

    BassCompressor c;
    c.setAmount(1.0f);
    c.setDetectorKeyHz(static_cast<float>(f0));
    c.prepare(kFs, kMaxBlock);

    std::size_t lawFailures = 0;
    std::size_t dynFailures = 0;
    const auto runInterval = [&](double gain) {
        REQUIRE(pos + kCI <= x.size());
        for (std::size_t i = 0; i < kCI; ++i) {
            L[i] = static_cast<float>(gain * static_cast<double>(x[pos + i]));
            R[i] = L[i];
        }
        pos += kCI;
        c.processBlock(L.data(), R.data(), kCI);
        const double law = 50.0 + 450.0 * static_cast<double>(c.programFraction());
        if (std::abs(static_cast<double>(c.currentReleaseMs()) - law) > 1e-3)
            ++lawFailures;
        if (Probe::releaseTimeInDynamics(c) != c.currentReleaseMs())
            ++dynFailures;
    };

    // +12 dB re the amount-1 threshold for 1 s: f -> 1, release -> 500 ms.
    for (std::size_t k = 0; k < intervalsIn(kFs, 1.0); ++k)
        runInterval(driveGain);
    std::printf("MassCompressor_ReleaseRuleLaw: after 1 s at +12 dB f = %.6g, release %.6g ms, GR %.6g dB\n",
                static_cast<double>(c.programFraction()), static_cast<double>(c.currentReleaseMs()),
                static_cast<double>(c.currentGainReductionDb()));
    REQUIRE(c.programFraction() >= 0.999f);
    REQUIRE(std::abs(static_cast<double>(c.currentReleaseMs()) - 500.0) <= 1e-3);

    // 30 dB lower until f reads 0 (within 3 s).
    const double lowGain = driveGain * std::pow(10.0, -30.0 / 20.0);
    bool drained = false;
    std::size_t drainIntervals = 0;
    for (std::size_t k = 0; k < intervalsIn(kFs, 3.0); ++k) {
        runInterval(lowGain);
        ++drainIntervals;
        if (c.programFraction() == 0.0f) {
            drained = true;
            break;
        }
    }
    std::printf("MassCompressor_ReleaseRuleLaw: f reached 0 after %.6g ms at -30 dB\n",
                static_cast<double>(drainIntervals * kCI) * 1000.0 / kFs);
    REQUIRE(drained);
    REQUIRE(c.currentReleaseMs() == 50.0f);

    // A rising level ramp (-30 dB -> 0 dB re the +12 dB drive, log-linear over 2 s): release
    // non-decreasing whenever f is.
    const std::size_t rampIntervals = intervalsIn(kFs, 2.0);
    float prevF = c.programFraction();
    float prevRel = c.currentReleaseMs();
    std::size_t monoBreaks = 0;
    for (std::size_t k = 0; k < rampIntervals; ++k) {
        const double db = -30.0 + 30.0 * static_cast<double>(k) / static_cast<double>(rampIntervals - 1);
        runInterval(driveGain * std::pow(10.0, db / 20.0));
        const float f = c.programFraction();
        const float rel = c.currentReleaseMs();
        if (f >= prevF && rel < prevRel)
            ++monoBreaks;
        prevF = f;
        prevRel = rel;
    }
    std::printf("MassCompressor_ReleaseRuleLaw: ramp end f = %.6g, release %.6g ms; law failures %zu, "
                "dynamics mismatches %zu, monotonicity breaks %zu\n",
                static_cast<double>(c.programFraction()), static_cast<double>(c.currentReleaseMs()), lawFailures,
                dynFailures, monoBreaks);
    REQUIRE(lawFailures == 0);
    REQUIRE(dynFailures == 0);
    REQUIRE(monoBreaks == 0);

    // The probe's fixed release: 275 ms at every interval.
    Probe::setFixedReleaseMs(c, 275.0f);
    c.reset();
    std::size_t fixedFailures = 0;
    for (std::size_t k = 0; k < intervalsIn(kFs, 0.5); ++k) {
        for (std::size_t i = 0; i < kCI; ++i) {
            L[i] = static_cast<float>(driveGain * static_cast<double>(x[pos + i]));
            R[i] = L[i];
        }
        pos += kCI;
        c.processBlock(L.data(), R.data(), kCI);
        if (c.currentReleaseMs() != 275.0f || Probe::releaseTimeInDynamics(c) != 275.0f)
            ++fixedFailures;
    }
    REQUIRE(fixedFailures == 0);
}

// =============================================================================
// 4. SC-007: program-dependent release (plan S1 C-13)
// =============================================================================
TEST_CASE("MassCompressor_ProgramDependentRelease", "[systems][profundum][mass-dynamics]") {
    const std::array<NoteCase, 2> notes{{{"E1", tst::kE1Hz}, {"C3", tst::kC3Hz}}};
    for (const auto& note : notes) {
        const Calibration cal = calibrateToThreshold(kFs, note.hz);
        const Recovery shortEv = eventRecovery(kFs, note.hz, cal.scale, 0.020, 0.0f);
        const Recovery longEv = eventRecovery(kFs, note.hz, cal.scale, 1.0, 0.0f);
        std::printf("MassCompressor_ProgramDependentRelease %s: threshold %.6g dB, 1 s plateau detector %.6g dB "
                    "(expected %.6g +/- 0.5)\n",
                    note.name, cal.thresholdDb, longEv.plateauDetectorDb, cal.thresholdDb + 12.0);
        std::printf("  short (20 ms): peak GR %.6g dB, recovery from Peak %.6g ms, from PlateauEnd %.6g ms\n",
                    shortEv.peakGrDb, shortEv.fromPeakMs, shortEv.fromPlateauEndMs);
        std::printf("  long (1 s):    peak GR %.6g dB, recovery from Peak %.6g ms, from PlateauEnd %.6g ms\n",
                    longEv.peakGrDb, longEv.fromPeakMs, longEv.fromPlateauEndMs);
        std::printf("  short/long: Peak %.6g, PlateauEnd %.6g (gated: %s, bar <= 0.5)\n",
                    shortEv.fromPeakMs / longEv.fromPeakMs, shortEv.fromPlateauEndMs / longEv.fromPlateauEndMs,
                    kRecoveryFrom == RecoveryFrom::PlateauEnd ? "PlateauEnd" : "Peak");
        REQUIRE(gatedReading(longEv) > 0.0);
        CHECK(gatedReading(shortEv) <= 0.5 * gatedReading(longEv));
    }
}

// =============================================================================
// 5. SC-007 positive control: a fixed 275 ms release fails the 1/2 bar
// =============================================================================
TEST_CASE("MassCompressor_FixedReleasePositiveControl", "[systems][profundum][mass-dynamics]") {
    const std::array<NoteCase, 2> notes{{{"E1", tst::kE1Hz}, {"C3", tst::kC3Hz}}};
    for (const auto& note : notes) {
        const Calibration cal = calibrateToThreshold(kFs, note.hz);
        const Recovery shortEv = eventRecovery(kFs, note.hz, cal.scale, 0.020, 275.0f);
        const Recovery longEv = eventRecovery(kFs, note.hz, cal.scale, 1.0, 275.0f);
        std::printf("MassCompressor_FixedReleasePositiveControl %s (fixed 275 ms): short Peak %.6g / PlateauEnd %.6g "
                    "ms, long Peak %.6g / PlateauEnd %.6g ms; ratio Peak %.6g, PlateauEnd %.6g (must be > 0.5)\n",
                    note.name, shortEv.fromPeakMs, shortEv.fromPlateauEndMs, longEv.fromPeakMs,
                    longEv.fromPlateauEndMs, shortEv.fromPeakMs / longEv.fromPeakMs,
                    shortEv.fromPlateauEndMs / longEv.fromPlateauEndMs);
        REQUIRE(gatedReading(longEv) > 0.0);
        CHECK(gatedReading(shortEv) / gatedReading(longEv) > 0.5);
    }
}

// =============================================================================
// 6. SC-024 (a)(b)(c)(e), FR-018: the keytracked corner
// =============================================================================
TEST_CASE("MassCompressor_KeytrackedCorner", "[systems][profundum][mass-dynamics]") {
    const std::size_t settle = intervalsIn(kFs, 0.2);
    const auto within1pct = [](float hz, double expected) {
        return std::abs(static_cast<double>(hz) / expected - 1.0) <= 0.01;
    };

    SECTION("(a) key E1 -> 2 * E1 by 200 ms") {
        BassCompressor c;
        c.prepare(kFs, kMaxBlock);
        REQUIRE(within1pct(c.detectorCornerHz(), static_cast<double>(BassCompressor::kFallbackCornerHz)));
        c.setDetectorKeyHz(static_cast<float>(tst::kE1Hz));
        runSilentIntervals(c, settle);
        std::printf("MassCompressor_KeytrackedCorner (a): corner %.6g Hz (2*E1 = %.6g)\n",
                    static_cast<double>(c.detectorCornerHz()), 2.0 * tst::kE1Hz);
        REQUIRE(within1pct(c.detectorCornerHz(), 2.0 * tst::kE1Hz));
    }

    SECTION("(b) clamps: key 5 Hz -> 20 Hz, key 300 Hz -> 500 Hz") {
        BassCompressor c;
        c.prepare(kFs, kMaxBlock);
        c.setDetectorKeyHz(5.0f);
        runSilentIntervals(c, settle);
        REQUIRE(within1pct(c.detectorCornerHz(), 20.0));
        c.setDetectorKeyHz(300.0f);
        runSilentIntervals(c, settle);
        REQUIRE(within1pct(c.detectorCornerHz(), 500.0));
    }

    SECTION("(b) continuity: a 48-semitone Drop ramp from C3 and a key step E1 -> C3") {
        CornerStepCheck check(kFs);
        {
            BassCompressor c;
            c.setDetectorKeyHz(static_cast<float>(tst::kC3Hz));
            c.prepare(kFs, kMaxBlock);
            REQUIRE(within1pct(c.detectorCornerHz(), 2.0 * tst::kC3Hz));
            const std::size_t rampIntervals = intervalsIn(kFs, 1.0);
            float key = static_cast<float>(tst::kC3Hz);
            for (std::size_t k = 0; k <= rampIntervals + settle; ++k) {
                const double frac = std::min(1.0, static_cast<double>(k) / static_cast<double>(rampIntervals));
                key = static_cast<float>(tst::kC3Hz * std::exp2(-4.0 * frac));
                const double prev = static_cast<double>(c.detectorCornerHz());
                c.setDetectorKeyHz(key);
                runSilentIntervals(c, 1);
                check.step(prev, static_cast<double>(c.detectorCornerHz()), key);
            }
            std::printf("MassCompressor_KeytrackedCorner (b) ramp end: key %.6g Hz, corner %.6g Hz\n",
                        static_cast<double>(key), static_cast<double>(c.detectorCornerHz()));
            REQUIRE(within1pct(c.detectorCornerHz(), 20.0));   // 2 * (C3 - 48 st) = 16.35 Hz clamps to 20
        }
        {
            BassCompressor c;
            c.setDetectorKeyHz(static_cast<float>(tst::kE1Hz));
            c.prepare(kFs, kMaxBlock);
            const auto key = static_cast<float>(tst::kC3Hz);
            for (std::size_t k = 0; k < settle; ++k) {
                const double prev = static_cast<double>(c.detectorCornerHz());
                c.setDetectorKeyHz(key);
                runSilentIntervals(c, 1);
                check.step(prev, static_cast<double>(c.detectorCornerHz()), key);
            }
            REQUIRE(within1pct(c.detectorCornerHz(), 2.0 * tst::kC3Hz));
        }
        std::printf("MassCompressor_KeytrackedCorner (b) continuity: %zu steps, %zu over the smoother bound, "
                    "max excess %.6g log2 units\n",
                    check.steps, check.violations, check.maxExcess);
        REQUIRE(check.violations == 0);
    }

    SECTION("(c) no voice -> the 80 Hz fallback") {
        const std::array<float, 4> keys{0.0f, -1.0f, quietNaN(), posInfF()};
        const std::array<const char*, 4> names{"0", "-1", "NaN", "+Inf"};
        for (std::size_t i = 0; i < keys.size(); ++i) {
            BassCompressor c;
            c.setDetectorKeyHz(static_cast<float>(tst::kC3Hz));
            c.prepare(kFs, kMaxBlock);
            c.setDetectorKeyHz(keys[i]);
            runSilentIntervals(c, settle);
            std::printf("MassCompressor_KeytrackedCorner (c) key %s: corner %.6g Hz\n", names[i],
                        static_cast<double>(c.detectorCornerHz()));
            REQUIRE(within1pct(c.detectorCornerHz(), static_cast<double>(BassCompressor::kFallbackCornerHz)));
        }
    }

    SECTION("(e) key change at sample 8704: partition invariant") {
        const std::vector<float> x = sawTone(tst::kE1Hz, kFs, 1.0);
        const std::array<std::size_t, 3> blocks{1, 17, 512};
        std::vector<std::vector<float>> outsL;
        std::vector<std::vector<float>> outsR;
        for (const std::size_t block : blocks) {
            std::vector<float> L = x;
            std::vector<float> R(x.size(), 0.0f);
            for (std::size_t n = 0; n < x.size(); ++n)
                R[n] = 0.7f * x[n];
            BassCompressor c;
            c.setAmount(1.0f);
            c.setDetectorKeyHz(static_cast<float>(tst::kE1Hz));
            c.prepare(kFs, kMaxBlock);
            const std::vector<Event> events{
                {.at = 8704, .apply = [](BassCompressor& b) { b.setDetectorKeyHz(static_cast<float>(tst::kC3Hz)); }}};
            renderPartitioned(c, L.data(), R.data(), L.size(), block, events);
            REQUIRE(within1pct(c.detectorCornerHz(), 2.0 * tst::kC3Hz));
            outsL.push_back(std::move(L));
            outsR.push_back(std::move(R));
        }
        for (std::size_t b = 1; b < blocks.size(); ++b) {
            std::printf("MassCompressor_KeytrackedCorner (e) block %zu vs 1: %s\n", blocks[b],
                        bitEqual(outsL[b], outsL[0]) && bitEqual(outsR[b], outsR[0]) ? "bit-equal" : "DIFFERS");
            REQUIRE(bitEqual(outsL[b], outsL[0]));
            REQUIRE(bitEqual(outsR[b], outsR[0]));
        }
    }
}

// =============================================================================
// 7. SC-012 compressor arm, FR-013: mono-sum
// =============================================================================
TEST_CASE("MassCompressor_MonoSum", "[systems][profundum][mass-dynamics]") {
    const std::vector<float> x = sawTone(tst::kE1Hz, kFs, 2.0);

    SECTION("(a) L == R in gives L == R out, bit-identical") {
        for (const float amount : {0.0f, 0.5f, 1.0f}) {
            std::vector<float> L = x;
            std::vector<float> R = x;
            BassCompressor c;
            c.setAmount(amount);
            c.setDetectorKeyHz(static_cast<float>(tst::kE1Hz));
            c.prepare(kFs, kMaxBlock);
            renderPartitioned(c, L.data(), R.data(), L.size(), kMaxBlock);
            std::printf("MassCompressor_MonoSum (a) amount %.6g: %s, final GR %.6g dB\n", static_cast<double>(amount),
                        bitEqual(L, R) ? "L == R bit-equal" : "L != R",
                        static_cast<double>(c.currentGainReductionDb()));
            REQUIRE(bitEqual(L, R));
        }
    }

    SECTION("(b) a 1 : 0.5 pair gets one gain; side / mid ratio unchanged") {
        std::vector<float> inR(x.size(), 0.0f);
        for (std::size_t n = 0; n < x.size(); ++n)
            inR[n] = 0.5f * x[n];
        std::vector<float> L = x;
        std::vector<float> R = inR;
        BassCompressor c;
        c.setAmount(1.0f);
        c.setDetectorKeyHz(static_cast<float>(tst::kE1Hz));
        c.prepare(kFs, kMaxBlock);
        float minGr = 0.0f;
        for (std::size_t pos = 0; pos < L.size(); pos += kCI) {
            const std::size_t len = std::min(kCI, L.size() - pos);
            c.processBlock(&L[pos], &R[pos], len);
            minGr = std::min(minGr, c.currentGainReductionDb());
        }
        REQUIRE(minGr < -1.0f);   // engaged, so the gain is not trivially 1

        std::size_t gainMismatch = 0;
        const double tol = 4.0 * static_cast<double>(FLT_EPSILON);
        for (std::size_t n = 0; n < L.size(); ++n) {
            const double a = static_cast<double>(L[n]) * static_cast<double>(inR[n]);
            const double b = static_cast<double>(R[n]) * static_cast<double>(x[n]);
            if (std::abs(a - b) > tol * std::abs(a))
                ++gainMismatch;
        }
        const double inRatio = tst::sideMidRatioDb(x.data(), inR.data(), x.size());
        const double outRatio = tst::sideMidRatioDb(L.data(), R.data(), L.size());
        std::printf("MassCompressor_MonoSum (b): min GR %.6g dB, per-sample gain mismatches %zu, side/mid in %.6g dB, "
                    "out %.6g dB (|delta| %.6g)\n",
                    static_cast<double>(minGr), gainMismatch, inRatio, outRatio, std::abs(outRatio - inRatio));
        REQUIRE(gainMismatch == 0);
        REQUIRE(std::abs(outRatio - inRatio) <= 0.01);
    }
}

// =============================================================================
// 8. SC-016 compressor arm, FR-014 / FR-040: no allocation
// =============================================================================
TEST_CASE("MassCompressor_NoAllocation", "[systems][profundum][mass-dynamics]") {
    auto& detector = TestHelpers::AllocationDetector::instance();

    // Positive control: the detector sees a real allocation in this binary.
    detector.startTracking();
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    int* probe = new int[16];
    probe[0] = 42;
    volatile int* probeSink = probe;   // defeat new/delete elision
    const int probeObserved = probeSink[0];
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    delete[] probe;
    const std::size_t livenessCount = detector.stopTracking();
    REQUIRE(probeObserved == 42);
    REQUIRE(livenessCount >= std::size_t{1});

    const std::vector<float> x = sawTone(tst::kE1Hz, kFs, 1.0);
    std::vector<float> L = x;
    std::vector<float> R = x;
    const float nan = quietNaN();
    const float pinf = posInfF();
    const float ninf = negInfF();
    BassCompressor c;
    c.prepare(kFs, kMaxBlock);   // control thread, outside the window

    detector.startTracking();
    c.setAmount(1.0f);
    c.setAmount(nan);
    c.setAmount(pinf);
    c.setAmount(ninf);
    c.setAmount(-1.0f);
    c.setAmount(0.75f);
    c.setDetectorKeyHz(static_cast<float>(tst::kE1Hz));
    c.setDetectorKeyHz(nan);
    c.setDetectorKeyHz(pinf);
    c.setDetectorKeyHz(ninf);
    c.setDetectorKeyHz(0.0f);
    c.setDetectorKeyHz(static_cast<float>(tst::kC3Hz));
    c.processBlock(L.data(), R.data(), 4096);
    Probe::setFixedReleaseMs(c, 275.0f);
    c.processBlock(L.data() + 4096, R.data() + 4096, 4096);
    Probe::setFixedReleaseMs(c, 0.0f);
    c.processBlock(L.data() + 8192, R.data() + 8192, 4096);
    c.reset();
    c.processBlock(L.data() + 12288, R.data() + 12288, 4096);
    c.processBlock(L.data() + 16384, L.data() + 16384, 4096);   // mono path
    const std::size_t count = detector.stopTracking();
    std::printf("MassCompressor_NoAllocation: %zu allocation(s) (positive control %zu)\n", count, livenessCount);
    REQUIRE(count == 0);
}

// =============================================================================
// 9. SC-018 compressor arm: partition invariance and reset determinism
// =============================================================================
TEST_CASE("MassCompressor_PartitionInvariant", "[systems][profundum][mass-dynamics]") {
    const std::vector<float> x = sawTone(tst::kE1Hz, kFs, 3.0);
    std::vector<float> inR(x.size(), 0.0f);
    for (std::size_t n = 0; n < x.size(); ++n)
        inR[n] = 0.8f * x[n];
    constexpr std::size_t kStepAt = 8704;                 // lcm(17, 32, 64, 512), plan C-11
    constexpr std::size_t kReleaseAt = kStepAt + kCI * 64;
    const std::vector<Event> events{
        {.at = kStepAt,
         .apply =
             [](BassCompressor& b) {
                 b.setAmount(1.0f);
                 b.setDetectorKeyHz(static_cast<float>(tst::kE2Hz));
             }},
        {.at = kReleaseAt, .apply = [](BassCompressor& b) { Probe::setFixedReleaseMs(b, 0.0f); }}};
    const auto configure = [](BassCompressor& b) {
        b.setAmount(0.5f);
        b.setDetectorKeyHz(static_cast<float>(tst::kE1Hz));
        Probe::setFixedReleaseMs(b, 275.0f);
    };

    const std::array<std::size_t, 5> blocks{1, 17, 32, 64, 512};
    std::vector<std::vector<float>> outsL;
    std::vector<std::vector<float>> outsR;
    for (const std::size_t block : blocks) {
        std::vector<float> L = x;
        std::vector<float> R = inR;
        BassCompressor c;
        configure(c);
        c.prepare(kFs, kMaxBlock);
        renderPartitioned(c, L.data(), R.data(), L.size(), block, events);
        outsL.push_back(std::move(L));
        outsR.push_back(std::move(R));
    }
    for (std::size_t b = 1; b < blocks.size(); ++b) {
        std::printf("MassCompressor_PartitionInvariant block %zu vs 1: %s\n", blocks[b],
                    bitEqual(outsL[b], outsL[0]) && bitEqual(outsR[b], outsR[0]) ? "bit-equal" : "DIFFERS");
        REQUIRE(bitEqual(outsL[b], outsL[0]));
        REQUIRE(bitEqual(outsR[b], outsR[0]));
    }

    // Two renders from reset() on one instance. reset() snaps the smoothers to the LATCHED targets
    // (plan S5.2 step 6) and the previous render's events latched amount 1 / key E2, so the starting
    // controls are re-latched (one silent interval) before each reset().
    BassCompressor c;
    configure(c);
    c.prepare(kFs, kMaxBlock);
    std::array<std::vector<float>, 2> rl;
    std::array<std::vector<float>, 2> rr;
    for (std::size_t r = 0; r < 2; ++r) {
        configure(c);
        runSilentIntervals(c, 1);
        c.reset();
        rl[r] = x;
        rr[r] = inR;
        renderPartitioned(c, rl[r].data(), rr[r].data(), x.size(), 64, events);
    }
    REQUIRE(bitEqual(rl[0], rl[1]));
    REQUIRE(bitEqual(rr[0], rr[1]));
}

// =============================================================================
// 10. SC-021 compressor arm: sample rates
// =============================================================================
TEST_CASE("MassCompressor_SampleRates", "[systems][profundum][mass-dynamics]") {
    const std::array<double, 5> rates{44100.0, 48000.0, 88200.0, 96000.0, 192000.0};
    const double bound = static_cast<double>(maxMakeupGain()) * (1.0 + 1e-6);
    for (const double fs : rates) {
        const Calibration cal = calibrateToThreshold(fs, tst::kE1Hz);
        const double drive = cal.scale * std::pow(10.0, 12.0 / 20.0);
        const std::vector<float> x = sawTone(tst::kE1Hz, fs, 1.0);
        std::vector<float> in(x.size(), 0.0f);
        for (std::size_t n = 0; n < x.size(); ++n)
            in[n] = static_cast<float>(drive * static_cast<double>(x[n]));
        std::vector<float> L = in;
        std::vector<float> R = in;
        BassCompressor c;
        c.setAmount(1.0f);
        c.setDetectorKeyHz(static_cast<float>(tst::kE1Hz));
        c.prepare(fs, kMaxBlock);
        renderPartitioned(c, L.data(), R.data(), L.size(), kMaxBlock);
        std::size_t nonFinite = 0;
        std::size_t overBound = 0;
        for (std::size_t n = 0; n < L.size(); ++n) {
            if (!tst::isFiniteBits(L[n]) || !tst::isFiniteBits(R[n]))
                ++nonFinite;
            const double lim = std::abs(static_cast<double>(in[n])) * bound;
            if (std::abs(static_cast<double>(L[n])) > lim || std::abs(static_cast<double>(R[n])) > lim)
                ++overBound;
        }
        std::printf("MassCompressor_SampleRates fs %.6g: scale %.6g, f = %.6g, release %.6g ms, GR %.6g dB, "
                    "non-finite %zu, over bound %zu\n",
                    fs, cal.scale, static_cast<double>(c.programFraction()), static_cast<double>(c.currentReleaseMs()),
                    static_cast<double>(c.currentGainReductionDb()), nonFinite, overBound);
        REQUIRE(nonFinite == 0);
        REQUIRE(overBound == 0);
        REQUIRE(c.programFraction() >= 0.999f);
        REQUIRE(c.stateFinite());
    }

    // prepare(48k) -> prepare(96k) -> prepare(48k), then a render, equals a fresh 48 kHz render.
    const std::vector<float> x48 = sawTone(tst::kE1Hz, 48000.0, 1.0);
    const std::vector<float> x96 = sawTone(tst::kE1Hz, 96000.0, 0.5);
    const auto setup = [](BassCompressor& b) {
        b.setAmount(1.0f);
        b.setDetectorKeyHz(static_cast<float>(tst::kE1Hz));
    };
    BassCompressor fresh;
    setup(fresh);
    fresh.prepare(48000.0, kMaxBlock);
    std::vector<float> fl = x48;
    std::vector<float> fr = x48;
    renderPartitioned(fresh, fl.data(), fr.data(), fl.size(), kMaxBlock);

    BassCompressor c;
    setup(c);
    c.prepare(48000.0, kMaxBlock);
    std::vector<float> tl = x48;
    std::vector<float> tr = x48;
    renderPartitioned(c, tl.data(), tr.data(), tl.size() / 2, kMaxBlock);
    c.prepare(96000.0, kMaxBlock);
    std::vector<float> ml = x96;
    std::vector<float> mr = x96;
    renderPartitioned(c, ml.data(), mr.data(), ml.size(), kMaxBlock);
    c.prepare(48000.0, kMaxBlock);
    std::vector<float> l = x48;
    std::vector<float> r = x48;
    renderPartitioned(c, l.data(), r.data(), l.size(), kMaxBlock);
    REQUIRE(bitEqual(l, fl));
    REQUIRE(bitEqual(r, fr));
}

// =============================================================================
// 11. FR-047 lifecycle (plan S13.3 FR-047 row (1)-(4)), inside AllocationScope
// =============================================================================
TEST_CASE("MassCompressor_Lifecycle", "[systems][profundum][mass-dynamics]") {
    auto& detector = TestHelpers::AllocationDetector::instance();
    constexpr double kFs96 = 96000.0;
    const auto e1 = static_cast<float>(tst::kE1Hz);

    // Every buffer is allocated before the tracked windows.
    const std::vector<float> x48 = sawTone(tst::kE1Hz, kFs, 3.0);   // 2 s drive + 1 s render
    const std::vector<float> x96 = sawTone(tst::kE1Hz, kFs96, 2.0);
    const std::size_t n2s = tst::secondsToSamples(kFs, 2.0);
    const std::size_t n1s = tst::secondsToSamples(kFs, 1.0);
    std::vector<float> sentinel(64, 0.123f);
    const std::vector<float> sentinelRef = sentinel;
    std::vector<float> sentinelR(64, -0.456f);
    const std::vector<float> sentinelRRef = sentinelR;
    std::vector<float> aL(x48.begin(), x48.begin() + static_cast<std::ptrdiff_t>(n1s));
    std::vector<float> aR = aL;
    std::vector<float> bL = aL;
    std::vector<float> bR = aL;
    std::vector<float> driveL(x48.begin(), x48.begin() + static_cast<std::ptrdiff_t>(n2s));
    std::vector<float> driveR = driveL;
    std::vector<float> r3L(x48.begin() + static_cast<std::ptrdiff_t>(n2s), x48.end());
    std::vector<float> r3R = r3L;
    std::vector<float> f3L = r3L;
    std::vector<float> f3R = r3L;
    std::vector<float> d96L = x96;
    std::vector<float> d96R = x96;
    std::vector<float> r4L = r3L;
    std::vector<float> r4R = r3L;
    std::vector<float> f4L = r3L;
    std::vector<float> f4R = r3L;

    // ---- (1) before prepare: no-op, taps at their defaults; setters store a pending value ----
    BassCompressor a;
    detector.startTracking();
    a.setAmount(1.0f);
    a.setDetectorKeyHz(e1);
    a.processBlock(sentinel.data(), sentinelR.data(), sentinel.size());
    const bool untouched1 = bitEqual(sentinel, sentinelRef) && bitEqual(sentinelR, sentinelRRef);
    const bool tapsDefault = a.currentGainDb() == 0.0f && a.currentGainReductionDb() == 0.0f &&
                             a.currentReleaseMs() == BassCompressor::kMinReleaseMs && a.programFraction() == 0.0f &&
                             a.detectorCornerHz() == BassCompressor::kFallbackCornerHz && a.detectorLevel() == 0.0f &&
                             a.amount() == 0.0f && a.stateFinite();
    const std::size_t count1 = detector.stopTracking();

    // First prepares (control thread, outside the window).
    a.prepare(kFs, kMaxBlock);
    const bool pendingApplied = a.amount() == 1.0f;
    BassCompressor b;
    b.setAmount(1.0f);
    b.setDetectorKeyHz(e1);
    b.prepare(kFs, kMaxBlock);
    BassCompressor fresh3;
    fresh3.setAmount(1.0f);
    fresh3.setDetectorKeyHz(e1);
    fresh3.prepare(kFs, kMaxBlock);
    BassCompressor fresh4;
    fresh4.setAmount(1.0f);
    fresh4.setDetectorKeyHz(e1);
    fresh4.prepare(kFs, kMaxBlock);

    detector.startTracking();
    // ---- (2) null channel / 0 samples are no-ops ----
    a.processBlock(nullptr, sentinelR.data(), sentinelR.size());
    a.processBlock(sentinel.data(), nullptr, sentinel.size());
    a.processBlock(sentinel.data(), sentinelR.data(), 0);
    const bool untouched2 = bitEqual(sentinel, sentinelRef) && bitEqual(sentinelR, sentinelRRef);
    a.processBlock(aL.data(), aR.data(), aL.size());
    b.processBlock(bL.data(), bR.data(), bL.size());
    const bool noPhaseAdvanced = bitEqual(aL, bL) && bitEqual(aR, bR);

    // ---- (3) reset() after 2 s at amount 1 with f > 0.5, then a render == a fresh instance's ----
    a.processBlock(driveL.data(), driveR.data(), driveL.size());
    const float f3 = a.programFraction();
    a.reset();
    a.processBlock(r3L.data(), r3R.data(), r3L.size());
    fresh3.processBlock(f3L.data(), f3R.data(), f3L.size());
    const bool resetEqualsFresh = bitEqual(r3L, f3L) && bitEqual(r3R, f3R);

    // ---- (4) prepare(96k), 2 s at f > 0.5, prepare(48k) ----
    a.prepare(kFs96, kMaxBlock);
    a.processBlock(d96L.data(), d96R.data(), d96L.size());
    const float f4 = a.programFraction();
    a.prepare(kFs, kMaxBlock);
    const float f4After = a.programFraction();
    const float releaseAfter = a.currentReleaseMs();
    a.processBlock(r4L.data(), r4R.data(), r4L.size());
    fresh4.processBlock(f4L.data(), f4R.data(), f4L.size());
    const bool reprepareEqualsFresh = bitEqual(r4L, f4L) && bitEqual(r4R, f4R);
    const std::size_t count2 = detector.stopTracking();

    std::printf("MassCompressor_Lifecycle: f before reset %.6g, f at 96 kHz %.6g, f after prepare(48k) %.6g, "
                "release %.6g ms; allocations %zu + %zu\n",
                static_cast<double>(f3), static_cast<double>(f4), static_cast<double>(f4After),
                static_cast<double>(releaseAfter), count1, count2);
    REQUIRE(untouched1);
    REQUIRE(tapsDefault);
    REQUIRE(pendingApplied);
    REQUIRE(untouched2);
    REQUIRE(noPhaseAdvanced);
    REQUIRE(f3 > 0.5f);
    REQUIRE(resetEqualsFresh);
    REQUIRE(f4 > 0.5f);
    REQUIRE(f4After == 0.0f);
    REQUIRE(releaseAfter == BassCompressor::kMinReleaseMs);
    REQUIRE(reprepareEqualsFresh);
    REQUIRE(count1 == 0);
    REQUIRE(count2 == 0);
}
