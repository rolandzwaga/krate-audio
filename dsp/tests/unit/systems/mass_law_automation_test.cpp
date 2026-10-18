// ==============================================================================
// Profundum Phase 6 (specs/profundum-phase6-mass-dynamics)
// Mass anchors / automation: SC-002, SC-005 (a)(b)(c), SC-011 Mass, SC-023 rendered
// (FR-023, FR-025, FR-027, FR-043). tasks.md T015.
//
// Every case renders the FR-033 validation chain (mass_dynamics_test_helpers.h, T011). Tags:
// [systems][profundum][mass-law]; the grid shards add [long] and each has a `...Smoke` sibling
// (tasks.md deviation 10). Every measured value is printed with std::setprecision(6).
//
// Expected red until T019 (tasks.md Group 7 gate rule, deviation 6): MassLaw_AnchorPoints and
// MassLaw_Audibility read harvest-dependent levels (kRows, kAmountTable, kDurationTable seeds).
//
// Recorded decisions (evidence/run-log/T015.txt):
//   - GR is reported as a positive dB figure: -BassCompressor::currentGainReductionDb() (that tap is
//     0 = none, negative = reduction; mass_dynamics.h:269-270).
//   - E_low, E_high and THD are compared after the OQ-S -60 dB floor clamp (ShapeCharacterTest::
//     floorClamp, the T011 self-test convention); raw values are printed beside them.
//   - "w1 of the sum" is ProfundumTest::renderedHarmonicPowers on the sumL tap (static frames) and
//     L + 10·log10(w1 / Σw) of the resampled period (one-period windows).
// ==============================================================================

#include "mass_dynamics_test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <optional>
#include <span>
#include <utility>
#include <vector>

using namespace Krate::DSP;

