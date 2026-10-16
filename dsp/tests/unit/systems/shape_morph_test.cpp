// ==============================================================================
// Profundum Phase 5 (specs/profundum-phase5-shape-character)
// ==============================================================================
// ShapeMorph lifecycle, weights, engage and mono tests.
//   T010: ShapeMorph_Lifecycle, ShapeMorph_RegimeWeights (FR-011),
//         ShapeMorph_DisabledBitIdentical (SC-008 Shape part, C-22),
//         ShapeMorph_LREqual (SC-013(a) Shape part),
//         ShapeMorph_NullPointerEqualsConstantFeed (SC-025(e) Shape part).
//   T013: ShapeMorph_ClickCountPositiveControl, ShapeMorph_AutomationClickFreeSmoke,
//         ShapeMorph_AutomationClickFree [long] (SC-003; FR-016, FR-019; D-11, C-22).
// ==============================================================================

#include "shape_character_test_helpers.h"
#include "shape_morph_probe.h"

#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/core/closed_form_adaa2.h>
#include <krate/dsp/processors/keytracked_tilt.h>
#include <krate/dsp/processors/oversampled_residual_path.h>
#include <krate/dsp/processors/rectifier.h>
#include <krate/dsp/systems/shape_morph.h>
#include <krate/dsp/systems/bass_character_stage.h>
#include <krate/dsp/systems/profundum_core.h>
#include <krate/dsp/processors/harmonic_fold.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

using Krate::DSP::ShapeMorph;
using Krate::DSP::SpectralShapeRecipe;
namespace SCT = Krate::DSP::ShapeCharacterTest;
using ShapeMorphProbe = Krate::DSP::detail::ShapeMorphProbe;

namespace {

constexpr double kFs = 48000.0;

/// Renders L / R in place in blocks of `block` samples (f0 pointer optional, offset per block).
void renderShape(ShapeMorph& m, std::vector<float>& left, std::vector<float>& right, std::size_t block,
                 const float* f0 = nullptr) {
    const std::size_t n = left.size();
    for (std::size_t i = 0; i < n; i += block) {
        const std::size_t len = std::min(block, n - i);
        m.processBlock(left.data() + i, right.data() + i, len, f0 != nullptr ? f0 + i : nullptr);
    }
}

/// Index of the first sample whose bit patterns differ, or n when all match.
std::size_t firstBitMismatch(const std::vector<float>& a, const std::vector<float>& b, std::size_t from = 0) {
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = from; i < n; ++i) {
        if (std::bit_cast<std::uint32_t>(a[i]) != std::bit_cast<std::uint32_t>(b[i]))
            return i;
    }
    return n;
}

/// kSawAnchor at f0 (L) and the same signal rotated by 1/3 period (R), so the channels differ.
void sawStereo(double f0, double seconds, std::vector<float>& left, std::vector<float>& right) {
    left = SCT::fixture(SpectralShapeRecipe::kSawAnchor, f0, kFs, seconds);
    right = left;
    const auto shift = static_cast<std::size_t>(kFs / f0 / 3.0);
    std::rotate(right.begin(), right.begin() + static_cast<std::ptrdiff_t>(shift), right.end());
}

} // namespace

TEST_CASE("ShapeMorph_Lifecycle", "[systems][profundum][shape-character][shape-morph]") {
    static_assert(ShapeMorph::latencySamples() == 0);

    std::vector<float> inL;
    std::vector<float> inR;
    sawStereo(SCT::kE1Hz, 4096.0 / kFs + 0.01, inL, inR);
    inL.resize(4096);
    inR.resize(4096);

    SECTION("unprepared processBlock leaves the buffers unchanged") {
        ShapeMorph m;
        m.setShape(0.5f);
        auto l = inL;
        auto r = inR;
        m.processBlock(l.data(), r.data(), l.size());
        CHECK(firstBitMismatch(l, inL) == l.size());
        CHECK(firstBitMismatch(r, inR) == r.size());
    }

    SECTION("null pointers and zero length are no-ops") {
        ShapeMorph m;
        m.prepare(kFs, 512);
        auto l = inL;
        auto r = inR;
        m.processBlock(nullptr, r.data(), 512);
        m.processBlock(l.data(), nullptr, 512);
        m.processBlock(l.data(), r.data(), 0);
        CHECK(firstBitMismatch(l, inL) == l.size());
        CHECK(firstBitMismatch(r, inR) == r.size());
    }

    SECTION("right == left equals channel 0 of an L == R stereo render") {
        for (const float shape : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
            INFO("SHAPE " << shape);
            ShapeMorph mono;
            ShapeMorph stereo;
            for (ShapeMorph* m : {&mono, &stereo}) {
                m->setShape(shape);
                m->setFundamentalHz(static_cast<float>(SCT::kE1Hz));
                m->prepare(kFs, 512);
            }
            auto buf = inL;
            mono.processBlock(buf.data(), buf.data(), buf.size());
            auto l = inL;
            auto r = inL;
            stereo.processBlock(l.data(), r.data(), l.size());
            CHECK(firstBitMismatch(buf, l) == buf.size());
            CHECK(firstBitMismatch(buf, r) == buf.size());
        }
    }

    SECTION("one 4096-sample call equals eight 512-sample calls (long calls split)") {
        ShapeMorph a;
        ShapeMorph b;
        for (ShapeMorph* m : {&a, &b}) {
            m->setFundamentalHz(static_cast<float>(SCT::kE1Hz));
            m->prepare(kFs, 512);
            m->setShape(0.3f);  // after prepare: the smoother ramps inside the render
        }
        auto la = inL;
        auto ra = inR;
        a.processBlock(la.data(), ra.data(), la.size());
        auto lb = inL;
        auto rb = inR;
        renderShape(b, lb, rb, 512);
        CHECK(firstBitMismatch(la, lb) == la.size());
        CHECK(firstBitMismatch(ra, rb) == ra.size());
        // The render is not the identity (the stage is engaged and ran).
        CHECK(firstBitMismatch(la, inL) < la.size());
    }

    SECTION("prepare 48k -> 96k -> 48k equals a fresh 48k render") {
        ShapeMorph fresh;
        ShapeMorph cycled;
        for (ShapeMorph* m : {&fresh, &cycled}) {
            m->setShape(0.75f);
            m->setFundamentalHz(static_cast<float>(SCT::kE1Hz));
        }
        fresh.prepare(kFs, 512);
        cycled.prepare(kFs, 512);
        {
            auto l = inL;
            auto r = inR;
            renderShape(cycled, l, r, 512);
        }
        cycled.prepare(96000.0, 512);
        {
            auto l = inL;
            auto r = inR;
            renderShape(cycled, l, r, 512);
        }
        cycled.prepare(kFs, 512);

        auto lf = inL;
        auto rf = inR;
        renderShape(fresh, lf, rf, 512);
        auto lc = inL;
        auto rc = inR;
        renderShape(cycled, lc, rc, 512);
        CHECK(firstBitMismatch(lf, lc) == lf.size());
        CHECK(firstBitMismatch(rf, rc) == rf.size());
    }
}

