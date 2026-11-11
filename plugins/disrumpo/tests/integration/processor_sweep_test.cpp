// ==============================================================================
// Processor sweep automation: LFO and envelope follower clocking, tempo sync
// ==============================================================================
// Disrumpo audit fix plan T1.10 (findings D-035, D-036, D-043, D-126).
// These cases drive the whole Processor through the render rig and read the
// sweep centre the processor reports (kSweepModulatedFrequencyOutputId, one
// point per block) and the smoothed sweep centre (getSweepProcessor()), so they
// see how the processor clocks the sweep LFO and envelope follower, not how the
// components behave when a test clocks them directly.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "../test_support/disrumpo_rig.h"
#include "dsp/sweep_envelope.h"
#include "dsp/sweep_morph_link.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstprocesscontext.h"

#include "test_helpers/artifact_detection.h"
#include "test_helpers/signal_metrics.h"

#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace {

namespace ts = Disrumpo::TestSupport;
using Disrumpo::makeSweepParamId;
using SPT = Disrumpo::SweepParamType;
using Steinberg::int32;

constexpr double kBaseHz = 1000.0;

// Processor mapping of kSweepLFORate (processor_params.cpp): ln-linear 0.01..20 Hz.
double lfoRateNorm(double hz) {
    const double kMinRateLog = std::log(0.01);  // state_codec.h computes ln(0.01) and ln(20) (T1.17)
    const double kMaxRateLog = std::log(20.0);
    return (std::log(hz) - kMinRateLog) / (kMaxRateLog - kMinRateLog);
}

double sweepFreqNorm(double hz) {
    return (std::log2(hz) - static_cast<double>(Disrumpo::kLog2MinFreq)) /
           static_cast<double>(Disrumpo::kLog2FreqRange);
}

// log2 of the base frequency exactly as the processor denormalizes it.
double baseLog2() {
    return static_cast<double>(Disrumpo::kLog2MinFreq) +
           sweepFreqNorm(kBaseHz) * static_cast<double>(Disrumpo::kLog2FreqRange);
}

/// Block-end time (s) and reported sweep-centre shift (octaves vs the base).
struct Track {
    std::vector<double> t;
    std::vector<double> shift;
    std::vector<size_t> endSample;  // index of the last sample of the block
    bool allFinite = true;
};

Track renderTrack(ts::Rig& rig, const std::vector<float>& in, int32 block,
                  std::vector<float>* outL = nullptr) {
    Track tr;
    const size_t n = in.size();
    const double b2 = baseLog2();
    std::vector<float> oL;
    std::vector<float> oR;
    rig.render(in, in, oL, oR, block, {},
               [&](size_t blk, ts::CaptureChanges& cap) {
                   const size_t start = blk * static_cast<size_t>(block);
                   const size_t end = std::min(start + static_cast<size_t>(block), n);
                   double v = 0.0;
                   if (!cap.last(Disrumpo::kSweepModulatedFrequencyOutputId, v)) return;
                   if (!Krate::DSP::detail::isFinite(v)) tr.allFinite = false;
                   tr.t.push_back(static_cast<double>(end - 1) / rig.sr);
                   tr.endSample.push_back(end - 1);
                   tr.shift.push_back(static_cast<double>(Disrumpo::kLog2MinFreq) +
                                      v * static_cast<double>(Disrumpo::kLog2FreqRange) - b2);
               });
    for (float s : oL) if (!Krate::DSP::detail::isFinite(s)) tr.allFinite = false;
    if (outL) *outL = std::move(oL);
    return tr;
}

/// Upward zero crossings of the shift, linearly interpolated between block ends.
std::vector<double> upCrossings(const Track& tr) {
    std::vector<double> c;
    for (size_t i = 1; i < tr.shift.size(); ++i) {
        const double a = tr.shift[i - 1];
        const double b = tr.shift[i];
        if (a < 0.0 && b >= 0.0) {
            const double frac = -a / (b - a);
            c.push_back(tr.t[i - 1] + frac * (tr.t[i] - tr.t[i - 1]));
        }
    }
    return c;
}

double meanPeriod(const std::vector<double>& c) {
    if (c.size() < 2) return 0.0;
    return (c.back() - c.front()) / static_cast<double>(c.size() - 1);
}

ts::PList lfoParams(double rateHz) {
    return {{makeSweepParamId(SPT::kSweepEnable), 1.0},
            {makeSweepParamId(SPT::kSweepFrequency), sweepFreqNorm(kBaseHz)},
            {makeSweepParamId(SPT::kSweepLFOEnable), 1.0},
            {makeSweepParamId(SPT::kSweepLFODepth), 1.0},
            {makeSweepParamId(SPT::kSweepLFOWaveform), 0.0},  // Sine
            {makeSweepParamId(SPT::kSweepLFORate), lfoRateNorm(rateHz)}};
}

}  // namespace