namespace {

namespace mla = Krate::DSP::MassDynamicsTest;
namespace mlaTs = Krate::DSP::TransientShaperTest;
namespace mlaSc = Krate::DSP::ShapeCharacterTest;
namespace mlaHf = Krate::DSP::HarmonicFoldTest;

constexpr double kMlaFs = 48000.0;
constexpr std::size_t kMlaFrame = Krate::DSP::TestUtils::kLowFrequencyFftSize;

// SC-002 bars (spec SC-002 row).
constexpr double kMlaThd0MaxDb = -30.0;
constexpr double kMlaThdStepDb = 6.0;
constexpr double kMlaElowStepDb = 3.0;
constexpr double kMlaGr100MinDb = 3.0;

// SC-005(b) bars (the Phase 5 SC-002 bars).
constexpr double kMlaLatticeLDb = 0.5;
constexpr double kMlaLatticeCOct = 0.1;
constexpr double kMlaLatticeEDb = 1.0;

// SC-011 bar.
constexpr double kMlaAudibleDb = 3.0;

// Repeat-noise caps (plan S13.2): 0.1 dB for dB descriptors, 0.005 oct for the centroid.
constexpr double kMlaEpsCapDb = 0.1;
constexpr double kMlaEpsCapOct = 0.005;

// SC-005(a)(c) automation fixture: 4 s, the Mass move at 2 s (a grid point), 20 ms host ramps.
constexpr double kMlaAutomationSeconds = 4.0;
constexpr double kMlaStepSeconds = 2.0;
constexpr double kMlaRampMs = 20.0;
constexpr double kMlaHardSwitchDb = 12.0;

// SC-001(b) compression row: the step from -12 dB to 0 dB at 1 s + kMlaFrame / 2 (tasks.md T014).
constexpr double kMlaStepLowDb = -12.0;

// ---- descriptors --------------------------------------------------------------------------

/// The SC-001(b) descriptor set, in the order kMlaDescNames lists.
constexpr std::size_t kMlaNumDesc = 8;
constexpr std::size_t kMlaDescC = 4;   // the centroid (octaves); every other entry is dB
constexpr std::array<const char*, kMlaNumDesc> kMlaDescNames{"w1/Sum(w)", "E_low", "E_high", "THD",
                                                             "C",         "L",     "w1(sum)", "GR"};

using MlaDescArray = std::array<double, kMlaNumDesc>;

struct MlaDesc {
    double w1FracDb = 0.0;   ///< 10·log10(w1 / Σw) of outL
    double Elow = 0.0;       ///< raw (unclamped)
    double Ehigh = 0.0;      ///< raw
    double Thd = 0.0;        ///< raw
    double C = 0.0;          ///< octaves above f0
    double L = 0.0;          ///< dBFS
    double w1SumDb = 0.0;    ///< 10·log10(w1) of sumL
    double grDb = 0.0;       ///< mean positive GR over the frame (0 without per-sample taps)
};

/// Values for comparison: E_low, E_high and THD floor-clamped at the OQ-S -60 dB.
MlaDescArray mlaCompared(const MlaDesc& d) {
    return MlaDescArray{d.w1FracDb,
                        mlaSc::floorClamp(d.Elow),
                        mlaSc::floorClamp(d.Ehigh),
                        mlaSc::floorClamp(d.Thd),
                        d.C,
                        d.L,
                        d.w1SumDb,
                        d.grDb};
}

MlaDescArray mlaEpsCaps() {
    MlaDescArray e{};
    for (std::size_t k = 0; k < kMlaNumDesc; ++k)
        e[k] = (k == kMlaDescC) ? kMlaEpsCapOct : kMlaEpsCapDb;
    return e;
}

/// Positive GR in dB, mean over [begin, end) of the per-sample compressor tap.
double mlaMeanGrDb(const mla::ChainTaps& t, std::size_t begin, std::size_t end) {
    REQUIRE(!t.compGrDb.empty());
    return -mlaTs::meanOf(std::span<const float>(t.compGrDb), begin, end);
}

/// Descriptors of a static render on its last kMlaFrame samples (spec "Descriptor definitions").
MlaDesc mlaDescribeTaps(const mla::ChainTaps& t, double fs, double f0) {
    const std::size_t n = t.outL.size();
    REQUIRE(n >= kMlaFrame);
    const mlaSc::Descriptors d = mlaSc::describe(t.outL.data(), n, fs, f0);
    REQUIRE(!d.w.empty());
    double total = 0.0;
    for (const double w : d.w)
        total += w;
    REQUIRE(total > 0.0);
    MlaDesc r{};
    r.w1FracDb = mlaHf::safeDb10(d.w[0] / total);
    r.Elow = d.Elow;
    r.Ehigh = d.Ehigh;
    r.Thd = d.Thd;
    r.C = d.C;
    r.L = d.L;
    const std::vector<double> ws =
        Krate::DSP::ProfundumTest::renderedHarmonicPowers(t.sumL.data() + (n - kMlaFrame), fs, f0, 1);
    REQUIRE(ws.size() == 1);
    r.w1SumDb = mlaHf::safeDb10(ws[0]);
    r.grDb = t.compGrDb.empty() ? 0.0 : mlaMeanGrDb(t, n - kMlaFrame, n);
    return r;
}

void mlaPrintDesc(const char* tag, float mass, const MlaDesc& d) {
    std::cout << std::setprecision(6) << tag << " Mass " << mass << ": w1/Sum " << d.w1FracDb << " dB, E_low "
              << d.Elow << ", E_high " << d.Ehigh << ", THD " << d.Thd << " dB, C " << d.C << " oct, L " << d.L
              << " dBFS, w1(sum) " << d.w1SumDb << " dB, GR " << d.grDb << " dB\n";
}

// ---- fixtures ------------------------------------------------------------------------------

std::function<float(std::size_t)> mlaConstMass(float m) {
    return [m](std::size_t) { return m; };
}

/// A static-Mass chain render of descriptorSeconds(fs) (seconds = 0).
mla::ChainConfig mlaStatic(const SpectralShapeRecipe::Controls& base, double f0, float mass, std::size_t hostBlock,
                           bool grTaps) {
    mla::ChainConfig cfg;
    cfg.base = base;
    cfg.f0 = f0;
    cfg.fs = kMlaFs;
    cfg.hostBlock = hostBlock;
    cfg.massAt = mlaConstMass(mass);
    cfg.perSampleDynamicsTaps = grTaps;
    return cfg;
}

/// SC-001(b) override scheme: massAt ≡ 0 and all seven targets overridden; the driven target gets
/// evaluateRow(kRows[k], m, base_k), every other target j its Mass 0 value evaluateRow(kRows[j], 0, base_j).
/// The compression lever's others sit at their Mass 50 base values (G8-7 refinement 2026-10-17, spec
/// SC-001(b) / SC-011 rows): at Mass 0 the Depth bias purifies the step fixture's saw to a voice the 2 f0
/// sidechain cannot see (GR 0.03 dB at amount 1).
mla::ChainConfig mlaOverrideScheme(mla::ChainConfig cfg, MassLaw::Target driven, float m) {
    cfg.massAt = mlaConstMass(0.0f);
    const MassLaw::Targets base = mla::lawBaseFor(cfg);
    const float othersMass = driven == MassLaw::Target::CompressionAmount ? MassLaw::kNeutralMass : 0.0f;
    for (std::size_t j = 0; j < MassLaw::kNumTargets; ++j) {
        const MassLaw::Row& row = MassLaw::kRows[j];
        const float mj = (row.target == driven) ? m : othersMass;
        const float v = MassLaw::evaluateRow(row, mj, mla::targetField(base, row.target));
        cfg.targetOverride[static_cast<std::size_t>(row.target)] = [v](std::size_t) -> std::optional<float> {
            return v;
        };
    }
    return cfg;
}

enum class MlaShape : std::uint8_t { Step, Ramp };

/// Mass `from` before tStep, then a step (or a linear ramp over rampLen samples) to `to`.
std::function<float(std::size_t)> mlaTransitionMass(float from, float to, MlaShape shape, std::size_t tStep,
                                                    std::size_t rampLen) {
    return [=](std::size_t n) -> float {
        if (n < tStep)
            return from;
        if (shape == MlaShape::Step || n >= tStep + rampLen)
            return to;
        const double u = static_cast<double>(n - tStep) / static_cast<double>(rampLen);
        return static_cast<float>(static_cast<double>(from) + (static_cast<double>(to) - static_cast<double>(from)) * u);
    };
}

/// The SC-005 automation fixture: kSawAnchor base, 4 s.
mla::ChainConfig mlaAutomationConfig(double f0, std::function<float(std::size_t)> massAt, std::size_t hostBlock,
                                     bool grTaps) {
    mla::ChainConfig cfg;
    cfg.base = SpectralShapeRecipe::kSawAnchor;
    cfg.f0 = f0;
    cfg.fs = kMlaFs;
    cfg.seconds = kMlaAutomationSeconds;
    cfg.hostBlock = hostBlock;
    cfg.massAt = std::move(massAt);
    cfg.perSampleDynamicsTaps = grTaps;
    return cfg;
}

std::size_t mlaStepSample() {
    const std::size_t t = mlaTs::secondsToSamples(kMlaFs, kMlaStepSeconds);
    REQUIRE(t % mla::kGrid == 0);
    return t;
}

std::size_t mlaRampSamples() { return mlaTs::secondsToSamples(kMlaFs, kMlaRampMs * 0.001); }

/// Field-by-field bit equality of two MassLaw::Targets (every field compared as its float bits).
bool mlaTargetsBitEqual(const MassLaw::Targets& a, const MassLaw::Targets& b) noexcept {
    const auto eq = [](float x, float y) { return std::bit_cast<std::uint32_t>(x) == std::bit_cast<std::uint32_t>(y); };
    return eq(a.depth, b.depth) && eq(a.body, b.body) && eq(a.transientDurationMs, b.transientDurationMs) &&
           eq(a.foldAmount, b.foldAmount) && eq(a.compressionAmount, b.compressionAmount) &&
           eq(a.anchorLevel, b.anchorLevel) && eq(a.outputTrimDb, b.outputTrimDb);
}

bool mlaVectorsBitEqual(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
}

bool mlaAllFinite(const std::vector<float>& v) {
    return std::all_of(v.begin(), v.end(), [](float s) { return mlaTs::isFiniteBits(s); });
}

// ---- SC-005(a) clicks ------------------------------------------------------------------------

mlaSc::ClickCount mlaClicks(const std::vector<float>& tr, const std::vector<float>& refStart,
                            const std::vector<float>& refEnd, double f0) {
    REQUIRE(tr.size() == refStart.size());
    REQUIRE(tr.size() == refEnd.size());
    return mlaSc::differentialClicks(tr.data(), refStart.data(), refEnd.data(), tr.size(), kMlaFs, f0);
}

void mlaPrintClicks(const char* note, const char* arm, std::size_t block, const mlaSc::ClickCount& c) {
    std::cout << std::setprecision(6) << "[T015 SC-005(a)] " << note << " " << arm << ", block " << block
              << ": counted " << c.counted << " (transition " << c.transitionDetections << ", reference "
              << c.referenceDetections << ", N " << c.windowN << ", guarded out " << c.guardedOut << ")\n";
}

/// The transition render with a hard 0 -> +12 dB gain switch from sample `at` (SC-005(a) positive control).
std::vector<float> mlaHardSwitch(std::vector<float> y, std::size_t at) {
    const auto g = static_cast<float>(std::pow(10.0, kMlaHardSwitchDb / 20.0));
    for (std::size_t i = at; i < y.size(); ++i)
        y[i] *= g;
    return y;
}

/// Where the hard-switch positive control lands (spec SC-005(a) as amended 2026-10-17; the Phase 5 placement
/// rule, profundum-phase5-shape-character/spec.md "Group 9 rulings": a positive control is harness construction
/// and must sit where the forced discontinuity is visible to the differential count). kSawAnchor carries one
/// ClickDetector detection per period on each reference (its reset edge), so a switch within +/-N of one is
/// matched away by construction: at E2 the step sample of the 100 -> 0 arms sat inside that window and the
/// control read 0. The site is the first sample in [tStep, tStep + 4 periods) that is at least 2N from every
/// reference detection and whose forced jump |y[s]| * (g - 1) is >= kMlaControlMinRatio x the local reference
/// max |dy| (the guard needs 1.1 x); failing that, the valid sample with the largest ratio.
constexpr double kMlaControlMinRatio = 10.0;

struct MlaControlSite {
    std::size_t at = 0;
    bool valid = false;
    double ratio = 0.0;
    std::size_t nearestRef = 0;
};

MlaControlSite mlaHardSwitchSite(const std::vector<float>& tr, const std::vector<float>& refS,
                                 const std::vector<float>& refE, std::size_t tStep, double f0) {
    TestUtils::ClickDetectorConfig cfg{};   // the documented defaults differentialClicks uses
    cfg.sampleRate = static_cast<float>(kMlaFs);
    std::vector<std::size_t> det;
    for (const std::vector<float>* r : {&refS, &refE}) {
        TestUtils::ClickDetector d(cfg);
        d.prepare();
        for (const TestUtils::ClickDetection& c : d.detect(r->data(), r->size()))
            det.push_back(c.sampleIndex);
    }
    std::sort(det.begin(), det.end());
    const auto windowN = static_cast<std::size_t>(std::floor(kMlaFs / f0 / 8.0));
    const auto period = static_cast<std::size_t>(std::ceil(kMlaFs / f0));
    const double g = std::pow(10.0, kMlaHardSwitchDb / 20.0);
    MlaControlSite best;
    for (std::size_t s = tStep; s < tStep + 4 * period && s + 2 * windowN < tr.size(); ++s) {
        const auto it = std::lower_bound(det.begin(), det.end(), s);
        std::size_t nearest = tr.size();
        if (it != det.end())
            nearest = *it - s;
        if (it != det.begin())
            nearest = std::min(nearest, s - *std::prev(it));
        if (nearest < 2 * windowN)
            continue;
        const double local = std::max(mlaSc::maxDeltaAround(refS.data(), refS.size(), s, windowN),
                                      mlaSc::maxDeltaAround(refE.data(), refE.size(), s, windowN));
        const double ratio = std::abs(static_cast<double>(tr[s])) * (g - 1.0) / std::max(local, 1.0e-12);
        if (!best.valid || ratio > best.ratio)
            best = MlaControlSite{.at = s, .valid = true, .ratio = ratio, .nearestRef = nearest};
        if (ratio >= kMlaControlMinRatio)
            return best;
    }
    return best;
}

/// SC-005(a) for one note: steps 0 <-> 100 and 20 ms ramps at blocks {64, 512}, plus the positive controls.
void mlaAutomationContinuous(double f0, const char* note) {
    const std::size_t tStep = mlaStepSample();
    const std::size_t rampLen = mlaRampSamples();
    struct Arm {
        const char* name;
        bool fromZero;
        MlaShape shape;
    };
    const std::array<Arm, 4> arms{{
        {.name = "step 0->100", .fromZero = true, .shape = MlaShape::Step},
        {.name = "step 100->0", .fromZero = false, .shape = MlaShape::Step},
        {.name = "20 ms ramp 0->100", .fromZero = true, .shape = MlaShape::Ramp},
        {.name = "20 ms ramp 100->0", .fromZero = false, .shape = MlaShape::Ramp},
    }};
    for (const std::size_t block : {std::size_t{64}, std::size_t{512}}) {
        const mla::ChainTaps r0 = mla::renderChain(mlaAutomationConfig(f0, mlaConstMass(0.0f), block, false));
        const mla::ChainTaps r100 = mla::renderChain(mlaAutomationConfig(f0, mlaConstMass(100.0f), block, false));
        for (const Arm& arm : arms) {
            const float from = arm.fromZero ? 0.0f : 100.0f;
            const float to = arm.fromZero ? 100.0f : 0.0f;
            const mla::ChainTaps tr = mla::renderChain(
                mlaAutomationConfig(f0, mlaTransitionMass(from, to, arm.shape, tStep, rampLen), block, false));
            const std::vector<float>& refS = arm.fromZero ? r0.outL : r100.outL;
            const std::vector<float>& refE = arm.fromZero ? r100.outL : r0.outL;
            INFO(note << " " << arm.name << ", block " << block);
            const mlaSc::ClickCount c = mlaClicks(tr.outL, refS, refE, f0);
            mlaPrintClicks(note, arm.name, block, c);
            CHECK(c.counted == 0);
            // Positive control: the same transition with a hard +12 dB switch, sited by mlaHardSwitchSite
            // (nearest the step sample, clear of reference detections by 2N).
            const MlaControlSite site = mlaHardSwitchSite(tr.outL, refS, refE, tStep, f0);
            std::cout << "[T015 SC-005(a)] " << note << " " << arm.name << ", block " << block
                      << ": hard-switch site " << site.at << " (step + " << (site.at - tStep)
                      << "), nearest reference detection " << site.nearestRef << ", jump / local max|dy| "
                      << site.ratio << (site.valid ? "" : " (NO VALID SITE)") << "\n";
            REQUIRE(site.valid);
            const mlaSc::ClickCount hc = mlaClicks(mlaHardSwitch(tr.outL, site.at), refS, refE, f0);
            mlaPrintClicks(note, "hard +12 dB switch control", block, hc);
            CHECK(hc.counted >= 1);
        }
        // Positive control: the unsmoothed 0 -> 100 step (targets at massAt(n) directly).
        mla::ChainConfig u = mlaAutomationConfig(f0, mlaTransitionMass(0.0f, 100.0f, MlaShape::Step, tStep, rampLen),
                                                 block, false);
        u.unsmoothedMass = true;
        const mla::ChainTaps ut = mla::renderChain(u);
        const mlaSc::ClickCount uc = mlaClicks(ut.outL, r0.outL, r100.outL, f0);
        mlaPrintClicks(note, "unsmoothedMass 0->100 control", block, uc);
        // Recorded, not gated (G8-7 refinement 2026-10-17, spec SC-005(a) row): every target is smoothed
        // downstream of the law, so an unsmoothed Mass step is not a click generator; the hard +12 dB
        // switch above is the gated positive control.
        WARN(note << " unsmoothedMass control, block " << block << ": counted " << uc.counted << " (recorded, G8-7)");
    }
}

// ---- SC-005(b) static lattice ----------------------------------------------------------------

/// Descriptors of the static kSawAnchor chain at each Mass (descriptorSeconds renders, last frame).
std::vector<MlaDesc> mlaLattice(double f0, const char* note, const std::vector<float>& masses) {
    std::vector<MlaDesc> out;
    out.reserve(masses.size());
    for (const float m : masses) {
        const mla::ChainTaps t = mla::renderChain(mlaStatic(SpectralShapeRecipe::kSawAnchor, f0, m, 512, true));
        out.push_back(mlaDescribeTaps(t, kMlaFs, f0));
        std::cout << "[T015 lattice " << note << "]";
        mlaPrintDesc("", m, out.back());
    }
    return out;
}

struct MlaSteps {
    MlaDescArray maxAbs{};    ///< largest |Δ| over the pairs
    MlaDescArray perUnit{};   ///< largest |Δ| / ΔMass over the pairs
};

/// Adjacent-pair changes over `pairs` (indices into masses / d). Gates the SC-005(b) bars when `gate`.
MlaSteps mlaAdjacentSteps(const char* note, const std::vector<float>& masses, const std::vector<MlaDesc>& d,
                          const std::vector<std::pair<std::size_t, std::size_t>>& pairs, bool gate) {
    REQUIRE(masses.size() == d.size());
    MlaSteps s{};
    for (const auto& [i, j] : pairs) {
        REQUIRE(i < d.size());
        REQUIRE(j < d.size());
        const MlaDescArray a = mlaCompared(d[i]);
        const MlaDescArray b = mlaCompared(d[j]);
        const double dm = std::abs(static_cast<double>(masses[j]) - static_cast<double>(masses[i]));
        REQUIRE(dm > 0.0);
        for (std::size_t k = 0; k < kMlaNumDesc; ++k) {
            const double delta = std::abs(b[k] - a[k]);
            s.maxAbs[k] = std::max(s.maxAbs[k], delta);
            s.perUnit[k] = std::max(s.perUnit[k], delta / dm);
        }
        if (gate) {
            INFO(note << " Mass " << masses[i] << " -> " << masses[j]);
            CHECK(std::abs(b[5] - a[5]) <= kMlaLatticeLDb);           // L
            CHECK(std::abs(b[kMlaDescC] - a[kMlaDescC]) <= kMlaLatticeCOct);
            CHECK(std::abs(b[1] - a[1]) <= kMlaLatticeEDb);           // E_low (floored)
            CHECK(std::abs(b[2] - a[2]) <= kMlaLatticeEDb);           // E_high (floored)
        }
    }
    std::cout << std::setprecision(6) << "[T015 SC-005(b)] " << note << " largest adjacent step:";
    for (std::size_t k = 0; k < kMlaNumDesc; ++k)
        std::cout << " " << kMlaDescNames[k] << " " << s.maxAbs[k] << (k == kMlaDescC ? " oct" : " dB") << " ("
                  << s.perUnit[k] << " /unit)";
    std::cout << "\n";
    return s;
}

std::vector<float> mlaFullLatticeMasses() {
    std::vector<float> m(101);
    for (std::size_t i = 0; i < m.size(); ++i)
        m[i] = static_cast<float>(i);
    return m;
}

std::vector<std::pair<std::size_t, std::size_t>> mlaAllAdjacentPairs(std::size_t count) {
    std::vector<std::pair<std::size_t, std::size_t>> p;
    for (std::size_t i = 0; i + 1 < count; ++i)
        p.emplace_back(i, i + 1);
    return p;
}

/// Plan S13.2 repeat noise of the static lattice at Mass 0 and Mass 100: max − min of each descriptor
/// over 8 note-on offsets (k · P / 8, rounded to the 32-sample grid the fixture REQUIREs) × blocks
/// {64, 512}, capped at 0.1 dB / 0.005 oct.
MlaDescArray mlaRepeatNoise(double f0, const char* note) {
    const MlaDescArray caps = mlaEpsCaps();
    MlaDescArray eps{};
    const double P = kMlaFs / f0;
    for (const float mass : {0.0f, 100.0f}) {
        MlaDescArray lo{};
        MlaDescArray hi{};
        bool first = true;
        for (int k = 0; k < 8; ++k) {
            const auto grids = static_cast<std::size_t>(
                std::llround(static_cast<double>(k) * P / 8.0 / static_cast<double>(mla::kGrid)));
            for (const std::size_t block : {std::size_t{64}, std::size_t{512}}) {
                mla::ChainConfig cfg = mlaStatic(SpectralShapeRecipe::kSawAnchor, f0, mass, block, true);
                cfg.noteOnSample = grids * mla::kGrid;
                const MlaDescArray v = mlaCompared(mlaDescribeTaps(mla::renderChain(cfg), kMlaFs, f0));
                for (std::size_t d = 0; d < kMlaNumDesc; ++d) {
                    lo[d] = first ? v[d] : std::min(lo[d], v[d]);
                    hi[d] = first ? v[d] : std::max(hi[d], v[d]);
                }
                first = false;
            }
        }
        for (std::size_t d = 0; d < kMlaNumDesc; ++d)
            eps[d] = std::max(eps[d], hi[d] - lo[d]);
    }
    std::cout << std::setprecision(6) << "[T015 SC-005(c)] " << note << " repeat noise (measured, cap):";
    for (std::size_t d = 0; d < kMlaNumDesc; ++d) {
        std::cout << " " << kMlaDescNames[d] << " " << eps[d] << " / " << caps[d];
        eps[d] = std::min(eps[d], caps[d]);
    }
    std::cout << "\n";
    return eps;
}

// ---- SC-005(c) trajectory --------------------------------------------------------------------

/// One-period window descriptors (periodDescriptors, the Group 6 resampled estimator) at `start`.
MlaDescArray mlaWindowDesc(const mla::ChainTaps& t, std::size_t start, double fs, double f0) {
    const mla::PeriodDescriptors o = mla::periodDescriptors(t.outL.data(), start, fs, f0);
    const mla::PeriodDescriptors s = mla::periodDescriptors(t.sumL.data(), start, fs, f0);
    const auto len = static_cast<std::size_t>(std::floor(fs / f0));
    return MlaDescArray{mlaHf::safeDb10(o.w1Frac),
                        mlaSc::floorClamp(o.Elow),
                        mlaSc::floorClamp(o.Ehigh),
                        mlaSc::floorClamp(o.Thd),
                        o.C,
                        o.L,
                        s.L + mlaHf::safeDb10(s.w1Frac),
                        mlaMeanGrDb(t, start, start + len)};
}

/// SC-005(c) on one render: windows one period long slide one period at a time across the Mass
/// traversal (the intervals whose smoothed Mass differs from the start and end values); each per-window
/// change ≤ perUnit × (Mass units the two windows span) + eps.
void mlaTrajectory(double f0, const char* note, const char* arm, std::function<float(std::size_t)> massAt,
                   const MlaDescArray& perUnit, const MlaDescArray& eps) {
    const double fs = kMlaFs;
    const mla::ChainTaps t = mla::renderChain(mlaAutomationConfig(f0, std::move(massAt), 512, true));
    const std::vector<float>& mp = t.massPerInterval;
    REQUIRE(mp.size() >= 2);
    std::size_t firstMove = mp.size();
    for (std::size_t i = 0; i < mp.size(); ++i) {
        if (mp[i] != mp.front()) {
            firstMove = i;
            break;
        }
    }
    std::size_t lastMove = 0;
    for (std::size_t i = mp.size(); i-- > 0;) {
        if (mp[i] != mp.back()) {
            lastMove = i;
            break;
        }
    }
    REQUIRE(firstMove < mp.size());
    REQUIRE(lastMove >= firstMove);
    const std::size_t tBegin = firstMove * mla::kGrid;
    const std::size_t tEnd = (lastMove + 1) * mla::kGrid;
    const double P = fs / f0;
    const auto ceilP = static_cast<std::size_t>(std::ceil(P));
    const std::size_t n = t.outL.size();

    std::vector<std::size_t> starts;
    for (int k = 0;; ++k) {
        const double s = static_cast<double>(tBegin) - P + static_cast<double>(k) * P;
        const auto si = static_cast<std::size_t>(std::llround(s));
        if (si > tEnd)
            break;
        REQUIRE(si >= static_cast<std::size_t>(mla::kResampleHalf));
        REQUIRE(si + ceilP + static_cast<std::size_t>(mla::kResampleHalf) + 1 < n);
        starts.push_back(si);
    }
    REQUIRE(starts.size() >= 2);
    std::vector<MlaDescArray> v;
    v.reserve(starts.size());
    for (const std::size_t s : starts)
        v.push_back(mlaWindowDesc(t, s, fs, f0));

    MlaDescArray worstDelta{};
    MlaDescArray worstRatio{};
    std::array<std::size_t, kMlaNumDesc> violations{};
    for (std::size_t w = 0; w + 1 < v.size(); ++w) {
        const std::size_t i0 = starts[w] / mla::kGrid;
        const std::size_t i1 = std::min(mp.size() - 1, (starts[w + 1] + ceilP - 1) / mla::kGrid);
        const auto [lo, hi] = std::minmax_element(mp.begin() + static_cast<std::ptrdiff_t>(i0),
                                                  mp.begin() + static_cast<std::ptrdiff_t>(i1) + 1);
        const double units = static_cast<double>(*hi) - static_cast<double>(*lo);
        for (std::size_t d = 0; d < kMlaNumDesc; ++d) {
            const double delta = std::abs(v[w + 1][d] - v[w][d]);
            const double bound = perUnit[d] * units + eps[d];
            worstDelta[d] = std::max(worstDelta[d], delta);
            worstRatio[d] = std::max(worstRatio[d], delta / std::max(bound, 1e-12));
            if (delta > bound)
                ++violations[d];
        }
    }
    std::cout << std::setprecision(6) << "[T015 SC-005(c)] " << note << " " << arm << ": traversal [" << tBegin << ", "
              << tEnd << ") samples, " << starts.size() << " windows; per descriptor worst |change| / worst "
              << "change-to-bound ratio / violations:";
    for (std::size_t d = 0; d < kMlaNumDesc; ++d)
        std::cout << " " << kMlaDescNames[d] << " " << worstDelta[d] << " / " << worstRatio[d] << " / "
                  << violations[d];
    std::cout << "\n";
    for (std::size_t d = 0; d < kMlaNumDesc; ++d) {
        INFO(note << " " << arm << " " << kMlaDescNames[d] << ": worst change " << worstDelta[d] << ", worst ratio "
                  << worstRatio[d]);
        CHECK(violations[d] == 0);
    }
}

/// SC-005(c) for one note: the 0 -> 100 host step (the 40 ms law traversal) and the 20 ms host ramp.
void mlaTrajectoryArms(double f0, const char* note, const MlaDescArray& perUnit, const MlaDescArray& eps) {
    const std::size_t tStep = mlaStepSample();
    const std::size_t rampLen = mlaRampSamples();
    mlaTrajectory(f0, note, "0->100 host step", mlaTransitionMass(0.0f, 100.0f, MlaShape::Step, tStep, rampLen),
                  perUnit, eps);
    mlaTrajectory(f0, note, "0->100 20 ms host ramp",
                  mlaTransitionMass(0.0f, 100.0f, MlaShape::Ramp, tStep, rampLen), perUnit, eps);
}

}  // namespace