TEST_CASE("ShapeMorph_RegimeWeights", "[systems][profundum][shape-character][shape-morph]") {
    // FR-011, plan S6.3: w_R = s <= 1/2 ? 1 - t : 0, w_H = s <= 1/2 ? t : 1 - t2, w_B = t2,
    // t = min(1, 2s), t2 = max(0, 2s - 1).
    std::vector<float> in = SCT::fixture(SpectralShapeRecipe::kSawAnchor, SCT::kE1Hz, kFs, 0.05);
    for (const float shape : {0.0f, 0.1f, 0.25f, 0.5f, 0.6f, 0.75f, 1.0f}) {
        INFO("SHAPE " << shape);
        ShapeMorph m;
        m.setFundamentalHz(static_cast<float>(SCT::kE1Hz));
        m.prepare(kFs, 512);
        ShapeMorphProbe::setSeamUnsmoothed(m, true);
        m.setShape(shape);
        REQUIRE(ShapeMorphProbe::smoothedShape(m) == shape);
        auto l = in;
        auto r = in;
        renderShape(m, l, r, 512);

        const double s = static_cast<double>(shape);
        const double t = std::min(1.0, 2.0 * s);
        const double t2 = std::max(0.0, 2.0 * s - 1.0);
        const double wR = s <= 0.5 ? 1.0 - t : 0.0;
        const double wH = s <= 0.5 ? t : 1.0 - t2;
        const double wB = t2;
        const std::array<float, 3> w = ShapeMorphProbe::weights(m);
        CHECK(std::abs(static_cast<double>(w[0]) - wR) <= 1e-6);
        CHECK(std::abs(static_cast<double>(w[1]) - wH) <= 1e-6);
        CHECK(std::abs(static_cast<double>(w[2]) - wB) <= 1e-6);

        const int nonzero = static_cast<int>(w[0] > 0.0f) + static_cast<int>(w[1] > 0.0f)
                            + static_cast<int>(w[2] > 0.0f);
        CHECK(nonzero <= 2);
        if (shape == 0.0f || shape == 0.5f || shape == 1.0f)
            CHECK(nonzero == 1);
        CHECK(ShapeMorphProbe::runningRegimeCount(m) == nonzero);
        CHECK(ShapeMorphProbe::maxRunningRegimeCount(m) <= 2);
        CHECK(m.stateFinite());
    }
}

