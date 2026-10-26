// ==============================================================================
// Profundum Phase 6 (specs/profundum-phase6-mass-dynamics)
// Chain validation: SC-014, SC-015, SC-018, SC-021; FR-030 - FR-035, FR-049
// ==============================================================================
// tasks.md T012 (plan S9, S11, S13.1; deviations 5, 8, 13). Tags [systems][profundum][mass-dynamics].
// The chain is MassDynamicsTest::renderChain (mass_dynamics_test_helpers.h, T011):
//   ProfundumCore -> a1 -> AnchorOscillator (Sine, LOCK); ProfundumCore -> HarmonicFold;
//   sum = fold + anchor -> MassDynamics -> DCBlocker x2 -> TruePeakLimiter.
//
// This TU is compiled in the shipping FP mode (not -fno-fast-math): finiteness is read from bit
// patterns (TransientShaperTest::isFiniteBits, mcFiniteD), never std::isfinite. No bit-exact float
// goldens: every memcmp compares two renders of this binary in this process (tasks.md rule 7).
// TU-local names carry an `mc` / `kMc` / `Mc` prefix (tasks.md rule 5).
//
// Recorded decisions (evidence/run-log/T012.txt):
//   - No case is [long]: tasks.md T012 names none (rule 6), and SC-018 is a bit-identity case
//     (never [long]). Cells run serially: renderChain REQUIREs inside, and Catch2 assertions are
//     not thread-safe, so the optional std::async of SC-015 is not used.
//   - SC-014(d): the mean is taken over the whole f0 periods that fit inside the last 2 s
//     (HarmonicFoldTest::integerPeriodLength), so a partial period of the note is not read as DC.
//   - SC-015 attack record: the attack GR peak is the minimum GR before 1 s; its duration is the
//     span from the first to the last sample before 1 s with GR < -0.01 dB (the SC-015 tolerance;
//     the limiter's release float floor, FR-034, keeps GR slightly below 0 after any touch).
//   - SC-018: renderChain calls configureDynamics once after prepare, so the hook stores the
//     MassDynamics pointer and the massAt callback (called once at every grid point, before that
//     interval's dynamics run) applies setFixedReleaseMs(275) at the grid point 8704.
// ==============================================================================

#include "mass_dynamics_test_helpers.h"

#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/processors/harmonic_fold.h>
#include <krate/dsp/processors/period_peak_detector.h>
#include <krate/dsp/processors/spectral_shape_recipe.h>
#include <krate/dsp/processors/transient_shaper.h>
#include <krate/dsp/processors/true_peak_limiter.h>
#include <krate/dsp/systems/anchor_oscillator.h>
#include <krate/dsp/systems/mass_dynamics.h>
#include <krate/dsp/systems/mass_law.h>
#include <krate/dsp/systems/profundum_core.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

using namespace Krate::DSP;
namespace mdc = Krate::DSP::MassDynamicsTest;
namespace tst = Krate::DSP::TransientShaperTest;
using McProbe = Krate::DSP::detail::MassDynamicsProbe;

