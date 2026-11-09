// =============================================================================
// Morph continuity and on-node behaviour through the Processor (fix plan T6.06)
// =============================================================================
// Promoted from the audit probes "morph continuity" and "morph on-node and
// smoothing" (specs/disrumpo-audit/probes/audit_probes.cpp.txt, morphSweep and the
// on-node loop). One band, 220 Hz at -12 dBFS, 48 kHz, 512-sample blocks, the cursor
// automated once per block:
// - a 2 s cursor sweep: jump ratio <= 1.3 (largest sample step during the sweep
//   against the steady windows before and after it) and no click;
// - the cursor parked on or next to a node: weights finite and summing to 1, level
//   within 0.5 dB of that node alone.
// Audit baseline (2026-11-05): 2-node SoftClip-SineFold jump ratio 1.83 / 1 click;
// 4-node planar 9.90 / 54 clicks; same-family SoftClip-Tube 1.00.
// Held back for a ruling (follow-ups 6, 12 and 84, patch f:/tmp/disrumpo-fix/p6b/
// t606-held-bars.patch): the plan's level bar (every 20 ms window within 1 dB of the
// straight dB line) on both cross-family sweeps and the 4-node sweep's click bar.
// The engine-level counterparts (MorphEngine without the band, oversampling and
// crossover around it) are in unit/morph_transition_test.cpp; the smoothing time and
// the stale-transition-gain cases of the same probes are asserted there and in
// unit/morph_band_integration_test.cpp (see report.md's probe baseline table).
// =============================================================================

#include "../test_support/disrumpo_rig.h"

#include "dsp/distortion_types.h"
#include "plugin_ids.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
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

std::vector<float> sine220AtMinus12(size_t n) {
    const double a = std::pow(10.0, -12.0 / 20.0);
    std::vector<float> x(n);
    for (size_t i = 0; i < n; ++i) {
        x[i] = static_cast<float>(a * std::sin(2.0 * std::numbers::pi * 220.0 * static_cast<double>(i) / kSr));
    }
    return x;
}

double typeNorm(DistortionType t) { return static_cast<double>(static_cast<int>(t)) / 25.0; }

ParamID nodeId(int node, NodeParamType t) { return makeNodeParamId(0, static_cast<uint8_t>(node), t); }

ParamID bandId(BandParamType t) { return makeBandParamId(0, t); }

/// One band, `types.size()` nodes at drive 3 (normalized 0.3), morph mode `modeNorm`.
PList morphSetup(const std::vector<DistortionType>& types, double modeNorm) {
    PList l = {{kBandCountId, 0.0},
               {bandId(BandParamType::kBandActiveNodes), static_cast<double>(types.size() - 1) / 3.0},
               {bandId(BandParamType::kBandMorphMode), modeNorm}};
    for (size_t i = 0; i < types.size(); ++i) {
        l.emplace_back(nodeId(static_cast<int>(i), NodeParamType::kNodeType), typeNorm(types[i]));
        l.emplace_back(nodeId(static_cast<int>(i), NodeParamType::kNodeDrive), 0.3);
    }
    return l;
}

constexpr double kLinear1D = 0.0;
constexpr double kPlanar2D = 0.5;

const std::vector<DistortionType> kFourCrossFamily = {DistortionType::SoftClip, DistortionType::SineFold,
                                                      DistortionType::FullRectify, DistortionType::RingSaturation};

struct SweepReading {
    double ratio = 0.0;
    size_t clicks = 0;
    size_t nonFinite = 0;
    double startDb = 0.0;
    double endDb = 0.0;
    double maxDevDb = 0.0;  ///< signed, the largest |deviation| of a 20 ms window from the straight dB line
    std::vector<std::pair<double, int>> factorChanges;  ///< (time s, band factor) at each change
};