TEST_CASE("ShapeMorph_DisabledBitIdentical", "[systems][profundum][shape-character][shape-morph]") {
    // SC-008 (Shape part), C-22: setEnabled(false) after 0.5 s; from 100 ms after the call both
    // channels are the input bit for bit; the processed path is still warm before re-engaging.
    constexpr std::size_t kBlock = 512;
    const auto halfSecond = static_cast<std::size_t>(0.5 * kFs);
    const auto settle = static_cast<std::size_t>(0.1 * kFs);   // max(100 ms, 5 x 10 ms), C-16
    std::vector<float> inL;
    std::vector<float> inR;
    sawStereo(SCT::kE1Hz, 1.2, inL, inR);
    const std::size_t total = inL.size();

    for (const float shape : {0.0f, 0.5f, 1.0f}) {
        INFO("SHAPE " << shape);
        ShapeMorph m;
        m.setShape(shape);
        m.setFundamentalHz(static_cast<float>(SCT::kE1Hz));
        m.prepare(kFs, kBlock);

        auto l = inL;
        auto r = inR;
        // Enabled for 0.5 s (a whole number of blocks: the call lands on a block boundary).
        const std::size_t enabledLen = (halfSecond / kBlock) * kBlock;
        for (std::size_t i = 0; i < enabledLen; i += kBlock)
            m.processBlock(l.data() + i, r.data() + i, kBlock);
        CHECK(firstBitMismatch(l, inL) < enabledLen);  // the engaged stage changed the signal

        m.setEnabled(false);
        const std::size_t reengageAt = total - kBlock;
        for (std::size_t i = enabledLen; i < reengageAt; i += kBlock) {
            const std::size_t len = std::min(kBlock, reengageAt - i);
            m.processBlock(l.data() + i, r.data() + i, len);
        }
        const std::size_t from = enabledLen + settle;
        CHECK(firstBitMismatch(l, inL, from) >= reengageAt);
        CHECK(firstBitMismatch(r, inR, from) >= reengageAt);
        CHECK(ShapeMorphProbe::engage(m) == 0.0f);
        CHECK(ShapeMorphProbe::regimeStateWarm(m));

        m.setEnabled(true);
        m.processBlock(l.data() + reengageAt, r.data() + reengageAt, total - reengageAt);
        CHECK(ShapeMorphProbe::engage(m) > 0.0f);
        CHECK(m.stateFinite());
    }
}

TEST_CASE("ShapeMorph_LREqual", "[systems][profundum][shape-character][shape-morph]") {
    // SC-013(a) (Shape part): L == R input -> L == R output bit for bit, the stage stays linked.
    const std::vector<float> in = SCT::fixture(SpectralShapeRecipe::kSawAnchor, SCT::kE1Hz, kFs, 0.5);
    for (const float shape : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        INFO("SHAPE " << shape);
        ShapeMorph m;
        m.setShape(shape);
        m.setFundamentalHz(static_cast<float>(SCT::kE1Hz));
        m.prepare(kFs, 512);
        auto l = in;
        auto r = in;
        renderShape(m, l, r, 512);
        CHECK(firstBitMismatch(l, r) == l.size());
        CHECK(ShapeMorphProbe::linked(m));
        CHECK(firstBitMismatch(l, in) < l.size());  // processed, not bypassed
    }

    SECTION("a differing sample unlinks; reset re-links") {
        ShapeMorph m;
        m.setShape(0.5f);
        m.setFundamentalHz(static_cast<float>(SCT::kE1Hz));
        m.prepare(kFs, 512);
        std::vector<float> l;
        std::vector<float> r;
        sawStereo(SCT::kE1Hz, 0.1, l, r);
        renderShape(m, l, r, 512);
        CHECK_FALSE(ShapeMorphProbe::linked(m));
        m.reset();
        CHECK(ShapeMorphProbe::linked(m));
    }
}

TEST_CASE("ShapeMorph_NullPointerEqualsConstantFeed", "[systems][profundum][shape-character][shape-morph]") {
    // SC-025(e) (Shape part): a null pointer with setFundamentalHz(C2) equals a constant C2
    // f0PerSample array, bit for bit, at SHAPE 0.5 and through a SHAPE ramp 0 -> 1.
    constexpr std::size_t kBlock = 64;
    const auto c2 = static_cast<float>(SCT::kC2Hz);
    std::vector<float> inL;
    std::vector<float> inR;
    sawStereo(SCT::kC2Hz, 1.0, inL, inR);
    const std::size_t total = inL.size();
    const std::vector<float> f0Feed(total, c2);

    ShapeMorph viaSetter;
    ShapeMorph viaPointer;
    viaSetter.setFundamentalHz(c2);
    viaPointer.setFundamentalHz(static_cast<float>(SCT::kE1Hz));  // the feed must override it
    for (ShapeMorph* m : {&viaSetter, &viaPointer}) {
        m->setShape(0.5f);
        m->prepare(kFs, 512);
    }

    auto ls = inL;
    auto rs = inR;
    auto lp = inL;
    auto rp = inR;
    const auto holdEnd = static_cast<std::size_t>(0.25 * kFs);
    const auto rampEnd = static_cast<std::size_t>(0.5 * kFs);
    for (std::size_t i = 0; i < total; i += kBlock) {
        const std::size_t len = std::min(kBlock, total - i);
        if (i >= holdEnd && i < rampEnd) {
            // SHAPE 0 -> 1 linearly over 0.25 s, set once per block.
            const float shape = static_cast<float>(i - holdEnd) / static_cast<float>(rampEnd - holdEnd);
            viaSetter.setShape(shape);
            viaPointer.setShape(shape);
        } else if (i >= rampEnd) {
            viaSetter.setShape(1.0f);
            viaPointer.setShape(1.0f);
        }
        viaSetter.processBlock(ls.data() + i, rs.data() + i, len);
        viaPointer.processBlock(lp.data() + i, rp.data() + i, len, f0Feed.data() + i);
    }
    CHECK(firstBitMismatch(ls, lp) == total);
    CHECK(firstBitMismatch(rs, rp) == total);
    CHECK(viaSetter.stateFinite());
    CHECK(viaPointer.stateFinite());
}

