// ==============================================================================
// Profundum Phase 9 (specs/profundum-phase9-voice-engine) - mass_dynamics_oversampling_test.cpp
// ==============================================================================
// FR-082 MassDynamics oversampling opt-in (plan S7, C-3, R-J; tasks.md T005). Created as a stub by T001
// (CMake registration, tasks.md deviation 1); filled by T005. Still includes every Phase 9 header so each
// is compiled by every gate from Group 1 on.
//
// Input (tasks.md T005): the Phase 6 chain's sum tap (fold + anchor, before the dynamics) at E2, Fold 100,
// Mass 100, from MassDynamicsTest::renderChain. Phase 6's Fold-100 fixture (mass_dynamics_aliasing_test.cpp
// mdaRunGrid / mdaRunCell) is ShapeCharacterTest::fold100(kSawAnchor) and is not a ChainConfig, so the
// chain is configured with that fixture's recipe (base = SpectralShapeRecipe::kSawAnchor) and the
// FoldAmount target overridden to 100 (HarmonicFold::setAmount range [0, 100], mass_law.h:86). The sum is
// fed to fresh MassDynamics instances set to the Mass-law targets for Mass 100
// (MassLaw::evaluate(kMaxMass, lawBaseFor(cfg))), keyed at E2.
//
// Evidence: specs/profundum-phase9-voice-engine/evidence/os-dynamics-delta.txt (written by
// MassDynamics_OversamplingTransfer: the 40 Hz group delay and the R-J GR / makeup record; recorded, not
// gated).
// ==============================================================================

#include <krate/dsp/systems/profundum_engine.h>
#include <krate/dsp/systems/profundum_engine_observer.h>
#include <krate/dsp/systems/profundum_macro_matrix.h>
#include <krate/dsp/systems/profundum_motion.h>
#include <krate/dsp/systems/profundum_voice.h>

#include "mass_dynamics_test_helpers.h"

#include <allocation_detector.h>

#include <krate/dsp/processors/spectral_shape_recipe.h>
#include <krate/dsp/processors/transient_shaper.h>
#include <krate/dsp/systems/mass_dynamics.h>
#include <krate/dsp/systems/mass_law.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

using namespace Krate::DSP;

namespace {

namespace mdo = Krate::DSP::MassDynamicsTest;
namespace mdoTs = Krate::DSP::TransientShaperTest;

constexpr double kMdoFs = 48000.0;
constexpr std::size_t kMdoHostBlock = 64;
constexpr double kMdoInputSeconds = 2.0;
constexpr double kMdoLongSeconds = 10.0;
constexpr double kMdoTransferBoundDb = 0.05;   // tasks.md T005: |on / off| within +/-0.05 dB
constexpr float kMdoSineAmplitude = 0.25f;
constexpr std::array<double, 4> kMdoTransferHz{40.0, 200.0, 1000.0, 5000.0};
constexpr int kMdoMaxLag = 32;                 // group-delay search range, base samples

std::string mdoEvidenceDir() {
    return std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase9-voice-engine/evidence/";
}

/// The Fold-100 chain at E2 / Mass 100 (see the file banner).
mdo::ChainConfig mdoFold100Config() {
    mdo::ChainConfig c;
    c.base = SpectralShapeRecipe::kSawAnchor;
    c.f0 = mdoTs::kE2Hz;
    c.fs = kMdoFs;
    c.seconds = kMdoInputSeconds;
    c.massAt = [](std::size_t) { return MassLaw::kMaxMass; };
    c.targetOverride[static_cast<std::size_t>(MassLaw::Target::FoldAmount)] =
        [](std::size_t) -> std::optional<float> { return 100.0f; };
    return c;
}

struct MdoStereo {
    std::vector<float> l, r;
};

/// The chain's sum tap, rendered once per process.
const MdoStereo& mdoInput() {
    static const MdoStereo in = [] {
        const mdo::ChainTaps t = mdo::renderChain(mdoFold100Config());
        return MdoStereo{t.sumL, t.sumR};
    }();
    return in;
}

/// The sum tiled to `seconds` (for the 10 s cases).
MdoStereo mdoTiledInput(double seconds) {
    const MdoStereo& src = mdoInput();
    const auto n = static_cast<std::size_t>(std::llround(seconds * kMdoFs));
    MdoStereo out;
    out.l.resize(n);
    out.r.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        out.l[i] = src.l[i % src.l.size()];
        out.r[i] = src.r[i % src.r.size()];
    }
    return out;
}