// =============================================================================
// SC-002 (FR-023): Mass anchors on the reference voice. Expected red until T019.
// =============================================================================
TEST_CASE("MassLaw_AnchorPoints", "[systems][profundum][mass-law]") {
    const std::array<float, 3> masses{0.0f, 50.0f, 100.0f};
    for (const double f0 : {mlaTs::kE1Hz, mlaTs::kC3Hz}) {
        std::array<mlaSc::Descriptors, 3> d{};
        std::array<double, 3> gr{};
        std::size_t gr0NonZero = 0;
        for (std::size_t i = 0; i < masses.size(); ++i) {
            const mla::ChainTaps t =
                mla::renderChain(mlaStatic(SpectralShapeRecipe::kDefaultControls, f0, masses[i], 512, true));
            const std::size_t n = t.outL.size();
            d[i] = mlaSc::describe(t.outL.data(), n, kMlaFs, f0);
            gr[i] = mlaMeanGrDb(t, mlaTs::secondsToSamples(kMlaFs, 1.0), n);
            if (i == 0)
                gr0NonZero = static_cast<std::size_t>(
                    std::count_if(t.compGrDb.begin(), t.compGrDb.end(), [](float g) { return g != 0.0f; }));
            std::cout << std::setprecision(6) << "[T015 SC-002] f0 " << f0 << " Mass " << masses[i] << ": THD "
                      << d[i].Thd << " dB, E_low " << d[i].Elow << " dB, E_high " << d[i].Ehigh << " dB, L " << d[i].L
                      << " dBFS, mean GR [1 s, end] " << gr[i] << " dB\n";
        }
        std::cout << "[T015 SC-002] f0 " << f0 << " Mass 0: samples with GR != 0: " << gr0NonZero << "\n";
        INFO("f0 " << f0);
        CHECK(d[0].Thd <= kMlaThd0MaxDb);
        CHECK(gr0NonZero == 0);
        CHECK(d[1].Thd >= d[0].Thd + kMlaThdStepDb);
        CHECK(d[1].Elow >= d[0].Elow + kMlaElowStepDb);
        CHECK(d[2].Thd >= d[1].Thd + kMlaThdStepDb);
        CHECK(gr[2] >= kMlaGr100MinDb);
    }
}

