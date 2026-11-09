// =============================================================================
// Parameter automation through the Processor (fix plan T6.07)
// =============================================================================
// Promoted from the audit probes "automation steps" and "sweep system" (sweep:
// full-automation) in specs/disrumpo-audit/probes/audit_probes.cpp.txt. 220 Hz at
// -12 dBFS, 48 kHz, 512-sample blocks; each parameter steps at a block boundary and
// the jump ratio (largest sample step in the 50 ms from the step, the step into its first
// sample included, against the steady windows before and after it) must stay <= 1.3.
// The probe started its event window on the step's first sample, so a one-sample step
// at the block boundary fell outside it.
// Covered elsewhere with the plan's bar or a stronger one (cited in report.md):
// band count 2 -> 4 -> 1 and the node type switch (integration/processor_audio_output_
// test.cpp, T2.08 / T2.13), bypass ("Band bypass passes the crossover band unchanged"),
// the sweep LFO range and rate, envelope attack, off-centre band level and intensity
// 200 % (integration/processor_sweep_test.cpp).
// Audit baseline (2026-11-05): every step here 0.92-1.00; full automation jump ratio
// 0.48 against the sweep off, RMS -20.98 dB on vs -11.99 dB off (-9 dB).
// Held back for a ruling (follow-up 85, f:/tmp/disrumpo-fix/p6b/t607-held-jump.patch):
// the full automation's jump ratio against the sweep off (1.63; the sweep raises the
// drive of the bands near its centre since D-019).
// =============================================================================

#include "../test_support/disrumpo_rig.h"

#include "dsp/distortion_types.h"
#include "dsp/sweep_morph_link.h"
#include "plugin_ids.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <numbers>
#include <utility>
#include <vector>

using namespace Disrumpo;
using namespace Disrumpo::TestSupport;

namespace {

constexpr double kSr = 48000.0;
constexpr size_t kBlock = 512;

std::vector<float> sineAt(size_t n, double hz, double dbfs) {
    const double a = std::pow(10.0, dbfs / 20.0);
    std::vector<float> x(n);
    for (size_t i = 0; i < n; ++i) {
        x[i] = static_cast<float>(a * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / kSr));
    }
    return x;
}

/// `tenths` x 0.1 s, down to its block boundary (the probe's step times).
size_t stepAt(size_t tenths) {
    return (static_cast<size_t>(kSr * 0.1 * static_cast<double>(tenths)) / kBlock) * kBlock;
}

PList allBands(NodeParamType t, double v) {
    PList l;
    for (uint8_t b = 0; b < 4; ++b) l.emplace_back(makeNodeParamId(b, 0, t), v);
    return l;
}

PList concat(PList a, const PList& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

struct Step {
    size_t tenths;
    PList changes;
};

struct StepCase {
    const char* name;
    PList setup;
    std::vector<Step> steps;
    double seconds;
};

double typeNorm(DistortionType t) { return static_cast<double>(static_cast<int>(t)) / 25.0; }

double sweepFreqNorm(double hz) {
    return (std::log2(hz) - static_cast<double>(kLog2MinFreq)) / static_cast<double>(kLog2FreqRange);
}

// Processor mapping of kSweepLFORate: ln-linear 0.01..20 Hz.
double lfoRateNorm(double hz) { return (std::log(hz) - std::log(0.01)) / (std::log(20.0) - std::log(0.01)); }

}  // namespace