// =============================================================================
// SC-003: SHAPE automation and engage are click-free (T013; FR-016, FR-019; D-11, C-22)
// =============================================================================
// Method (recorded per spec "Differential click count"): ClickDetector at its documented
// defaults; a transition detection counts only if neither held reference (the stage held at
// the start setting and at the end setting, same input, phase-aligned sample for sample) has a
// detection within +-N, N = floor(fs / f0 / 8) (Group 3 gate ruling 2026-10-16, three-render
// form). Every render is mono (left == right). ShapeMorph is per-sample, so a render's output
// does not depend on the host block partition; the block size only decides where setShape /
// setEnabled land. The positive controls are gated per SC arm and per note (ruling 2026-10-16):
// at each note at least one fixture must flag each control; every cell is recorded with WARN.

namespace {

namespace TU = Krate::DSP::TestUtils;

constexpr double kClickSeconds = 1.0;
constexpr std::size_t kClickEvent = 24064;    // 47 x 512: a block boundary for both 64 and 512
constexpr std::size_t kRampSamples = 2400;    // 50 ms at 48 kHz
constexpr std::size_t kSettleSamples = 4800;  // 100 ms: max(100 ms, 5 x 10 ms), C-16
constexpr std::size_t kEngageWindow = 4800;   // the D-11 window: the 10 ms ramp plus the settle
constexpr std::size_t kSteadyBegin = 7200;    // 0.15 s: steady-state measurements start here
constexpr std::size_t kSteadyEnd = 16800;     // 0.35 s: the 101-point grid renders stop here
constexpr std::size_t kGridPoints = 101;
constexpr std::size_t kNoIndex = static_cast<std::size_t>(-1);

struct ClickNote {
    const char* name;
    double hz;
};

/// Sets every control before prepare (the smoother and the engage ramp start at their targets).
void prepareMorph(ShapeMorph& m, float shape, double f0, bool enabled = true) {
    m.setShape(shape);
    m.setFundamentalHz(static_cast<float>(f0));
    m.setEnabled(enabled);
    m.prepare(kFs, 512);
}

/// Mono render of `in` in blocks of `block`; onBlock(pos) runs before the block that starts at pos.
template <typename OnBlock>
std::vector<float> renderMono(ShapeMorph& m, const std::vector<float>& in, std::size_t block, const OnBlock& onBlock) {
    std::vector<float> y = in;
    for (std::size_t i = 0; i < y.size(); i += block) {
        onBlock(i);
        const std::size_t len = std::min(block, y.size() - i);
        m.processBlock(y.data() + i, y.data() + i, len);
    }
    return y;
}

/// The stage held at `shape` (enabled or bypassed) over the whole input, with the probe seams.
std::vector<float> heldRender(const std::vector<float>& in, double f0, float shape, bool enabled = true,
                              bool unsmoothed = false, bool hardRegime = false) {
    ShapeMorph m;
    prepareMorph(m, shape, f0, enabled);
    ShapeMorphProbe::setSeamUnsmoothed(m, unsmoothed);
    ShapeMorphProbe::setSeamHardRegime(m, hardRegime);
    return renderMono(m, in, 512, [](std::size_t) {});
}

/// Sorted ClickDetector detection indices (documented defaults, sampleRate = fs).
std::vector<std::size_t> detectionIndices(const std::vector<float>& y) {
    TU::ClickDetectorConfig cfg{};
    cfg.sampleRate = static_cast<float>(kFs);
    TU::ClickDetector detector(cfg);
    detector.prepare();
    const std::vector<TU::ClickDetection> d = detector.detect(y.data(), y.size());
    std::vector<std::size_t> idx;
    idx.reserve(d.size());
    for (const TU::ClickDetection& c : d)
        idx.push_back(c.sampleIndex);
    std::sort(idx.begin(), idx.end());
    return idx;
}

std::vector<std::size_t> mergedDetections(const std::vector<float>& a, const std::vector<float>& b) {
    std::vector<std::size_t> d = detectionIndices(a);
    const std::vector<std::size_t> e = detectionIndices(b);
    d.insert(d.end(), e.begin(), e.end());
    std::sort(d.begin(), d.end());
    return d;
}

/// Distance from k to the nearest entry of a sorted index list (kNoIndex when empty).
std::size_t nearestDistance(const std::vector<std::size_t>& sorted, std::size_t k) {
    std::size_t best = kNoIndex;
    const auto it = std::lower_bound(sorted.begin(), sorted.end(), k);
    if (it != sorted.end())
        best = std::min(best, *it - k);
    if (it != sorted.begin())
        best = std::min(best, k - *(it - 1));
    return best;
}

double maxAbs(const std::vector<float>& y, std::size_t begin, std::size_t end) {
    double m = 0.0;
    for (std::size_t k = begin; k < end; ++k)
        m = std::max(m, std::abs(static_cast<double>(y[k])));
    return m;
}

/// SC-003(b): the maximum steady-state |y[k] - y[k-1]| over the 101-point SHAPE grid (endpoints
/// included). Every SC-003 move is full range (0 -> 1 or 1 -> 0, smoothed or stepped), so every
/// grid point lies inside the traversed range. Each grid render holds SHAPE from prepare and is
/// measured over [0.15 s, 0.35 s) (>= 4 periods of E0).
double steadyDeltaMaxOverGrid(const std::vector<float>& in, double f0) {
    REQUIRE(in.size() >= kSteadyEnd);
    const std::vector<float> prefix(in.begin(), in.begin() + static_cast<std::ptrdiff_t>(kSteadyEnd));
    double worst = 0.0;
    for (std::size_t i = 0; i < kGridPoints; ++i) {
        const float s = static_cast<float>(i) / static_cast<float>(kGridPoints - 1);
        const std::vector<float> y = heldRender(prefix, f0, s);
        worst = std::max(worst, SCT::steadyDeltaMax(y.data(), kSteadyBegin, kSteadyEnd));
    }
    return worst;
}

/// One SC-003 SHAPE move: from -> to as a 50 ms ramp (setShape every block, linearly
/// interpolated) or as one step at the block boundary kClickEvent. Gates (a) and (b).
/// Samples of the 10 ms SHAPE ramp at kFs (ShapeMorph::kShapeSmoothMs, C-33).
std::size_t shapeRampSamples() {
    return static_cast<std::size_t>(std::lround(static_cast<double>(ShapeMorph::kShapeSmoothMs) * 0.001 * kFs));
}

/// The differential count's own matching and amplitude guard, restricted to transition detections inside
/// [begin, end]: how many of `counted` fall in that window (SC-003(a) single-step ruling 2026-10-16).
std::size_t countedInsideWindow(const std::vector<float>& y, const std::vector<float>& refStart,
                                const std::vector<float>& refEnd, double f0, std::size_t begin, std::size_t end) {
    const auto N = static_cast<std::size_t>(std::floor(kFs / f0 / 8.0));
    std::vector<std::size_t> ref = detectionIndices(refStart);
    const std::vector<std::size_t> refE = detectionIndices(refEnd);
    ref.insert(ref.end(), refE.begin(), refE.end());
    std::sort(ref.begin(), ref.end());
    std::size_t inside = 0;
    for (const std::size_t i : detectionIndices(y)) {
        if (i < begin || i > end)
            continue;
        const std::size_t lo = i > N ? i - N : 0;
        const auto first = std::lower_bound(ref.begin(), ref.end(), lo);
        if (first != ref.end() && *first <= i + N)
            continue;
        const double refMax = std::max(SCT::maxDeltaAround(refStart.data(), y.size(), i, N),
                                       SCT::maxDeltaAround(refEnd.data(), y.size(), i, N));
        if (SCT::maxDeltaAround(y.data(), y.size(), i, N) > 1.1 * refMax)
            ++inside;
    }
    return inside;
}

void checkShapeMove(const std::vector<float>& in, double f0, const char* note, const char* fixtureName,
                    const std::vector<float>& refStart, const std::vector<float>& refEnd, double steadyMax,
                    float from, float to, bool ramp, std::size_t block) {
    ShapeMorph m;
    prepareMorph(m, from, f0);
    const std::vector<float> y = renderMono(m, in, block, [&](std::size_t pos) {
        if (pos < kClickEvent)
            return;
        if (!ramp) {
            if (pos == kClickEvent)
                m.setShape(to);
            return;
        }
        const double t =
            std::min(1.0, static_cast<double>(pos - kClickEvent) / static_cast<double>(kRampSamples));
        m.setShape(static_cast<float>(static_cast<double>(from) +
                                      (static_cast<double>(to) - static_cast<double>(from)) * t));
    });
    const std::size_t n = y.size();
    const SCT::ClickCount cc = SCT::differentialClicks(y.data(), refStart.data(), refEnd.data(), n, kFs, f0);
    // SC-003(a) single-step cells (user ruling 2026-10-16, Clarifications "Group 10 close rulings"): a counted
    // detection inside the 10 ms SHAPE ramp window [step, step + ramp + N] is the crossfade's own slope (the
    // detector cannot align it) and is recorded; arm (b) gates the cell. Detections outside the window still count.
    const std::size_t insideRamp = ramp ? 0 : countedInsideWindow(y, refStart, refEnd, f0, kClickEvent,
                                                                  kClickEvent + shapeRampSamples() + cc.windowN);
    const std::size_t gatedCount = cc.counted - std::min(cc.counted, insideRamp);
    const std::size_t trEnd = std::min(n, kClickEvent + kRampSamples + kSettleSamples);
    const double trMax = SCT::steadyDeltaMax(y.data(), kClickEvent, trEnd);
    INFO(note << ", " << fixtureName << ", SHAPE " << from << " -> " << to << ", "
              << (ramp ? "50 ms ramp" : "single step") << ", block " << block << ": counted " << cc.counted
              << " (inside the ramp window, recorded: " << insideRamp << "; gated " << gatedCount << "; guarded out " << cc.guardedOut << ", transition detections " << cc.transitionDetections << ", reference detections "
              << cc.referenceDetections << ", N " << cc.windowN << "); transition max |dy| " << trMax
              << " vs 1.1 x steady grid max " << 1.1 * steadyMax << " (ratio " << trMax / steadyMax << ")");
    if (insideRamp > 0)
        WARN(note << ", " << fixtureName << ", SHAPE " << from << " -> " << to << ", single step, block " << block
                  << ": " << insideRamp << " detection(s) inside the 10 ms ramp window recorded (SC-003(a) ruling 2026-10-16); (b) ratio "
                  << trMax / steadyMax);
    CHECK(gatedCount == 0u);            // (a)
    CHECK(trMax <= 1.1 * steadyMax);    // (b)
    CHECK(ShapeMorphProbe::maxRunningRegimeCount(m) <= 2);
    CHECK(m.stateFinite());
}

/// One SC-003 engage (engage == true) or disengage cell at a fixed SHAPE. Gates (a) and (c).
/// Rendered one sample per call so e is read sample by sample through the probe.
void checkEngage(const std::vector<float>& in, double f0, const char* note, const char* fixtureName, float shape,
                 bool engage) {
    const std::size_t n = in.size();
    REQUIRE(n >= kClickEvent + kEngageWindow);
    const std::vector<float> yBypass = heldRender(in, f0, shape, false);  // == the input (SC-008)
    const std::vector<float> yOn = heldRender(in, f0, shape, true);

    ShapeMorph m;
    prepareMorph(m, shape, f0, !engage);
    std::vector<float> y = in;
    std::vector<float> ga(n, 0.0f);
    std::vector<float> gb(n, 0.0f);
    bool warmBefore = true;
    for (std::size_t k = 0; k < n; ++k) {
        if (k == kClickEvent) {
            if (engage)
                warmBefore = ShapeMorphProbe::regimeStateWarm(m);
            m.setEnabled(engage);
        }
        m.processBlock(y.data() + k, y.data() + k, 1);
        const float e = ShapeMorphProbe::engage(m);
        ga[k] = 1.0f - e;
        gb[k] = e;
    }

    // D-11 with branch a = the bypass (the input), branch b = the regime at this SHAPE.
    const double Da = SCT::steadyDeltaMax(in.data(), kSteadyBegin, n);
    const double Db = SCT::steadyDeltaMax(yOn.data(), kSteadyBegin, n);
    const double maxXa = maxAbs(in, kSteadyBegin, n);
    const double maxXb = maxAbs(yOn, kSteadyBegin, n);
    const double ratio = SCT::d11Ratio(y.data(), ga.data(), gb.data(), Da, Db, maxXa, maxXb, kClickEvent,
                                       kClickEvent + kEngageWindow);

    const std::vector<float>& refStart = engage ? yBypass : yOn;
    const std::vector<float>& refEnd = engage ? yOn : yBypass;
    const SCT::ClickCount cc = SCT::differentialClicks(y.data(), refStart.data(), refEnd.data(), n, kFs, f0);
    const float eEnd = gb[kClickEvent + kEngageWindow - 1];
    INFO(note << ", " << fixtureName << ", SHAPE " << shape << ", " << (engage ? "engage" : "disengage")
              << ": counted " << cc.counted << " (guarded out " << cc.guardedOut << ", transition detections " << cc.transitionDetections
              << ", reference detections " << cc.referenceDetections << ", N " << cc.windowN << "); d11Ratio "
              << ratio << " (Da " << Da << ", Db " << Db << ", max|xa| " << maxXa << ", max|xb| " << maxXb
              << "); e at window end " << eEnd);
    CHECK(cc.counted == 0u);                       // (a)
    CHECK(ratio <= 1.0);                           // (c)
    CHECK(eEnd == (engage ? 1.0f : 0.0f));         // the 10 ms ramp completed inside the window
    if (engage)
        CHECK(warmBefore);                         // C-22: the regime state is warm just before engaging
    CHECK(ShapeMorphProbe::maxRunningRegimeCount(m) <= 2);
    CHECK(m.stateFinite());
}

/// Positive-control event choice (round-4 ruling 2026-10-16, the SC-025(d) chain-test siting): candidates every
/// `kControlStride` samples over one period of f0 (+ 64) from kClickEvent. A candidate is valid when every
/// switch sample (event + each offset) lies at least 2·N from every detection of either reference (a snap
/// inside ±N of a reference detection is masked by the differential rule by construction, see
/// Rectifier_BlendStepClickFree). Its score is the largest, over the offsets, of the forced step's size
/// proxy jump(i, k) at k = event + offsets[i] over the larger reference's max |dy| within ±N of k (the
/// amplitude guard's comparison); the valid candidate with the largest score wins. With no valid candidate
/// kClickEvent is used and `foundValid` is false (recorded by the caller).
constexpr std::size_t kControlStride = 8;  // also the controls' host block, so every candidate is a block start

template <typename Jump>
std::size_t chooseControlEvent(double f0, const std::vector<std::size_t>& refDetections,
                               const std::vector<float>& refStart, const std::vector<float>& refEnd,
                               const std::vector<std::size_t>& offsets, std::size_t n, const Jump& jump,
                               bool& foundValid, double& scoreOut) {
    const auto windowN = static_cast<std::size_t>(std::floor(kFs / f0 / 8.0));
    const auto period = static_cast<std::size_t>(std::ceil(kFs / f0));
    std::size_t best = kClickEvent;
    double bestScore = -1.0;
    foundValid = false;
    for (std::size_t e = kClickEvent; e < kClickEvent + period + 64; e += kControlStride) {
        bool valid = true;
        for (const std::size_t d : offsets) {
            if (e + d + 2 * windowN >= n || nearestDistance(refDetections, e + d) < 2 * windowN)
                valid = false;
        }
        if (!valid)
            continue;
        double score = 0.0;
        for (std::size_t i = 0; i < offsets.size(); ++i) {
            const std::size_t k = e + offsets[i];
            const double local = std::max(SCT::maxDeltaAround(refStart.data(), n, k, windowN),
                                          SCT::maxDeltaAround(refEnd.data(), n, k, windowN));
            score = std::max(score, jump(i, k) / std::max(local, 1.0e-12));
        }
        if (score > bestScore) {
            bestScore = score;
            best = e;
            foundValid = true;
        }
    }
    scoreOut = bestScore;
    return best;
}

/// Positive control (i): an unsmoothed (seamUnsmoothed_) SHAPE 0 -> 1 step at a block boundary.
SCT::ClickCount unsmoothedStepControl(const std::vector<float>& in, double f0, std::size_t& eventOut,
                                      bool& validOut, double& scoreOut) {
    const std::vector<float> refStart = heldRender(in, f0, 0.0f, true, true, false);
    const std::vector<float> refEnd = heldRender(in, f0, 1.0f, true, true, false);
    const std::vector<std::size_t> det = mergedDetections(refStart, refEnd);
    // Jump proxy: the snap moves the output from the ROUND render to the BITE render at the event.
    const std::size_t event = chooseControlEvent(
        f0, det, refStart, refEnd, {std::size_t{0}}, in.size(),
        [&](std::size_t, std::size_t k) {
            return std::abs(static_cast<double>(refEnd[k]) - static_cast<double>(refStart[k]));
        },
        validOut, scoreOut);
    eventOut = event;

    ShapeMorph m;
    prepareMorph(m, 0.0f, f0);
    ShapeMorphProbe::setSeamUnsmoothed(m, true);
    const std::vector<float> y = renderMono(m, in, kControlStride, [&](std::size_t pos) {
        if (pos == event)
            m.setShape(1.0f);
    });
    return SCT::differentialClicks(y.data(), refStart.data(), refEnd.data(), y.size(), kFs, f0);
}

/// Positive control (ii): a hard regime switch (seamHardRegime_: the weights jump ROUND -> HOLLOW
/// at s = 0.25 and HOLLOW -> BITE past s = 0.75, no crossfade) on a smoothed SHAPE 0 -> 1 step.
SCT::ClickCount hardRegimeControl(const std::vector<float>& in, double f0, std::size_t& eventOut, bool& validOut,
                                  double& scoreOut, std::size_t& d1Out) {
    // Pre-pass: the switch offsets d1 (HOLLOW takes over) and d2 (BITE takes over) after
    // setShape(1). The smoother sits at 0 before the step in both this pass and the control, so the
    // offsets do not depend on where the event lies.
    std::size_t d1 = kNoIndex;
    std::size_t d2 = kNoIndex;
    {
        ShapeMorph m;
        prepareMorph(m, 0.0f, f0);
        ShapeMorphProbe::setSeamHardRegime(m, true);
        m.setShape(1.0f);
        std::vector<float> buf = in;
        for (std::size_t k = 0; k < buf.size() && d2 == kNoIndex; ++k) {
            m.processBlock(buf.data() + k, buf.data() + k, 1);
            const std::array<float, 3> w = ShapeMorphProbe::weights(m);
            if (d1 == kNoIndex && w[1] == 1.0f)
                d1 = k;
            if (w[2] == 1.0f)
                d2 = k;
        }
    }
    REQUIRE(d1 != kNoIndex);
    REQUIRE(d2 != kNoIndex);
    REQUIRE(d1 < d2);

    const std::vector<float> refStart = heldRender(in, f0, 0.0f, true, false, true);
    const std::vector<float> refEnd = heldRender(in, f0, 1.0f, true, false, true);
    const std::vector<float> yHollow = heldRender(in, f0, 0.5f, true, false, true);  // jump proxy only
    const std::vector<std::size_t> det = mergedDetections(refStart, refEnd);
    // The two discontinuities: ROUND -> HOLLOW at event + d1, HOLLOW -> BITE at event + d2.
    const std::size_t event = chooseControlEvent(
        f0, det, refStart, refEnd, {d1, d2}, in.size(),
        [&](std::size_t i, std::size_t k) {
            return i == 0 ? std::abs(static_cast<double>(yHollow[k]) - static_cast<double>(refStart[k]))
                          : std::abs(static_cast<double>(refEnd[k]) - static_cast<double>(yHollow[k]));
        },
        validOut, scoreOut);
    eventOut = event;
    d1Out = d1;

    ShapeMorph m;
    prepareMorph(m, 0.0f, f0);
    ShapeMorphProbe::setSeamHardRegime(m, true);
    const std::vector<float> y = renderMono(m, in, kControlStride, [&](std::size_t pos) {
        if (pos == event)
            m.setShape(1.0f);
    });
    return SCT::differentialClicks(y.data(), refStart.data(), refEnd.data(), y.size(), kFs, f0);
}

} // namespace