/// The probe's sweep: 0.5 s at (0, 0), the cursor from 0 to 1 over 2 s (x, or x and y
/// when `diagonal`), 0.5 s at the end.
SweepReading sweep(const PList& setup, bool diagonal) {
    const auto S = [](double s) { return static_cast<size_t>(s * kSr); };
    const auto in = sine220AtMinus12(S(3.0));
    Rig r(kSr);
    r.apply(setup);
    r.apply({{bandId(BandParamType::kBandMorphX), 0.0}, {bandId(BandParamType::kBandMorphY), 0.0}});
    r.reactivate();
    SweepReading res;
    std::vector<float> outL;
    std::vector<float> outR;
    r.render(in, in, outL, outR, 512, [&](size_t, size_t start, PChanges& pc) {
        const double t = static_cast<double>(start) / kSr;
        double x = 0.0;
        if (t >= 0.5 && t < 2.5) x = (t - 0.5) / 2.0;
        else if (t >= 2.5) x = 1.0;
        pc.addChange(bandId(BandParamType::kBandMorphX), x);
        if (diagonal) pc.addChange(bandId(BandParamType::kBandMorphY), x);
        const int factor = r.p->getBandProcessor(0).getActiveOversampleFactor();
        if (res.factorChanges.empty() || res.factorChanges.back().second != factor) res.factorChanges.emplace_back(t, factor);
    });
    const auto c = continuityAround(outL, S(0.5), S(2.5), S(0.1), S(0.5), S(2.6), S(3.0), kSr);
    res.ratio = c.ratio;
    res.clicks = c.clicksEvent;
    res.nonFinite = c.nonFinite;
    res.startDb = toDb(rmsOf(outL.data() + S(0.1), S(0.4)));
    res.endDb = toDb(rmsOf(outL.data() + S(2.6), S(0.4)));
    const size_t win = S(0.02);
    for (size_t s = S(0.5); s + win <= S(2.5); s += win) {
        const double frac = (static_cast<double>(s) + 0.5 * static_cast<double>(win) - static_cast<double>(S(0.5))) / static_cast<double>(S(2.0));
        const double d = toDb(rmsOf(outL.data() + s, win)) - (res.startDb + (res.endDb - res.startDb) * frac);
        if (std::fabs(d) > std::fabs(res.maxDevDb)) res.maxDevDb = d;
    }
    return res;
}

void report(const char* label, const SweepReading& s) {
    std::printf("T6.06 %s: jump ratio %.2f, clicks %zu, non-finite %zu, start %.2f dB, end %.2f dB, "
                "max level deviation %.2f dB, band factor",
                label, s.ratio, s.clicks, s.nonFinite, s.startDb, s.endDb, s.maxDevDb);
    for (const auto& [t, f] : s.factorChanges) std::printf(" %dx@%.3fs", f, t);
    std::printf("\n");
}

}  // namespace

TEST_CASE("Morph sweep through the Processor: 2 cross-family nodes", "[disrumpo][audit][morph][D-139]") {
    const auto s = sweep(morphSetup({DistortionType::SoftClip, DistortionType::SineFold}, kLinear1D), false);
    report("2-node SoftClip-SineFold", s);
    INFO("jump ratio " << s.ratio << ", clicks " << s.clicks);
    CHECK(s.nonFinite == 0);
    CHECK(s.ratio <= 1.3);
    CHECK(s.clicks == 0);
}

TEST_CASE("Morph sweep through the Processor: 2 same-family nodes", "[disrumpo][audit][morph]") {
    const auto s = sweep(morphSetup({DistortionType::SoftClip, DistortionType::Tube}, kLinear1D), false);
    report("2-node SoftClip-Tube", s);
    INFO("jump ratio " << s.ratio);
    CHECK(s.nonFinite == 0);
    CHECK(s.ratio <= 1.3);
}

TEST_CASE("Morph sweep through the Processor: 4 cross-family nodes on the planar diagonal",
          "[disrumpo][audit][morph][D-139]") {
    const auto s = sweep(morphSetup(kFourCrossFamily, kPlanar2D), true);
    report("4-node planar diagonal", s);
    INFO("jump ratio " << s.ratio);
    CHECK(s.nonFinite == 0);
    CHECK(s.ratio <= 1.3);
}

TEST_CASE("Cursor on or next to a node through the Processor plays that node", "[disrumpo][audit][morph][D-155]") {
    const size_t len = static_cast<size_t>(kSr * 0.5);
    const auto in = sine220AtMinus12(len);

    // One node of `type` alone, drive 3: the level the parked cursor must reproduce.
    const auto aloneDb = [&](DistortionType type) {
        Rig r(kSr);
        r.apply(morphSetup({type}, kPlanar2D));
        r.reactivate();
        std::vector<float> oL;
        std::vector<float> oR;
        r.render(in, in, oL, oR);
        return toDb(rmsOf(oL.data() + len / 2, len / 2));
    };

    struct Pos {
        const char* name;
        double x;
        double y;
        int node;  ///< node whose level the cursor must reproduce (-1: centre, weights only)
    };
    const Pos positions[] = {{.name = "exact node 1 (1, 0)", .x = 1.0, .y = 0.0, .node = 1},
                             {.name = "near node 1 (0.9995, 0.0005)", .x = 0.9995, .y = 0.0005, .node = 1},
                             {.name = "close to node 1 (0.998, 0.002)", .x = 0.998, .y = 0.002, .node = 1},
                             {.name = "exact node 0 (0, 0)", .x = 0.0, .y = 0.0, .node = 0},
                             {.name = "centre (0.5, 0.5)", .x = 0.5, .y = 0.5, .node = -1}};
    const std::array<double, 2> refDb = {aloneDb(kFourCrossFamily[0]), aloneDb(kFourCrossFamily[1])};

    for (const auto& p : positions) {
        Rig r(kSr);
        r.apply(morphSetup(kFourCrossFamily, kPlanar2D));
        r.apply({{bandId(BandParamType::kBandMorphX), p.x}, {bandId(BandParamType::kBandMorphY), p.y}});
        r.reactivate();
        std::vector<float> oL;
        std::vector<float> oR;
        r.render(in, in, oL, oR);
        const auto& w = r.p->getMorphWeightsForBand(0);
        double sum = 0.0;
        size_t nonFiniteWeights = 0;
        for (const float v : w) {
            if (fin(v)) sum += v;
            else ++nonFiniteWeights;
        }
        size_t nonFinite = 0;
        for (const float v : oL) nonFinite += fin(v) ? 0 : 1;
        for (const float v : oR) nonFinite += fin(v) ? 0 : 1;
        const double level = toDb(rmsOf(oL.data() + len / 2, len / 2));
        const double ref = p.node >= 0 ? refDb[static_cast<size_t>(p.node)] : level;
        std::printf("T6.06 cursor %s: weights %.4f / %.4f / %.4f / %.4f (sum %.6f), level %.2f dB, node alone %.2f dB, "
                    "delta %.3f dB\n",
                    p.name, static_cast<double>(w[0]), static_cast<double>(w[1]), static_cast<double>(w[2]),
                    static_cast<double>(w[3]), sum, level, ref, level - ref);
        INFO(p.name << ": weight sum " << sum << ", level " << level << " dB, node alone " << ref << " dB");
        CHECK(nonFiniteWeights == 0);
        CHECK(std::fabs(sum - 1.0) <= 1e-3);
        CHECK(nonFinite == 0);
        CHECK(std::fabs(level - ref) <= 0.5);
    }
}