// =============================================================================
// SC-005(a) (FR-025): automation is click-free. [long] shards + smoke.
// =============================================================================
TEST_CASE("MassLaw_AutomationContinuous_E0", "[systems][profundum][mass-law][long]") {
    mlaAutomationContinuous(mlaTs::kE0Hz, "E0");
}

TEST_CASE("MassLaw_AutomationContinuous_E2", "[systems][profundum][mass-law][long]") {
    mlaAutomationContinuous(mlaTs::kE2Hz, "E2");
}

TEST_CASE("MassLaw_AutomationContinuous_C3", "[systems][profundum][mass-law][long]") {
    mlaAutomationContinuous(mlaTs::kC3Hz, "C3");
}

// SC-005(a) smoke: E2, the 0 -> 100 step at block 512, the hard +12 dB switch positive control (gated)
// and the unsmoothedMass control (recorded, G8-7).
TEST_CASE("MassLaw_AutomationContinuousSmoke", "[systems][profundum][mass-law]") {
    const double f0 = mlaTs::kE2Hz;
    const std::size_t tStep = mlaStepSample();
    const std::size_t rampLen = mlaRampSamples();
    const mla::ChainTaps r0 = mla::renderChain(mlaAutomationConfig(f0, mlaConstMass(0.0f), 512, false));
    const mla::ChainTaps r100 = mla::renderChain(mlaAutomationConfig(f0, mlaConstMass(100.0f), 512, false));
    const mla::ChainTaps tr = mla::renderChain(
        mlaAutomationConfig(f0, mlaTransitionMass(0.0f, 100.0f, MlaShape::Step, tStep, rampLen), 512, false));
    const mlaSc::ClickCount c = mlaClicks(tr.outL, r0.outL, r100.outL, f0);
    mlaPrintClicks("E2", "step 0->100 (smoke)", 512, c);
    CHECK(c.counted == 0);
    // Positive control (gated): the same transition with a hard +12 dB switch (G8-7), sited by mlaHardSwitchSite.
    const MlaControlSite site = mlaHardSwitchSite(tr.outL, r0.outL, r100.outL, tStep, f0);
    REQUIRE(site.valid);
    const mlaSc::ClickCount hc = mlaClicks(mlaHardSwitch(tr.outL, site.at), r0.outL, r100.outL, f0);
    mlaPrintClicks("E2", "hard +12 dB switch control (smoke)", 512, hc);
    CHECK(hc.counted >= 1);

    mla::ChainConfig u =
        mlaAutomationConfig(f0, mlaTransitionMass(0.0f, 100.0f, MlaShape::Step, tStep, rampLen), 512, false);
    u.unsmoothedMass = true;
    const mla::ChainTaps ut = mla::renderChain(u);
    const mlaSc::ClickCount uc = mlaClicks(ut.outL, r0.outL, r100.outL, f0);
    mlaPrintClicks("E2", "unsmoothedMass 0->100 control (smoke)", 512, uc);
    WARN("E2 unsmoothedMass control (smoke): counted " << uc.counted << " (recorded, G8-7)");
}