// -----------------------------------------------------------------------------
// D-035 / D-126: the LFO advances once per sample, so its rate does not depend
// on the host block size.
// -----------------------------------------------------------------------------
TEST_CASE("Sweep LFO rate is independent of block size", "[disrumpo][sweep][D-035]") {
    constexpr double kSr = 48000.0;
    const std::vector<float> in(static_cast<size_t>(kSr * 2.2), 0.0f);
    for (const int32 block : {64, 1024}) {
        ts::Rig rig(kSr, 1024);
        rig.apply(lfoParams(2.0));
        const Track tr = renderTrack(rig, in, block);
        const auto c = upCrossings(tr);
        const double period = meanPeriod(c);
        INFO("block " << block << ": " << c.size() << " upward crossings, mean period " << period
                      << " s (expected 0.5 s)");
        WARN("D-035 block " << block << ": crossings " << c.size() << ", period " << period << " s");
        CHECK(tr.allFinite);
        CHECK(c.size() == 4);
        CHECK(std::abs(period - 0.5) <= 0.5 * 0.02);
    }

    // Audit probe sweep:lfo-rate (T6.07): 5 Hz, depth 1 (+/-2 octaves), 2 s at 512-sample
    // blocks spans about 4 octaves in about 10 cycles (baseline 0.244 oct, 0 cycles).
    {
        ts::Rig rig(kSr, 512);
        rig.apply(lfoParams(5.0));
        const std::vector<float> twoSeconds(static_cast<size_t>(kSr * 2.0), 0.0f);
        const Track tr = renderTrack(rig, twoSeconds, 512);
        const auto [lo, hi] = std::minmax_element(tr.shift.begin(), tr.shift.end());
        const double rangeOct = *hi - *lo;
        const size_t cycles = upCrossings(tr).size();
        WARN("T6.07 LFO 5 Hz over 2 s: range " << rangeOct << " oct, " << cycles << " upward crossings");
        CHECK(tr.allFinite);
        CHECK(std::abs(rangeOct - 4.0) <= 0.2);
        CHECK(cycles >= 9);
        CHECK(cycles <= 11);
    }
}

// -----------------------------------------------------------------------------
// D-035 (a): the sweep centre moves per sample inside a block. The trajectory
// rendered at 512- and 37-sample blocks must equal the one rendered one sample
// per block (where block-rate and per-sample clocking coincide).
// -----------------------------------------------------------------------------
TEST_CASE("Sweep centre follows the LFO per sample within a block", "[disrumpo][sweep][D-035]") {
    constexpr double kSr = 48000.0;
    const std::vector<float> in(static_cast<size_t>(kSr * 0.5), 0.0f);

    // Reference: one sample per block, the smoothed centre after every sample.
    std::vector<double> ref;
    std::vector<double> refTarget;
    {
        ts::Rig rig(kSr, 512);
        rig.apply(lfoParams(5.0));
        std::vector<float> oL;
        std::vector<float> oR;
        rig.render(in, in, oL, oR, 1, {}, [&](size_t, ts::CaptureChanges& cap) {
            ref.push_back(static_cast<double>(rig.p->getSweepProcessor().getSmoothedFrequency()));
            double v = 0.0;
            if (cap.last(Disrumpo::kSweepModulatedFrequencyOutputId, v))
                refTarget.push_back(static_cast<double>(Disrumpo::kLog2MinFreq) +
                                    v * static_cast<double>(Disrumpo::kLog2FreqRange));
        });
    }
    REQUIRE(ref.size() == in.size());

    double maxJumpSmoothed = 0.0;
    for (size_t i = 1; i < ref.size(); ++i)
        maxJumpSmoothed = std::max(maxJumpSmoothed, std::abs(std::log2(ref[i] / ref[i - 1])));
    double maxJumpTarget = 0.0;
    for (size_t i = 1; i < refTarget.size(); ++i)
        maxJumpTarget = std::max(maxJumpTarget, std::abs(refTarget[i] - refTarget[i - 1]));
    // Analytic steepest slope of a +/-2 octave 5 Hz sine, per sample.
    const double analyticMaxJump = 2.0 * 2.0 * std::numbers::pi * 5.0 / kSr;
    WARN("D-035(a) per-sample reference: max per-sample jump target " << maxJumpTarget
         << " oct, smoothed centre " << maxJumpSmoothed << " oct (analytic sine slope "
         << analyticMaxJump << " oct/sample)");

    for (const int32 block : {512, 37}) {
        ts::Rig rig(kSr, 512);
        rig.apply(lfoParams(5.0));
        std::vector<double> got;
        std::vector<size_t> ends;
        std::vector<float> oL;
        std::vector<float> oR;
        rig.render(in, in, oL, oR, block, {}, [&](size_t blk, ts::CaptureChanges&) {
            const size_t end = std::min((blk + 1) * static_cast<size_t>(block), in.size());
            got.push_back(static_cast<double>(rig.p->getSweepProcessor().getSmoothedFrequency()));
            ends.push_back(end - 1);
        });
        double maxDev = 0.0;
        double maxStep = 0.0;
        for (size_t k = 0; k < got.size(); ++k) {
            maxDev = std::max(maxDev, std::abs(std::log2(got[k] / ref[ends[k]])));
            if (k > 0) maxStep = std::max(maxStep, std::abs(std::log2(got[k] / got[k - 1])));
        }
        WARN("D-035(a) block " << block << ": max deviation of the smoothed centre from the "
             "per-sample reference " << maxDev << " oct; max block-to-block move " << maxStep << " oct");
        CHECK(maxDev <= 1e-4);
    }
    REQUIRE(maxJumpSmoothed <= analyticMaxJump * 1.05);
}