TEST_CASE("Parameter steps during playback keep the jump ratio at most 1.3", "[disrumpo][audit][automation]") {
    const ParamID band0Gain = makeBandParamId(0, BandParamType::kBandGain);
    const ParamID band0Mute = makeBandParamId(0, BandParamType::kBandMute);
    const ParamID band1Solo = makeBandParamId(1, BandParamType::kBandSolo);
    const StepCase cases[] = {
        {.name = "Drive 0 -> 10 on every band",
         .setup = allBands(NodeParamType::kNodeDrive, 0.0),
         .steps = {{.tenths = 5, .changes = allBands(NodeParamType::kNodeDrive, 1.0)}},
         .seconds = 1.0},
        {.name = "Global Mix 0 -> 100 % (drive 5)",
         .setup = concat(allBands(NodeParamType::kNodeDrive, 0.5), {{kGlobalMixId, 0.0}}),
         .steps = {{.tenths = 5, .changes = {{kGlobalMixId, 1.0}}}},
         .seconds = 1.0},
        {.name = "Node Mix 0 -> 100 % (drive 5)",
         .setup = concat(allBands(NodeParamType::kNodeDrive, 0.5), allBands(NodeParamType::kNodeMix, 0.0)),
         .steps = {{.tenths = 5, .changes = allBands(NodeParamType::kNodeMix, 1.0)}},
         .seconds = 1.0},
        {.name = "Band Gain -24 -> +24 dB (1 band)",
         .setup = {{kBandCountId, 0.0}, {band0Gain, 0.0}},
         .steps = {{.tenths = 5, .changes = {{band0Gain, 1.0}}}},
         .seconds = 1.0},
        {.name = "Band 0 Mute on then off",
         .setup = {},
         .steps = {{.tenths = 5, .changes = {{band0Mute, 1.0}}}, {.tenths = 10, .changes = {{band0Mute, 0.0}}}},
         .seconds = 1.5},
        {.name = "Band 1 Solo on then off",
         .setup = {},
         .steps = {{.tenths = 5, .changes = {{band1Solo, 1.0}}}, {.tenths = 10, .changes = {{band1Solo, 0.0}}}},
         .seconds = 1.5},
        {.name = "Oversample Max 1x -> 8x (HardClip drive 5)",
         .setup = concat(concat(allBands(NodeParamType::kNodeType, typeNorm(DistortionType::HardClip)),
                                allBands(NodeParamType::kNodeDrive, 0.5)),
                         {{kOversampleMaxId, 0.0}}),
         .steps = {{.tenths = 5, .changes = {{kOversampleMaxId, 1.0}}}},
         .seconds = 1.0},
    };

    for (const auto& c : cases) {
        const auto in = sineAt(static_cast<size_t>(kSr * c.seconds), 220.0, -12.0);
        Rig r(kSr);
        r.apply(c.setup);
        r.reactivate();
        std::vector<size_t> at;
        for (const auto& s : c.steps) at.push_back(stepAt(s.tenths));
        std::vector<float> outL;
        std::vector<float> outR;
        r.render(in, in, outL, outR, static_cast<int32>(kBlock), [&](size_t, size_t start, PChanges& pc) {
            for (size_t k = 0; k < c.steps.size(); ++k) {
                if (start != at[k]) continue;
                for (const auto& [id, v] : c.steps[k].changes) pc.addChange(id, v);
            }
        });
        const auto cont = stepContinuity(outL, at, kSr, 1);
        for (size_t k = 0; k < cont.size(); ++k) {
            std::printf("T6.07 %s, step %zu at %.3f s: jump ratio %.2f, clicks %zu (steady %zu), non-finite %zu\n",
                        c.name, k + 1, static_cast<double>(at[k]) / kSr, cont[k].ratio, cont[k].clicksEvent,
                        cont[k].clicksBase, cont[k].nonFinite);
            INFO(c.name << ", step " << k + 1 << ": jump ratio " << cont[k].ratio);
            CHECK(cont[k].nonFinite == 0);
            CHECK(cont[k].ratio <= 1.3);
        }
    }
}