// =============================================================================
// SC-005(b): the 101-point static lattice. [long] shards + smoke.
// =============================================================================
TEST_CASE("MassLaw_StaticLattice_E0", "[systems][profundum][mass-law][long]") {
    const std::vector<float> m = mlaFullLatticeMasses();
    const std::vector<MlaDesc> d = mlaLattice(mlaTs::kE0Hz, "E0", m);
    (void)mlaAdjacentSteps("E0", m, d, mlaAllAdjacentPairs(m.size()), true);
}

TEST_CASE("MassLaw_StaticLattice_E2", "[systems][profundum][mass-law][long]") {
    const std::vector<float> m = mlaFullLatticeMasses();
    const std::vector<MlaDesc> d = mlaLattice(mlaTs::kE2Hz, "E2", m);
    (void)mlaAdjacentSteps("E2", m, d, mlaAllAdjacentPairs(m.size()), true);
}

TEST_CASE("MassLaw_StaticLattice_C3", "[systems][profundum][mass-law][long]") {
    const std::vector<float> m = mlaFullLatticeMasses();
    const std::vector<MlaDesc> d = mlaLattice(mlaTs::kC3Hz, "C3", m);
    (void)mlaAdjacentSteps("C3", m, d, mlaAllAdjacentPairs(m.size()), true);
}