// -----------------------------------------------------------------------------
// D-036 / D-126: the envelope follower sees every input sample, so its attack
// and release (in ms) hold at every sample rate and block size.
// -----------------------------------------------------------------------------
TEST_CASE("Sweep envelope follower attack is block-size independent", "[disrumpo][sweep][D-036]") {
    constexpr double kAttackNorm = 0.0909;     // 1 + 0.0909 * 99 = 10.0 ms
    constexpr double kReleaseNorm = 0.183673;  // 10 + 0.183673 * 490 = 100.0 ms
    const double amp = std::pow(10.0, -3.0 / 20.0);

    for (const double sr : {44100.0, 48000.0, 96000.0}) {
        const auto seg = static_cast<size_t>(sr * 0.5);
        std::vector<float> in(seg * 3, 0.0f);
        for (size_t i = 0; i < seg; ++i)
            in[seg + i] = static_cast<float>(amp * std::sin(2.0 * std::numbers::pi * 1000.0 *
                                                            static_cast<double>(i) / sr));

        // Per-sample reference: the same follower, clocked once per sample.
        Disrumpo::SweepEnvelope env;
        env.prepare(sr, 512);
        env.setEnabled(true);
        env.setAttackTime(Disrumpo::kMinSweepEnvAttackMs +
                          static_cast<float>(kAttackNorm) *
                              (Disrumpo::kMaxSweepEnvAttackMs - Disrumpo::kMinSweepEnvAttackMs));
        env.setReleaseTime(Disrumpo::kMinSweepEnvReleaseMs +
                           static_cast<float>(kReleaseNorm) *
                               (Disrumpo::kMaxSweepEnvReleaseMs - Disrumpo::kMinSweepEnvReleaseMs));
        env.setSensitivity(1.0f);
        std::vector<double> ref(in.size());
        for (size_t i = 0; i < in.size(); ++i) {
            (void)env.processSample((in[i] + in[i]) * 0.5f);
            ref[i] = 2.0 * static_cast<double>(env.getModulationAmount());
        }
        double finalShift = 0.0;
        const size_t tailLen = static_cast<size_t>(sr * 0.1);
        for (size_t i = 2 * seg - tailLen; i < 2 * seg; ++i) finalShift += ref[i];
        finalShift /= static_cast<double>(tailLen);

        auto t63Attack = [&](const std::vector<double>& t, const std::vector<double>& s) {
            for (size_t i = 0; i < s.size(); ++i)
                if (t[i] >= 0.5 && s[i] >= 0.632 * finalShift) return (t[i] - 0.5) * 1000.0;
            return -1.0;
        };
        auto t63Release = [&](const std::vector<double>& t, const std::vector<double>& s) {
            for (size_t i = 0; i < s.size(); ++i)
                if (t[i] >= 1.0 && s[i] <= (1.0 - 0.632) * finalShift) return (t[i] - 1.0) * 1000.0;
            return -1.0;
        };
        std::vector<double> refT(in.size());
        for (size_t i = 0; i < in.size(); ++i) refT[i] = static_cast<double>(i) / sr;
        const double refAtk = t63Attack(refT, ref);
        const double refRel = t63Release(refT, ref);

        for (const int32 block : {512, 16}) {
            ts::Rig rig(sr, 512);
            rig.apply({{makeSweepParamId(SPT::kSweepEnable), 1.0},
                       {makeSweepParamId(SPT::kSweepFrequency), sweepFreqNorm(kBaseHz)},
                       {makeSweepParamId(SPT::kSweepEnvEnable), 1.0},
                       {makeSweepParamId(SPT::kSweepEnvSensitivity), 1.0},
                       {makeSweepParamId(SPT::kSweepEnvAttack), kAttackNorm},
                       {makeSweepParamId(SPT::kSweepEnvRelease), kReleaseNorm}});
            const Track tr = renderTrack(rig, in, block);
            double maxDev = 0.0;
            for (size_t k = 0; k < tr.shift.size(); ++k)
                maxDev = std::max(maxDev, std::abs(tr.shift[k] - ref[tr.endSample[k]]));
            const double atk = t63Attack(tr.t, tr.shift);
            const double rel = t63Release(tr.t, tr.shift);
            const double blockMs = static_cast<double>(block) / sr * 1000.0;
            WARN("D-036 sr " << sr << " block " << block << ": 63% attack " << atk << " ms, 63% release "
                 << rel << " ms (per-sample reference " << refAtk << " / " << refRel
                 << " ms, final shift " << finalShift << " oct); max deviation from reference "
                 << maxDev << " oct");
            CHECK(tr.allFinite);
            CHECK(maxDev <= 1e-3);
            CHECK(atk >= 0.0);
            CHECK(atk <= 40.0);
            CHECK(atk <= refAtk + blockMs);
            CHECK(rel >= 0.0);
            CHECK(rel <= refRel + blockMs);
        }
    }
}