MassLaw::Targets mdoTargetsAt(float mass) {
    return MassLaw::evaluate(mass, mdo::lawBaseFor(mdoFold100Config()));
}

/// Mass-law targets applied before prepare (prepare latches the amount and duration; reset() snaps
/// the trim), keyed at E2.
void mdoApplyTargets(MassDynamics& md, const MassLaw::Targets& t) {
    md.setTransientDurationMs(t.transientDurationMs);
    md.setCompressionAmount(t.compressionAmount);
    md.setOutputTrimDb(t.outputTrimDb);
    md.setDetectorKeyHz(static_cast<float>(mdoTs::kE2Hz));
}

/// A fresh instance. touch = false never calls setOversampling (the existing-caller path).
std::unique_ptr<MassDynamics> mdoMake(bool touch, bool oversample, const MassLaw::Targets& t, double fs = kMdoFs) {
    auto md = std::make_unique<MassDynamics>();
    mdoApplyTargets(*md, t);
    if (touch)
        md->setOversampling(oversample);
    md->prepare(fs, kMdoHostBlock);
    return md;
}

/// In place on a copy, host blocks of `hostBlock`.
MdoStereo mdoRender(MassDynamics& md, const MdoStereo& in, std::size_t hostBlock) {
    MdoStereo o = in;
    for (std::size_t b = 0; b < o.l.size(); b += hostBlock) {
        const std::size_t n = std::min(hostBlock, o.l.size() - b);
        md.processBlock(o.l.data() + b, o.r.data() + b, n);
    }
    return o;
}

std::size_t mdoBitMismatches(const std::vector<float>& a, const std::vector<float>& b) {
    REQUIRE(a.size() == b.size());
    std::size_t m = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::bit_cast<std::uint32_t>(a[i]) != std::bit_cast<std::uint32_t>(b[i]))
            ++m;
    }
    return m;
}

/// Bit-pattern finiteness (fast-math immune).
bool mdoFiniteBits(float x) noexcept {
    return (std::bit_cast<std::uint32_t>(x) & 0x7F800000u) != 0x7F800000u;
}

bool mdoFiniteBitsD(double x) noexcept {
    return (std::bit_cast<std::uint64_t>(x) & 0x7FF0000000000000ull) != 0x7FF0000000000000ull;
}

double mdoRms(const std::vector<float>& x, std::size_t begin, std::size_t end) {
    REQUIRE(begin < end);
    REQUIRE(end <= x.size());
    double s = 0.0;
    for (std::size_t i = begin; i < end; ++i)
        s += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    return std::sqrt(s / static_cast<double>(end - begin));
}

/// Delay of y against x (y[n] ~ x[n - d]) over [begin, end): cross-correlation peak over lags
/// [0, kMdoMaxLag], parabolic interpolation. `interior` is false when the peak sits on the range edge.
double mdoDelaySamples(const std::vector<float>& x, const std::vector<float>& y, std::size_t begin, std::size_t end,
                       bool& interior) {
    REQUIRE(begin >= static_cast<std::size_t>(kMdoMaxLag));
    REQUIRE(end <= x.size());
    REQUIRE(end <= y.size());
    std::array<double, kMdoMaxLag + 1> c{};
    for (int k = 0; k <= kMdoMaxLag; ++k) {
        double s = 0.0;
        for (std::size_t n = begin; n < end; ++n)
            s += static_cast<double>(y[n]) * static_cast<double>(x[n - static_cast<std::size_t>(k)]);
        c[static_cast<std::size_t>(k)] = s;
    }
    const auto best = static_cast<int>(std::distance(c.begin(), std::max_element(c.begin(), c.end())));
    interior = best > 0 && best < kMdoMaxLag;
    if (!interior)
        return static_cast<double>(best);
    const auto b = static_cast<std::size_t>(best);
    const double cm = c[b - 1];
    const double c0 = c[b];
    const double cp = c[b + 1];
    const double den = cm - 2.0 * c0 + cp;
    const double frac = (den != 0.0) ? 0.5 * (cm - cp) / den : 0.0;
    return static_cast<double>(best) + frac;
}

