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

#include <krate/dsp/core/db_utils.h>

#include <algorithm>
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
    constexpr double kMinRateLog = -4.6052;  // ln(0.01), as the processor rounds it
    constexpr double kMaxRateLog = 2.9957;   // ln(20)
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