// -----------------------------------------------------------------------------
// D-043: a synced sweep LFO follows the host tempo; without a valid tempo it
// keeps running at the last valid tempo (120 BPM before any).
// -----------------------------------------------------------------------------
TEST_CASE("Sweep LFO tempo sync follows ProcessContext tempo", "[disrumpo][sweep][D-043]") {
    constexpr double kSr = 48000.0;
    const std::vector<float> in(static_cast<size_t>(kSr * 3.2), 0.0f);
    ts::PList params = lfoParams(1.0);
    params.emplace_back(makeSweepParamId(SPT::kSweepLFOSync), 1.0);
    params.emplace_back(makeSweepParamId(SPT::kSweepLFONoteValue), 13.0 / 29.0);  // 1/4

    struct Case {
        const char* name;
        bool hasCtx;
        double tempo;
        double expectedPeriod;
    };
    const double nanTempo = std::bit_cast<double>(UINT64_C(0x7FF8000000000000));
    const Case cases[] = {
        {.name = "120 BPM", .hasCtx = true, .tempo = 120.0, .expectedPeriod = 0.5},
        {.name = "90 BPM", .hasCtx = true, .tempo = 90.0, .expectedPeriod = 60.0 / 90.0},
        {.name = "no ProcessContext (default 120 BPM)", .hasCtx = false, .tempo = 0.0, .expectedPeriod = 0.5},
        {.name = "tempo 0 flagged valid (ignored, 120 BPM)", .hasCtx = true, .tempo = 0.0, .expectedPeriod = 0.5},
        {.name = "tempo -60 flagged valid (ignored, 120 BPM)", .hasCtx = true, .tempo = -60.0, .expectedPeriod = 0.5},
        {.name = "tempo NaN flagged valid (ignored, 120 BPM)", .hasCtx = true, .tempo = nanTempo, .expectedPeriod = 0.5},
    };

    for (const auto& c : cases) {
        ts::Rig rig(kSr, 512);
        Steinberg::Vst::ProcessContext ctx{};
        ctx.sampleRate = kSr;
        ctx.tempo = c.tempo;
        ctx.state = Steinberg::Vst::ProcessContext::kTempoValid | Steinberg::Vst::ProcessContext::kPlaying;
        if (c.hasCtx) rig.ctx = &ctx;
        rig.apply(params);
        const Track tr = renderTrack(rig, in, 512);
        const auto cr = upCrossings(tr);
        const double period = meanPeriod(cr);
        WARN("D-043 " << c.name << ": " << cr.size() << " upward crossings, period " << period
             << " s (expected " << c.expectedPeriod << " s), finite " << tr.allFinite);
        CHECK(tr.allFinite);
        CHECK(cr.size() >= 4);
        CHECK(std::abs(period - c.expectedPeriod) <= c.expectedPeriod * 0.05);
    }
}

// =============================================================================
// T1.11 (D-019, D-037, D-038): the sweep targets the real band centres and
// scales each band's drive, not its level (FR-SWEEP-001 "Focused distortion
// intensity moving through frequency spectrum", FR-SWEEP-002 intensity 0-200 %).
// =============================================================================
namespace {

using Disrumpo::makeNodeParamId;
using NPT = Disrumpo::NodeParamType;

constexpr double kT111Sr = 48000.0;

std::vector<float> sineOf(double hz, double amp, size_t n) {
    std::vector<float> v(n);
    for (size_t i = 0; i < n; ++i)
        v[i] = static_cast<float>(amp * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / kT111Sr));
    return v;
}

// Processor mapping of a crossover parameter (processor_params.cpp): log10-linear 20 Hz..20 kHz.
double xoverNorm(double hz) { return std::log10(hz / 20.0) / 3.0; }

// Processor mapping of kBandCountId: bands = 1 + round(norm * 3).
double bandCountNorm(int bands) { return static_cast<double>(bands - 1) / 3.0; }

/// Band count, crossover frequencies and the drive (normalized, x10) of every node of every band.
ts::PList layoutParams(int bands, const std::vector<double>& xovers, double driveNorm) {
    ts::PList p;
    p.emplace_back(Disrumpo::kBandCountId, bandCountNorm(bands));
    for (size_t i = 0; i < xovers.size(); ++i)
        p.emplace_back(Disrumpo::makeCrossoverParamId(static_cast<uint8_t>(i)), xoverNorm(xovers[i]));
    for (uint8_t b = 0; b < 4; ++b)
        for (uint8_t nd = 0; nd < 4; ++nd)
            p.emplace_back(makeNodeParamId(b, nd, NPT::kNodeDrive), driveNorm);
    return p;
}

/// Sweep on at a centre frequency and an intensity (normalized: 0.5 = 100 %, 1 = 200 %).
ts::PList withSweep(ts::PList p, double centreHz, double intensityNorm) {
    p.emplace_back(makeSweepParamId(SPT::kSweepEnable), 1.0);
    p.emplace_back(makeSweepParamId(SPT::kSweepFrequency), sweepFreqNorm(centreHz));
    p.emplace_back(makeSweepParamId(SPT::kSweepIntensity), intensityNorm);
    return p;
}

/// Render `in` on both channels through a fresh processor configured with `params`; left output.
std::vector<float> renderWith(const ts::PList& params, const std::vector<float>& in) {
    ts::Rig rig(kT111Sr, 512);
    rig.apply(params);
    std::vector<float> oL;
    std::vector<float> oR;
    rig.render(in, in, oL, oR, 512);
    return oL;
}

double tailDb(const std::vector<float>& v, size_t from) {
    return ts::toDb(ts::rmsOf(v.data() + from, v.size() - from));
}

float tailThdPercent(const std::vector<float>& v, double hz) {
    constexpr size_t kN = 8192;
    return Krate::DSP::TestUtils::SignalMetrics::calculateTHD(v.data() + (v.size() - kN), kN, static_cast<float>(hz),
                                               static_cast<float>(kT111Sr));
}