// SC-005(b) smoke: E2 at m ∈ {0, 1, 2, 49, 50, 51, 98, 99, 100}; the bars on the adjacent pairs of each
// three-point run.
TEST_CASE("MassLaw_StaticLatticeSmoke", "[systems][profundum][mass-law]") {
    const std::vector<float> m{0.0f, 1.0f, 2.0f, 49.0f, 50.0f, 51.0f, 98.0f, 99.0f, 100.0f};
    const std::vector<MlaDesc> d = mlaLattice(mlaTs::kE2Hz, "E2 smoke", m);
    const std::vector<std::pair<std::size_t, std::size_t>> pairs{{0, 1}, {1, 2}, {3, 4}, {4, 5}, {6, 7}, {7, 8}};
    (void)mlaAdjacentSteps("E2 smoke", m, d, pairs, true);
}

// =============================================================================
// SC-005(c): descriptor trajectory at automation speed. [long] shards + smoke.
// Each shard computes the (b) lattice itself (shards run concurrently) and the plan S13.2 repeat noise.
// =============================================================================
namespace {
void mlaTrajectoryShard(double f0, const char* note) {
    const std::vector<float> m = mlaFullLatticeMasses();
    const std::vector<MlaDesc> d = mlaLattice(f0, note, m);
    const MlaSteps s = mlaAdjacentSteps(note, m, d, mlaAllAdjacentPairs(m.size()), false);
    const MlaDescArray eps = mlaRepeatNoise(f0, note);
    mlaTrajectoryArms(f0, note, s.perUnit, eps);
}
}  // namespace

TEST_CASE("MassLaw_AutomationDescriptorTrajectory_E0", "[systems][profundum][mass-law][long]") {
    mlaTrajectoryShard(mlaTs::kE0Hz, "E0");
}

TEST_CASE("MassLaw_AutomationDescriptorTrajectory_E2", "[systems][profundum][mass-law][long]") {
    mlaTrajectoryShard(mlaTs::kE2Hz, "E2");
}

TEST_CASE("MassLaw_AutomationDescriptorTrajectory_C3", "[systems][profundum][mass-law][long]") {
    mlaTrajectoryShard(mlaTs::kC3Hz, "C3");
}