TEST_CASE("ShapeMorph_ClickCountPositiveControl", "[systems][profundum][shape-character][shape-morph]") {
    // SC-003(a) positive controls: (i) an unsmoothed SHAPE 0 -> 1 step, (ii) a hard regime switch. Events are
    // sited by chooseControlEvent (round-4 ruling 2026-10-16: every 8 samples, largest step over the references'
    // local max |dy|, 2N clear of reference detections); the amplitude guard stays active.
    // Gated per SC arm and per note (ruling 2026-10-16) at the SC-003 notes E2 and E0 over the
    // SC-003 fixtures; kSawAnchor E2 is the task's named cell. Every cell is recorded.
    const std::array<ClickNote, 2> notes{{{"E2", SCT::kE2Hz}, {"E0", SCT::kE0Hz}}};
    const std::array<SCT::Fixture, 2> fixtures{{SCT::kFixtures[2], SCT::kFixtures[3]}};  // saw, growl
    for (const ClickNote& note : notes) {
        std::size_t unsmoothedFlagged = 0;
        std::size_t hardFlagged = 0;
        for (const SCT::Fixture& fx : fixtures) {
            const std::vector<float> in = SCT::fixture(fx.controls, note.hz, kFs, kClickSeconds);
            std::size_t at = 0;
            bool valid = false;
            double score = 0.0;
            std::size_t d1 = 0;
            const SCT::ClickCount c1 = unsmoothedStepControl(in, note.hz, at, valid, score);
            WARN(note.name << ", " << fx.name << " control (i) unsmoothed step at " << at << " (step / local ref max|dy| "
                           << score << ")"
                           << (valid ? "" : " (no candidate clear of reference detections)") << ": counted "
                           << c1.counted << " (guarded out " << c1.guardedOut << ", transition detections " << c1.transitionDetections
                           << ", reference detections " << c1.referenceDetections << ", N " << c1.windowN << ")"
                           << (c1.counted >= 1u ? " - FLAGGED" : " - not flagged (recorded)"));
            if (c1.counted >= 1u)
                ++unsmoothedFlagged;

            const SCT::ClickCount c2 = hardRegimeControl(in, note.hz, at, valid, score, d1);
            WARN(note.name << ", " << fx.name << " control (ii) hard regime switch, event at " << at
                           << " (ROUND -> HOLLOW at +" << d1 << ", step / local ref max|dy| " << score << ")"
                           << (valid ? "" : " (no candidate clear of reference detections)") << ": counted "
                           << c2.counted << " (guarded out " << c2.guardedOut << ", transition detections " << c2.transitionDetections
                           << ", reference detections " << c2.referenceDetections << ", N " << c2.windowN << ")"
                           << (c2.counted >= 1u ? " - FLAGGED" : " - not flagged (recorded)"));
            if (c2.counted >= 1u)
                ++hardFlagged;
        }
        INFO(note.name << ": control (i) flagged over the fixtures = " << unsmoothedFlagged
                       << ", control (ii) flagged = " << hardFlagged);
        CHECK(unsmoothedFlagged >= 1u);
        CHECK(hardFlagged >= 1u);
    }
}