double maxStep(const std::vector<float>& v, size_t from) {
    double m = 0.0;
    for (size_t i = from + 1; i < v.size(); ++i)
        m = std::max(m, std::fabs(static_cast<double>(v[i]) - static_cast<double>(v[i - 1])));
    return m;
}

}  // namespace

// -----------------------------------------------------------------------------
// D-019 / D-038: a band far from the sweep centre keeps its sound (the sweep no
// longer scales its input level); the band under the centre at 200 % is driven
// harder (more harmonics) without following the drive change in level.
// -----------------------------------------------------------------------------
TEST_CASE("Sweep leaves out-of-focus bands passing clean", "[disrumpo][sweep][D-019][D-038]") {
    const auto n = static_cast<size_t>(kT111Sr * 0.5);
    const size_t from = n / 2;

    SECTION("11 kHz band with the sweep centred at 100 Hz, intensity 100 %") {
        const auto in = sineOf(11000.0, 0.25, n);  // -12 dBFS
        const ts::PList base = layoutParams(4, {120.0, 630.0, 3500.0}, 0.3);
        const double off = tailDb(renderWith(base, in), from);
        const double on = tailDb(renderWith(withSweep(base, 100.0, 0.5), in), from);
        WARN("D-038 11 kHz band, sweep at 100 Hz: level off " << off << " dB, on " << on << " dB, delta "
             << (on - off) << " dB");
        CHECK(on >= off - 1.0);
        CHECK(on <= off + 1.0);
    }

    SECTION("band 2 under the sweep centre at 200 % is driven harder") {
        // Crossovers 150 / 750 / 3000 Hz: band 2 spans 750-3000 Hz, geometric centre 1500 Hz.
        // At -30 dBFS: the drive re-stage (user ruling 2026-11-10) runs the shapers 18.06 dB
        // hotter, and at the -12 dBFS this case was written for drive 3 already saturated band 2
        // (THD 18.1 % off, 17.1 % on); 18.06 dB lower the shapers see the drive they saw before
        const auto in = sineOf(1500.0, 0.25 * 0.125, n);
        const ts::PList base = layoutParams(4, {150.0, 750.0, 3000.0}, 0.3);
        const auto offOut = renderWith(base, in);
        const auto onOut = renderWith(withSweep(base, 1500.0, 1.0), in);
        const double offDb = tailDb(offOut, from);
        const double onDb = tailDb(onOut, from);
        const float thdOff = tailThdPercent(offOut, 1500.0);
        const float thdOn = tailThdPercent(onOut, 1500.0);
        WARN("D-019 band 2 at 200 %: THD off " << thdOff << " %, on " << thdOn << " %; level off " << offDb
             << " dB, on " << onDb << " dB");
        CHECK(thdOn > thdOff * 1.1f);
        CHECK(std::abs(onDb - offDb) <= 3.0);
    }
}

// -----------------------------------------------------------------------------
// D-037: the band weighting comes from the actual crossover frequencies (the
// geometric centre of each band's edges, 20 Hz / 20 kHz outside), and the single
// full-range band of 1-band mode is always in focus.
// -----------------------------------------------------------------------------
TEST_CASE("Sweep follows the actual crossover layout", "[disrumpo][sweep][D-037]") {
    const auto n = static_cast<size_t>(kT111Sr * 0.5);
    const size_t from = n / 2;

    SECTION("1 band: sweep on at defaults leaves a clean band unchanged, multiplier ignores the centre") {
        const auto in = sineOf(1000.0, 0.25, n);
        const ts::PList base = layoutParams(1, {}, 0.0);
        const double off = tailDb(renderWith(base, in), from);
        ts::PList on = base;
        on.emplace_back(makeSweepParamId(SPT::kSweepEnable), 1.0);  // defaults: 1 kHz, 1.5 oct, 50 %
        const double onDb = tailDb(renderWith(on, in), from);
        WARN("D-037 1 band, drive 0, sweep defaults: level off " << off << " dB, on " << onDb << " dB");
        CHECK(std::abs(onDb - off) <= 0.1);

        for (const double centre : {100.0, 1000.0, 10000.0}) {
            ts::Rig rig(kT111Sr, 512);
            rig.apply(withSweep(base, centre, 0.25));  // 50 %
            std::vector<float> oL;
            std::vector<float> oR;
            rig.render(in, in, oL, oR, 512);
            const float m = rig.p->getBandProcessor(0).getSweepDriveMultiplier();
            WARN("D-037 1 band, centre " << centre << " Hz, 50 %: drive multiplier " << m);
            CHECK(std::abs(m - 1.25f) <= 1e-5f);
        }
    }

    SECTION("4 bands: the focused band moves with the crossovers") {
        struct Layout {
            const char* name;
            std::vector<double> xovers;
            double centreHz;
            int expectedMaxBand;
        };
        const Layout layouts[] = {
            // Centres 49 / 275 / 1485 / 8367 Hz: 400 Hz is nearest band 1.
            {.name = "120/630/3500 Hz, sweep 400 Hz", .xovers = {120.0, 630.0, 3500.0}, .centreHz = 400.0,
             .expectedMaxBand = 1},
            // Crossovers moved up: centres 141 / 1414 / 2828 / 8944 Hz: 400 Hz is now nearest band 0.
            {.name = "1000/2000/4000 Hz, sweep 400 Hz", .xovers = {1000.0, 2000.0, 4000.0}, .centreHz = 400.0,
             .expectedMaxBand = 0},
            // Fix plan case: band 2 = 1500-6000 Hz, geometric centre 3000 Hz.
            {.name = "200/1500/6000 Hz, sweep 3000 Hz", .xovers = {200.0, 1500.0, 6000.0}, .centreHz = 3000.0,
             .expectedMaxBand = 2},
            // Band 2 = 600-1200 Hz (centre 849 Hz); the old fixed table put band 2 at 3 kHz.
            {.name = "300/600/1200 Hz, sweep 849 Hz", .xovers = {300.0, 600.0, 1200.0}, .centreHz = 848.5,
             .expectedMaxBand = 2},
        };
        const std::vector<float> silence(static_cast<size_t>(kT111Sr * 0.3), 0.0f);
        for (const auto& l : layouts) {
            ts::Rig rig(kT111Sr, 512);
            rig.apply(withSweep(layoutParams(4, l.xovers, 0.3), l.centreHz, 0.5));  // 100 %
            std::vector<float> oL;
            std::vector<float> oR;
            rig.render(silence, silence, oL, oR, 512);
            std::array<float, 4> m{};
            int maxBand = 0;
            for (int b = 0; b < 4; ++b) {
                m[static_cast<size_t>(b)] = rig.p->getBandProcessor(b).getSweepDriveMultiplier();
                if (m[static_cast<size_t>(b)] > m[static_cast<size_t>(maxBand)]) maxBand = b;
            }
            WARN("D-037 " << l.name << ": drive multipliers " << m[0] << " / " << m[1] << " / " << m[2] << " / "
                 << m[3] << " -> max band " << maxBand);
            CHECK(maxBand == l.expectedMaxBand);
        }
    }
}