// SC-005(c) smoke: E2, the (b) maxima from a 21-point lattice {0, 5, ..., 100} scaled to per-unit; ε =
// the plan S13.2 caps. (M-4, 2026-10-18: the 9-point lattice's 12.5-unit averages under-read the local
// slope of the SCurve Fold row and bounded L at 0.95x its measured change while the [long] E2 shard, on
// the 101-point lattice, read 0.43x; a 5-unit average follows the bend within about 10 %.)
TEST_CASE("MassLaw_AutomationDescriptorTrajectorySmoke", "[systems][profundum][mass-law]") {
    std::vector<float> m(21);
    for (std::size_t i = 0; i < m.size(); ++i)
        m[i] = 5.0f * static_cast<float>(i);
    const std::vector<MlaDesc> d = mlaLattice(mlaTs::kE2Hz, "E2 trajectory smoke", m);
    const MlaSteps s = mlaAdjacentSteps("E2 trajectory smoke", m, d, mlaAllAdjacentPairs(m.size()), false);
    mlaTrajectoryArms(mlaTs::kE2Hz, "E2 smoke", s.perUnit, mlaEpsCaps());
}

// =============================================================================
// SC-011 Mass arm (FR-043): the Mass endpoints separate, and each target alone moves its SC-001(b)
// descriptor by >= 3 dB. Expected red until T019.
// =============================================================================
TEST_CASE("MassLaw_Audibility", "[systems][profundum][mass-law]") {
    const double fs = kMlaFs;
    const double f0 = mlaTs::kE1Hz;
    const SpectralShapeRecipe::Controls ref = SpectralShapeRecipe::kDefaultControls;

    // (1) Mass 0 vs Mass 100 by the Phase 5 OQ-S bar.
    {
        const mla::ChainTaps t0 = mla::renderChain(mlaStatic(ref, f0, 0.0f, 512, false));
        const mla::ChainTaps t100 = mla::renderChain(mlaStatic(ref, f0, 100.0f, 512, false));
        const mlaSc::Descriptors d0 = mlaSc::describe(t0.outL.data(), t0.outL.size(), fs, f0);
        const mlaSc::Descriptors d100 = mlaSc::describe(t100.outL.data(), t100.outL.size(), fs, f0);
        const mlaSc::Separation sep = mlaSc::separate(d0, d100);
        std::cout << std::setprecision(6) << "[T015 SC-011] Mass 0 vs 100 (E1): E_low " << d0.Elow << " / " << d100.Elow
                  << ", E_high " << d0.Ehigh << " / " << d100.Ehigh << ", THD " << d0.Thd << " / " << d100.Thd
                  << ", I " << d0.I << " / " << d100.I << " dB, C " << d0.C << " / " << d100.C
                  << " oct; separation " << sep.maxDb << " dB (" << sep.which << "), dC " << sep.dC
                  << " oct, distinct " << sep.distinct << "\n";
        CHECK(sep.distinct);
    }

    // (2) Each target alone (the SC-001(b) override scheme) from its Mass 0 to its Mass 100 value.
    const std::size_t total = mla::renderLength(fs, 0.0);
    const std::size_t settle = mlaTs::secondsToSamples(fs, mla::kSettleSeconds);
    const std::size_t stepAt = settle + kMlaFrame / 2;   // tasks.md T014: 1 s + 131072 samples
    REQUIRE(stepAt % mla::kGrid == 0);
    const std::size_t seg = kMlaFrame / 4;               // 65536 samples per ΔL_step segment
    REQUIRE(stepAt >= settle + seg);
    REQUIRE(total >= stepAt + 2 * seg);
    const std::size_t W = mlaTs::dTrWindow(fs, 250.0);

    for (std::size_t k = 0; k < MassLaw::kNumTargets; ++k) {
        const auto target = static_cast<MassLaw::Target>(k);
        const bool compression = target == MassLaw::Target::CompressionAmount;
        mla::ChainConfig base = mlaStatic(compression ? SpectralShapeRecipe::kSawAnchor : ref, f0, 0.0f, 512,
                                          compression);
        if (compression)
            base.preDynamicsGainDb = [stepAt](std::size_t n) {
                return n < stepAt ? static_cast<float>(kMlaStepLowDb) : 0.0f;
            };
        const mla::ChainConfig c0 = mlaOverrideScheme(base, target, 0.0f);
        const mla::ChainConfig c100 = mlaOverrideScheme(base, target, 100.0f);
        const mla::ChainTaps t0 = mla::renderChain(c0);
        const mla::ChainTaps t100 = mla::renderChain(c100);
        const float v0 = mla::targetField(t0.targetsPerInterval.back(), target);
        const float v100 = mla::targetField(t100.targetsPerInterval.back(), target);
        const std::size_t n = t0.outL.size();
        REQUIRE(n == total);

        const char* name = "";
        double a = 0.0;
        double b = 0.0;
        double rawA = 0.0;
        double rawB = 0.0;
        double moved = 0.0;
        switch (target) {
            case MassLaw::Target::Depth: {
                name = "Depth -> w1/Sum(w) (dB)";
                a = rawA = mlaDescribeTaps(t0, fs, f0).w1FracDb;
                b = rawB = mlaDescribeTaps(t100, fs, f0).w1FracDb;
                moved = std::abs(b - a);
                break;
            }
            case MassLaw::Target::Body: {
                name = "Body -> E_low (dB, floored)";
                rawA = mlaSc::describe(t0.outL.data(), n, fs, f0).Elow;
                rawB = mlaSc::describe(t100.outL.data(), n, fs, f0).Elow;
                a = mlaSc::floorClamp(rawA);
                b = mlaSc::floorClamp(rawB);
                moved = std::abs(b - a);
                break;
            }
            case MassLaw::Target::TransientDuration: {
                name = "Duration -> D_tr (dB, shaper +6 / 0)";
                a = rawA = mlaTs::dTrDb(t0.outL.data(), n, c0.noteOnSample, n - W, W);
                b = rawB = mlaTs::dTrDb(t100.outL.data(), n, c100.noteOnSample, n - W, W);
                moved = std::abs(b - a);
                break;
            }
            case MassLaw::Target::FoldAmount: {
                name = "Fold -> THD (dB, floored)";
                rawA = mlaSc::describe(t0.outL.data(), n, fs, f0).Thd;
                rawB = mlaSc::describe(t100.outL.data(), n, fs, f0).Thd;
                a = mlaSc::floorClamp(rawA);
                b = mlaSc::floorClamp(rawB);
                moved = std::abs(b - a);
                break;
            }
            case MassLaw::Target::CompressionAmount: {
                name = "Compression -> DeltaL_step (dB; moved = DeltaL(0) - DeltaL(100))";
                a = rawA = mla::deltaLStepDb(t0.outL, fs, f0, stepAt - seg, stepAt, total - seg, total);
                b = rawB = mla::deltaLStepDb(t100.outL, fs, f0, stepAt - seg, stepAt, total - seg, total);
                moved = a - b;
                std::cout << std::setprecision(6) << "[T015 SC-011] Compression steady GR (recorded) over the high "
                          << "segment: amount " << v0 << " " << mlaMeanGrDb(t0, total - seg, total) << " dB, amount "
                          << v100 << " " << mlaMeanGrDb(t100, total - seg, total) << " dB\n";
                break;
            }
            case MassLaw::Target::AnchorLevel: {
                name = "AnchorLevel -> w1 of the sum (dB)";
                a = rawA = mlaDescribeTaps(t0, fs, f0).w1SumDb;
                b = rawB = mlaDescribeTaps(t100, fs, f0).w1SumDb;
                moved = std::abs(b - a);
                break;
            }
            case MassLaw::Target::OutputTrim: {
                // FR-021 output trim (G8-5 ruling 2026-10-17): a calibration lever that cancels level change,
                // not an audibility lever; its move is recorded, not gated.
                name = "OutputTrim -> L of outL (dB, recorded)";
                a = rawA = mlaSc::describe(t0.outL.data(), n, fs, f0).L;
                b = rawB = mlaSc::describe(t100.outL.data(), n, fs, f0).L;
                moved = std::abs(b - a);
                break;
            }
        }
        std::cout << std::setprecision(6) << "[T015 SC-011] " << name << ": target " << v0 << " -> " << v100
                  << ", descriptor " << a << " -> " << b << " (raw " << rawA << " -> " << rawB << "), moved " << moved
                  << " dB (bar " << kMlaAudibleDb << ")\n";
        INFO(name << ": moved " << moved);
        if (target == MassLaw::Target::OutputTrim)
            WARN(name << ": moved " << moved << " dB (recorded, G8-5)");
        else
            CHECK(moved >= kMlaAudibleDb);
    }
}