TEST_CASE("ShapeMorph_AutomationClickFreeSmoke", "[systems][profundum][shape-character][shape-morph]") {
    // The per-push sibling of ShapeMorph_AutomationClickFree: E2, kSawAnchor, block 64,
    // SHAPE 0 -> 1 as a 50 ms ramp; (a) counted == 0 and (b) the sample-delta bound.
    const double f0 = SCT::kE2Hz;
    const std::vector<float> in = SCT::fixture(SpectralShapeRecipe::kSawAnchor, f0, kFs, kClickSeconds);
    const double steadyMax = steadyDeltaMaxOverGrid(in, f0);
    REQUIRE(steadyMax > 0.0);
    const std::vector<float> refStart = heldRender(in, f0, 0.0f);
    const std::vector<float> refEnd = heldRender(in, f0, 1.0f);
    checkShapeMove(in, f0, "E2", "saw", refStart, refEnd, steadyMax, 0.0f, 1.0f, true, 64);
}

TEST_CASE("ShapeMorph_AutomationClickFree", "[systems][profundum][shape-character][shape-morph][long]") {
    // SC-003: SHAPE 0 -> 1 and 1 -> 0, each as a 50 ms ramp and as a single step at a block
    // boundary, at block sizes {64, 512}; plus engage and disengage at SHAPE {0, 0.5, 1}; at E0
    // and E2, kSawAnchor and kGrowl, 48 kHz. Positive controls: ShapeMorph_ClickCountPositiveControl.
    const std::array<ClickNote, 2> notes{{{"E0", SCT::kE0Hz}, {"E2", SCT::kE2Hz}}};
    const std::array<SCT::Fixture, 2> fixtures{{SCT::kFixtures[2], SCT::kFixtures[3]}};  // saw, growl
    const std::array<std::pair<float, float>, 2> directions{{{0.0f, 1.0f}, {1.0f, 0.0f}}};
    for (const ClickNote& note : notes) {
        for (const SCT::Fixture& fx : fixtures) {
            const std::vector<float> in = SCT::fixture(fx.controls, note.hz, kFs, kClickSeconds);
            const double steadyMax = steadyDeltaMaxOverGrid(in, note.hz);
            REQUIRE(steadyMax > 0.0);
            for (const auto& [from, to] : directions) {
                const std::vector<float> refStart = heldRender(in, note.hz, from);
                const std::vector<float> refEnd = heldRender(in, note.hz, to);
                for (const std::size_t block : {std::size_t{64}, std::size_t{512}}) {
                    for (const bool ramp : {true, false})
                        checkShapeMove(in, note.hz, note.name, fx.name, refStart, refEnd, steadyMax, from, to,
                                       ramp, block);
                }
            }
            for (const float shape : {0.0f, 0.5f, 1.0f}) {
                checkEngage(in, note.hz, note.name, fx.name, shape, true);
                checkEngage(in, note.hz, note.name, fx.name, shape, false);
            }
        }
    }
}