// -----------------------------------------------------------------------------
// D-019: intensity scales drive. A clean (drive 0) band does not change at any
// intensity; a saturating band at intensity I sounds exactly like the band with
// its drive set to drive * (1 + I / 2) and the sweep off: 0 % leaves the drive as
// set, 200 % doubles it at the centre, and the level does not follow the drive.
// -----------------------------------------------------------------------------
TEST_CASE("Sweep intensity scales drive and not level", "[disrumpo][sweep][D-019]") {
    const auto n = static_cast<size_t>(kT111Sr * 0.5);
    const size_t from = n / 2;
    const auto in = sineOf(1500.0, 0.25, n);  // -12 dBFS, bin 256 of the 8192-point THD FFT
    const double intensities[] = {0.0, 0.25, 0.5, 0.75, 1.0};  // 0, 50, 100, 150, 200 %

    SECTION("clean band: level unchanged at every intensity") {
        const ts::PList base = layoutParams(1, {}, 0.0);
        const double off = tailDb(renderWith(base, in), from);
        for (const double i : intensities) {
            const double on = tailDb(renderWith(withSweep(base, 1500.0, i), in), from);
            WARN("D-019 clean band, intensity " << (i * 200.0) << " %: level " << on << " dB (off " << off << ")");
            CHECK(std::abs(on - off) <= 0.01);
        }
    }

    SECTION("saturating band: intensity I equals drive x (1 + I / 2); THD rises, level does not follow") {
        // At -30 dBFS: the drive re-stage (user ruling 2026-11-10) runs the shapers 18.06 dB
        // hotter, and at -12 dBFS drive 3 already saturated the band (THD 28.0 % off, 26.2 % at
        // 200 %); 18.06 dB lower the shaper sees the drive this case was written for
        const auto in = sineOf(1500.0, 0.25 * 0.125, n);
        constexpr double kDriveNorm = 0.3;  // drive 3
        const ts::PList base = layoutParams(1, {}, kDriveNorm);
        const auto offOut = renderWith(base, in);
        const double offDb = tailDb(offOut, from);
        const float offThd = tailThdPercent(offOut, 1500.0);
        WARN("D-019 saturating band (drive 3), sweep off: THD " << offThd << " %, level " << offDb << " dB");
        double lastDb = 0.0;
        float lastThd = 0.0f;
        for (const double i : intensities) {
            ts::Rig rig(kT111Sr, 512);
            rig.apply(withSweep(base, 1500.0, i));
            std::vector<float> oL;
            std::vector<float> oR;
            rig.render(in, in, oL, oR, 512);
            const float m = rig.p->getBandProcessor(0).getSweepDriveMultiplier();
            const double db = tailDb(oL, from);
            const float thd = tailThdPercent(oL, 1500.0);
            // The same band with its drive raised by hand and the sweep off
            const auto refOut = renderWith(layoutParams(1, {}, kDriveNorm * (1.0 + i)), in);
            double maxDiffVsOff = 0.0;
            for (size_t k = 0; k < n; ++k)
                maxDiffVsOff = std::max(maxDiffVsOff, std::fabs(static_cast<double>(oL[k]) - offOut[k]));
            double maxDiffVsRef = 0.0;
            for (size_t k = from; k < n; ++k)
                maxDiffVsRef = std::max(maxDiffVsRef, std::fabs(static_cast<double>(oL[k]) - refOut[k]));
            WARN("D-019 intensity " << (i * 200.0) << " %: drive multiplier " << m << ", THD " << thd
                 << " % (drive " << (kDriveNorm * 10.0 * (1.0 + i)) << " sweep off: "
                 << tailThdPercent(refOut, 1500.0) << " %), level " << db << " dB; max |diff| vs sweep off "
                 << maxDiffVsOff << ", vs drive-raised reference " << maxDiffVsRef);
            CHECK(std::abs(m - static_cast<float>(1.0 + i)) <= 1e-5f);  // 1 + intensity / 2
            CHECK(maxDiffVsRef <= 1e-4);
            if (i == 0.0) {
                CHECK(maxDiffVsOff <= 1e-6);  // 0 %: drive exactly as set
            }
            lastDb = db;
            lastThd = thd;
        }
        // 200 % doubles the drive (+6.02 dB at the shaper input); the saturated output moves far less.
        WARN("D-019 0 % -> 200 %: THD " << offThd << " -> " << lastThd << " %, level change "
             << (lastDb - offDb) << " dB for a +6.02 dB drive change");
        CHECK(lastThd > offThd * 1.1f);
        CHECK(lastDb - offDb < 3.0);
    }
}

