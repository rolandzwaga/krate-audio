// ==============================================================================
// Layer 3: System Tests - ProfundumCore, spectral TU
//                                    (specs/profundum-phase1-harmonic-core)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/profundum-phase1-harmonic-core/spec.md
//            specs/profundum-phase1-harmonic-core/plan.md  (S8.1, S8.3)
//            specs/profundum-phase1-harmonic-core/tasks.md (T003 creates and
//                                                           wires this TU;
//                                                           T013, T016 fill it)
//
// SCOPE OF THIS TU (plan S8.1: 262 144-point / BH7 analyses; full grids [long]
//   plus one per-push smoke sibling each; tags [systems][profundum]):
//   SC-001, SC-002 (render arm), SC-004 (render arm), SC-005, SC-006 (render
//   arm), SC-007 (render arm), SC-008, SC-009, SC-010(b), SC-011(a), SC-017
//   (SampleRateChange), SC-019 (render arm), SC-022 (render arm), and the
//   ListeningRenders evaluation case ([.listen], not a gate).
//
// T013 (part 1): SC-001, SC-002 render, SC-004 render, SC-005, SC-006 render,
//   SC-007 render. T016 (part 2) appends the rest: SC-008, SC-009 (static and
//   bend), SC-010(b) (ProfundumCore_NoZipperControlSweepsSidebands), SC-011(a),
//   SC-017 rate arm, SC-019 render arm, SC-022 render arm, ListeningRenders.
//   SC-019 split: the recipe-vector arm keeps the spec name
//   ProfundumCore_BodyColoursDistinct (recipe TU); the render arm here is
//   ProfundumCore_BodyColoursDistinctRendered. SC-022: the full 90-point grid
//   with the 33-step FR-023 floor sweep is [long]
//   (ProfundumCore_EmphasisOddOnlyRendered); ...RenderedSmoke is per-push.
//
// Rendered descriptors (spec "Success Criteria" notation, plan S8.2): every
// render is a fresh Reset-policy noteOn, which starts the slewed shape at its
// target (S4.7, the SC-010(d) Reset exemption; asserted bitwise per render).
// One kLowFrequencyFftSize frame of L is taken >= 50 ms after the note and
// analysed with magnitudeSpectrum + harmonicMainLobePower. SC-001 uses two 1 s
// BH7 Goertzel frames instead. Every render has an all-zero pan vector, so
// every render calls requireLREqual (SC-014(a)).
//
// No measurement allowance is added to any bar (plan S8.3 SC-004 row; tasks.md
// rule 9): a render miss is a defect, recorded under tasks.md "Defects found".
// ==============================================================================

#include "profundum_core_test_helpers.h"

#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/processors/spectral_shape_recipe.h>
#include <krate/dsp/systems/profundum_core.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

using Krate::DSP::ProfundumCore;
using Krate::DSP::SpectralShapeRecipe;
namespace PT = Krate::DSP::ProfundumTest;

namespace {

using Controls = SpectralShapeRecipe::Controls;
using Descriptors = PT::Descriptors;

constexpr double kFs48 = 48000.0;
constexpr float kC1 = 32.70f;
constexpr float kC2 = 65.41f;
constexpr float kC3 = 130.81f;
constexpr std::array<float, 3> kNotesC1toC3{kC1, kC2, kC3};
constexpr std::array<float, 3> kUnit{0.0f, 0.5f, 1.0f};
constexpr std::array<float, 3> kSigned{-1.0f, 0.0f, 1.0f};

/// Rendered-descriptor frame length (spec: kLowFrequencyFftSize = 262 144 samples).
constexpr std::size_t kFrame = Krate::DSP::TestUtils::kLowFrequencyFftSize;
/// Steady state: the frame starts >= 50 ms after the (Reset) note, whose shape is already converged.
constexpr double kSettleSeconds = 0.05;

std::size_t samplesAt(double seconds, double fs) {
    return static_cast<std::size_t>(std::llround(seconds * fs));
}

/// 33 evenly spaced points over [lo, hi], both ends included (spec sweep resolution).
std::vector<float> sweep33(float lo, float hi) {
    std::vector<float> xs(33);
    for (std::size_t i = 0; i < xs.size(); ++i)
        xs[i] = lo + (hi - lo) * static_cast<float>(i) / 32.0f;
    return xs;
}

std::string controlsText(const Controls& c) {
    std::ostringstream os;
    os << "depth " << c.depth << " body " << c.body << " curv " << c.bodyCurvature << " emph " << c.bodyEmphasis
       << " edge " << c.edge << " shift " << c.shift;
    return os.str();
}

/// Fresh Reset render of `c` at f0; returns `frameLen` samples of L starting kSettleSeconds after
/// the note. Asserts L == R (SC-014(a)) and that the slewed shape equals evaluateShape(c) bitwise
/// (converged: the Reset noteOn starts at the target, S4.7).
std::vector<float> steadyFrame(ProfundumCore& core, const Controls& c, float f0, std::size_t frameLen = kFrame) {
    const double fs = PT::coreSampleRate(core);
    const std::size_t skip = samplesAt(kSettleSeconds, fs);
    const double seconds = static_cast<double>(skip + frameLen + ProfundumCore::kControlInterval) / fs;
    const PT::Render r = PT::renderCore(core, c, f0, seconds);
    PT::requireLREqual(r.L.data(), r.R.data(), r.L.size());
    REQUIRE(r.L.size() >= skip + frameLen);

    const std::vector<float> target = PT::shapeOf(c, core.numPartials());
    const auto shape = core.shapeGains();
    REQUIRE(shape.size() == target.size());
    REQUIRE(PT::samplesBitEqual(shape.data(), target.data(), target.size()));

    const auto first = r.L.begin() + static_cast<std::ptrdiff_t>(skip);
    return {first, first + static_cast<std::ptrdiff_t>(frameLen)};
}

/// Rendered harmonic power weights w_n (n = 1..numPartials) of a steady frame.
std::vector<double> renderedPowers(ProfundumCore& core, const Controls& c, float f0) {
    const std::vector<float> frame = steadyFrame(core, c, f0);
    return PT::renderedHarmonicPowers(frame.data(), PT::coreSampleRate(core), static_cast<double>(f0),
                                      core.numPartials());
}

Descriptors renderedDescriptors(ProfundumCore& core, const Controls& c, float f0) {
    const std::vector<double> w = renderedPowers(core, c, f0);
    return PT::describePowers(w);
}

/// Rendered descriptors over a 33-point sweep of one field from `base` (one fresh render per step).
std::vector<Descriptors> renderedSweep(ProfundumCore& core, Controls base, float Controls::*field, float lo,
                                       float hi, float f0) {
    std::vector<Descriptors> out;
    out.reserve(33);
    for (const float x : sweep33(lo, hi)) {
        base.*field = x;
        out.push_back(renderedDescriptors(core, base, f0));
    }
    return out;
}

/// Nothing in the cap taper and the guard at identity: every mask factor at (f0, fs) is exactly 1,
/// and N·f0 is below the taper start capHz·2^(−kCapTaperCents/1200) (plan S1 C-8 (i)).
void requireNoHarmonicInTaper(float f0, double fs, int numPartials) {
    const auto n = static_cast<std::size_t>(numPartials);
    std::array<float, Krate::DSP::kMaxPartials> mask{};
    SpectralShapeRecipe::evaluateMask(f0, fs, std::span<float>(mask.data(), n));
    for (std::size_t k = 0; k < n; ++k) {
        INFO("f0 " << f0 << " fs " << fs << " n " << (k + 1) << " mask " << mask[k]);
        REQUIRE(mask[k] == 1.0f);
    }
    const double capHz = static_cast<double>(SpectralShapeRecipe::capFrequency(fs));
    const double taperStart =
        capHz * std::exp2(-static_cast<double>(SpectralShapeRecipe::kCapTaperCents) / 1200.0);
    INFO("N·f0 " << static_cast<double>(numPartials) * static_cast<double>(f0) << " Hz, taper start " << taperStart);
    REQUIRE(static_cast<double>(numPartials) * static_cast<double>(f0) < taperStart);
}

/// Strict increase with +inf as the largest value (fast-math-immune classification).
bool strictlyGreater(double cur, double prev) {
    if (PT::isPosInf(prev))
        return false;
    if (PT::isPosInf(cur))
        return true;
    return cur > prev;
}

}  // namespace

// =============================================================================
// SC-001, FR-044: every measured harmonic within < 0.1 cent of n·f0
// =============================================================================