/// A steady sine, L == R copies.
MdoStereo mdoSine(double hz, std::size_t n) {
    MdoStereo s;
    s.l.resize(n);
    for (std::size_t i = 0; i < n; ++i)
        s.l[i] = kMdoSineAmplitude *
                 static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / kMdoFs));
    s.r = s.l;
    return s;
}

/// Compression 0, trim 0, transient gains 0 (tasks.md T005 transfer case).
std::unique_ptr<MassDynamics> mdoMakeNeutral(bool oversample) {
    auto md = std::make_unique<MassDynamics>();
    md->setTransientAttackDb(0.0f);
    md->setTransientSustainDb(0.0f);
    md->setCompressionAmount(0.0f);
    md->setOutputTrimDb(0.0f);
    md->setDetectorKeyHz(static_cast<float>(mdoTs::kE2Hz));
    md->setOversampling(oversample);
    md->prepare(kMdoFs, kMdoHostBlock);
    return md;
}

struct MdoGrReading {
    double grDb = 0.0;       ///< mean compressor().currentGainReductionDb() over [1 s, 2 s)
    double makeupDb = 0.0;   ///< mean (currentGainDb() - currentGainReductionDb()) over [1 s, 2 s)
};

/// R-J: steady GR and makeup at one Mass on the Fold-100 input, read at every host-block end in [1 s, 2 s).
MdoGrReading mdoSteadyGr(float mass, bool oversample) {
    const MdoStereo& in = mdoInput();
    auto md = mdoMake(true, oversample, mdoTargetsAt(mass));
    MdoStereo buf = in;
    const std::size_t settle = mdoTs::secondsToSamples(kMdoFs, 1.0);
    double gr = 0.0;
    double mk = 0.0;
    std::size_t reads = 0;
    for (std::size_t b = 0; b < buf.l.size(); b += kMdoHostBlock) {
        const std::size_t n = std::min(kMdoHostBlock, buf.l.size() - b);
        md->processBlock(buf.l.data() + b, buf.r.data() + b, n);
        if (b >= settle) {
            const double g = static_cast<double>(md->compressor().currentGainReductionDb());
            gr += g;
            mk += static_cast<double>(md->compressor().currentGainDb()) - g;
            ++reads;
        }
    }
    REQUIRE(reads > 0);
    return MdoGrReading{.grDb = gr / static_cast<double>(reads), .makeupDb = mk / static_cast<double>(reads)};
}

}  // namespace

// FR-084 / SC-030: an instance never touched by the opt-in and one with setOversampling(false) before
// prepare render bitwise-identical L and R.
TEST_CASE("MassDynamics_OversamplingOffUnchanged",
          "[systems][profundum][mass-dynamics][voice-engine][fr-084][sc-030]") {
    const MdoStereo& in = mdoInput();
    REQUIRE(in.l.size() == mdoTs::secondsToSamples(kMdoFs, kMdoInputSeconds));
    const MassLaw::Targets t = mdoTargetsAt(MassLaw::kMaxMass);
    auto x = mdoMake(false, false, t);
    auto y = mdoMake(true, false, t);
    CHECK_FALSE(y->oversamplingRequested());
    CHECK_FALSE(y->oversamplingActive());
    CHECK_FALSE(x->oversamplingRequested());
    CHECK_FALSE(x->oversamplingActive());
    const MdoStereo ox = mdoRender(*x, in, kMdoHostBlock);
    const MdoStereo oy = mdoRender(*y, in, kMdoHostBlock);
    CHECK(mdoBitMismatches(ox.l, oy.l) == 0);
    CHECK(mdoBitMismatches(ox.r, oy.r) == 0);
    // The dynamics did something on this input (the comparison is not of two copies of the input).
    CHECK(mdoBitMismatches(ox.l, in.l) > 0);
}