// -----------------------------------------------------------------------------
// D-019: the per-band drive multiplier is smoothed, so a sweep centre moving
// across the band edges does not click.
// -----------------------------------------------------------------------------
TEST_CASE("Sweep crossing band edges is click-free", "[disrumpo][sweep][D-019]") {
    const auto n = static_cast<size_t>(kT111Sr * 3.0);
    const size_t from = static_cast<size_t>(kT111Sr * 0.2);
    std::vector<float> in(n);
    for (size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / kT111Sr;
        in[i] = static_cast<float>(0.125 * (std::sin(2.0 * std::numbers::pi * 200.0 * t) +
                                            std::sin(2.0 * std::numbers::pi * 1000.0 * t) +
                                            std::sin(2.0 * std::numbers::pi * 5000.0 * t)));
    }
    const std::vector<double> xovers = {120.0, 630.0, 3500.0};

    // Static references: every band at drive 3 (sweep off) and at drive 6 (the 200 % peak).
    const double stepLow = maxStep(renderWith(layoutParams(4, xovers, 0.3), in), from);
    const double stepHigh = maxStep(renderWith(layoutParams(4, xovers, 0.6), in), from);

    // Sweep 200 %, narrowest width, a 1 Hz +/-2 octave LFO around 1 kHz: the centre
    // crosses the 630 Hz and 3500 Hz edges four times a second.
    ts::PList p = withSweep(layoutParams(4, xovers, 0.3), 1000.0, 1.0);
    p.emplace_back(makeSweepParamId(SPT::kSweepWidth), 0.0);
    p.emplace_back(makeSweepParamId(SPT::kSweepLFOEnable), 1.0);
    p.emplace_back(makeSweepParamId(SPT::kSweepLFODepth), 1.0);
    p.emplace_back(makeSweepParamId(SPT::kSweepLFOWaveform), 0.0);
    p.emplace_back(makeSweepParamId(SPT::kSweepLFORate), lfoRateNorm(1.0));
    const auto out = renderWith(p, in);

    Krate::DSP::TestUtils::ClickDetectorConfig cfg;
    cfg.sampleRate = static_cast<float>(kT111Sr);
    cfg.frameSize = 512;
    cfg.hopSize = 256;
    cfg.detectionThreshold = 6.0f;
    cfg.energyThresholdDb = -50.0f;
    Krate::DSP::TestUtils::ClickDetector det(cfg);
    det.prepare();
    const auto clicks = det.detect(out.data() + from, out.size() - from);
    const double step = maxStep(out, from);
    bool finite = true;
    for (float s : out) if (!Krate::DSP::detail::isFinite(s)) finite = false;
    WARN("D-019 sweep across band edges: clicks " << clicks.size() << ", max sample-to-sample jump " << step
         << " (static drive 3: " << stepLow << ", drive 6: " << stepHigh << ")");
    CHECK(finite);
    CHECK(clicks.empty());
    CHECK(step <= std::max(stepLow, stepHigh) * 1.05);
}