namespace {

constexpr std::array<int, 7> kFrequencyCandidates{1, 2, 3, 8, 16, 32, 64};

double wrapHalfCycle(double c) { return c - std::round(c); }

/// Frequency error (cents) of the component at nominal `hz`: Goertzel phase advance between two
/// 1 s BH7 frames whose starts are 1 s apart, unwrapped against the nominal 2π·hz·1 s.
double harmonicErrorCents(const std::vector<float>& L, std::size_t start, double fs, double hz) {
    const std::size_t frame = samplesAt(1.0, fs);
    const std::size_t a = start;
    const std::size_t b = start + frame;   // starts 1 s apart
    REQUIRE(b + frame <= L.size());
    const double seconds = static_cast<double>(frame) / fs;
    const double phA = PT::goertzelPhase(L.data() + a, frame, fs, hz);
    const double phB = PT::goertzelPhase(L.data() + b, frame, fs, hz);
    const double nominalCycles = hz * seconds;
    const double residualCycles =
        wrapHalfCycle((phB - phA) / PT::kTwoPi - (nominalCycles - std::floor(nominalCycles)));
    const double deltaHz = residualCycles / seconds;
    return 1200.0 * std::log2((hz + deltaHz) / hz);
}

float midiToHz(int midi) { return static_cast<float>(440.0 * std::pow(2.0, (midi - 69) / 12.0)); }

/// Renders kSawAnchor at `midi` and measures every candidate n that is measurable: n·f0 < capHz and
/// its post-mask evaluate gain >= −60 dB re h1 (plan S1 C-8 (ii)). Each measured n must be within
/// 0.1 cent. Returns the measured candidates.
std::vector<int> checkPartialFrequencies(ProfundumCore& core, int midi, std::span<const int> candidates,
                                         double& worstCents) {
    const double fs = PT::coreSampleRate(core);
    const float f0 = midiToHz(midi);
    const int numPartials = core.numPartials();
    const PT::Render r = PT::renderCore(core, SpectralShapeRecipe::kSawAnchor, f0, 2.25);
    PT::requireLREqual(r.L.data(), r.R.data(), r.L.size());

    const std::vector<float> gains = PT::fullOf(SpectralShapeRecipe::kSawAnchor, f0, fs, numPartials);
    REQUIRE(gains[0] > 0.0f);
    const double capHz = static_cast<double>(SpectralShapeRecipe::capFrequency(fs));
    const std::size_t start = samplesAt(0.2, fs);   // 0.2 s settle

    std::vector<int> measured;
    for (const int n : candidates) {
        if (n > numPartials)
            continue;
        const double hz = static_cast<double>(n) * static_cast<double>(f0);
        if (!(hz < capHz))
            continue;
        const double g = static_cast<double>(gains[static_cast<std::size_t>(n - 1)]);
        const double ratio = g / static_cast<double>(gains[0]);
        if (!(ratio > 0.0) || 20.0 * std::log10(ratio) < -60.0)
            continue;   // plan S1 C-8 (ii): below -60 dB re h1 -> not measured
        const double cents = harmonicErrorCents(r.L, start, fs, hz);
        INFO("fs " << fs << " MIDI " << midi << " f0 " << f0 << " n " << n << " (" << hz << " Hz, gain "
                   << 20.0 * std::log10(ratio) << " dB re h1): error " << cents << " cent");
        REQUIRE(std::abs(cents) < 0.1);
        worstCents = std::max(worstCents, std::abs(cents));
        measured.push_back(n);
    }
    return measured;
}

bool contains(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

}  // namespace

TEST_CASE("ProfundumCore_PartialFrequencyAccuracy", "[systems][profundum][long]") {
    for (const double fs : {44100.0, 48000.0, 96000.0}) {
        auto core = std::make_unique<ProfundumCore>();
        core->prepare(fs);
        double worst = 0.0;
        // Coverage buckets: octaves C1 (MIDI 24-35), C2 (36-47) and C3 (48-59), each needing
        // n = 16, 32 and 64 measured at >= 1 note (plan S1 C-8 (ii)).
        std::array<std::array<bool, 3>, 3> covered{};
        for (int midi = 12; midi <= 60; ++midi) {
            const std::vector<int> measured = checkPartialFrequencies(*core, midi, kFrequencyCandidates, worst);
            INFO("fs " << fs << " MIDI " << midi);
            REQUIRE(contains(measured, 1));
            REQUIRE(contains(measured, 2));
            REQUIRE(contains(measured, 3));
            if (midi >= 24 && midi < 60) {
                const auto octave = static_cast<std::size_t>((midi - 24) / 12);
                covered[octave][0] = covered[octave][0] || contains(measured, 16);
                covered[octave][1] = covered[octave][1] || contains(measured, 32);
                covered[octave][2] = covered[octave][2] || contains(measured, 64);
            }
        }
        constexpr std::array<int, 3> kHigh{16, 32, 64};
        for (std::size_t o = 0; o < covered.size(); ++o) {
            for (std::size_t k = 0; k < kHigh.size(); ++k) {
                INFO("fs " << fs << " octave C" << (o + 1) << " n " << kHigh[k] << " measured at >= 1 note");
                REQUIRE(covered[o][k]);
            }
        }
        WARN("SC-001 fs " << fs << ": worst measured error " << worst << " cent (bar < 0.1)");
    }
}

TEST_CASE("ProfundumCore_PartialFrequencyAccuracySmoke", "[systems][profundum]") {
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(kFs48);
    double worst = 0.0;
    constexpr std::array<int, 2> kSmoke{1, 8};
    const std::vector<int> measured = checkPartialFrequencies(*core, 36, kSmoke, worst);
    REQUIRE(contains(measured, 1));
    REQUIRE(contains(measured, 8));
    WARN("SC-001 smoke (MIDI 36, 48 kHz, n = 1, 8): worst error " << worst << " cent (bar < 0.1)");
}

// =============================================================================
// SC-002 render arm, FR-010: rendered h1/rest strictly increasing in Depth
// =============================================================================

namespace {

/// The 33-point depth sweep plus kDepthTriangle (0.5, already a sweep point), deduplicated.
std::vector<float> depthSweep() {
    std::vector<float> depths = sweep33(0.0f, 1.0f);
    depths.push_back(SpectralShapeRecipe::kDepthTriangle);
    std::sort(depths.begin(), depths.end());
    depths.erase(std::unique(depths.begin(), depths.end()), depths.end());
    REQUIRE(depths.size() == 33);
    return depths;
}

void requireDepthRenderMonotonic(ProfundumCore& core, Controls c, float f0) {
    INFO("f0 " << f0 << " body " << c.body << " edge " << c.edge << " shift " << c.shift);
    double prev = 0.0;
    bool first = true;
    for (const float d : depthSweep()) {
        c.depth = d;
        const double h1Rest = renderedDescriptors(core, c, f0).h1RestDb;
        INFO("depth " << d << " h1/rest " << (PT::isPosInf(h1Rest) ? std::string("+inf") : std::to_string(h1Rest))
                      << " dB, prev " << prev);
        REQUIRE_FALSE(PT::isNegInf(h1Rest));
        if (!first)
            REQUIRE(strictlyGreater(h1Rest, prev));   // depth 1 may be +inf: the largest
        prev = h1Rest;
        first = false;
    }
}

}  // namespace

TEST_CASE("ProfundumCore_DepthRenderMonotonic", "[systems][profundum][long]") {
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(kFs48);
    for (const float f0 : kNotesC1toC3)
        for (int iB = 0; iB < 3; ++iB)
            for (int iE = 0; iE < 3; ++iE)
                for (int iS = 0; iS < 3; ++iS)
                    requireDepthRenderMonotonic(*core, PT::gridPoint(1, iB, iE, iS), f0);
}

TEST_CASE("ProfundumCore_DepthRenderMonotonicSmoke", "[systems][profundum]") {
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(kFs48);
    requireDepthRenderMonotonic(*core, PT::midGrid(), kC2);
    for (const int iB : {0, 2})
        for (const int iE : {0, 2})
            for (const int iS : {0, 2})
                requireDepthRenderMonotonic(*core, PT::gridPoint(1, iB, iE, iS), kC2);
}

// =============================================================================
// SC-004 render arm (FR-011/012/015/016, FR-071): Body, Edge and Shift each move their
// rendered descriptor monotonically and audibly; bars unchanged from the recipe arm (T007)
// =============================================================================

namespace {

struct BodyStats {
    int nHeadroom6 = 0;
    int nLow = 0;
    double minSpan6 = 1.0e9;
    double minRatioLow = 1.0e9;
};

/// Body: R_body strictly increasing; span >= 6 dB where headroom >= 6 dB, else >= 0.7 × headroom
/// (headroom = −rendered R_body at body = 0; plan S1 C-1).
void checkBodySweep(ProfundumCore& core, const Controls& base, float f0, BodyStats& s) {
    INFO("Body sweep, f0 " << f0 << ", " << controlsText(base));
    const auto d = renderedSweep(core, base, &Controls::body, 0.0f, 1.0f, f0);
    for (std::size_t i = 1; i < d.size(); ++i) {
        INFO("step " << i << " R_body " << d[i].rBodyDb << " prev " << d[i - 1].rBodyDb);
        REQUIRE(d[i].rBodyDb > d[i - 1].rBodyDb);
    }
    const double headroom = -d.front().rBodyDb;   // plan S1 C-1
    const double span = d.back().rBodyDb - d.front().rBodyDb;
    INFO("headroom " << headroom << " dB, span " << span << " dB");
    if (headroom >= 6.0) {
        REQUIRE(span >= 6.0);
        s.minSpan6 = std::min(s.minSpan6, span);
        ++s.nHeadroom6;
    } else {
        REQUIRE(headroom > 0.0);
        REQUIRE(span >= 0.7 * headroom);
        s.minRatioLow = std::min(s.minRatioLow, span / headroom);
        ++s.nLow;
    }
}

/// Edge: R_pres strictly increasing, span >= 6 dB. Returns the span.
double checkEdgeSweep(ProfundumCore& core, const Controls& base, float f0) {
    INFO("Edge sweep, f0 " << f0 << ", " << controlsText(base));
    const auto d = renderedSweep(core, base, &Controls::edge, 0.0f, 1.0f, f0);
    for (std::size_t i = 1; i < d.size(); ++i) {
        INFO("step " << i << " R_pres " << d[i].rPresDb << " prev " << d[i - 1].rPresDb);
        REQUIRE(d[i].rPresDb > d[i - 1].rPresDb);
    }
    const double span = d.back().rPresDb - d.front().rPresDb;
    INFO("span " << span << " dB");
    REQUIRE(span >= 6.0);
    return span;
}

/// Shift: C strictly increasing, span >= 1 octave. Returns the span.
double checkShiftSweep(ProfundumCore& core, const Controls& base, float f0) {
    INFO("Shift sweep, f0 " << f0 << ", " << controlsText(base));
    const auto d = renderedSweep(core, base, &Controls::shift, -1.0f, 1.0f, f0);
    for (std::size_t i = 1; i < d.size(); ++i) {
        INFO("step " << i << " C " << d[i].centroidOct << " prev " << d[i - 1].centroidOct);
        REQUIRE(d[i].centroidOct > d[i - 1].centroidOct);
    }
    const double span = d.back().centroidOct - d.front().centroidOct;
    INFO("span " << span << " oct");
    REQUIRE(span >= 1.0);
    return span;
}

/// Endpoints from mid grid (body = 1, edge = 1, shift = 1): pairwise descriptor distance >= 6 dB.
double checkEndpoints(ProfundumCore& core, float f0) {
    Controls bodyEnd = PT::midGrid();
    bodyEnd.body = 1.0f;
    Controls edgeEnd = PT::midGrid();
    edgeEnd.edge = 1.0f;
    Controls shiftEnd = PT::midGrid();
    shiftEnd.shift = 1.0f;
    const Descriptors dB = renderedDescriptors(core, bodyEnd, f0);
    const Descriptors dE = renderedDescriptors(core, edgeEnd, f0);
    const Descriptors dS = renderedDescriptors(core, shiftEnd, f0);
    const double bodyEdge = PT::descriptorDistance(dB, dE);
    const double bodyShift = PT::descriptorDistance(dB, dS);
    const double edgeShift = PT::descriptorDistance(dE, dS);
    INFO("f0 " << f0 << " endpoints: body-edge " << bodyEdge << " body-shift " << bodyShift << " edge-shift "
               << edgeShift << " dB");
    REQUIRE(bodyEdge >= 6.0);
    REQUIRE(bodyShift >= 6.0);
    REQUIRE(edgeShift >= 6.0);
    return std::min({bodyEdge, bodyShift, edgeShift});
}

}  // namespace

TEST_CASE("ProfundumCore_AudibilityGate_BodyEdgeShift", "[systems][profundum][long]") {
    constexpr int kN = 64;
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(kFs48, kN);

    for (const float f0 : kNotesC1toC3) {
        requireNoHarmonicInTaper(f0, kFs48, kN);   // N = 64 at C1-C3 / 48 kHz (plan S1 C-8 (i))

        // Body over the 27 (depth, edge, shift) grid points.
        BodyStats body;
        for (const float depth : kUnit) {
            for (const float edge : kUnit) {
                for (const float shift : kSigned) {
                    Controls base = PT::midGrid();
                    base.depth = depth;
                    base.edge = edge;
                    base.shift = shift;
                    checkBodySweep(*core, base, f0, body);
                }
            }
        }
        // Exactly 5 low-headroom points (plan S1 C-1): the rendered sets match the recipe's.
        REQUIRE(body.nLow == 5);
        REQUIRE(body.nHeadroom6 == 22);

        // Edge over the 27 (depth, body, shift) grid points.
        double minEdge = 1.0e9;
        for (const float depth : kUnit) {
            for (const float b : kUnit) {
                for (const float shift : kSigned) {
                    Controls base = PT::midGrid();
                    base.depth = depth;
                    base.body = b;
                    base.shift = shift;
                    minEdge = std::min(minEdge, checkEdgeSweep(*core, base, f0));
                }
            }
        }

        // Shift over depth {0, 0.5} x (body, edge) minus body = edge = 0: 16 points. The depth-1
        // points are omitted from the render arm (plan S1 C-2).
        double minShift = 1.0e9;
        int nShift = 0;
        for (const float depth : {0.0f, 0.5f}) {
            for (const float b : kUnit) {
                for (const float edge : kUnit) {
                    if (b == 0.0f && edge == 0.0f)
                        continue;
                    Controls base = PT::midGrid();
                    base.depth = depth;
                    base.body = b;
                    base.edge = edge;
                    minShift = std::min(minShift, checkShiftSweep(*core, base, f0));
                    ++nShift;
                }
            }
        }
        REQUIRE(nShift == 16);

        const double minEndpoint = checkEndpoints(*core, f0);

        WARN("SC-004 render f0 " << f0 << ": Body min span (headroom >= 6 dB, " << body.nHeadroom6
                                 << " points) " << body.minSpan6 << " dB (bar 6, recipe 8.65); min span/headroom ("
                                 << body.nLow << " points) " << body.minRatioLow << " (bar 0.7, recipe 0.740)");
        WARN("SC-004 render f0 " << f0 << ": Edge min span " << minEdge << " dB (bar 6, recipe 8.89); Shift min span "
                                 << minShift << " oct (bar 1, recipe 1.427); endpoint min distance " << minEndpoint
                                 << " dB (bar 6, recipe 7.07)");
    }
}

TEST_CASE("ProfundumCore_AudibilityGate_BodyEdgeShiftSmoke", "[systems][profundum]") {
    constexpr int kN = 64;
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(kFs48, kN);
    for (const float f0 : kNotesC1toC3) {
        requireNoHarmonicInTaper(f0, kFs48, kN);
        BodyStats body;
        checkBodySweep(*core, PT::midGrid(), f0, body);
        const double edgeSpan = checkEdgeSweep(*core, PT::midGrid(), f0);
        const double shiftSpan = checkShiftSweep(*core, PT::midGrid(), f0);
        const double endpoint = checkEndpoints(*core, f0);
        WARN("SC-004 render smoke (mid grid) f0 " << f0 << ": Body span "
                                                 << (body.nHeadroom6 > 0 ? body.minSpan6 : body.minRatioLow)
                                                 << (body.nHeadroom6 > 0 ? " dB" : " x headroom") << ", Edge span "
                                                 << edgeSpan << " dB, Shift span " << shiftSpan
                                                 << " oct, endpoint min " << endpoint << " dB");
    }
}

// =============================================================================
// SC-005, FR-013/FR-023: Body curvature and emphasis move the rendered spread and
// odd/even ratio (C2, mid grid)
// =============================================================================

TEST_CASE("ProfundumCore_AudibilityGate_BodyCurvatureEmphasis", "[systems][profundum]") {
    constexpr int kN = 64;
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(kFs48, kN);
    requireNoHarmonicInTaper(kC2, kFs48, kN);

    // Curvature: sigma strictly decreasing (S3.6), |span| >= 0.25 octave.
    {
        const auto d = renderedSweep(*core, PT::midGrid(), &Controls::bodyCurvature, 0.0f, 1.0f, kC2);
        for (std::size_t i = 1; i < d.size(); ++i) {
            INFO("curvature step " << i << " sigma " << d[i].spreadOct << " prev " << d[i - 1].spreadOct);
            REQUIRE(d[i].spreadOct < d[i - 1].spreadOct);
        }
        const double span = d.back().spreadOct - d.front().spreadOct;
        INFO("sigma span " << span << " oct");
        REQUIRE(std::abs(span) >= 0.25);
        WARN("SC-005 render curvature sigma span: " << span << " oct (bar |span| >= 0.25, recipe -0.375)");
    }

    // Emphasis over [-1, 0]: odd/even strictly decreasing from the measured first point (which
    // must exceed the second; plan R-11), span >= 20 dB, and the FR-023 floor (>= the
    // emphasis-0 value, the last point) at every step.
    {
        const auto d = renderedSweep(*core, PT::midGrid(), &Controls::bodyEmphasis, -1.0f, 0.0f, kC2);
        const double neutral = d.back().oddEvenDb;
        REQUIRE_FALSE(PT::isPosInf(neutral));
        REQUIRE_FALSE(PT::isNegInf(neutral));
        for (std::size_t i = 0; i < d.size(); ++i) {
            const double oe = d[i].oddEvenDb;
            INFO("emphasis step " << i << " odd/even "
                                  << (PT::isPosInf(oe) ? std::string("+inf") : std::to_string(oe))
                                  << " dB, neutral " << neutral);
            REQUIRE_FALSE(PT::isNegInf(oe));
            if (i > 0) {
                INFO("prev " << d[i - 1].oddEvenDb);
                REQUIRE(strictlyGreater(d[i - 1].oddEvenDb, oe));
            }
            // FR-023 floor.
            if (!PT::isPosInf(oe))
                REQUIRE(oe >= neutral);
        }
        const double first = d.front().oddEvenDb;
        if (PT::isPosInf(first)) {
            WARN("SC-005 render emphasis odd/even: +inf (every rendered even bin exactly 0) -> " << neutral
                                                                                              << " dB (bar 20)");
        } else {
            const double span = first - neutral;
            INFO("odd/even " << first << " -> " << neutral << " dB, span " << span);
            REQUIRE(span >= 20.0);
            WARN("SC-005 render emphasis odd/even: " << first << " -> " << neutral << " dB, span " << span
                                                     << " dB (bar 20)");
        }
    }
}

// =============================================================================
// SC-006 render arm, FR-018/FR-021: the three waveform anchors reached in the rendered
// audio (C1, 48 kHz), plus the anchor aliasing gate
// =============================================================================

namespace {

/// Relative L2 error ||a/a1 − r||2 / ||r||2 of rendered amplitudes a_n = sqrt(w_n) (r_1 = 1).
double renderedRelativeL2(const std::vector<double>& w, const std::vector<double>& r) {
    const double a1 = std::sqrt(w[0]);
    double num = 0.0;
    double den = 0.0;
    for (std::size_t i = 0; i < w.size(); ++i) {
        const double e = std::sqrt(w[i]) / a1 - r[i];
        num += e * e;
        den += r[i] * r[i];
    }
    return std::sqrt(num / den);
}

/// Rendered level of harmonic n (1-based) re h1, in dB (w_n > 0 required by the caller).
double levelReH1Db(const std::vector<double>& w, std::size_t n) { return 10.0 * std::log10(w[n - 1] / w[0]); }

/// Power outside the SC-009 exclusion zones <= −60 dB re total (BH7, aliasFftLength), with the
/// 50 % non-vacuity guard.
void requireAnchorAliasFree(const std::vector<float>& frame, double fs, float f0, const char* name) {
    const std::size_t len = PT::aliasFftLength(fs, static_cast<double>(f0));
    REQUIRE(len <= frame.size());
    const PT::AliasResult a = PT::aliasedPower(frame.data(), len, fs, static_cast<double>(f0));
    const double reTotal = a.aliasedDbfs - a.totalDb;
    INFO(name << " aliasing: FFT " << len << ", excluded fraction " << a.excludedFraction << ", aliased "
              << a.aliasedDbfs << " dBFS, total " << a.totalDb << " dBFS, re total " << reTotal << " dB");
    REQUIRE(a.excludedFraction <= 0.5);
    REQUIRE(reTotal <= -60.0);
    WARN("SC-006 " << name << " aliasing re total: " << reTotal << " dB (bar -60), excluded fraction "
                   << a.excludedFraction);
}

}  // namespace

TEST_CASE("ProfundumCore_WaveformAnchorsRendered", "[systems][profundum]") {
    for (const int numPartials : {64, 96}) {
        INFO("N " << numPartials);
        const auto size = static_cast<std::size_t>(numPartials);
        auto core = std::make_unique<ProfundumCore>();
        core->prepare(kFs48, numPartials);
        requireNoHarmonicInTaper(kC1, kFs48, numPartials);

        // sine: rest <= -60 dB re h1
        {
            const std::vector<float> frame = steadyFrame(*core, SpectralShapeRecipe::kSineAnchor, kC1);
            const std::vector<double> w =
                PT::renderedHarmonicPowers(frame.data(), kFs48, static_cast<double>(kC1), numPartials);
            REQUIRE(w[0] > 0.0);
            const double h1Rest = PT::describePowers(w).h1RestDb;
            if (!PT::isPosInf(h1Rest)) {   // +inf: rest exactly 0, below any bar
                INFO("sine rendered h1/rest " << h1Rest << " dB");
                REQUIRE(h1Rest >= 60.0);
            }
            requireAnchorAliasFree(frame, kFs48, kC1, "kSineAnchor");
        }

        // triangle: odd n <= 15 within ±1.5 dB of 1/n^2 re h1; every even n <= -50 dB re h1; L2 <= 5 %
        {
            const std::vector<float> frame = steadyFrame(*core, SpectralShapeRecipe::kTriangleAnchor, kC1);
            const std::vector<double> w =
                PT::renderedHarmonicPowers(frame.data(), kFs48, static_cast<double>(kC1), numPartials);
            REQUIRE(w[0] > 0.0);
            std::vector<double> r(size, 0.0);
            for (std::size_t n = 1; n <= size; ++n) {
                INFO("triangle n " << n << " w " << w[n - 1]);
                if (n % 2 == 0) {
                    REQUIRE(w[n - 1] <= w[0] * 1.0e-5);   // <= -50 dB re h1
                    continue;
                }
                r[n - 1] = 1.0 / (static_cast<double>(n) * static_cast<double>(n));
                if (n <= 15) {
                    REQUIRE(w[n - 1] > 0.0);
                    const double err = levelReH1Db(w, n) - 20.0 * std::log10(r[n - 1]);
                    INFO("error " << err << " dB");
                    REQUIRE(std::abs(err) <= 1.5);
                }
            }
            const double l2 = renderedRelativeL2(w, r);
            INFO("triangle rendered relative L2 " << l2);
            REQUIRE(l2 <= 0.05);
            requireAnchorAliasFree(frame, kFs48, kC1, "kTriangleAnchor");
        }

        // saw: n <= 16 within ±1.5 dB of 1/n re h1; 17..N within ±3 dB; L2 <= 5 %
        {
            const std::vector<float> frame = steadyFrame(*core, SpectralShapeRecipe::kSawAnchor, kC1);
            const std::vector<double> w =
                PT::renderedHarmonicPowers(frame.data(), kFs48, static_cast<double>(kC1), numPartials);
            REQUIRE(w[0] > 0.0);
            std::vector<double> r(size, 0.0);
            for (std::size_t n = 1; n <= size; ++n) {
                r[n - 1] = 1.0 / static_cast<double>(n);
                INFO("saw n " << n << " w " << w[n - 1]);
                REQUIRE(w[n - 1] > 0.0);
                const double err = levelReH1Db(w, n) - 20.0 * std::log10(r[n - 1]);
                INFO("error " << err << " dB");
                REQUIRE(std::abs(err) <= (n <= 16 ? 1.5 : 3.0));
            }
            const double l2 = renderedRelativeL2(w, r);
            INFO("saw rendered relative L2 " << l2);
            REQUIRE(l2 <= 0.05);
            requireAnchorAliasFree(frame, kFs48, kC1, "kSawAnchor");
        }
    }
}

// =============================================================================
// SC-007 render arm, FR-022: Heavy / Hollow / Growl reached in the rendered audio
// =============================================================================

TEST_CASE("ProfundumCore_NamedDistributionsRendered", "[systems][profundum]") {
    constexpr int kN = 64;
    struct Named {
        const char* name;
        const Controls* c;
        std::array<std::size_t, 3> order;   // region indices, dominant first: 0 = sub, 1 = body, 2 = pres
    };
    const std::array<Named, 3> named{{
        {"kHeavy", &SpectralShapeRecipe::kHeavy, {0, 1, 2}},
        {"kHollow", &SpectralShapeRecipe::kHollow, {1, 0, 2}},
        {"kGrowl", &SpectralShapeRecipe::kGrowl, {2, 1, 0}},
    }};

    auto core = std::make_unique<ProfundumCore>();
    core->prepare(kFs48, kN);
    for (const Named& nm : named) {
        double minMargin = 1.0e9;
        double worstLevelErr = 0.0;
        for (const float f0 : kNotesC1toC3) {
            const std::vector<double> w = renderedPowers(*core, *nm.c, f0);
            const Descriptors d = PT::describePowers(w);
            const std::array<double, 3> e{d.eSub, d.eBody, d.ePres};
            const double first = e[nm.order[0]];
            const double second = e[nm.order[1]];
            const double third = e[nm.order[2]];
            INFO(nm.name << " f0 " << f0 << " rendered E_sub " << d.eSub << " E_body " << d.eBody << " E_pres "
                         << d.ePres);
            REQUIRE(third > 0.0);
            REQUIRE(first > second);
            REQUIRE(second > third);
            const double dominantDb = 10.0 * std::log10(first / second);
            INFO("dominant margin " << dominantDb << " dB");
            REQUIRE(dominantDb >= 3.0);
            minMargin = std::min(minMargin, dominantDb);

            // Per-harmonic: every harmonic whose recipe level is >= -40 dB re the loudest is within
            // ±3 dB of evaluate, both levels taken re their own loudest harmonic.
            const std::vector<float> a = PT::fullOf(*nm.c, f0, kFs48, kN);
            const double aMax = static_cast<double>(*std::max_element(a.begin(), a.end()));
            const double wMax = *std::max_element(w.begin(), w.end());
            REQUIRE(aMax > 0.0);
            REQUIRE(wMax > 0.0);
            for (std::size_t n = 1; n <= a.size(); ++n) {
                const double an = static_cast<double>(a[n - 1]);
                if (!(an > 0.0))
                    continue;
                const double recipeDb = 20.0 * std::log10(an / aMax);
                if (recipeDb < -40.0)
                    continue;
                INFO("n " << n << " recipe " << recipeDb << " dB re loudest, rendered w " << w[n - 1]);
                REQUIRE(w[n - 1] > 0.0);
                const double renderedDb = 10.0 * std::log10(w[n - 1] / wMax);
                const double err = renderedDb - recipeDb;
                INFO("rendered " << renderedDb << " dB re loudest, error " << err << " dB");
                REQUIRE(std::abs(err) <= 3.0);
                worstLevelErr = std::max(worstLevelErr, std::abs(err));
            }
        }
        WARN("SC-007 render " << nm.name << ": min dominant margin over C1/C2/C3 " << minMargin
                              << " dB (bar 3); worst per-harmonic error " << worstLevelErr << " dB (bar 3)");
    }
}

// =============================================================================
// T016 (part 2) shared fixtures
// =============================================================================

namespace {

struct NamedState {
    const char* name;
    Controls c;
};

/// The 12 named states: the six FR-020 coordinates, the five FR-014 Body colours and mid grid.
std::vector<NamedState> namedStates() {
    return {
        {"kSineAnchor", SpectralShapeRecipe::kSineAnchor},
        {"kTriangleAnchor", SpectralShapeRecipe::kTriangleAnchor},
        {"kSawAnchor", SpectralShapeRecipe::kSawAnchor},
        {"kHeavy", SpectralShapeRecipe::kHeavy},
        {"kHollow", SpectralShapeRecipe::kHollow},
        {"kGrowl", SpectralShapeRecipe::kGrowl},
        {"kBodyRound", SpectralShapeRecipe::kBodyRound},
        {"kBodyHollow", SpectralShapeRecipe::kBodyHollow},
        {"kBodyWoody", SpectralShapeRecipe::kBodyWoody},
        {"kBodyNasal", SpectralShapeRecipe::kBodyNasal},
        {"kBodyThick", SpectralShapeRecipe::kBodyThick},
        {"midGrid", PT::midGrid()},
    };
}

/// "+inf" / "-inf" / the number, for printing descriptors that may be infinite.
std::string dbText(double x) {
    if (PT::isPosInf(x))
        return "+inf";
    if (PT::isNegInf(x))
        return "-inf";
    return std::to_string(x);
}

}  // namespace

// =============================================================================
// SC-008, FR-031: character tracks the keyboard, C1 -> C3 (rendered vs evaluateShape)
// =============================================================================

namespace {

constexpr int kMidiC1 = 24;
constexpr int kMidiC3 = 48;

struct InvarianceStats {
    double worstDb = 0.0;   ///< worst |rendered - recipe| level re h1 over compared harmonics
    int compared = 0;       ///< harmonics compared (recipe level >= -40 dB re h1)
    int excluded = 0;       ///< harmonics >= -40 dB re h1 excluded as in the cap taper
};

/// SC-008 at one (state, f0) on a prepared core: every harmonic whose recipe level is >= -40 dB re
/// h1 is rendered within ±0.5 dB of evaluateShape, both re h1. Without `allowTaperExclusion` the
/// case first asserts that nothing is in the cap taper (plan S1 C-8 (i)), so a constant change
/// that shrinks the compared set fails here. With it (SC-017 at 44.1 kHz), harmonics at or above
/// the taper start are excluded and counted, and every compared harmonic has mask factor exactly 1.
void checkSpectrumInvariant(ProfundumCore& core, const Controls& c, float f0, bool allowTaperExclusion,
                            InvarianceStats& st) {
    const double fs = PT::coreSampleRate(core);
    const int numPartials = core.numPartials();
    const auto n = static_cast<std::size_t>(numPartials);
    if (!allowTaperExclusion)
        requireNoHarmonicInTaper(f0, fs, numPartials);   // plan S1 C-8 (i)

    const double capHz = static_cast<double>(SpectralShapeRecipe::capFrequency(fs));
    const double taperStart =
        capHz * std::exp2(-static_cast<double>(SpectralShapeRecipe::kCapTaperCents) / 1200.0);
    std::array<float, Krate::DSP::kMaxPartials> mask{};
    SpectralShapeRecipe::evaluateMask(f0, fs, std::span<float>(mask.data(), n));

    const std::vector<double> w = renderedPowers(core, c, f0);
    const std::vector<float> a = PT::shapeOf(c, numPartials);
    REQUIRE(w.size() == n);
    REQUIRE(w[0] > 0.0);
    REQUIRE(a[0] > 0.0f);
    const double a1 = static_cast<double>(a[0]);
    for (std::size_t k = 1; k <= n; ++k) {
        const double an = static_cast<double>(a[k - 1]);
        if (!(an > 0.0))
            continue;
        const double recipeDb = 20.0 * std::log10(an / a1);
        if (recipeDb < -40.0)
            continue;
        const double hz = static_cast<double>(k) * static_cast<double>(f0);
        INFO(controlsText(c) << ", fs " << fs << ", f0 " << f0 << ", n " << k << " (" << hz << " Hz), recipe "
                             << recipeDb << " dB re h1, taper start " << taperStart << " Hz");
        if (hz >= taperStart) {
            REQUIRE(allowTaperExclusion);
            ++st.excluded;
            continue;
        }
        REQUIRE(mask[k - 1] == 1.0f);
        REQUIRE(w[k - 1] > 0.0);
        const double renderedDb = 10.0 * std::log10(w[k - 1] / w[0]);
        const double err = renderedDb - recipeDb;
        INFO("rendered " << renderedDb << " dB re h1, error " << err << " dB");
        REQUIRE(std::abs(err) <= 0.5);
        st.worstDb = std::max(st.worstDb, std::abs(err));
        ++st.compared;
    }
}

}  // namespace

TEST_CASE("ProfundumCore_SpectrumInvariantC1toC3", "[systems][profundum][long]") {
    constexpr int kN = 64;
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(kFs48, kN);
    for (const NamedState& s : namedStates()) {
        InvarianceStats st;
        for (int midi = kMidiC1; midi <= kMidiC3; ++midi)
            checkSpectrumInvariant(*core, s.c, midiToHz(midi), false, st);
        REQUIRE(st.excluded == 0);
        WARN("SC-008 " << s.name << " (25 semitones C1-C3, 48 kHz, N 64): worst error " << st.worstDb
                       << " dB over " << st.compared << " harmonics (bar 0.5)");
    }
}

// OQ-3 condition (3) (plan S9.3): SC-008 at 96 partials. At 48 kHz 96·f0 crosses the 8 410 Hz taper
// start above about MIDI 33, so harmonics in the taper are excluded (S3.3) and every compared harmonic
// has mask factor exactly 1.
TEST_CASE("ProfundumCore_SpectrumInvariantC1toC3N96", "[systems][profundum][long]") {
    constexpr int kN = 96;
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(kFs48, kN);
    REQUIRE(core->numPartials() == kN);
    for (const NamedState& s : namedStates()) {
        InvarianceStats st;
        for (int midi = kMidiC1; midi <= kMidiC3; ++midi)
            checkSpectrumInvariant(*core, s.c, midiToHz(midi), true, st);
        REQUIRE(st.compared > 0);
        WARN("SC-008 N 96 " << s.name << " (25 semitones C1-C3, 48 kHz): worst error " << st.worstDb << " dB over "
                            << st.compared << " harmonics (bar 0.5), " << st.excluded << " excluded in the taper");
    }
}

TEST_CASE("ProfundumCore_SpectrumInvariantC1toC3Smoke", "[systems][profundum]") {
    constexpr int kN = 64;
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(kFs48, kN);
    InvarianceStats st;
    for (const float f0 : kNotesC1toC3)
        checkSpectrumInvariant(*core, PT::midGrid(), f0, false, st);
    REQUIRE(st.excluded == 0);
    WARN("SC-008 smoke (mid grid, C1/C2/C3, 48 kHz): worst error " << st.worstDb << " dB over " << st.compared
                                                                   << " harmonics (bar 0.5)");
}

// =============================================================================
// SC-009, FR-032/FR-051: aliased power <= -96 dBFS, static and during a bend
// =============================================================================

namespace {

/// SC-009 state: kSawAnchor with edge = 1 and shift = +1.
Controls aliasState() {
    Controls c = SpectralShapeRecipe::kSawAnchor;
    c.edge = 1.0f;
    c.shift = 1.0f;
    return c;
}

struct AliasStats {
    double worstDbfs = -1.0e9;
    double maxExcluded = 0.0;
};

void requireAliasFree(const float* frame, std::size_t len, double fs, float f0, AliasStats& st) {
    const PT::AliasResult a = PT::aliasedPower(frame, len, fs, static_cast<double>(f0));
    INFO("fs " << fs << " f0 " << f0 << " FFT " << len << ": aliased " << a.aliasedDbfs << " dBFS, excluded fraction "
               << a.excludedFraction << ", total " << a.totalDb << " dBFS");
    REQUIRE(a.excludedFraction <= 0.5);
    REQUIRE(a.aliasedDbfs <= -96.0);
    st.worstDbfs = std::max(st.worstDbfs, a.aliasedDbfs);
    st.maxExcluded = std::max(st.maxExcluded, a.excludedFraction);
}

/// Static arm at one MIDI note: fresh Reset note, frame >= 50 ms after it, aliasFftLength frame.
void requireStaticAliasFree(ProfundumCore& core, int midi, AliasStats& st) {
    const double fs = PT::coreSampleRate(core);
    const float f0 = midiToHz(midi);
    const std::size_t len = PT::aliasFftLength(fs, static_cast<double>(f0));
    INFO("MIDI " << midi);
    const std::vector<float> frame = steadyFrame(core, aliasState(), f0, len);
    requireAliasFree(frame.data(), len, fs, f0, st);
}

constexpr int kBendLowMidi = 24;
constexpr int kBendHighMidi = 108;
constexpr double kBendLegSeconds = 0.010;   // 1 semitone per 10 ms leg (100 semitones/s)
/// Frame start after each leg's ramp: the spec's maximum, 2 x kControlInterval.
constexpr std::size_t kBendFrameOffset = 2 * ProfundumCore::kControlInterval;

struct BendLeg {
    std::size_t frameStart;   ///< index into BendRender::L
    std::size_t frameLen;     ///< aliasFftLength at the held f0
    float heldF0;
    int fromMidi;
    int toMidi;
};

struct BendRender {
    std::vector<float> L, R;
    std::vector<BendLeg> legs;
};

/// lowMidi -> highMidi -> lowMidi in 1-semitone steps (the leg end points).
std::vector<int> bendPath(int lowMidi, int highMidi) {
    std::vector<int> path;
    for (int m = lowMidi; m <= highMidi; ++m)
        path.push_back(m);
    for (int m = highMidi - 1; m >= lowMidi; --m)
        path.push_back(m);
    return path;
}

/// SC-009 bend fixture, one continuous render: a Reset noteOn at lowMidi, then per 1-semitone leg a
/// 10 ms log-linear f0PerSample ramp followed by a hold of (frame length + 50 ms) at the held f0.
/// Each leg's analysis frame starts kBendFrameOffset samples after the ramp's last sample.
BendRender renderBend(double fs, int lowMidi, int highMidi) {
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(fs);
    core->setRetriggerPhase(ProfundumCore::RetriggerPhase::Reset);
    core->setControls(aliasState());
    core->noteOn(midiToHz(lowMidi));

    const std::size_t ramp = samplesAt(kBendLegSeconds, fs);
    const std::size_t settle = samplesAt(kSettleSeconds, fs);
    REQUIRE(ramp >= 2);
    REQUIRE(kBendFrameOffset <= settle);   // the frame fits inside the hold
    constexpr std::size_t kBlock = ProfundumCore::kControlInterval;

    const std::vector<int> path = bendPath(lowMidi, highMidi);
    BendRender out;
    for (std::size_t i = 1; i < path.size(); ++i) {
        const float fa = midiToHz(path[i - 1]);
        const float fb = midiToHz(path[i]);
        const std::size_t frameLen = PT::aliasFftLength(fs, static_cast<double>(fb));
        const std::size_t legLen = ramp + frameLen + settle;   // hold = frame length + 50 ms
        std::vector<float> traj = PT::logLinearRamp(fa, fb, ramp);
        traj.back() = fb;
        traj.resize(legLen, fb);

        const std::size_t legStart = out.L.size();
        out.L.resize(legStart + legLen, 0.0f);
        out.R.resize(legStart + legLen, 0.0f);
        for (std::size_t pos = 0; pos < legLen; pos += kBlock) {
            const std::size_t n = std::min(kBlock, legLen - pos);
            core->processBlock(out.L.data() + legStart + pos, out.R.data() + legStart + pos, n, traj.data() + pos);
        }
        out.legs.push_back(BendLeg{.frameStart = legStart + ramp + kBendFrameOffset,
                                   .frameLen = frameLen,
                                   .heldF0 = fb,
                                   .fromMidi = path[i - 1],
                                   .toMidi = path[i]});
    }
    PT::requireLREqual(out.L.data(), out.R.data(), out.L.size());
    return out;
}

void requireBendAliasFree(double fs, int lowMidi, int highMidi, AliasStats& st) {
    const BendRender b = renderBend(fs, lowMidi, highMidi);
    REQUIRE(b.legs.size() == static_cast<std::size_t>(2 * (highMidi - lowMidi)));
    for (const BendLeg& leg : b.legs) {
        INFO("leg MIDI " << leg.fromMidi << " -> " << leg.toMidi << ", frame start " << leg.frameStart);
        REQUIRE(leg.frameStart + leg.frameLen <= b.L.size());
        requireAliasFree(b.L.data() + leg.frameStart, leg.frameLen, fs, leg.heldF0, st);
    }
}

}  // namespace

TEST_CASE("ProfundumCore_NoAliasingAllNotes", "[systems][profundum][long]") {
    for (const double fs : {44100.0, 48000.0, 96000.0}) {
        auto core = std::make_unique<ProfundumCore>();
        core->prepare(fs);
        AliasStats st;
        for (int midi = 0; midi <= 127; ++midi)
            requireStaticAliasFree(*core, midi, st);
        WARN("SC-009 static fs " << fs << " (MIDI 0-127, kSawAnchor edge 1 shift 1): worst aliased " << st.worstDbfs
                                 << " dBFS (bar -96), max excluded fraction " << st.maxExcluded << " (bar 0.5)");
    }
}

TEST_CASE("ProfundumCore_NoAliasingAllNotesSmoke", "[systems][profundum]") {
    // The extremes (MIDI 0/60/127 at 48 kHz) plus the band where the SC-009 state's crest factor is
    // highest (h64 still un-tapered, guard off or mild): MIDI 21-62 railed the bank's +-2 output clamp
    // before the core's bank headroom (measured 2026-10-10: -74.5 dBFS at MIDI 21 / 44.1 kHz, -55 dBFS
    // at MIDI 44, -54.4 dBFS at MIDI 58 / 96 kHz). One note per rate from that band runs per push.
    struct Arm {
        double fs;
        std::array<int, 4> notes;
        std::size_t count;
    };
    constexpr std::array<Arm, 3> kArms{{
        {kFs48, {0, 60, 127, 36}, 4},
        {44100.0, {21, 44, 0, 0}, 2},
        {96000.0, {58, 0, 0, 0}, 1},
    }};
    for (const Arm& arm : kArms) {
        auto core = std::make_unique<ProfundumCore>();
        core->prepare(arm.fs);
        AliasStats st;
        for (std::size_t k = 0; k < arm.count; ++k)
            requireStaticAliasFree(*core, arm.notes[k], st);
        WARN("SC-009 static smoke fs " << arm.fs << ": worst aliased " << st.worstDbfs
                                       << " dBFS (bar -96), max excluded fraction " << st.maxExcluded << " (bar 0.5)");
    }
}

TEST_CASE("ProfundumCore_NoAliasingDuringBend", "[systems][profundum][long]") {
    for (const double fs : {44100.0, 48000.0}) {
        AliasStats st;
        requireBendAliasFree(fs, kBendLowMidi, kBendHighMidi, st);
        WARN("SC-009 bend fs " << fs << " (MIDI 24 -> 108 -> 24, 10 ms legs): worst aliased " << st.worstDbfs
                               << " dBFS (bar -96), max excluded fraction " << st.maxExcluded << " (bar 0.5)");
    }
}

TEST_CASE("ProfundumCore_NoAliasingDuringBendSmoke", "[systems][profundum]") {
    // The top of the bend, where partials cross the cap on every leg: MIDI 100 -> 108 -> 100, 48 kHz.
    AliasStats st;
    requireBendAliasFree(kFs48, 100, kBendHighMidi, st);
    WARN("SC-009 bend smoke (MIDI 100 -> 108 -> 100, 48 kHz): worst aliased "
         << st.worstDbfs << " dBFS (bar -96), max excluded fraction " << st.maxExcluded << " (bar 0.5)");

    // The bottom of the bend at 44.1 kHz, where the full case's first leg (24 -> 25) railed the bank's
    // output clamp (-69.9 dBFS, 2026-10-10): MIDI 24 -> 26 -> 24.
    AliasStats low;
    requireBendAliasFree(44100.0, kBendLowMidi, kBendLowMidi + 2, low);
    WARN("SC-009 bend smoke (MIDI 24 -> 26 -> 24, 44.1 kHz): worst aliased "
         << low.worstDbfs << " dBFS (bar -96), max excluded fraction " << low.maxExcluded << " (bar 0.5)");
}

// =============================================================================
// SC-010(b), FR-042/FR-043/FR-050: control-rate and pitch-update sidebands <= -60 dB re total
// =============================================================================

namespace {

/// = 750 / 5 1/3: at 48 kHz the k = 1, 2 sidebands of the kControlInterval = 32 control rate
/// (1500/140.625 = 10 2/3; D-4 ruling, plan S9.4) and of the 16-sample pitch cadence
/// (3000/140.625 = 21 1/3) fall >= f0/3 from every harmonic (spec SC-010(b), plan S4.6).
constexpr double kSidebandF0 = 140.625;
constexpr double kSweepRateHz = 2.0;
constexpr double kVibratoRateHz = 5.0;
constexpr double kVibratoSemitones = 2.0;
constexpr std::size_t kSidebandFrame = std::size_t{1} << 17;
constexpr double kSidebandStartSeconds = 1.0;   // frame starts >= 1 s into the modulation

std::size_t sidebandTotalSamples() { return samplesAt(kSidebandStartSeconds, kFs48) + kSidebandFrame; }

/// 0 -> 1 -> 0 triangle at rateHz, value 0 at t = 0.
double triangle01(double t, double rateHz) {
    const double x = t * rateHz;
    const double p = x - std::floor(x);
    return p < 0.5 ? 2.0 * p : 2.0 - 2.0 * p;
}

struct Lever {
    const char* name;
    float Controls::*field;
    float lo;
    float hi;
};

constexpr std::array<Lever, 4> kLevers{{
    {"depth", &Controls::depth, 0.0f, 1.0f},
    {"body", &Controls::body, 0.0f, 1.0f},
    {"edge", &Controls::edge, 0.0f, 1.0f},
    {"shift", &Controls::shift, -1.0f, 1.0f},
}};

/// The 2 Hz full-range triangle trajectory on one lever at sample i (mid grid otherwise). The
/// core latches it at every control instant (renderLeverTriangle); the SC-010 references evaluate
/// it at every sample.
Controls leverTriangleAt(const Lever& lever, std::size_t i) {
    Controls c = PT::midGrid();
    const double t = static_cast<double>(i) / kFs48;
    c.*lever.field = lever.lo + (lever.hi - lever.lo) * static_cast<float>(triangle01(t, kSweepRateHz));
    return c;
}

/// Mid grid at f0 = 140.625 Hz, 48 kHz, with a 2 Hz full-range triangle on one lever. setControls
/// runs before every grid-aligned kControlInterval-sample block, so each control update latches
/// the triangle at its own first sample.
PT::Render renderLeverTriangle(const Lever& lever, std::size_t total) {
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(kFs48);
    PT::Render r;
    r.L.assign(total, 0.0f);
    r.R.assign(total, 0.0f);
    Controls c = PT::midGrid();
    c.*lever.field = lever.lo;
    core->setRetriggerPhase(ProfundumCore::RetriggerPhase::Reset);
    core->setControls(c);
    core->noteOn(static_cast<float>(kSidebandF0));
    constexpr std::size_t kBlock = ProfundumCore::kControlInterval;
    for (std::size_t pos = 0; pos < total; pos += kBlock) {
        const std::size_t n = std::min(kBlock, total - pos);
        core->setControls(leverTriangleAt(lever, pos));
        core->processBlock(r.L.data() + pos, r.R.data() + pos, n);
    }
    PT::requireLREqual(r.L.data(), r.R.data(), r.L.size());
    return r;
}

/// 5 Hz ±2-semitone vibrato around f0 = 140.625 Hz, one value per sample.
std::vector<float> vibratoTrajectory(std::size_t n) {
    std::vector<float> traj(n);
    for (std::size_t i = 0; i < traj.size(); ++i) {
        const double t = static_cast<double>(i) / kFs48;
        traj[i] = static_cast<float>(
            kSidebandF0 * std::exp2(kVibratoSemitones / 12.0 * std::sin(PT::kTwoPi * kVibratoRateHz * t)));
    }
    return traj;
}

/// kSineAnchor at f0 = 140.625 Hz with a 5 Hz ±2-semitone vibrato through f0PerSample. `traj`
/// holds one spare block beyond `total`, so renderCore can never read past the end.
PT::Render renderVibrato(std::size_t total, const std::vector<float>& traj) {
    REQUIRE(traj.size() >= total + ProfundumCore::kControlInterval);
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(kFs48);
    const double seconds = static_cast<double>(total) / kFs48;
    PT::Render r = PT::renderCore(*core, SpectralShapeRecipe::kSineAnchor, static_cast<float>(kSidebandF0), seconds,
                                  ProfundumCore::RetriggerPhase::Reset, false, ProfundumCore::kControlInterval,
                                  traj.data());
    REQUIRE(r.L.size() == total);
    PT::requireLREqual(r.L.data(), r.R.data(), r.L.size());
    return r;
}

/// Amended SC-010(b) (D-2 ruling): the region power of `x` (a residual against the ideal) re the
/// core render's total power, over the 2^17 frame starting kSidebandStartSeconds in. The 25 %
/// non-vacuity guard is asserted here.
double residualSidebandDb(const std::string& name, const std::vector<float>& x, const PT::Render& core,
                          std::size_t pitchInterval, double amRateHz, double vibratoPeakHz) {
    const std::size_t start = samplesAt(kSidebandStartSeconds, kFs48);
    REQUIRE(x.size() == core.L.size());
    REQUIRE(core.L.size() >= start + kSidebandFrame);
    const PT::SidebandResult s = PT::sidebandPower(x.data() + start, kFs48, kSidebandF0,
                                                   ProfundumCore::kControlInterval, pitchInterval, amRateHz,
                                                   vibratoPeakHz, core.L.data() + start);
    INFO(name << ": sideband power " << s.powerDbReTotal << " dB re core total, remaining fraction "
              << s.remainingFraction);
    REQUIRE(s.remainingFraction >= 0.25);
    return s.powerDbReTotal;
}

void requireSidebandsBelow(const std::string& name, const PT::Render& r, const std::vector<double>& ideal,
                           std::size_t pitchInterval, double amRateHz, double vibratoPeakHz) {
    const std::vector<float> res = PT::residual(r.L, ideal);
    const double db = residualSidebandDb(name, res, r, pitchInterval, amRateHz, vibratoPeakHz);
    INFO(name << ": residual (core - ideal) sideband power " << db << " dB re core total");
    WARN("SC-010(b) " << name << ": residual (core - ideal) sideband power " << db
                      << " dB re core total (bar -60)");
    REQUIRE(db <= -60.0);
}

}  // namespace

TEST_CASE("ProfundumCore_NoZipperControlSweepsSidebands", "[systems][profundum]") {
    const std::size_t total = sidebandTotalSamples();
    const auto f0Const = [](std::size_t) { return kSidebandF0; };
    double heldRawLoudestDb = -1000.0;
    for (const Lever& lever : kLevers) {
        const std::string name = std::string("2 Hz triangle on ") + lever.name;
        const PT::Render r = renderLeverTriangle(lever, total);
        const auto controlsAt = [&](std::size_t i) { return leverTriangleAt(lever, i); };
        const std::vector<double> ideal = PT::renderIdeal(kFs48, total, controlsAt, f0Const);
        requireSidebandsBelow(name, r, ideal, 0, kSweepRateHz, 0.0);

        // Positive control (teeth): the held-raw staircase minus the ideal, same measure.
        const std::vector<double> heldRaw = PT::renderHeldRaw(kFs48, total, controlsAt, f0Const);
        const std::vector<float> staircase = PT::residual(heldRaw, ideal);
        const double heldDb = residualSidebandDb(name + " (held raw)", staircase, r, 0, kSweepRateHz, 0.0);
        WARN("SC-010(b) positive control " << name << ": held raw - ideal sideband power " << heldDb
                                           << " dB re core total (must exceed -60 on at least one lever)");
        heldRawLoudestDb = std::max(heldRawLoudestDb, heldDb);
    }
    INFO("loudest held raw - ideal sideband power " << heldRawLoudestDb << " dB re core total");
    REQUIRE(heldRawLoudestDb > -60.0);

    // Vibrato: the pitch-update clause (U = kPitchUpdateInterval) applies; there is no triangle AM,
    // so only the Carson band (n·Δf_peak + 2·5 Hz + 7 bins, Δf_peak the upward peak deviation) is
    // subtracted. The ideal follows the per-sample f, never the core's U-held f, delayed by the
    // U-hold's group delay (U − 1)/2 (PT::pitchHoldAlignedF0).
    const double vibratoPeakHz = kSidebandF0 * (std::exp2(kVibratoSemitones / 12.0) - 1.0);
    const std::vector<float> traj = vibratoTrajectory(total + ProfundumCore::kControlInterval);
    const PT::Render r = renderVibrato(total, traj);
    const std::vector<double> ideal = PT::renderIdeal(
        kFs48, total, [](std::size_t) { return SpectralShapeRecipe::kSineAnchor; },
        [&](std::size_t i) { return PT::pitchHoldAlignedF0(traj, i); });
    requireSidebandsBelow("5 Hz +-2 semitone vibrato (kSineAnchor)", r, ideal, ProfundumCore::kPitchUpdateInterval,
                          0.0, vibratoPeakHz);
}

// =============================================================================
// SC-011(a), FR-030/FR-031: the Low Note Guard reduces R_pres one octave below the onset
// (public API only; the guard is always on)
// =============================================================================

TEST_CASE("ProfundumCore_LowNoteGuardReducesUpperEnergy", "[systems][profundum]") {
    constexpr float kOnset = SpectralShapeRecipe::kLowNoteGuardOnsetHz;   // 32.70 Hz, C1
    constexpr float kOctaveBelow = kOnset * 0.5f;                         // 16.35 Hz, C0
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(kFs48);
    struct GuardState {
        const char* name;
        Controls c;
        const char* model;   // plan S3.6 model reduction
    };
    const std::array<GuardState, 2> states{{
        {"kSawAnchor", SpectralShapeRecipe::kSawAnchor, "10.45"},
        {"midGrid", PT::midGrid(), "8.56"},
    }};
    for (const GuardState& s : states) {
        const double atOnset = renderedDescriptors(*core, s.c, kOnset).rPresDb;
        const double below = renderedDescriptors(*core, s.c, kOctaveBelow).rPresDb;
        INFO(s.name << ": R_pres at " << kOnset << " Hz " << dbText(atOnset) << " dB, at " << kOctaveBelow << " Hz "
                    << dbText(below) << " dB");
        REQUIRE_FALSE(PT::isNegInf(atOnset));
        REQUIRE_FALSE(PT::isPosInf(atOnset));
        REQUIRE_FALSE(PT::isPosInf(below));
        REQUIRE_FALSE(PT::isNegInf(below));
        const double reduction = atOnset - below;
        WARN("SC-011(a) " << s.name << ": R_pres reduction one octave below the onset " << reduction
                          << " dB (bar 6, model " << s.model << ")");
        REQUIRE(below <= atOnset - 6.0);
    }
}

// =============================================================================
// SC-017 rate arm: prepare 48 -> 96 -> 44.1 kHz on one core re-establishes SC-001 and SC-008
// =============================================================================

TEST_CASE("ProfundumCore_SampleRateChange", "[systems][profundum]") {
    constexpr std::array<double, 3> kRates{48000.0, 96000.0, 44100.0};
    constexpr std::array<int, 2> kCandidates{1, 8};
    auto core = std::make_unique<ProfundumCore>();
    for (const double fs : kRates) {
        core->prepare(fs);
        INFO("fs " << fs);
        REQUIRE(PT::coreSampleRate(*core) == fs);

        // SC-001, n = 1 and 8 at MIDI 36.
        double worstCents = 0.0;
        const std::vector<int> measured = checkPartialFrequencies(*core, 36, kCandidates, worstCents);
        REQUIRE(contains(measured, 1));
        REQUIRE(contains(measured, 8));

        // SC-008 at mid grid, C1/C2/C3. At 44.1 kHz the C3 harmonics above the 7 723 Hz taper start
        // are excluded (plan S1 C-8 (i)); at 48 and 96 kHz nothing may be in the taper.
        const bool taperExclusion = fs == 44100.0;
        InvarianceStats st;
        for (const float f0 : kNotesC1toC3)
            checkSpectrumInvariant(*core, PT::midGrid(), f0, taperExclusion, st);
        WARN("SC-017 fs " << fs << ": SC-001 MIDI 36 worst " << worstCents << " cent (bar < 0.1); SC-008 mid grid "
                          << "worst " << st.worstDb << " dB over " << st.compared << " harmonics (bar 0.5), "
                          << st.excluded << " excluded in the taper");
    }
}

// =============================================================================
// SC-019 render arm, FR-014: the five Body colours distinct and extremal, rendered at C2.
// The recipe-vector arm keeps the spec name ProfundumCore_BodyColoursDistinct (recipe TU,
// dsp_processors_tests); this render arm is ProfundumCore_BodyColoursDistinctRendered.
// =============================================================================

TEST_CASE("ProfundumCore_BodyColoursDistinctRendered", "[systems][profundum]") {
    constexpr int kN = 64;
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(kFs48, kN);
    requireNoHarmonicInTaper(kC2, kFs48, kN);

    struct Colour {
        const char* name;
        Descriptors d;
    };
    const std::array<Colour, 5> colours{{
        {"kBodyRound", renderedDescriptors(*core, SpectralShapeRecipe::kBodyRound, kC2)},
        {"kBodyHollow", renderedDescriptors(*core, SpectralShapeRecipe::kBodyHollow, kC2)},
        {"kBodyWoody", renderedDescriptors(*core, SpectralShapeRecipe::kBodyWoody, kC2)},
        {"kBodyNasal", renderedDescriptors(*core, SpectralShapeRecipe::kBodyNasal, kC2)},
        {"kBodyThick", renderedDescriptors(*core, SpectralShapeRecipe::kBodyThick, kC2)},
    }};
    constexpr std::size_t kRound = 0;
    constexpr std::size_t kHollow = 1;
    constexpr std::size_t kNasal = 3;
    constexpr std::size_t kThick = 4;

    // Pairwise body-colour distance >= 6 dB.
    double minDistance = 1.0e9;
    for (std::size_t i = 0; i < colours.size(); ++i) {
        for (std::size_t j = i + 1; j < colours.size(); ++j) {
            const double dist = PT::bodyColourDistance(colours[i].d, colours[j].d);
            INFO(colours[i].name << " vs " << colours[j].name << ": " << dist << " dB");
            REQUIRE(dist >= 6.0);
            minDistance = std::min(minDistance, dist);
        }
    }

    // Extremal margins over the runner-up (the other four).
    double roundMargin = 1.0e9;    // min over others of C, minus Round's C
    double nasalMargin = 1.0e9;    // min over others of sigma, minus Nasal's sigma
    double thickMargin = 1.0e9;    // Thick's R_body minus max over others
    double hollowMargin = 1.0e9;   // Hollow's unclipped odd/even minus max over others (finite case)
    const Descriptors& hollow = colours[kHollow].d;
    REQUIRE_FALSE(PT::isNegInf(hollow.oddEvenDb));
    for (std::size_t k = 0; k < colours.size(); ++k) {
        const Descriptors& o = colours[k].d;
        INFO("other " << colours[k].name << " C " << o.centroidOct << " sigma " << o.spreadOct << " R_body "
                      << dbText(o.rBodyDb) << " odd/even " << dbText(o.oddEvenDb));
        if (k != kRound)
            roundMargin = std::min(roundMargin, o.centroidOct - colours[kRound].d.centroidOct);
        if (k != kNasal)
            nasalMargin = std::min(nasalMargin, o.spreadOct - colours[kNasal].d.spreadOct);
        if (k != kThick)
            thickMargin = std::min(thickMargin, colours[kThick].d.rBodyDb - o.rBodyDb);
        if (k != kHollow) {
            // Unclipped odd/even: Hollow exceeds every other by >= 1 dB. A +inf Hollow (every
            // rendered even bin exactly 0) beats any finite value; a tie at +inf fails.
            REQUIRE_FALSE(PT::isPosInf(o.oddEvenDb));
            if (!PT::isPosInf(hollow.oddEvenDb) && !PT::isNegInf(o.oddEvenDb))
                hollowMargin = std::min(hollowMargin, hollow.oddEvenDb - o.oddEvenDb);
        }
    }
    INFO("round C margin " << roundMargin << " oct, nasal sigma margin " << nasalMargin << " oct, thick R_body margin "
                           << thickMargin << " dB, hollow odd/even margin " << hollowMargin << " dB (hollow "
                           << dbText(hollow.oddEvenDb) << ")");
    REQUIRE(roundMargin >= 0.05);
    REQUIRE(nasalMargin >= 0.05);
    REQUIRE(thickMargin >= 1.0);
    REQUIRE(hollowMargin >= 1.0);

    WARN("SC-019 render (C2): min pairwise body-colour distance " << minDistance << " dB (bar 6, model 16.24)");
    WARN("SC-019 render (C2): kBodyRound lowest C by " << roundMargin << " oct (bar 0.05, model 0.375); kBodyHollow "
                                                       << "highest odd/even " << dbText(hollow.oddEvenDb) << " dB by "
                                                       << hollowMargin << " dB (bar 1, model 18.9)");
    WARN("SC-019 render (C2): kBodyNasal smallest sigma by " << nasalMargin << " oct (bar 0.05, model 0.064); "
                                                             << "kBodyThick highest R_body by " << thickMargin
                                                             << " dB (bar 1, model 1.27)");
}

// =============================================================================
// SC-022 render arm, FR-013/FR-015/FR-023: emphasis -1 removes every even harmonic, rendered
// at C2, over the notation grid and the mid grid x edge x curvature; FR-023 floor at every step
// =============================================================================

namespace {

/// The SC-022 grid: the 81-point notation grid plus mid grid x edge {0, 0.5, 1} x curvature
/// {0, 0.5, 1} (90 points; emphasis is set per step).
std::vector<Controls> emphasisGrid() {
    std::vector<Controls> pts;
    for (int iD = 0; iD < 3; ++iD)
        for (int iB = 0; iB < 3; ++iB)
            for (int iE = 0; iE < 3; ++iE)
                for (int iS = 0; iS < 3; ++iS)
                    pts.push_back(PT::gridPoint(iD, iB, iE, iS));
    for (const float edge : kUnit) {
        for (const float curvature : kUnit) {
            Controls c = PT::midGrid();
            c.edge = edge;
            c.bodyCurvature = curvature;
            pts.push_back(c);
        }
    }
    return pts;
}

struct EmphasisStats {
    double worstEvenDb = -1.0e9;    ///< loudest rendered even harmonic re h1 at emphasis -1
    double minFloorMargin = 1.0e9;  ///< min (odd/even - neutral) over the non-neutral finite steps
    int evensChecked = 0;
    int highEvensAtEdge1 = 0;       ///< evens n >= 10 checked at edge = 1
};

/// At emphasis -1 every even n <= -50 dB re h1 (rendered, C2). With `floorSweep`, also the 33-step
/// emphasis sweep over [-1, 0]: odd/even >= the emphasis-0 (neutral) value at every step (FR-023).
void checkEmphasisPoint(ProfundumCore& core, Controls c, bool floorSweep, EmphasisStats& st) {
    INFO(controlsText(c));
    const std::vector<float> steps = floorSweep ? sweep33(-1.0f, 0.0f) : std::vector<float>{-1.0f};
    REQUIRE(steps.front() == -1.0f);
    std::vector<double> oddEven;
    oddEven.reserve(steps.size());
    for (std::size_t i = 0; i < steps.size(); ++i) {
        c.bodyEmphasis = steps[i];
        const std::vector<double> w = renderedPowers(core, c, kC2);
        REQUIRE(w[0] > 0.0);
        if (i == 0) {
            for (std::size_t n = 2; n <= w.size(); n += 2) {
                INFO("emphasis -1, even n " << n << ": " << PT::safeDb10(w[n - 1] / w[0]) << " dB re h1");
                REQUIRE(w[n - 1] <= w[0] * 1.0e-5);   // <= -50 dB re h1
                st.worstEvenDb = std::max(st.worstEvenDb, PT::safeDb10(w[n - 1] / w[0]));
                ++st.evensChecked;
                if (n >= 10 && c.edge == 1.0f)
                    ++st.highEvensAtEdge1;
            }
        }
        oddEven.push_back(PT::describePowers(w).oddEvenDb);
    }
    if (!floorSweep)
        return;

    const double neutral = oddEven.back();   // emphasis 0
    INFO("neutral odd/even " << dbText(neutral) << " dB");
    REQUIRE_FALSE(PT::isPosInf(neutral));
    REQUIRE_FALSE(PT::isNegInf(neutral));
    for (std::size_t i = 0; i < oddEven.size(); ++i) {
        INFO("emphasis " << steps[i] << " odd/even " << dbText(oddEven[i]) << " dB");
        REQUIRE_FALSE(PT::isNegInf(oddEven[i]));
        if (PT::isPosInf(oddEven[i]))
            continue;   // every rendered even bin exactly 0: above any finite floor
        REQUIRE(oddEven[i] >= neutral);   // FR-023 floor
        if (i + 1 < oddEven.size())
            st.minFloorMargin = std::min(st.minFloorMargin, oddEven[i] - neutral);
    }
}

}  // namespace

TEST_CASE("ProfundumCore_EmphasisOddOnlyRendered", "[systems][profundum][long]") {
    constexpr int kN = 64;
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(kFs48, kN);
    requireNoHarmonicInTaper(kC2, kFs48, kN);
    const std::vector<Controls> grid = emphasisGrid();
    REQUIRE(grid.size() == 90);
    EmphasisStats st;
    for (const Controls& c : grid)
        checkEmphasisPoint(*core, c, true, st);
    REQUIRE(st.highEvensAtEdge1 > 0);
    WARN("SC-022 render (C2, 90 points): loudest even at emphasis -1 "
         << st.worstEvenDb << " dB re h1 (bar -50), " << st.evensChecked << " evens checked ("
         << st.highEvensAtEdge1 << " with n >= 10 at edge 1); FR-023 floor min margin " << st.minFloorMargin << " dB");
}

TEST_CASE("ProfundumCore_EmphasisOddOnlyRenderedSmoke", "[systems][profundum]") {
    constexpr int kN = 64;
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(kFs48, kN);
    requireNoHarmonicInTaper(kC2, kFs48, kN);
    EmphasisStats st;
    // Mid grid x edge x curvature at emphasis -1 (includes edge = 1), plus the full floor sweep at
    // mid grid with edge = 1.
    for (const float edge : kUnit) {
        for (const float curvature : kUnit) {
            Controls c = PT::midGrid();
            c.edge = edge;
            c.bodyCurvature = curvature;
            checkEmphasisPoint(*core, c, edge == 1.0f && curvature == 0.5f, st);
        }
    }
    REQUIRE(st.highEvensAtEdge1 > 0);
    WARN("SC-022 render smoke (C2): loudest even at emphasis -1 " << st.worstEvenDb << " dB re h1 (bar -50); "
                                                                  << "FR-023 floor min margin " << st.minFloorMargin
                                                                  << " dB");
}

// =============================================================================
// Evaluation (not a gate): listening renders, 32-bit float stereo WAVs (plan S8.3 "Evaluation",
// S9.3 OQ-3 E1 renders, R-14 C4 at 44.1 kHz). Hidden tag: run explicitly with "[.listen]".
// =============================================================================

namespace {

/// Relative to the working directory: run the exe from the repository root.
std::filesystem::path listenDir() {
    return std::filesystem::path("build") / "windows-x64-release" / "profundum-listen";
}

constexpr double kListenStaticSeconds = 3.0;

void putU16(std::vector<unsigned char>& b, std::uint16_t v) {
    b.push_back(static_cast<unsigned char>(v & 0xFFu));
    b.push_back(static_cast<unsigned char>((v >> 8) & 0xFFu));
}

void putU32(std::vector<unsigned char>& b, std::uint32_t v) {
    for (int shift = 0; shift < 32; shift += 8)
        b.push_back(static_cast<unsigned char>((v >> shift) & 0xFFu));
}

void putTag(std::vector<unsigned char>& b, const char* tag) {
    for (std::size_t i = 0; i < 4; ++i)
        b.push_back(static_cast<unsigned char>(tag[i]));
}

/// Minimal RIFF/WAVE writer: WAVE_FORMAT_IEEE_FLOAT (3), 2 channels, 32-bit, little-endian, with
/// the 18-byte fmt chunk and the fact chunk that non-PCM formats carry. tests/test_helpers has no
/// WAV writer (grep "writeWav\|RIFF" finds none), so it is local to this TU.
bool writeWavFloatStereo(const std::filesystem::path& path, const std::vector<float>& L, const std::vector<float>& R,
                         double fs) {
    if (L.size() != R.size())
        return false;
    const std::uint64_t frames = L.size();
    const std::uint64_t dataBytes = frames * 8u;   // 2 channels x 4 bytes
    if (dataBytes > 0xFFFF0000ull)
        return false;
    const auto sr = static_cast<std::uint32_t>(std::llround(fs));
    std::vector<unsigned char> b;
    b.reserve(static_cast<std::size_t>(dataBytes) + 64u);
    putTag(b, "RIFF");
    putU32(b, static_cast<std::uint32_t>(4u + (8u + 18u) + (8u + 4u) + 8u + dataBytes));
    putTag(b, "WAVE");
    putTag(b, "fmt ");
    putU32(b, 18u);
    putU16(b, 3u);        // WAVE_FORMAT_IEEE_FLOAT
    putU16(b, 2u);        // channels
    putU32(b, sr);
    putU32(b, sr * 8u);   // byte rate
    putU16(b, 8u);        // block align
    putU16(b, 32u);       // bits per sample
    putU16(b, 0u);        // cbSize
    putTag(b, "fact");
    putU32(b, 4u);
    putU32(b, static_cast<std::uint32_t>(frames));
    putTag(b, "data");
    putU32(b, static_cast<std::uint32_t>(dataBytes));
    for (std::size_t i = 0; i < L.size(); ++i) {
        putU32(b, std::bit_cast<std::uint32_t>(L[i]));
        putU32(b, std::bit_cast<std::uint32_t>(R[i]));
    }
    std::ofstream f(path, std::ios::binary);
    if (!f)
        return false;
    f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    return static_cast<bool>(f);
}

void writeListen(const std::string& name, const std::vector<float>& L, const std::vector<float>& R, double fs,
                 std::vector<std::string>& written) {
    const std::filesystem::path p = listenDir() / name;
    INFO("WAV " << p.string());
    REQUIRE(writeWavFloatStereo(p, L, R, fs));
    written.push_back(std::filesystem::absolute(p).string());
}

/// A fresh Reset render of `seconds` at (c, f0) on a prepared core, L == R asserted.
PT::Render listenStatic(ProfundumCore& core, const Controls& c, float f0, double seconds) {
    PT::Render r = PT::renderCore(core, c, f0, seconds);
    PT::requireLREqual(r.L.data(), r.R.data(), r.L.size());
    return r;
}

}  // namespace

TEST_CASE("ProfundumCore_ListeningRenders", "[systems][profundum][.listen]") {
    std::error_code ec;
    std::filesystem::create_directories(listenDir(), ec);
    INFO("create " << listenDir().string() << ": " << ec.message());
    REQUIRE_FALSE(ec);
    std::vector<std::string> written;
    const std::vector<NamedState> states = namedStates();

    // C0-C4 static notes at the 12 named states, 48 kHz; plus C4 at 44.1 kHz (plan R-14).
    {
        auto core = std::make_unique<ProfundumCore>();
        core->prepare(kFs48);
        for (int octave = 0; octave <= 4; ++octave) {
            const float f0 = midiToHz(12 + 12 * octave);
            for (const NamedState& s : states) {
                const PT::Render r = listenStatic(*core, s.c, f0, kListenStaticSeconds);
                writeListen("static_C" + std::to_string(octave) + "_" + s.name + "_48k.wav", r.L, r.R, kFs48,
                            written);
            }
        }
        auto core441 = std::make_unique<ProfundumCore>();
        core441->prepare(44100.0);
        for (const NamedState& s : states) {
            const PT::Render r = listenStatic(*core441, s.c, midiToHz(60), kListenStaticSeconds);
            writeListen(std::string("static_C4_") + s.name + "_44k1.wav", r.L, r.R, 44100.0, written);
        }
    }

    // The SC-009 bend (48 kHz, the gate's own fixture: 10 ms legs, frame + 50 ms holds).
    {
        const BendRender b = renderBend(kFs48, kBendLowMidi, kBendHighMidi);
        writeListen("bend_sc009_MIDI24-108-24_48k.wav", b.L, b.R, kFs48, written);
    }

    // The four 2 Hz full-range sweeps (the SC-010(b) fixture, f0 = 140.625 Hz).
    for (const Lever& lever : kLevers) {
        const PT::Render r = renderLeverTriangle(lever, sidebandTotalSamples());
        writeListen(std::string("sweep2hz_") + lever.name + "_48k.wav", r.L, r.R, kFs48, written);
    }

    // OQ-3 (plan S9.3): E1 (41.2 Hz) at 64 and at 96 partials, every named state.
    for (const int numPartials : {64, 96}) {
        auto core = std::make_unique<ProfundumCore>();
        core->prepare(kFs48, numPartials);
        REQUIRE(core->numPartials() == numPartials);
        for (const NamedState& s : states) {
            const PT::Render r = listenStatic(*core, s.c, midiToHz(28), kListenStaticSeconds);
            writeListen("E1_N" + std::to_string(numPartials) + "_" + s.name + "_48k.wav", r.L, r.R, kFs48, written);
        }
    }

    std::ostringstream os;
    os << written.size() << " listening WAVs in " << std::filesystem::absolute(listenDir()).string() << ":";
    for (const std::string& w : written)
        os << "\n  " << w;
    WARN(os.str());
}