// FR-082 / C-3: active for 2 * fs <= BassCompressor::kMaxSampleRate; a recorded no-op at 192 kHz; a
// request after prepare takes effect at the next prepare only.
TEST_CASE("MassDynamics_OversamplingActiveRange", "[systems][profundum][mass-dynamics][voice-engine][fr-082]") {
    for (const double fs : {22050.0, 44100.0, 48000.0, 96000.0}) {
        INFO("fs " << fs);
        MassDynamics md;
        md.setOversampling(true);
        md.prepare(fs, kMdoHostBlock);
        CHECK(md.oversamplingRequested());
        CHECK(md.oversamplingActive());
    }
    {
        MassDynamics md;
        md.setOversampling(true);
        md.prepare(192000.0, kMdoHostBlock);
        CHECK(md.oversamplingRequested());
        CHECK(md.oversamplingActive());   // T029: the gain application oversamples at every rate
    }
    {
        MassDynamics md;
        md.prepare(kMdoFs, kMdoHostBlock);
        CHECK_FALSE(md.oversamplingActive());
        md.setOversampling(true);
        CHECK(md.oversamplingRequested());
        CHECK_FALSE(md.oversamplingActive());   // configuration-time: not until the next prepare
        md.prepare(kMdoFs, kMdoHostBlock);
        CHECK(md.oversamplingActive());
        md.setOversampling(false);
        CHECK(md.oversamplingActive());
        md.prepare(kMdoFs, kMdoHostBlock);
        CHECK_FALSE(md.oversamplingActive());
    }
    STATIC_REQUIRE(MassDynamics::kOversamplingFactor == 2);
    STATIC_REQUIRE(MassDynamics::latencySamples() == 0);
}

// Q4 refinement (main loop, 2026-10-20, after probe P-3): factor 4 where 4 fs <= 192 kHz, 2 at 96 kHz, off at 192 kHz;
// the default request stays 2 so every existing caller is unchanged.
TEST_CASE("MassDynamics_OversamplingFactorFallback", "[systems][profundum][mass-dynamics][voice-engine][fr-082]") {
    STATIC_REQUIRE(MassDynamics::kMaxOversamplingFactor == 4);
    struct Cell { double fs; std::size_t requested; std::size_t expected; };
    // T029 (2026-10-20): only the gain application is oversampled, so the requested factor runs at every rate.
    for (const Cell c : {Cell{44100.0, 4, 4}, Cell{48000.0, 4, 4}, Cell{96000.0, 4, 4}, Cell{192000.0, 4, 4},
                         Cell{48000.0, 2, 2}, Cell{96000.0, 2, 2}, Cell{192000.0, 2, 2}}) {
        INFO("fs " << c.fs << " requested " << c.requested);
        MassDynamics md;
        md.setOversampling(true);
        md.setOversamplingFactor(c.requested);
        CHECK(md.oversamplingFactor() == c.requested);
        md.prepare(c.fs, kMdoHostBlock);
        CHECK(md.oversamplingActiveFactor() == c.expected);
        CHECK(md.oversamplingActive() == (c.expected != 0));
    }
    {
        MassDynamics md;
        CHECK(md.oversamplingFactor() == MassDynamics::kOversamplingFactor);
        md.setOversamplingFactor(3);   // anything but 4 is the default 2
        CHECK(md.oversamplingFactor() == 2);
        md.setOversamplingFactor(8);
        CHECK(md.oversamplingFactor() == 4);
    }
}

// FR-060: a re-prepare, reset() and 10 s of processing with the opt-in on allocate nothing.
TEST_CASE("MassDynamics_OversamplingNoAllocation", "[systems][profundum][mass-dynamics][voice-engine][fr-060]") {
    MdoStereo buf = mdoTiledInput(kMdoLongSeconds);
    auto md = mdoMake(true, true, mdoTargetsAt(MassLaw::kMaxMass));
    REQUIRE(md->oversamplingActive());
    std::size_t allocations = 0;
    {
        const TestHelpers::AllocationScope scope;
        md->prepare(kMdoFs, kMdoHostBlock);
        (*md).reset();
        for (std::size_t b = 0; b < buf.l.size(); b += kMdoHostBlock) {
            const std::size_t n = std::min(kMdoHostBlock, buf.l.size() - b);
            md->processBlock(buf.l.data() + b, buf.r.data() + b, n);
        }
        allocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }
    CHECK(allocations == 0);
    CHECK(md->oversamplingActive());
}