namespace {

constexpr double kMcFs = 48000.0;
constexpr std::size_t kMcStepAt = 8704;   // lcm(17, 32, 64, 512) grid point, plan S1 C-11
constexpr std::array<float, 5> kMcMasses{0.0f, 25.0f, 50.0f, 75.0f, 100.0f};

struct McNote {
    const char* name;
    double hz;
};

std::array<McNote, 4> mcNotes() {
    return {{{.name = "E0", .hz = tst::kE0Hz},
             {.name = "E1", .hz = tst::kE1Hz},
             {.name = "E2", .hz = tst::kE2Hz},
             {.name = "C3", .hz = tst::kC3Hz}}};
}

struct McFixture {
    const char* name;
    SpectralShapeRecipe::Controls base;
    bool sc015Gated = true;   ///< false: SC-015 recorded (G8-6 ruling 2026-10-17, the pure-sine fixture)
};

/// The three spec fixtures: the Mass reference voice, kSawAnchor (richest), kSineAnchor (purest).
std::array<McFixture, 3> mcFixtures() {
    return {{{.name = "reference", .base = SpectralShapeRecipe::kDefaultControls},
             {.name = "kSawAnchor", .base = SpectralShapeRecipe::kSawAnchor},
             // SC-015 recorded (spec SC-015 row, user ruling 2026-10-17 G8-6): a sub-only voice is invisible to
             // the 2 f0 sidechain, so at Mass 100 it carries the full makeup with no GR and sits above the ceiling.
             {.name = "kSineAnchor", .base = SpectralShapeRecipe::kSineAnchor, .sc015Gated = false}}};
}

bool mcBitEqual(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

bool mcBitEqualFrom(const std::vector<float>& a, const std::vector<float>& b, std::size_t from) {
    if (a.size() != b.size() || from > a.size())
        return false;
    return std::memcmp(a.data() + from, b.data() + from, (a.size() - from) * sizeof(float)) == 0;
}

/// Finite by bit pattern (fast-math immune).
bool mcFiniteD(double x) noexcept {
    return (std::bit_cast<std::uint64_t>(x) & 0x7FF0000000000000ull) != 0x7FF0000000000000ull;
}

/// The TruePeakLimiter ceiling the chain uses (default -1 dB).
float mcCeiling(double fs) {
    TruePeakLimiter l;
    l.prepare(fs, 512);
    return l.getCeilingLinear();
}

/// A static Mass for every sample.
std::function<float(std::size_t)> mcMassConst(float mass) {
    return [mass](std::size_t) { return mass; };
}

/// The constant override value v for one target.
mdc::TargetOverride mcOverrideConst(float v) {
    return [v](std::size_t) { return std::optional<float>(v); };
}

/// Number of samples that are not finite or exceed `ceiling` in magnitude, over both channels.
struct McBound {
    std::size_t nonFinite = 0;
    std::size_t overCeiling = 0;
    double maxAbs = 0.0;
};

McBound mcCheckBound(const std::vector<float>& L, const std::vector<float>& R, float ceiling) {
    McBound b;
    for (const std::vector<float>* ch : {&L, &R}) {
        for (const float v : *ch) {
            if (!tst::isFiniteBits(v)) {
                ++b.nonFinite;
                continue;
            }
            const float a = std::abs(v);
            b.maxAbs = std::max(b.maxAbs, static_cast<double>(a));
            if (a > ceiling)
                ++b.overCeiling;
        }
    }
    return b;
}

/// h1 amplitude of x by a rectangular single-bin DFT at f0 over the last round(64 · fs / f0)
/// samples (64 whole periods), in double: 2 |X| / L.
double mcH1Amplitude(const std::vector<float>& x, double fs, double f0) {
    const auto L = static_cast<std::size_t>(std::llround(64.0 * fs / f0));
    REQUIRE(L > 0);
    REQUIRE(L <= x.size());
    const std::size_t start = x.size() - L;
    const double omega = ProfundumTest::kTwoPi * f0 / fs;
    double re = 0.0;
    double im = 0.0;
    for (std::size_t k = 0; k < L; ++k) {
        const double v = static_cast<double>(x[start + k]);
        const double ph = omega * static_cast<double>(k);
        re += v * std::cos(ph);
        im -= v * std::sin(ph);
    }
    return 2.0 * std::hypot(re, im) / static_cast<double>(L);
}

/// 20·log10 |mean(y)| over the whole f0 periods that fit inside the last 2 s (SC-014(d)).
double mcDcDb(const std::vector<float>& y, double fs, double f0) {
    const std::size_t twoS = tst::secondsToSamples(fs, 2.0);
    REQUIRE(twoS <= y.size());
    const std::size_t len = HarmonicFoldTest::integerPeriodLength(twoS, fs, f0, 1.0);
    const std::size_t begin = y.size() - len;
    double sum = 0.0;
    for (std::size_t i = begin; i < y.size(); ++i)
        sum += static_cast<double>(y[i]);
    const double mean = sum / static_cast<double>(len);
    return 20.0 * std::log10(std::max(std::abs(mean), 1e-300));
}

/// RMS of y[begin, end), in double.
double mcRms(const std::vector<float>& y, std::size_t begin, std::size_t end) {
    REQUIRE(begin < end);
    REQUIRE(end <= y.size());
    double sq = 0.0;
    for (std::size_t i = begin; i < end; ++i)
        sq += static_cast<double>(y[i]) * static_cast<double>(y[i]);
    return std::sqrt(sq / static_cast<double>(end - begin));
}

/// The limiter's GR read from its in / out taps (dcL/R -> outL/R) on samples with |in| > 1e-4
/// (SC-015). GR = 20·log10(|out| / |in|), double.
struct McLimiterStats {
    double steadyMinGrDb = 0.0;      ///< minimum GR from 1 s on
    double attackPeakGrDb = 0.0;     ///< minimum GR before 1 s
    double attackDurationMs = 0.0;   ///< first..last sample before 1 s with GR < -0.01 dB
    bool engaged = false;            ///< any sample with GR < 0 from note-on (sample 0)
    std::size_t counted = 0;         ///< samples with |in| > 1e-4
};

constexpr double kMcGrInFloor = 1e-4;
constexpr double kMcGrBarDb = -0.01;

McLimiterStats mcLimiterStats(const mdc::ChainTaps& t, double fs) {
    McLimiterStats s;
    const std::size_t from = tst::secondsToSamples(fs, 1.0);
    std::size_t first = std::numeric_limits<std::size_t>::max();
    std::size_t last = 0;
    const std::array<std::pair<const std::vector<float>*, const std::vector<float>*>, 2> chans{
        {{&t.dcL, &t.outL}, {&t.dcR, &t.outR}}};
    for (const auto& [in, out] : chans) {
        REQUIRE(in->size() == out->size());
        for (std::size_t i = 0; i < in->size(); ++i) {
            const double a = std::abs(static_cast<double>((*in)[i]));
            if (!(a > kMcGrInFloor))
                continue;
            ++s.counted;
            const double b = std::max(std::abs(static_cast<double>((*out)[i])), 1e-300);
            const double gr = 20.0 * std::log10(b / a);
            if (gr < 0.0)
                s.engaged = true;
            if (i >= from) {
                s.steadyMinGrDb = std::min(s.steadyMinGrDb, gr);
            } else {
                s.attackPeakGrDb = std::min(s.attackPeakGrDb, gr);
                if (gr < kMcGrBarDb) {
                    first = std::min(first, i);
                    last = std::max(last, i);
                }
            }
        }
    }
    if (first <= last)
        s.attackDurationMs = static_cast<double>(last - first + 1) * 1000.0 / fs;
    return s;
}

/// Mass owns exactly one Anchor field: one kRows row targets AnchorLevel (SC-014(a), plan S13.3).
constexpr std::size_t mcAnchorLevelRows() {
    std::size_t c = 0;
    for (const MassLaw::Row& r : MassLaw::kRows) {
        if (r.target == MassLaw::Target::AnchorLevel)
            ++c;
    }
    return c;
}

/// The MassDynamics SC-008(a)(b) gains at E1 (burst, A = +12, S = -6, duration 40, amount 0):
/// steady = mean shaper gain over [0.85 s, 1.0 s]; onset peak = max over [onset, onset + 50 ms].
struct McShaperGains {
    double steadyDb = 0.0;
    double onsetPeakDb = 0.0;
};

McShaperGains mcShaperGains(tst::Tone tone, double fs) {
    const std::vector<float> x = tst::burst(tone, tst::kE1Hz, fs, 1.0, 0.0, 0.25);
    std::vector<float> L = x;
    std::vector<float> R = x;
    MassDynamics md;
    md.setTransientAttackDb(12.0f);
    md.setTransientSustainDb(-6.0f);
    md.setTransientDurationMs(40.0f);
    md.setCompressionAmount(0.0f);
    md.setDetectorKeyHz(static_cast<float>(tst::kE1Hz));
    md.prepare(fs, 512);
    std::vector<float> g(x.size(), 0.0f);
    for (std::size_t i = 0; i < x.size(); ++i) {
        md.processBlock(L.data() + i, R.data() + i, 1);
        g[i] = md.shaper().currentGainDb();
    }
    McShaperGains r;
    const std::span<const float> gs(g);
    r.steadyDb = tst::meanOf(gs, tst::secondsToSamples(fs, 0.85), tst::secondsToSamples(fs, 1.0));
    const std::size_t onsetEnd = tst::secondsToSamples(fs, 0.050) + 1;
    REQUIRE(onsetEnd <= g.size());
    r.onsetPeakDb = static_cast<double>(*std::max_element(g.begin(), g.begin() + static_cast<std::ptrdiff_t>(onsetEnd)));
    return r;
}

/// SC-006(a) M_sub at E1 on the chain (reference voice, Mass 50, compression amount 1, 3 s):
/// mSub of subBand(outL).dftDb over [1 s, 3 s].
double mcChainMSub(double fs) {
    mdc::ChainConfig cfg;
    cfg.f0 = tst::kE1Hz;
    cfg.fs = fs;
    cfg.seconds = 3.0;
    cfg.massAt = mcMassConst(50.0f);
    cfg.targetOverride[static_cast<std::size_t>(MassLaw::Target::CompressionAmount)] = mcOverrideConst(1.0f);
    const mdc::ChainTaps t = mdc::renderChain(cfg);
    const mdc::SubBand sb = mdc::subBand(t.outL.data(), t.outL.size(), fs, tst::kE1Hz);
    return mdc::mSubSamples(sb, sb.dftDb, tst::secondsToSamples(fs, 1.0), t.outL.size());
}

std::string mcEvidenceDir() {
    return std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase6-mass-dynamics/evidence/";
}

const char* mcTargetName(MassLaw::Target t) {
    switch (t) {
        case MassLaw::Target::Depth: return "Depth";
        case MassLaw::Target::Body: return "Body";
        case MassLaw::Target::TransientDuration: return "TransientDuration";
        case MassLaw::Target::FoldAmount: return "FoldAmount";
        case MassLaw::Target::CompressionAmount: return "CompressionAmount";
        case MassLaw::Target::AnchorLevel: return "AnchorLevel";
        case MassLaw::Target::OutputTrim: return "OutputTrim";
    }
    return "?";
}

}  // namespace

// =============================================================================
// Plan S11 (FR-049): the chain's stages share one 32-sample control grid
// =============================================================================
TEST_CASE("MassChain_ControlIntervalsAligned", "[systems][profundum][mass-dynamics]") {
    static_assert(BassCompressor::kControlInterval == ProfundumCore::kControlInterval);
    static_assert(MassDynamics::kControlInterval == ProfundumCore::kControlInterval);
    static_assert(MassLaw::kControlInterval == ProfundumCore::kControlInterval);
    static_assert(AnchorOscillator::kControlInterval == ProfundumCore::kControlInterval);
    static_assert(AnchorOscillator::kMaxCoreH1Amplitude == HarmonicFold::kCalibrationPeak);
    STATIC_REQUIRE(BassCompressor::kControlInterval == 32);
    STATIC_REQUIRE(MassDynamics::kControlInterval == ProfundumCore::kControlInterval);
    STATIC_REQUIRE(MassLaw::kControlInterval == ProfundumCore::kControlInterval);
    STATIC_REQUIRE(AnchorOscillator::kControlInterval == ProfundumCore::kControlInterval);
    STATIC_REQUIRE(AnchorOscillator::kMaxCoreH1Amplitude == HarmonicFold::kCalibrationPeak);
}

// =============================================================================
// SC-014 (FR-030 - FR-033): core + Anchor through the Mass dynamics and the output safety stages
// =============================================================================
TEST_CASE("MassChain_CoreAnchorValidated", "[systems][profundum][mass-dynamics]") {
    static_assert(sizeof(MassLaw::Targets) == 7 * sizeof(float),
                  "the law has the six roadmap outputs plus the FR-021 output trim (G8-5 ruling 2026-10-17)");
    static_assert(mcAnchorLevelRows() == 1, "Mass owns exactly one Anchor field (FR-030)");

    const float ceiling = mcCeiling(kMcFs);
    const AnchorOscillator::Controls start = mdc::anchorControlsFor(0.0f);

    for (const McNote& note : mcNotes()) {
        std::array<std::pair<double, double>, kMcMasses.size()> levelRatio{};   // (Anchor-level target, RMS / (a1/√2))
        std::optional<float> a1FirstRecorded;   // FR-031 (G8-3): the fed a1 is Mass-independent per note
        for (std::size_t m = 0; m < kMcMasses.size(); ++m) {
            const float mass = kMcMasses[m];
            INFO(note.name << " Mass " << mass);
            mdc::ChainConfig cfg;
            cfg.f0 = note.hz;
            cfg.fs = kMcFs;
            cfg.seconds = 5.0;
            cfg.massAt = mcMassConst(mass);
            const mdc::ChainTaps t = mdc::renderChain(cfg);
            mdc::ChainConfig cfgDetached = cfg;
            cfgDetached.attachAnchor = false;
            const mdc::ChainTaps d = mdc::renderChain(cfgDetached);
            const std::size_t n = t.outL.size();

            // (a) Product read-back: pendingControls() differs from the starting Controls only in level,
            //     and level is the law's Anchor-level target of that interval.
            REQUIRE(t.anchorControlsPerInterval.size() == t.targetsPerInterval.size());
            REQUIRE(t.a1PerInterval.size() == t.targetsPerInterval.size());
            REQUIRE_FALSE(t.a1PerInterval.empty());
            std::size_t otherFieldDiffs = 0;
            std::size_t levelDiffs = 0;
            for (std::size_t k = 0; k < t.anchorControlsPerInterval.size(); ++k) {
                const AnchorOscillator::Controls& c = t.anchorControlsPerInterval[k];
                if (c.waveform != start.waveform || c.mode != start.mode || c.punchBoostDb != start.punchBoostDb ||
                    c.punchTimeMs != start.punchTimeMs || c.ghostTimeMs != start.ghostTimeMs)
                    ++otherFieldDiffs;
                if (c.level != t.targetsPerInterval[k].anchorLevel)
                    ++levelDiffs;
            }
            // a1 against an independent observation: the h1 amplitude of the rendered core tap. FR-031 as
            // amended (user ruling 2026-10-17, G8-3): the fed a1 is the voice's unbiased h1, so it is
            // Mass-independent and equals the delivered h1 only at Mass 50 (no bias); elsewhere the
            // delivered-to-fed ratio is recorded (how far Mass thins the core's h1).
            const double a1Recorded = static_cast<double>(t.a1PerInterval.back());
            const double a1Measured = mcH1Amplitude(t.coreL, kMcFs, note.hz);
            REQUIRE(a1Recorded > 0.0);
            const double a1ErrDb = std::abs(20.0 * std::log10(std::max(a1Measured, 1e-300) / a1Recorded));
            if (!a1FirstRecorded.has_value())
                a1FirstRecorded = t.a1PerInterval.back();
            const bool a1MassIndependent = t.a1PerInterval.back() == *a1FirstRecorded;
            const bool a1Gated = mass == 50.0f;

            // (b) the core is bit-identical with the Anchor attached and detached (Phase 3 FR-025).
            const bool coreEqual = mcBitEqual(t.coreL, d.coreL) && mcBitEqual(t.coreR, d.coreR);

            // (c) finite and |out| <= the limiter ceiling at sample level.
            const McBound bound = mcCheckBound(t.outL, t.outR, ceiling);

            // (d) DC <= -80 dBFS.
            const double dcL = mcDcDb(t.outL, kMcFs, note.hz);
            const double dcR = mcDcDb(t.outR, kMcFs, note.hz);

            // (e) no hidden compensation: Anchor RMS / (a1 / √2), and sum == fold + anchor (float add).
            const std::size_t lastTwo = n - tst::secondsToSamples(kMcFs, 2.0);
            const double anchorRms = mcRms(t.anchorL, lastTwo, n);
            const double ratio = anchorRms / (a1Recorded / std::sqrt(2.0));
            const double levelTarget = static_cast<double>(t.targetsPerInterval.back().anchorLevel);
            levelRatio[m] = {levelTarget, ratio};
            std::vector<float> sumL(n);
            std::vector<float> sumR(n);
            for (std::size_t i = 0; i < n; ++i) {
                sumL[i] = t.foldL[i] + t.anchorL[i];
                sumR[i] = t.foldR[i] + t.anchorR[i];
            }
            const bool sumEqual = mcBitEqual(sumL, t.sumL) && mcBitEqual(sumR, t.sumR);

            // w1 of outL (recorded only, SC-014(e)).
            const auto ceilP = static_cast<std::size_t>(std::ceil(kMcFs / note.hz));
            const std::size_t w1Start = n - ceilP - 2 * static_cast<std::size_t>(mdc::kResampleHalf) - 1;
            const double w1 = mdc::periodDescriptors(t.outL.data(), w1Start, kMcFs, note.hz).w1Frac;

            std::printf("MassChain_CoreAnchorValidated %s Mass %.6g: (a) other-field diffs %zu, level diffs %zu, a1 recorded "
                        "%.6g measured %.6g (|err| %.6g dB, %s); (b) core attached == detached %s; (c) non-finite %zu, "
                        "over ceiling %zu (max |out| %.6g, ceiling %.6g); (d) DC L %.6g R %.6g dBFS (bar -80); (e) anchor "
                        "level target %.6g, RMS / (a1/sqrt2) %.6g, sum == fold + anchor %s; w1(outL) %.6g (recorded)\n",
                        note.name, static_cast<double>(mass), otherFieldDiffs, levelDiffs, a1Recorded, a1Measured, a1ErrDb,
                        a1Gated ? "bar 0.05 at Mass 50" : "recorded, G8-3", coreEqual ? "bit-equal" : "DIFFERS",
                        bound.nonFinite, bound.overCeiling, bound.maxAbs,
                        static_cast<double>(ceiling), dcL, dcR, levelTarget, ratio, sumEqual ? "bit-equal" : "DIFFERS", w1);
            CHECK(otherFieldDiffs == 0u);
            CHECK(levelDiffs == 0u);
            CHECK(a1MassIndependent);
            if (a1Gated)
                CHECK(a1ErrDb <= 0.05);
            CHECK(coreEqual);
            CHECK(bound.nonFinite == 0u);
            CHECK(bound.overCeiling == 0u);
            CHECK(dcL <= -80.0);
            CHECK(dcR <= -80.0);
            CHECK(sumEqual);
        }
        // (e) RMS / (a1 / √2) is non-decreasing in the Anchor-level target.
        std::sort(levelRatio.begin(), levelRatio.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        std::size_t decreases = 0;
        for (std::size_t k = 1; k < levelRatio.size(); ++k) {
            if (levelRatio[k].second < levelRatio[k - 1].second) {
                ++decreases;
                std::printf("MassChain_CoreAnchorValidated %s (e): ratio falls from %.6g (level %.6g) to %.6g (level %.6g)\n",
                            note.name, levelRatio[k - 1].second, levelRatio[k - 1].first, levelRatio[k].second,
                            levelRatio[k].first);
            }
        }
        INFO(note.name);
        CHECK(decreases == 0u);
    }
}

// =============================================================================
// SC-015 (FR-034): the limiter is idle in steady state.
// Expected red until T019 (harvest-dependent levels; tasks.md Group 7 gate rule).
// =============================================================================
TEST_CASE("MassChain_LimiterIdleSteadyState", "[systems][profundum][mass-dynamics]") {
    const std::size_t from = tst::secondsToSamples(kMcFs, 1.0);
    std::size_t cells = 0;
    std::size_t engagedCells = 0;
    for (const McFixture& fx : mcFixtures()) {
        for (const McNote& note : mcNotes()) {
            for (const float mass : kMcMasses) {
                INFO(fx.name << " " << note.name << " Mass " << mass);
                mdc::ChainConfig cfg;
                cfg.base = fx.base;
                cfg.f0 = note.hz;
                cfg.fs = kMcFs;
                cfg.seconds = 3.0;
                cfg.massAt = mcMassConst(mass);
                const mdc::ChainTaps t = mdc::renderChain(cfg);
                const McLimiterStats s = mcLimiterStats(t, kMcFs);
                ++cells;
                if (s.engaged)
                    ++engagedCells;
                std::printf("MassChain_LimiterIdleSteadyState %s %s Mass %.6g: engaged %s, attack GR peak %.6g dB, "
                            "attack duration %.6g ms, steady min GR from 1 s %.6g dB (bar -0.01), samples counted %zu\n",
                            fx.name, note.name, static_cast<double>(mass), s.engaged ? "yes" : "no", s.attackPeakGrDb,
                            s.attackDurationMs, s.steadyMinGrDb, s.counted);
                REQUIRE(s.counted > 0u);
                // (a) from 1 s, GR >= -0.01 dB on every sample with |in| > 1e-4 (the sine fixture recorded, G8-6).
                if (fx.sc015Gated)
                    CHECK(s.steadyMinGrDb >= kMcGrBarDb);
                else
                    WARN(fx.name << ' ' << note.name << " Mass " << mass << ": SC-015 steady min GR " << s.steadyMinGrDb
                                 << " dB recorded (G8-6: sub-only voice, makeup unopposed by the 2 f0 sidechain)");
                // (b) no-touch cells: in == out bit-equal from 1 s.
                if (!s.engaged) {
                    const bool equal = mcBitEqualFrom(t.dcL, t.outL, from) && mcBitEqualFrom(t.dcR, t.outR, from);
                    CHECK(equal);
                }
            }
        }
    }
    std::printf("MassChain_LimiterIdleSteadyState: %zu cells, %zu engaged at some sample\n", cells, engagedCells);
    REQUIRE(cells == mcFixtures().size() * mcNotes().size() * kMcMasses.size());
}

// =============================================================================
// SC-018 (FR-049): determinism and partition invariance of the full chain
// =============================================================================
TEST_CASE("MassDynamics_PartitionInvariant", "[systems][profundum][mass-dynamics]") {
    static_assert(kMcStepAt % mdc::kGrid == 0, "8704 is a grid point (plan S1 C-11)");

    // Mass 20 -> 80 and a forced release change (probe) at sample 8704, E1, 3 s.
    const auto render = [](std::size_t block) {
        MassDynamics* mdPtr = nullptr;
        bool applied = false;
        mdc::ChainConfig cfg;
        cfg.f0 = tst::kE1Hz;
        cfg.fs = kMcFs;
        cfg.seconds = 3.0;
        cfg.hostBlock = block;
        cfg.configureDynamics = [&mdPtr](MassDynamics& m) { mdPtr = &m; };
        cfg.massAt = [&mdPtr, &applied](std::size_t n) {
            if (n >= kMcStepAt && !applied && mdPtr != nullptr) {
                McProbe::setFixedReleaseMs(McProbe::compressor(*mdPtr), 275.0f);
                applied = true;
            }
            return n < kMcStepAt ? 20.0f : 80.0f;
        };
        mdc::ChainTaps t = mdc::renderChain(cfg);
        REQUIRE(applied);
        return std::make_pair(std::move(t.outL), std::move(t.outR));
    };

    const std::array<std::size_t, 5> blocks{1, 17, 32, 64, 512};
    std::vector<std::pair<std::vector<float>, std::vector<float>>> outs;
    outs.reserve(blocks.size());
    for (const std::size_t b : blocks)
        outs.push_back(render(b));
    for (std::size_t b = 1; b < blocks.size(); ++b) {
        const bool equal = mcBitEqual(outs[b].first, outs[0].first) && mcBitEqual(outs[b].second, outs[0].second);
        std::printf("MassDynamics_PartitionInvariant block %zu vs 1: %s\n", blocks[b], equal ? "bit-equal" : "DIFFERS");
        CHECK(equal);
    }

    // Two renders from fresh fixtures.
    const auto again = render(512);
    const bool freshEqual = mcBitEqual(again.first, outs[4].first) && mcBitEqual(again.second, outs[4].second);
    std::printf("MassDynamics_PartitionInvariant: two renders from fresh fixtures %s\n", freshEqual ? "bit-equal" : "DIFFERS");
    CHECK(freshEqual);
    // No random source: the four new headers contain no <random> / rand (grep recorded in
    // evidence/run-log/T012.txt).
}

// =============================================================================
// SC-021: sample-rate behaviour (chain bounds; MassDynamics SC-008(a)(b) gains; chain SC-006(a) M_sub)
// =============================================================================
TEST_CASE("MassDynamics_SampleRates", "[systems][profundum][mass-dynamics]") {
    const std::array<double, 5> rates{44100.0, 48000.0, 88200.0, 96000.0, 192000.0};
    constexpr std::size_t k48 = 1;
    constexpr std::array<float, 3> kMasses{0.0f, 50.0f, 100.0f};
    std::array<McShaperGains, 5> sawGains{};
    std::array<McShaperGains, 5> sineGains{};
    std::array<double, 5> mSub{};

    for (std::size_t r = 0; r < rates.size(); ++r) {
        const double fs = rates[r];
        INFO("fs " << fs);
        // Finite and bounded: the chain at Mass {0, 50, 100}, E1, 2 s.
        const float ceiling = mcCeiling(fs);
        for (const float mass : kMasses) {
            mdc::ChainConfig cfg;
            cfg.f0 = tst::kE1Hz;
            cfg.fs = fs;
            cfg.seconds = 2.0;
            cfg.massAt = mcMassConst(mass);
            const mdc::ChainTaps t = mdc::renderChain(cfg);
            const McBound b = mcCheckBound(t.outL, t.outR, ceiling);
            std::printf("MassDynamics_SampleRates fs %.6g Mass %.6g: non-finite %zu, over ceiling %zu, max |out| %.6g "
                        "(ceiling %.6g)\n",
                        fs, static_cast<double>(mass), b.nonFinite, b.overCeiling, b.maxAbs, static_cast<double>(ceiling));
            INFO("Mass " << mass);
            CHECK(b.nonFinite == 0u);
            CHECK(b.overCeiling == 0u);
        }
        sawGains[r] = mcShaperGains(tst::Tone::Saw, fs);
        sineGains[r] = mcShaperGains(tst::Tone::Sine, fs);
        mSub[r] = mcChainMSub(fs);
    }
    for (std::size_t r = 0; r < rates.size(); ++r) {
        std::printf("MassDynamics_SampleRates fs %.6g: saw steady %.6g dB (48k %.6g), onset peak %.6g dB (48k %.6g); sine "
                    "steady %.6g dB (48k %.6g), onset peak %.6g dB (48k %.6g); chain M_sub %.6g dB (48k %.6g)\n",
                    rates[r], sawGains[r].steadyDb, sawGains[k48].steadyDb, sawGains[r].onsetPeakDb,
                    sawGains[k48].onsetPeakDb, sineGains[r].steadyDb, sineGains[k48].steadyDb, sineGains[r].onsetPeakDb,
                    sineGains[k48].onsetPeakDb, mSub[r], mSub[k48]);
        INFO("fs " << rates[r]);
        CHECK(std::abs(sawGains[r].steadyDb - sawGains[k48].steadyDb) <= 0.25);
        CHECK(std::abs(sawGains[r].onsetPeakDb - sawGains[k48].onsetPeakDb) <= 0.25);
        CHECK(std::abs(sineGains[r].steadyDb - sineGains[k48].steadyDb) <= 0.25);
        CHECK(std::abs(sineGains[r].onsetPeakDb - sineGains[k48].onsetPeakDb) <= 0.25);
        CHECK(std::abs(mSub[r] - mSub[k48]) <= 0.25);
    }

    // MassDynamics prepare(48k) -> prepare(96k) -> prepare(48k), then a render == a fresh 48 kHz one.
    const auto setup = [](MassDynamics& m) {
        m.setTransientAttackDb(12.0f);
        m.setTransientSustainDb(-6.0f);
        m.setTransientDurationMs(40.0f);
        m.setCompressionAmount(1.0f);
        m.setDetectorKeyHz(static_cast<float>(tst::kE1Hz));
    };
    const std::vector<float> x48 = tst::steadyTone(tst::Tone::Saw, tst::kE1Hz, kMcFs, 1.0);
    const std::vector<float> x96 = tst::steadyTone(tst::Tone::Saw, tst::kE1Hz, 96000.0, 1.0);
    MassDynamics a;
    setup(a);
    a.prepare(kMcFs, 512);
    std::vector<float> wL = x48;
    std::vector<float> wR = x48;
    a.processBlock(wL.data(), wR.data(), wL.size());
    a.prepare(96000.0, 512);
    std::vector<float> hL = x96;
    std::vector<float> hR = x96;
    a.processBlock(hL.data(), hR.data(), hL.size());
    a.prepare(kMcFs, 512);
    std::vector<float> aL = x48;
    std::vector<float> aR = x48;
    a.processBlock(aL.data(), aR.data(), aL.size());
    MassDynamics fresh;
    setup(fresh);
    fresh.prepare(kMcFs, 512);
    std::vector<float> fL = x48;
    std::vector<float> fR = x48;
    fresh.processBlock(fL.data(), fR.data(), fL.size());
    const bool reprepareEqual = mcBitEqual(aL, fL) && mcBitEqual(aR, fR);
    std::printf("MassDynamics_SampleRates: prepare(48k) -> prepare(96k) -> prepare(48k) render vs fresh 48 kHz: %s\n",
                reprepareEqual ? "bit-equal" : "DIFFERS");
    CHECK(reprepareEqual);
}

// =============================================================================
// FR-035: the Phase 9 handoff record (recorded, not gated; asserts finite numbers and the file)
// =============================================================================
TEST_CASE("MassChain_Phase9HandoffRecord", "[systems][profundum][mass-dynamics]") {
    std::vector<double> numbers;   // every number written, checked finite below

    // 4-voice Mass 100 poly-sum steady GR: E1 detuned by {-6, -2, +2, +6} cents, mean over [1 s, 3 s].
    const std::array<double, 4> cents{-6.0, -2.0, 2.0, 6.0};
    std::array<double, 4> f0s{};
    for (std::size_t v = 0; v < f0s.size(); ++v)
        f0s[v] = tst::kE1Hz * std::exp2(cents[v] / 1200.0);
    mdc::ChainConfig poly;
    poly.fs = kMcFs;
    poly.seconds = 3.0;
    poly.massAt = mcMassConst(100.0f);
    poly.perSampleDynamicsTaps = true;
    const mdc::ChainTaps pt = mdc::renderPoly(std::span<const double>(f0s), poly);
    const double polyGr = tst::meanOf(std::span<const float>(pt.compGrDb), tst::secondsToSamples(kMcFs, 1.0),
                                      pt.compGrDb.size());
    numbers.push_back(polyGr);

    // Limiter attack GR at Mass 100, E1, per fixture.
    struct McAttack {
        const char* name;
        McLimiterStats s;
    };
    std::vector<McAttack> attacks;
    for (const McFixture& fx : mcFixtures()) {
        mdc::ChainConfig cfg;
        cfg.base = fx.base;
        cfg.f0 = tst::kE1Hz;
        cfg.fs = kMcFs;
        cfg.seconds = 3.0;
        cfg.massAt = mcMassConst(100.0f);
        const mdc::ChainTaps t = mdc::renderChain(cfg);
        attacks.push_back(McAttack{.name = fx.name, .s = mcLimiterStats(t, kMcFs)});
        numbers.push_back(attacks.back().s.attackPeakGrDb);
        numbers.push_back(attacks.back().s.attackDurationMs);
        numbers.push_back(attacks.back().s.steadyMinGrDb);
    }

    const std::string dir = mcEvidenceDir();
    std::filesystem::create_directories(dir);
    const std::string path = dir + "phase9-handoff.txt";
    {
        std::ofstream out(path);
        REQUIRE(out.is_open());
        out << std::setprecision(6);
        out << "# Profundum Phase 6 -> Phase 9 handoff record (FR-035). Written by MassChain_Phase9HandoffRecord\n";
        out << "# (dsp/tests/unit/systems/mass_chain_test.cpp). Recorded, not gated.\n\n";

        out << "== Global block latency ==\n";
        out << "MassDynamics::latencySamples() = " << MassDynamics::latencySamples() << " samples\n";
        numbers.push_back(static_cast<double>(MassDynamics::latencySamples()));
        out << "SC-017 cell: see evidence/sc017-cpu.txt (T018 / T024)\n\n";

        out << "== Mass-to-target table (MassLaw::kRows; target = clamp(base + (u >= 0 ? up : -down) * curve(|u|), lo, hi),"
               " u = (Mass - 50) / 50) ==\n";
        out << "target lo hi amountDown amountUp curve(0 Linear, 1 Exponential, 2 SCurve)\n";
        for (const MassLaw::Row& row : MassLaw::kRows) {
            out << mcTargetName(row.target) << " " << row.lo << " " << row.hi << " " << row.amountDown << " "
                << row.amountUp << " " << static_cast<int>(row.curve) << "\n";
            numbers.push_back(static_cast<double>(row.lo));
            numbers.push_back(static_cast<double>(row.hi));
            numbers.push_back(static_cast<double>(row.amountDown));
            numbers.push_back(static_cast<double>(row.amountUp));
        }
        out << "slew: full 0 -> 100 traversal " << MassLaw::kMassSweepMs << " ms, advanceInterval() once per "
            << MassLaw::kControlInterval << " samples\n\n";
        numbers.push_back(static_cast<double>(MassLaw::kMassSweepMs));

        out << "== Compression amount table (BassCompressor::kAmountTable) ==\n";
        out << "amount thresholdDb slope(1 - 1/ratio) kneeDb makeupDb\n";
        for (const BassCompressor::AmountRow& row : BassCompressor::kAmountTable) {
            out << row.amount << " " << row.thresholdDb << " " << row.slope << " " << row.kneeDb << " " << row.makeupDb
                << "\n";
            numbers.push_back(static_cast<double>(row.amount));
            numbers.push_back(static_cast<double>(row.thresholdDb));
            numbers.push_back(static_cast<double>(row.slope));
            numbers.push_back(static_cast<double>(row.kneeDb));
            numbers.push_back(static_cast<double>(row.makeupDb));
        }
        out << "\n== Transient duration table (TransientShaper::kDurationTable) ==\n";
        out << "durationMs slowAttackArgMs\n";
        for (const TransientShaper::DurationPoint& p : TransientShaper::kDurationTable) {
            out << p.durationMs << " " << p.slowAttackArgMs << "\n";
            numbers.push_back(static_cast<double>(p.durationMs));
            numbers.push_back(static_cast<double>(p.slowAttackArgMs));
        }

        out << "\n== Poly-sum sub-band GR (4 voices at Mass 100; recorded, not gated) ==\n";
        out << "renderPoly, reference voice, E1 detuned by {-6, -2, +2, +6} cents, keyed by lowestSoundingF0; mean "
               "BassCompressor GR over [1 s, 3 s] = "
            << polyGr << " dB\n";

        out << "\n== Limiter GR at note attacks (Mass 100, E1, 48 kHz; limiter in / out taps, |in| > 1e-4) ==\n";
        out << "fixture attackPeakGrDb attackDurationMs(GR < -0.01 dB, before 1 s) steadyMinGrDb(from 1 s)\n";
        for (const McAttack& a : attacks) {
            out << a.name << " " << a.s.attackPeakGrDb << " " << a.s.attackDurationMs << " " << a.s.steadyMinGrDb << "\n";
        }

        out << "\n== Keytracked-corner contract (FR-018) ==\n";
        out << "call setDetectorKeyHz(lowestSoundingF0(voices)) once per 32-sample interval; 0 / non-finite = no voice -> "
            << BassCompressor::kFallbackCornerHz << " Hz\n";
        out << "corner = clamp(2 * f0, " << BassCompressor::kMinCornerHz << ", " << BassCompressor::kMaxCornerHz
            << ") Hz, smoothed in log2 over " << BassCompressor::kCornerSmoothMs << " ms at the interval rate\n";
        numbers.push_back(static_cast<double>(BassCompressor::kFallbackCornerHz));
        numbers.push_back(static_cast<double>(BassCompressor::kMinCornerHz));
        numbers.push_back(static_cast<double>(BassCompressor::kMaxCornerHz));
        numbers.push_back(static_cast<double>(BassCompressor::kCornerSmoothMs));

        out << "\n== Shaper user-facing parameters (FR-009; Phase 11 700-799 registration) ==\n";
        out << "parameter min max default unit\n";
        out << "attackGain " << TransientShaper::kMinGainDb << " " << TransientShaper::kMaxGainDb << " "
            << TransientShaper::kDefaultAttackDb << " dB\n";
        out << "sustainGain " << TransientShaper::kMinGainDb << " " << TransientShaper::kMaxGainDb << " "
            << TransientShaper::kDefaultSustainDb << " dB\n";
        out << "transientDuration (Mass-driven, not user-facing) " << TransientShaper::kMinDurationMs << " "
            << TransientShaper::kMaxDurationMs << " " << TransientShaper::kDefaultDurationMs << " ms\n";
        numbers.push_back(static_cast<double>(TransientShaper::kMinGainDb));
        numbers.push_back(static_cast<double>(TransientShaper::kMaxGainDb));
        numbers.push_back(static_cast<double>(TransientShaper::kDefaultAttackDb));
        numbers.push_back(static_cast<double>(TransientShaper::kDefaultSustainDb));
        numbers.push_back(static_cast<double>(TransientShaper::kMinDurationMs));
        numbers.push_back(static_cast<double>(TransientShaper::kMaxDurationMs));
        numbers.push_back(static_cast<double>(TransientShaper::kDefaultDurationMs));
        REQUIRE(out.good());
    }
    std::printf("MassChain_Phase9HandoffRecord: wrote %s; poly-sum Mass 100 GR %.6g dB; %zu numbers\n", path.c_str(),
                polyGr, numbers.size());
    std::size_t nonFinite = 0;
    for (const double v : numbers) {
        if (!mcFiniteD(v))
            ++nonFinite;
    }
    CHECK(nonFinite == 0u);
    REQUIRE(std::filesystem::exists(path));
}