namespace {

/// Four sines at -18 dBFS each (150 / 700 / 3200 / 11000 Hz), the probe's sweep input.
std::vector<float> fourSines(size_t len) {
    std::vector<float> in(len, 0.0f);
    for (const double f : {150.0, 700.0, 3200.0, 11000.0}) {
        const auto s = sineAt(len, f, -18.0);
        for (size_t i = 0; i < len; ++i) in[i] += s[i];
    }
    return in;
}

/// Every band SoftClip at node drive `driveNorm` (normalized).
PList fourBands(double driveNorm) { return allBands(NodeParamType::kNodeDrive, driveNorm); }

/// Sweep on at 100 %, LFO 5 Hz depth 1, envelope on (sensitivity 1), frequency 100 Hz.
PList withFullSweep(const PList& base) {
    return concat(base, {{makeSweepParamId(SweepParamType::kSweepEnable), 1.0},
                         {makeSweepParamId(SweepParamType::kSweepIntensity), 0.5},
                         {makeSweepParamId(SweepParamType::kSweepLFOEnable), 1.0},
                         {makeSweepParamId(SweepParamType::kSweepLFORate), lfoRateNorm(5.0)},
                         {makeSweepParamId(SweepParamType::kSweepLFODepth), 1.0},
                         {makeSweepParamId(SweepParamType::kSweepEnvEnable), 1.0},
                         {makeSweepParamId(SweepParamType::kSweepEnvSensitivity), 1.0},
                         {makeSweepParamId(SweepParamType::kSweepFrequency), sweepFreqNorm(100.0)}});
}

/// Left output for the four sines through `params`; with `automate` the sweep frequency
/// moves 100 Hz -> 10 kHz over 2 s at block rate.
std::vector<float> renderFourSines(const PList& params, bool automate, size_t len, size_t* nonFinite = nullptr) {
    const auto in = fourSines(len);
    Rig r(kSr);
    r.apply(params);
    r.reactivate();
    std::vector<float> outL;
    std::vector<float> outR;
    r.render(in, in, outL, outR, static_cast<int32>(kBlock), [&](size_t, size_t start, PChanges& pc) {
        if (!automate) return;
        const double t = static_cast<double>(start) / kSr;
        pc.addChange(makeSweepParamId(SweepParamType::kSweepFrequency),
                     sweepFreqNorm(100.0 * std::pow(100.0, std::min(t / 2.0, 1.0))));
    });
    if (nonFinite) {
        *nonFinite = 0;
        for (const float v : outL) *nonFinite += fin(v) ? 0 : 1;
        for (const float v : outR) *nonFinite += fin(v) ? 0 : 1;
    }
    return outL;
}

}  // namespace

TEST_CASE("Full sweep automation stays finite and keeps the level", "[disrumpo][audit][automation][sweep]") {
    // 4 bands SoftClip drive 3, sweep on at 100 % automated 100 Hz -> 10 kHz over 2 s,
    // LFO 5 Hz depth 1, envelope on; reference: the same bands with the sweep off.
    const size_t len = static_cast<size_t>(kSr * 2.0);
    size_t nonFinite = 0;
    const auto on = renderFourSines(withFullSweep(fourBands(0.3)), true, len, &nonFinite);
    const auto off = renderFourSines(fourBands(0.3), false, len);
    const double rmsOnDb = toDb(rmsOf(on.data(), len));
    const double rmsOffDb = toDb(rmsOf(off.data(), len));
    const double ratio = maxJump(on.data(), len) / std::max(1e-12, maxJump(off.data(), len));
    std::printf("T6.07 full sweep automation: non-finite %zu, jump ratio vs sweep off %.2f, RMS on %.2f dB, off %.2f dB, "
                "delta %.2f dB\n",
                nonFinite, ratio, rmsOnDb, rmsOffDb, rmsOnDb - rmsOffDb);
    INFO("RMS on " << rmsOnDb << " dB, off " << rmsOffDb << " dB");
    CHECK(nonFinite == 0);
    CHECK(std::fabs(rmsOnDb - rmsOffDb) <= 3.0);
}

// Measurement probe (hidden) for the held jump-ratio bar of the full automation: the
// largest sample step with the sweep on against static renders with every band's drive
// raised by hand to what the sweep reaches (x 1.5 at 100 % on the centre, x 2 at 200 %),
// and the clicks the shared detector finds.
TEST_CASE("Probe: T6.07 full automation jump reference", "[.][p6b-auto]") {
    const size_t len = static_cast<size_t>(kSr * 2.0);
    const auto on = renderFourSines(withFullSweep(fourBands(0.3)), true, len);
    const double stepOn = maxJump(on.data(), len);
    std::printf("sweep on: max step %.4f, clicks %zu\n", stepOn, clickCount(on.data(), len, kSr));
    for (const double drive : {0.3, 0.45, 0.6}) {
        const auto ref = renderFourSines(fourBands(drive), false, len);
        std::printf("sweep off, drive %.1f: max step %.4f (on / this %.2f), clicks %zu\n", drive * 10.0,
                    maxJump(ref.data(), len), stepOn / maxJump(ref.data(), len), clickCount(ref.data(), len, kSr));
    }
    size_t at = 1;
    double m = 0.0;
    for (size_t i = 1; i < len; ++i) {
        const double j = std::fabs(static_cast<double>(on[i]) - static_cast<double>(on[i - 1]));
        if (j > m) {
            m = j;
            at = i;
        }
    }
    std::printf("largest step %.4f at %.4f s (block offset %zu)\n", m, static_cast<double>(at) / kSr, at % kBlock);
}