// FR-063: opt-in on, host blocks {1, 7, 32, 64, 511} render bitwise what block 64 renders.
TEST_CASE("MassDynamics_OversamplingPartition", "[systems][profundum][mass-dynamics][voice-engine][fr-063]") {
    const MdoStereo& in = mdoInput();
    const MassLaw::Targets t = mdoTargetsAt(MassLaw::kMaxMass);
    auto ref = mdoMake(true, true, t);
    REQUIRE(ref->oversamplingActive());
    const MdoStereo r = mdoRender(*ref, in, kMdoHostBlock);
    for (const std::size_t block : {std::size_t{1}, std::size_t{7}, std::size_t{32}, std::size_t{64}, std::size_t{511}}) {
        INFO("host block " << block);
        auto md = mdoMake(true, true, t);
        const MdoStereo o = mdoRender(*md, in, block);
        CHECK(mdoBitMismatches(o.l, r.l) == 0);
        CHECK(mdoBitMismatches(o.r, r.r) == 0);
    }
}

// FR-082: the neutral transfer (compression 0, trim 0, transient gains 0) of on over off within
// +/-0.05 dB at 40 / 200 / 1000 / 5000 Hz; the 40 Hz group delay and the R-J record are written to
// evidence/os-dynamics-delta.txt (recorded, not gated).
TEST_CASE("MassDynamics_OversamplingTransfer", "[systems][profundum][mass-dynamics][voice-engine][fr-082]") {
    const std::size_t settle = mdoTs::secondsToSamples(kMdoFs, 1.0);
    const std::size_t total = settle + mdoTs::secondsToSamples(kMdoFs, 1.0);
    std::cout << std::setprecision(6);

    double delay40 = 0.0;
    bool delayInterior = false;
    std::array<double, kMdoTransferHz.size()> gainDb{};
    for (std::size_t f = 0; f < kMdoTransferHz.size(); ++f) {
        const double hz = kMdoTransferHz[f];
        INFO("sine " << hz << " Hz");
        const MdoStereo s = mdoSine(hz, total);
        auto off = mdoMakeNeutral(false);
        auto on = mdoMakeNeutral(true);
        REQUIRE_FALSE(off->oversamplingActive());
        REQUIRE(on->oversamplingActive());
        const MdoStereo yo = mdoRender(*off, s, kMdoHostBlock);
        const MdoStereo yn = mdoRender(*on, s, kMdoHostBlock);
        const double rmsOff = mdoRms(yo.l, settle, total);
        const double rmsOn = mdoRms(yn.l, settle, total);
        REQUIRE(rmsOff > 0.0);
        gainDb[f] = 20.0 * std::log10(rmsOn / rmsOff);
        std::cout << "[T005 FR-082] transfer " << hz << " Hz: on/off " << gainDb[f] << " dB (bound +/-"
                  << kMdoTransferBoundDb << ")\n";
        CHECK(std::abs(gainDb[f]) <= kMdoTransferBoundDb);
        if (f == 0) {   // kMdoTransferHz[0] == 40 Hz
            delay40 = mdoDelaySamples(yo.l, yn.l, settle, total, delayInterior);
            std::cout << "[T005 FR-082] group delay at 40 Hz: " << delay40 << " base samples (plan S7 expects ~3.8)\n";
            // T029 (2026-10-20): only the sidechain is oversampled, so the audio path carries no halfband delay
            // (plan S7's ~3.8 samples described the whole-chain mechanism); the peak sits at lag 0, on the edge.
            CHECK(std::abs(delay40) <= 0.5);
            (void)delayInterior;
            CHECK(mdoFiniteBitsD(delay40));
        }
    }

    // R-J record: steady GR and makeup at Mass 0 / 50 / 100 on the Fold-100 input, off vs on.
    struct MdoRj {
        float mass;
        MdoGrReading off, on;
    };
    std::vector<MdoRj> rj;
    for (const float mass : {MassLaw::kMinMass, MassLaw::kNeutralMass, MassLaw::kMaxMass})
        rj.push_back(MdoRj{.mass = mass, .off = mdoSteadyGr(mass, false), .on = mdoSteadyGr(mass, true)});

    const std::string dir = mdoEvidenceDir();
    std::filesystem::create_directories(dir);
    const std::string path = dir + "os-dynamics-delta.txt";
    {
        std::ofstream out(path);
        REQUIRE(out.is_open());
        out << std::setprecision(6);
        out << "# Profundum Phase 9 T005 (FR-082, plan S7, R-J). Written by MassDynamics_OversamplingTransfer\n";
        out << "# (dsp/tests/unit/systems/mass_dynamics_oversampling_test.cpp). Recorded, not gated.\n";
        out << "# fs " << kMdoFs << " Hz, host block " << kMdoHostBlock << ".\n\n";
        out << "== Neutral transfer (compression 0, trim 0, transient gains 0; sine amplitude " << kMdoSineAmplitude
            << "; 1 s settle, 1 s read) ==\n";
        out << "hz on_over_off_db (gated +/-" << kMdoTransferBoundDb << " dB)\n";
        for (std::size_t f = 0; f < kMdoTransferHz.size(); ++f)
            out << kMdoTransferHz[f] << " " << gainDb[f] << "\n";
        out << "\n== Group delay at 40 Hz (cross-correlation peak, parabolic interpolation) ==\n";
        out << "delay_base_samples " << delay40 << (delayInterior ? "" : " (peak on the search edge)") << "\n";
        out << "\n== R-J: steady compressor GR and makeup, Fold-100 E2 sum input (Mass 100 chain render), mean over"
               " host-block ends in [1 s, 2 s) ==\n";
        out << "mass gr_off_db gr_on_db gr_delta_db makeup_off_db makeup_on_db makeup_delta_db\n";
        for (const MdoRj& e : rj) {
            out << e.mass << " " << e.off.grDb << " " << e.on.grDb << " " << (e.on.grDb - e.off.grDb) << " "
                << e.off.makeupDb << " " << e.on.makeupDb << " " << (e.on.makeupDb - e.off.makeupDb) << "\n";
            std::cout << "[T005 R-J] Mass " << e.mass << ": GR off " << e.off.grDb << " / on " << e.on.grDb
                      << " dB, makeup off " << e.off.makeupDb << " / on " << e.on.makeupDb << " dB\n";
        }
    }
    for (const MdoRj& e : rj) {
        CHECK(mdoFiniteBitsD(e.off.grDb));
        CHECK(mdoFiniteBitsD(e.on.grDb));
        CHECK(mdoFiniteBitsD(e.off.makeupDb));
        CHECK(mdoFiniteBitsD(e.on.makeupDb));
    }
}