// Measurement probe (hidden) for the held bars: the cross-family sweeps' level and clicks
// with the band held at one oversampling factor, per RingSaturation carrier mode, and the
// clicks of the 4-node mix parked at the positions where the sweep clicks.
TEST_CASE("Probe: T6.06 held morph-sweep bars", "[.][p6b-morph]") {
    for (const double os : {0.0, 1.0 / 3.0, 2.0 / 3.0}) {
        PList s = morphSetup({DistortionType::SoftClip, DistortionType::SineFold}, kLinear1D);
        s.emplace_back(kOversampleMaxId, os);
        const auto r = sweep(s, false);
        std::printf("Oversample Max %.0fx: ", std::pow(2.0, std::round(os * 3.0)));
        report("2-node SoftClip-SineFold", r);
    }
    struct Variant {
        const char* name;
        PList extra;
    };
    const Variant variants[] = {
        {.name = "as is", .extra = {}},
        {.name = "Oversample Max 1x", .extra = {{kOversampleMaxId, 0.0}}},
        {.name = "Oversample Max 2x", .extra = {{kOversampleMaxId, 1.0 / 3.0}}},
        {.name = "RingSat Fixed carrier", .extra = {{nodeId(3, NodeParamType::kNodeShape5), 0.0}}},
        {.name = "RingSat Harmonic carrier", .extra = {{nodeId(3, NodeParamType::kNodeShape5), 1.0 / 3.0}}},
        {.name = "RingSat -> SoftClip", .extra = {{nodeId(3, NodeParamType::kNodeType), typeNorm(DistortionType::SoftClip)}}},
        {.name = "FullRectify -> SoftClip", .extra = {{nodeId(2, NodeParamType::kNodeType), typeNorm(DistortionType::SoftClip)}}},
        {.name = "SineFold -> SoftClip", .extra = {{nodeId(1, NodeParamType::kNodeType), typeNorm(DistortionType::SoftClip)}}}};
    for (const auto& v : variants) {
        PList s = morphSetup(kFourCrossFamily, kPlanar2D);
        s.insert(s.end(), v.extra.begin(), v.extra.end());
        std::printf("%s: ", v.name);
        report("4-node planar diagonal", sweep(s, true));
    }
    const size_t len = static_cast<size_t>(kSr * 3.0);
    const auto in = sine220AtMinus12(len);
    for (const double p : {0.58, 0.60, 0.62, 0.64}) {
        Rig r(kSr);
        r.apply(morphSetup(kFourCrossFamily, kPlanar2D));
        r.apply({{bandId(BandParamType::kBandMorphX), p}, {bandId(BandParamType::kBandMorphY), p}});
        r.reactivate();
        std::vector<float> oL;
        std::vector<float> oR;
        r.render(in, in, oL, oR);
        std::printf("4-node parked at (%.2f, %.2f), factor %d, clicks / max step per 0.25 s:", p, p,
                    r.p->getBandProcessor(0).getActiveOversampleFactor());
        for (size_t from = 0; from + 12000 <= len; from += 12000) {
            std::printf(" %zu/%.3f", clickCount(oL.data() + from, 12000, kSr), maxJump(oL.data() + from, 12000));
        }
        std::printf("\n");
    }
}