// -----------------------------------------------------------------------------
// D-137 / D-039 (T1.12): the per-band sweep -> morph link runs in the processor,
// so it works with no controller (offline render, editor closed) and follows the
// sweep LFO, not only the base Sweep Frequency parameter.
// -----------------------------------------------------------------------------
TEST_CASE("Band morph X link follows the processor sweep without a controller",
          "[disrumpo][sweep-link][D-137]") {
    using Disrumpo::BandParamType;
    using Disrumpo::NodeParamType;
    constexpr double kSr = 48000.0;
    constexpr int32 kBlock = 512;
    const auto sine = [](size_t n) {
        std::vector<float> v(n);
        for (size_t i = 0; i < n; ++i)
            v[i] = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * 220.0 * static_cast<double>(i) / kSr));
        return v;
    };
    // 1 band, cross-family nodes SoftClip + Bitcrush, X linked to the sweep (Sweep Freq = 1/7)
    const ts::PList setup = {
        {Disrumpo::makeGlobalParamId(Disrumpo::GlobalParamType::kGlobalBandCount), 0.0},
        {Disrumpo::makeBandParamId(0, BandParamType::kBandActiveNodes), 1.0 / 3.0},  // 2 nodes
        {Disrumpo::makeNodeParamId(0, 0, NodeParamType::kNodeType), 0.0 / 25.0},   // SoftClip
        {Disrumpo::makeNodeParamId(0, 1, NodeParamType::kNodeType), 11.0 / 25.0},  // Bitcrush
        {Disrumpo::makeNodeParamId(0, 0, NodeParamType::kNodeDrive), 0.4},
        {Disrumpo::makeNodeParamId(0, 1, NodeParamType::kNodeDrive), 0.4},
        {Disrumpo::makeBandParamId(0, BandParamType::kBandMorphXLink), 1.0 / 7.0},
        {makeSweepParamId(SPT::kSweepEnable), 1.0},
    };
    ts::Rig rig(kSr, kBlock);
    rig.apply(setup);

    SECTION("base sweep frequency 20 Hz then 20 kHz moves morph X from 0 to 1") {
        const auto in = sine(static_cast<size_t>(kSr * 0.5));
        std::vector<float> lowL;
        std::vector<float> highL;
        std::vector<float> oR;
        rig.apply({{makeSweepParamId(SPT::kSweepFrequency), 0.0}});
        rig.render(in, in, lowL, oR, kBlock);
        const float xLow = rig.p->debugMorphPositionX(0);
        rig.apply({{makeSweepParamId(SPT::kSweepFrequency), 1.0}});
        rig.render(in, in, highL, oR, kBlock);
        const float xHigh = rig.p->debugMorphPositionX(0);
        const size_t from = in.size() / 2;
        double diff = 0.0;
        for (size_t i = from; i < in.size(); ++i) {
            const double d = static_cast<double>(highL[i]) - lowL[i];
            diff += d * d;
        }
        const double diffDb = ts::toDb(std::sqrt(diff / static_cast<double>(in.size() - from)));
        const double lowDb = ts::toDb(ts::rmsOf(lowL.data() + from, in.size() - from));
        WARN("D-137 morph X after 0.5 s at sweep 20 Hz: " << xLow << ", at 20 kHz: " << xHigh
             << "; RMS of (20 kHz render - 20 Hz render) " << diffDb << " dB vs 20 Hz render " << lowDb << " dB");
        CHECK(xLow < 0.02f);
        CHECK(xHigh > 0.98f);
        // The X move is audible: the residual between the renders is within 30 dB
        // of the signal (without the link the two renders are identical).
        CHECK(diffDb > lowDb - 30.0);
    }

    SECTION("sweep LFO at 2 Hz moves morph X; the link is evaluated once per block") {
        rig.apply({{makeSweepParamId(SPT::kSweepFrequency), 0.5},
                   {makeSweepParamId(SPT::kSweepLFOEnable), 1.0},
                   {makeSweepParamId(SPT::kSweepLFODepth), 1.0},
                   {makeSweepParamId(SPT::kSweepLFOWaveform), 0.0},
                   {makeSweepParamId(SPT::kSweepLFORate), lfoRateNorm(2.0)}});
        const auto in = sine(static_cast<size_t>(kSr * 2.0));
        std::vector<float> xs;
        std::vector<float> oL;
        std::vector<float> oR;
        rig.render(in, in, oL, oR, kBlock, {},
                   [&](size_t, ts::CaptureChanges&) { xs.push_back(rig.p->debugMorphPositionX(0)); });
        double mean = 0.0;
        for (float x : xs) mean += x;
        mean /= static_cast<double>(xs.size());
        double var = 0.0;
        double maxJump = 0.0;
        for (size_t i = 0; i < xs.size(); ++i) {
            var += (xs[i] - mean) * (xs[i] - mean);
            if (i > 0) maxJump = std::max(maxJump, static_cast<double>(std::fabs(xs[i] - xs[i - 1])));
        }
        var /= static_cast<double>(xs.size());
        const auto [mn, mx] = std::minmax_element(xs.begin(), xs.end());
        WARN("D-137 2 Hz sweep LFO, depth 1: morph X over " << xs.size() << " blocks of " << kBlock
             << ": min " << *mn << " max " << *mx << " variance " << var << ", max per-block jump " << maxJump);
        CHECK(var > 0.01);
        CHECK(maxJump < 0.05);  // 2 Hz, +/-0.2 of X: 2*pi*2*0.2*512/48000 = 0.027 per block
    }
}

// Measurement probe (hidden): real-time ratio of Processor::process with the
// sweep, its LFO and its envelope follower all on (default bands).
TEST_CASE("Probe: Processor render cost with sweep LFO and envelope on", "[.][t110-sweep-cpu-probe]") {
    constexpr double kSr = 48000.0;
    std::vector<float> in(static_cast<size_t>(kSr * 10.0));
    for (size_t i = 0; i < in.size(); ++i)
        in[i] = static_cast<float>(0.25 * std::sin(2.0 * std::numbers::pi * 220.0 * static_cast<double>(i) / kSr));
    ts::Rig rig(kSr);
    ts::PList p = lfoParams(5.0);
    p.emplace_back(makeSweepParamId(SPT::kSweepEnvEnable), 1.0);
    p.emplace_back(makeSweepParamId(SPT::kSweepEnvSensitivity), 1.0);
    rig.apply(p);
    std::vector<float> outL;
    std::vector<float> outR;
    const auto t0 = std::chrono::steady_clock::now();
    rig.render(in, in, outL, outR);
    const auto t1 = std::chrono::steady_clock::now();
    const double secs = std::chrono::duration<double>(t1 - t0).count();
    WARN("PROBE sweep cpu: " << secs << " s for 10 s of audio = " << (secs / 10.0 * 100.0) << " % of real time");
}