// FR-061: opt-in on, 10 s: oversamplerStateFinite() and stateFinite() at every 32-sample grid point;
// every output sample finite (bit test).
TEST_CASE("MassDynamics_OversamplingStateFinite", "[systems][profundum][mass-dynamics][voice-engine][fr-061]") {
    MdoStereo buf = mdoTiledInput(kMdoLongSeconds);
    auto md = mdoMake(true, true, mdoTargetsAt(MassLaw::kMaxMass));
    REQUIRE(md->oversamplingActive());
    constexpr std::size_t kGrid = MassDynamics::kControlInterval;
    std::size_t gridPoints = 0;
    std::size_t osStateBad = 0;
    std::size_t stateBad = 0;
    for (std::size_t b = 0; b < buf.l.size(); b += kGrid) {
        const std::size_t n = std::min(kGrid, buf.l.size() - b);
        md->processBlock(buf.l.data() + b, buf.r.data() + b, n);
        ++gridPoints;
        if (!md->oversamplerStateFinite())
            ++osStateBad;
        if (!md->stateFinite())
            ++stateBad;
    }
    std::size_t outputBad = 0;
    for (std::size_t i = 0; i < buf.l.size(); ++i) {
        if (!mdoFiniteBits(buf.l[i]) || !mdoFiniteBits(buf.r[i]))
            ++outputBad;
    }
    INFO("grid points " << gridPoints);
    CHECK(gridPoints == (buf.l.size() + kGrid - 1) / kGrid);
    CHECK(osStateBad == 0);
    CHECK(stateBad == 0);
    CHECK(outputBad == 0);
}
