// =============================================================================
// Parameter automation through the Processor (fix plan T6.07)
// =============================================================================
// Promoted from the audit probe "automation steps" in
// specs/disrumpo-audit/probes/audit_probes.cpp.txt. 220 Hz at
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
// Audit baseline (2026-11-05): every step here 0.92-1.00.
// =============================================================================

#include "../test_support/disrumpo_rig.h"

#include "dsp/distortion_types.h"
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