// =============================================================================
// SC-023 rendered (FR-027): Mass 50 is neutral through the chain; range-edge bases are exercised.
// =============================================================================
TEST_CASE("MassLaw_NeutralAtFiftyRendered", "[systems][profundum][mass-law]") {
    const double f0 = mlaTs::kE1Hz;
    struct Base {
        const char* name;
        SpectralShapeRecipe::Controls controls;
    };
    const std::array<Base, 2> bases{{
        {.name = "reference voice", .controls = SpectralShapeRecipe::kDefaultControls},
        {.name = "kSawAnchor", .controls = SpectralShapeRecipe::kSawAnchor},
    }};
    const auto tapsOf = [](const mla::ChainTaps& t) {
        return std::array<const std::vector<float>*, 17>{&t.coreL, &t.coreR, &t.foldL,  &t.foldR,  &t.anchorL,
                                                          &t.anchorR, &t.sumL, &t.sumR,  &t.dynL,   &t.dynR,
                                                          &t.dcL,   &t.dcR,   &t.outL,  &t.outR,   &t.shaperGainDb,
                                                          &t.compGainDb, &t.compGrDb};
    };
    for (const Base& b : bases) {
        mla::ChainConfig law = mlaStatic(b.controls, f0, MassLaw::kNeutralMass, 512, true);
        law.seconds = 2.0;
        mla::ChainConfig detached = law;
        detached.massLawDetached = true;
        const mla::ChainTaps a = mla::renderChain(law);
        const mla::ChainTaps c = mla::renderChain(detached);
        const auto aTaps = tapsOf(a);
        const auto cTaps = tapsOf(c);
        std::size_t equalTaps = 0;
        for (std::size_t k = 0; k < aTaps.size(); ++k) {
            INFO(b.name << " tap " << k);
            const bool eq = mlaVectorsBitEqual(*aTaps[k], *cTaps[k]);
            CHECK(eq);
            if (eq)
                ++equalTaps;
        }
        // Not vacuous: the voice sounds.
        CHECK(std::any_of(a.outL.begin(), a.outL.end(), [](float s) { return s != 0.0f; }));
        // The per-interval targets equal the base, bit for bit (MassLaw::Targets is seven floats).
        const MassLaw::Targets lawBase = mla::lawBaseFor(law);
        REQUIRE(a.targetsPerInterval.size() == c.targetsPerInterval.size());
        bool targetsNeutral = true;
        for (const MassLaw::Targets& t : a.targetsPerInterval)
            targetsNeutral = targetsNeutral && mlaTargetsBitEqual(t, lawBase);
        CHECK(targetsNeutral);
        std::cout << std::setprecision(6) << "[T015 SC-023] " << b.name << " E1 Mass 50 vs detached: " << equalTaps
                  << " / " << aTaps.size() << " taps memcmp-equal, targets neutral " << targetsNeutral << "\n";
    }

    // Range-edge bases: the tasks.md pair (depth 1 at Mass 100, fold 100 at Mass 0: the bias pulls away
    // from the edge) and the clamping pair (depth 1 at Mass 0, fold 100 at Mass 100: the bias pushes into it).
    struct Edge {
        const char* name;
        MassLaw::Targets lawBase;
        float mass;
        MassLaw::Target which;
        bool clamps;
        float edge;
    };
    const std::array<Edge, 4> edges{{
        {.name = "depth 1 base, Mass 100", .lawBase = MassLaw::Targets{.depth = 1.0f}, .mass = 100.0f,
         .which = MassLaw::Target::Depth, .clamps = false, .edge = 1.0f},
        {.name = "fold 100 base, Mass 0", .lawBase = MassLaw::Targets{.foldAmount = 100.0f}, .mass = 0.0f,
         .which = MassLaw::Target::FoldAmount, .clamps = false, .edge = 100.0f},
        {.name = "depth 1 base, Mass 0 (clamps)", .lawBase = MassLaw::Targets{.depth = 1.0f}, .mass = 0.0f,
         .which = MassLaw::Target::Depth, .clamps = true, .edge = 1.0f},
        {.name = "fold 100 base, Mass 100 (clamps)", .lawBase = MassLaw::Targets{.foldAmount = 100.0f},
         .mass = 100.0f, .which = MassLaw::Target::FoldAmount, .clamps = true, .edge = 100.0f},
    }};
    for (const Edge& e : edges) {
        mla::ChainConfig cfg = mlaStatic(SpectralShapeRecipe::kDefaultControls, f0, e.mass, 512, false);
        cfg.seconds = 2.0;
        cfg.lawBase = e.lawBase;
        const mla::ChainTaps t = mla::renderChain(cfg);
        REQUIRE(!t.targetsPerInterval.empty());
        const MassLaw::Targets& tg = t.targetsPerInterval.back();
        std::cout << std::setprecision(6) << "[T015 SC-023 edge] " << e.name << ": depth " << tg.depth << ", body "
                  << tg.body << ", duration " << tg.transientDurationMs << " ms, fold " << tg.foldAmount
                  << ", compression " << tg.compressionAmount << ", anchor level " << tg.anchorLevel << "\n";
        INFO(e.name);
        for (const MassLaw::Row& row : MassLaw::kRows) {
            const float v = mla::targetField(tg, row.target);
            CHECK(v >= row.lo);
            CHECK(v <= row.hi);
        }
        if (e.clamps)
            CHECK(mla::targetField(tg, e.which) == e.edge);
        CHECK(mlaAllFinite(t.outL));
        CHECK(mlaAllFinite(t.outR));
    }
}
