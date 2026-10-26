// ==============================================================================
// Layer 2: Processor Tests - Rectifier
//                          (Profundum Phase 5, specs/profundum-phase5-shape-character)
// ==============================================================================
// Reference: specs/profundum-phase5-shape-character/plan.md (S4.2, S4.3, S5.5, C-1)
//            specs/profundum-phase5-shape-character/tasks.md (T002 wires this TU,
//                                                             T007 fills it)
//
// SCOPE OF THIS TU (tags [processors][profundum][rectifier]): FR-001..FR-005,
//   SC-014 (a)(b), and the Rectifier arms of SC-010 (smoke + [long] grid),
//   SC-011, SC-016, SC-018, SC-019, SC-021, plus the C-1 constant equality with
//   OversampledResidualPath. SC-017 lives in rectifier_nonfinite_test.cpp
//   (-fno-fast-math). SC-012(c) lives in systems/bass_character_rlaw_test.cpp
//   (tasks.md deviation 4).
//   Layer rule: no systems/ include, directly or through a helper.
// ==============================================================================

#include "harmonic_fold_test_helpers.h"
#include "shape_character_metric_helpers.h"

// allocation_detector.h ONLY. NEVER allocation_operator_overrides.h: the global operator
// new/delete replacements are included from exactly one TU of dsp_processors_tests
// (brownian_drift_test.cpp); a second include is a duplicate-symbol link error.
#include <allocation_detector.h>

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/processors/harmonic_fold.h>
#include <krate/dsp/processors/oversampled_residual_path.h>
#include <krate/dsp/processors/rectifier.h>
#include <krate/dsp/processors/spectral_shape_recipe.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

// The test probe (tasks.md deviation 5): defined ONLY in this TU.
namespace Krate::DSP::detail {
struct RectifierProbe {
    /// setBlend(b), then snap the blend smoother to it (no smoothing: the positive control).
    static void snapBlend(Rectifier& r, float b) {
        r.setBlend(b);
        r.blend_.snapTo(b);
    }
    /// The mono-link flag (Rectifier_Lifecycle: reset() relinks).
    static bool linked(const Rectifier& r) { return r.linked_; }
};
}  // namespace Krate::DSP::detail

using namespace Krate::DSP;
namespace hft = Krate::DSP::HarmonicFoldTest;
namespace sct = Krate::DSP::ShapeCharacterTest;

namespace {

// Notes (tasks.md rule 8): computed in double.
double rtMidiHz(int note) { return 440.0 * std::pow(2.0, (static_cast<double>(note) - 69.0) / 12.0); }
const double kRtE0Hz = rtMidiHz(16);
const double kRtE1Hz = rtMidiHz(28);
const double kRtE2Hz = rtMidiHz(40);
const double kRtC2Hz = rtMidiHz(36);
const double kRtC3Hz = rtMidiHz(48);

constexpr double kRtAliasBarDbfs = -80.0;  // SC-010
constexpr double kRtDcBarDbfs = -80.0;     // SC-011

struct RtFixture {
    const char* name;
    SpectralShapeRecipe::Controls controls;
};

std::array<RtFixture, 2> rtFixtures() {
    return {RtFixture{.name = "kSawAnchor", .controls = SpectralShapeRecipe::kSawAnchor},
            RtFixture{.name = "kGrowl", .controls = SpectralShapeRecipe::kGrowl}};
}

/// Fold-100 input (tasks.md rule 9): a fresh HarmonicFold at Amount 100, keyed to f0.
std::vector<float> rtFold100(const std::vector<float>& in, double fs, double f0) {
    HarmonicFold fold;
    fold.prepare(fs, 512);
    fold.setAmount(100.0f);
    fold.setFundamentalHz(static_cast<float>(f0));
    return hft::renderFold(fold, in, 512);
}

/// The fixture input (raw or Fold-100), `seconds` long.
std::vector<float> rtInput(const SpectralShapeRecipe::Controls& c, double f0, double fs, double seconds, bool fold100) {
    std::vector<float> x = hft::recipeAdditive(c, f0, fs, seconds);
    return fold100 ? rtFold100(x, fs, f0) : x;
}

/// setBlend before prepare: prepare's reset snaps the smoother to it.
void rtPrepare(Rectifier& r, double fs, float b) {
    r.setBlend(b);
    r.prepare(fs, 512);
}

/// Stereo in-place render in `block`-sized host blocks; a block also ends at every index in
/// `splits` (sorted), so an event applied by `onBlock(pos)` at a split lands on that exact
/// absolute sample whatever the block size.
template <class OnBlock>
void rtRenderStereo(Rectifier& r, std::vector<float>& L, std::vector<float>& R, std::size_t block,
                    const std::vector<std::size_t>& splits, const OnBlock& onBlock) {
    REQUIRE(block > 0);
    REQUIRE(L.size() == R.size());
    const std::size_t total = L.size();
    std::size_t pos = 0;
    std::size_t si = 0;
    while (pos < total) {
        while (si < splits.size() && splits[si] <= pos)
            ++si;
        std::size_t n = std::min(block, total - pos);
        if (si < splits.size())
            n = std::min(n, splits[si] - pos);
        onBlock(pos);
        r.processBlock(L.data() + pos, R.data() + pos, n);
        pos += n;
    }
}

/// L == R render of `in`; returns L.
std::vector<float> rtRender(Rectifier& r, const std::vector<float>& in, std::size_t block = 512) {
    std::vector<float> L = in;
    std::vector<float> R = in;
    rtRenderStereo(r, L, R, block, {}, [](std::size_t) {});
    return L;
}

bool rtBitEqual(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

/// Render length for the aliasing frame: 1 s settle + the alias frame, in seconds (rule 9).
double rtAliasSeconds(double fs, double f0) {
    return 1.0 + static_cast<double>(hft::aliasFftLength(fs, f0)) / fs + 0.05;
}

struct RtAliasCell {
    double aOut;    ///< output aliased power, dBFS
    double aIn;     ///< input aliased power (same mask, same frame), dBFS
    double g;       ///< h1 power gain out/in (BH7 single bin at f0 on the same frame)
    double aAdded;  ///< 10·log10(max(P_alias,out − G·P_alias,in, 0)), dBFS
};

/// SC-010 attribution on the last aliasFftLength(fs, f0) samples of `in` and `out` (same length).
RtAliasCell rtAliasCell(const std::vector<float>& in, const std::vector<float>& out, double fs, double f0) {
    REQUIRE(in.size() == out.size());
    const std::size_t L = hft::aliasFftLength(fs, f0);
    REQUIRE(out.size() >= L + static_cast<std::size_t>(fs));  // the frame starts after the 1 s settle
    const float* yo = out.data() + (out.size() - L);
    const float* yi = in.data() + (in.size() - L);
    const hft::AliasResult ao = hft::aliasedPower(yo, L, fs, f0);
    const hft::AliasResult ai = hft::aliasedPower(yi, L, fs, f0);
    const std::complex<double> ho = hft::goertzelPhasor(yo, L, fs, f0);
    const std::complex<double> hi = hft::goertzelPhasor(yi, L, fs, f0);
    REQUIRE(std::norm(hi) > 0.0);
    const double g = std::norm(ho) / std::norm(hi);
    const double pOut = std::pow(10.0, ao.aliasedDbfs / 10.0);
    const double pIn = std::pow(10.0, ai.aliasedDbfs / 10.0);
    return RtAliasCell{.aOut = ao.aliasedDbfs,
                       .aIn = ai.aliasedDbfs,
                       .g = g,
                       .aAdded = hft::safeDb10(std::max(pOut - g * pIn, 0.0))};
}

std::string rtEvidenceDir() {
    return std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase5-shape-character/evidence/";
}

/// Index in [from, from + period) of the input's most negative sample, excluding samples within
/// 2·N (N = floor(fs / f0 / 8), the differential-count window) of the waveform's edges (samples
/// whose |Δx| is at least half the largest |Δx| in [from − period, from + 2·period)). See the
/// positive control in Rectifier_BlendStepClickFree for why the edges are excluded.
std::size_t rtNegativePeakAwayFromEdges(const std::vector<float>& x, std::size_t from, double fs, double f0) {
    const auto period = static_cast<std::size_t>(std::ceil(fs / f0));
    const auto windowN = static_cast<std::size_t>(std::floor(fs / f0 / 8.0));
    REQUIRE(from > period);
    REQUIRE(from + 2 * period < x.size());
    const std::size_t lo = from - period;
    const std::size_t hi = from + 2 * period;
    double maxDelta = 0.0;
    for (std::size_t k = lo + 1; k < hi; ++k)
        maxDelta = std::max(maxDelta, std::abs(static_cast<double>(x[k]) - static_cast<double>(x[k - 1])));
    REQUIRE(maxDelta > 0.0);
    std::vector<std::size_t> edges;
    for (std::size_t k = lo + 1; k < hi; ++k) {
        if (std::abs(static_cast<double>(x[k]) - static_cast<double>(x[k - 1])) >= 0.5 * maxDelta)
            edges.push_back(k);
    }
    std::size_t best = from;
    float bestV = 0.0f;
    bool found = false;
    for (std::size_t k = from; k < from + period; ++k) {
        bool nearEdge = false;
        for (const std::size_t e : edges) {
            const std::size_t d = (k > e) ? k - e : e - k;
            if (d <= 2 * windowN) {
                nearEdge = true;
                break;
            }
        }
        if (nearEdge)
            continue;
        if (!found || x[k] < bestV) {
            best = k;
            bestV = x[k];
            found = true;
        }
    }
    REQUIRE(found);
    REQUIRE(bestV < 0.0f);
    return best;
}

}  // namespace

// =============================================================================
// Constants and the law (C-1, FR-001, FR-002, SC-014(a), S5.5)
// =============================================================================

TEST_CASE("Rectifier_Constants", "[processors][profundum][rectifier]") {
    // plan S1 C-1: the residual-path constants are duplicated over L1 parts; they must match.
    static_assert(Rectifier::kNotchRatios == OversampledResidualPath::kNotchRatios);
    static_assert(Rectifier::kNotchQs == OversampledResidualPath::kNotchQs);
    static_assert(Rectifier::kResidualDcHz == OversampledResidualPath::kResidualDcHz);
    static_assert(Rectifier::kFourTimesLimitHz == OversampledResidualPath::kFourTimesLimitHz);
    static_assert(Rectifier::latencySamples() == 0);

    CHECK(Rectifier::kNotchRatios == OversampledResidualPath::kNotchRatios);
    CHECK(Rectifier::kNotchQs == OversampledResidualPath::kNotchQs);
    CHECK(Rectifier::kResidualDcHz == OversampledResidualPath::kResidualDcHz);
    CHECK(Rectifier::kFourTimesLimitHz == OversampledResidualPath::kFourTimesLimitHz);
    CHECK(Rectifier::latencySamples() == 0u);

    // FR-068 factor table, same as OversampledResidualPath.
    Rectifier r;
    r.prepare(96000.0, 512);
    CHECK(r.oversamplingFactor() == 4u);
    r.prepare(192000.0, 512);
    CHECK(r.oversamplingFactor() == 2u);
}

TEST_CASE("Rectifier_TransferLaw", "[processors][profundum][rectifier]") {
    constexpr std::array<float, 5> kBlends{0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    double worstLaw = 0.0;
    double worstIdentity = 0.0;
    for (const float b : kBlends) {
        const auto bd = static_cast<double>(b);
        for (int i = 0; i <= 4000; ++i) {
            const double xg = -2.0 + 4.0 * static_cast<double>(i) / 4000.0;  // i = 2000 -> exactly 0
            const auto x = static_cast<float>(xg);
            const auto xd = static_cast<double>(x);
            const auto y = static_cast<double>(Rectifier::transfer(x, b));
            // FR-002 law in double.
            const double law = (1.0 - bd) * std::max(xd, 0.0) + bd * std::abs(xd);
            worstLaw = std::max(worstLaw, std::abs(y - law));
            // Odd/even split: c(b)·x + (1 + b)/2·|x|.
            const double split =
                static_cast<double>(Rectifier::linearCoefficient(b)) * xd + 0.5 * (1.0 + bd) * std::abs(xd);
            worstIdentity = std::max(worstIdentity, std::abs(y - split));
        }
    }
    INFO("worst |transfer - law| = " << worstLaw << ", worst |transfer - split| = " << worstIdentity);
    CHECK(worstLaw <= 1.0e-6);
    CHECK(worstIdentity <= 1.0e-7);
    CHECK(Rectifier::transfer(0.0f, 0.5f) == 0.0f);
}

TEST_CASE("Rectifier_SineMakeup", "[processors][profundum][rectifier]") {
    constexpr double kPi = 3.14159265358979323846;
    CHECK(std::abs(static_cast<double>(Rectifier::sineMakeup(0.0f)) - 1.834) <= 0.001);
    CHECK(std::abs(static_cast<double>(Rectifier::sineMakeup(1.0f)) - 2.297) <= 0.001);
    constexpr std::array<float, 5> kBlends{0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    for (const float b : kBlends) {
        const auto bd = static_cast<double>(b);
        const double ac2 = (1.0 + bd * bd) / 4.0 - (1.0 + bd) * (1.0 + bd) / (kPi * kPi);
        const double expected = std::sqrt(0.5 / ac2);
        INFO("b = " << bd << ", sineMakeup = " << Rectifier::sineMakeup(b) << ", expected " << expected);
        CHECK(std::abs(static_cast<double>(Rectifier::sineMakeup(b)) - expected) <= 1.0e-6);
    }
}

// =============================================================================
// SC-014(b): the processed path reproduces the law's Fourier series
// =============================================================================

TEST_CASE("Rectifier_ProcessedHarmonics", "[processors][profundum][rectifier]") {
    constexpr double fs = 48000.0;
    const double f0 = kRtC3Hz;
    constexpr std::size_t kFrame = TestUtils::kLowFrequencyFftSize;
    const std::vector<float> in = hft::calibrationSine(fs, f0, 7.0);
    REQUIRE(in.size() >= kFrame + static_cast<std::size_t>(fs));  // frame starts after the 1 s settle
    const auto A = static_cast<double>(HarmonicFold::kCalibrationPeak);
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kDcCornerHz = static_cast<double>(Rectifier::kResidualDcHz);

    constexpr std::array<float, 3> kBlends{0.0f, 0.5f, 1.0f};
    for (const float b : kBlends) {
        const auto bd = static_cast<double>(b);
        Rectifier r;
        rtPrepare(r, fs, b);
        const std::vector<float> y = rtRender(r, in);
        const std::vector<double> w = hft::renderedHarmonicPowers(y.data() + (y.size() - kFrame), fs, f0, 8);

        // Analytic series (plan S4.3) for x = A·sin θ.
        std::array<double, 8> a{};
        for (int n = 1; n <= 8; ++n) {
            double an = 0.0;
            if (n == 1) {
                an = 0.5 * (1.0 - bd) * A;
            } else if (n % 2 == 0) {
                const double k = static_cast<double>(n / 2);
                an = 0.5 * (1.0 + bd) * A * 4.0 / (kPi * (4.0 * k * k - 1.0));
            }
            a[static_cast<std::size_t>(n - 1)] = an;
        }
        const double aMax = *std::max_element(a.begin(), a.end());
        const double wMax = *std::max_element(w.begin(), w.end());
        REQUIRE(aMax > 0.0);
        REQUIRE(wMax > 0.0);
        for (int n = 1; n <= 8; ++n) {
            const auto i = static_cast<std::size_t>(n - 1);
            if (a[i] <= 0.0)
                continue;
            const double analyticDb = 20.0 * std::log10(a[i] / aMax);
            if (analyticDb <= -60.0)
                continue;
            const double renderedDb = hft::safeDb10(w[i] / wMax);
            // First-order 2 Hz high-pass magnitude at n·f0 (plan S4.3: < 0.001 dB at C3), recorded.
            const double ratio = kDcCornerHz / (static_cast<double>(n) * f0);
            const double dcPoleDb = -10.0 * std::log10(1.0 + ratio * ratio);
            INFO("b = " << bd << ", h" << n << ": analytic " << analyticDb << " dB, rendered " << renderedDb
                        << " dB re largest; DC-pole attenuation " << dcPoleDb << " dB");
            CHECK(std::abs(renderedDb - analyticDb) <= 0.1);
        }
    }
}

// =============================================================================
// SC-010 Rectifier arm: aliasing
// =============================================================================

TEST_CASE("Rectifier_AliasingSmoke", "[processors][profundum][rectifier]") {
    constexpr double fs = 44100.0;
    const double f0 = kRtE2Hz;
    const double seconds = rtAliasSeconds(fs, f0);

    {   // b = 1, Fold-100 kGrowl
        const std::vector<float> in = rtInput(SpectralShapeRecipe::kGrowl, f0, fs, seconds, true);
        Rectifier r;
        rtPrepare(r, fs, 1.0f);
        const std::vector<float> y = rtRender(r, in);
        const RtAliasCell c = rtAliasCell(in, y, fs, f0);
        INFO("b = 1, Fold-100 kGrowl, E2, 44.1 kHz: A = " << c.aOut << " dBFS, A_in = " << c.aIn << ", G = " << c.g
                                                           << ", A_added = " << c.aAdded);
        CHECK(c.aOut <= kRtAliasBarDbfs);
    }
    {   // b = 0.5, raw kSawAnchor
        const std::vector<float> in = rtInput(SpectralShapeRecipe::kSawAnchor, f0, fs, seconds, false);
        Rectifier r;
        rtPrepare(r, fs, 0.5f);
        const std::vector<float> y = rtRender(r, in);
        const RtAliasCell c = rtAliasCell(in, y, fs, f0);
        INFO("b = 0.5, raw kSawAnchor, E2, 44.1 kHz: A = " << c.aOut << " dBFS, A_in = " << c.aIn << ", G = " << c.g
                                                            << ", A_added = " << c.aAdded);
        CHECK(c.aOut <= kRtAliasBarDbfs);
    }
}

TEST_CASE("Rectifier_AliasingE0E2", "[processors][profundum][rectifier][long]") {
    const std::string dir = rtEvidenceDir();
    std::filesystem::create_directories(dir);
    std::ofstream csv(dir + "sc010-rectifier.csv");
    REQUIRE(csv.is_open());
    csv << "b,note,fs,fixture,variant,A_out_dbfs,A_in_dbfs,G_h1_power,A_added_dbfs,pass\n";

    constexpr std::array<float, 3> kBlends{0.0f, 0.5f, 1.0f};
    const std::array<std::pair<const char*, double>, 3> notes{
        {{"E0", kRtE0Hz}, {"E1", kRtE1Hz}, {"E2", kRtE2Hz}}};
    constexpr std::array<double, 3> kRates{44100.0, 48000.0, 96000.0};
    double worst = -400.0;
    for (const auto& [noteName, f0] : notes) {
        for (const double fs : kRates) {
            const double seconds = rtAliasSeconds(fs, f0);
            for (const RtFixture& fx : rtFixtures()) {
                const std::vector<float> raw = rtInput(fx.controls, f0, fs, seconds, false);
                for (const bool fold : {false, true}) {
                    const std::vector<float> in = fold ? rtFold100(raw, fs, f0) : raw;
                    for (const float b : kBlends) {
                        Rectifier r;
                        rtPrepare(r, fs, b);
                        const std::vector<float> y = rtRender(r, in);
                        const RtAliasCell c = rtAliasCell(in, y, fs, f0);
                        const bool pass = c.aOut <= kRtAliasBarDbfs;
                        worst = std::max(worst, c.aOut);
                        csv << b << ',' << noteName << ',' << fs << ',' << fx.name << ','
                            << (fold ? "Fold-100" : "raw") << ',' << c.aOut << ',' << c.aIn << ',' << c.g << ','
                            << c.aAdded << ',' << (pass ? 1 : 0) << '\n';
                        if (!pass) {
                            if (fold && c.aAdded <= kRtAliasBarDbfs) {
                                // Never passed silently (SC-010 attribution, FR-070).
                                FAIL_CHECK("FR-070 candidate: input residual - b = "
                                           << b << ", " << noteName << ", " << fs << " Hz, " << fx.name
                                           << ", Fold-100: A = " << c.aOut << " dBFS, A_in = " << c.aIn
                                           << ", G = " << c.g << ", A_added = " << c.aAdded);
                            } else {
                                FAIL_CHECK("SC-010 Rectifier cell above -80 dBFS - b = "
                                           << b << ", " << noteName << ", " << fs << " Hz, " << fx.name << ", "
                                           << (fold ? "Fold-100" : "raw") << ": A = " << c.aOut
                                           << " dBFS, A_in = " << c.aIn << ", G = " << c.g
                                           << ", A_added = " << c.aAdded);
                            }
                        }
                    }
                }
            }
        }
    }
    INFO("worst output A over the grid: " << worst << " dBFS");
    CHECK(worst <= kRtAliasBarDbfs);
}

// =============================================================================
// SC-011 Rectifier arm: DC
// =============================================================================

TEST_CASE("Rectifier_DcBound", "[processors][profundum][rectifier]") {
    constexpr double fs = 48000.0;
    constexpr double kSeconds = 2.2;
    const auto settle = static_cast<std::size_t>(fs);  // 1 s
    constexpr std::array<float, 3> kBlends{0.0f, 0.5f, 1.0f};
    const std::array<std::pair<const char*, double>, 3> notes{
        {{"E0", kRtE0Hz}, {"E1", kRtE1Hz}, {"E2", kRtE2Hz}}};
    for (const auto& [noteName, f0] : notes) {
        for (const RtFixture& fx : rtFixtures()) {
            const std::vector<float> raw = rtInput(fx.controls, f0, fs, kSeconds, false);
            for (const bool fold : {false, true}) {
                const std::vector<float> in = fold ? rtFold100(raw, fs, f0) : raw;
                for (const float b : kBlends) {
                    Rectifier r;
                    rtPrepare(r, fs, b);
                    const std::vector<float> y = rtRender(r, in);
                    REQUIRE(y.size() > settle);
                    const std::size_t tail = y.size() - settle;
                    const std::size_t len = hft::integerPeriodLength(tail, fs, f0, 1.0);
                    const float* x = y.data() + (y.size() - len);
                    double sum = 0.0;
                    for (std::size_t k = 0; k < len; ++k)
                        sum += static_cast<double>(x[k]);
                    const double mean = sum / static_cast<double>(len);
                    const double dcDb = 20.0 * std::log10(std::max(std::abs(mean), 1.0e-300));
                    INFO("b = " << b << ", " << noteName << ", " << fx.name << ", " << (fold ? "Fold-100" : "raw")
                                << ": DC = " << dcDb << " dBFS");
                    CHECK(dcDb <= kRtDcBarDbfs);
                }
            }
        }
    }
}

// =============================================================================
// SC-021 Rectifier arm, FR-005: a full-range blend step is click-free
// =============================================================================

TEST_CASE("Rectifier_BlendStepClickFree", "[processors][profundum][rectifier]") {
    constexpr double fs = 48000.0;
    constexpr double kSeconds = 1.5;
    constexpr std::size_t kBlock = 16;
    constexpr std::size_t kEvent = 48000;  // 1.0 s, a multiple of kBlock
    constexpr std::size_t kRampSamples = 960;  // 20 ms
    const std::array<std::pair<const char*, double>, 2> notes{{{"E1", kRtE1Hz}, {"C2", kRtC2Hz}}};
    const std::array<std::pair<float, float>, 2> directions{{{0.0f, 1.0f}, {1.0f, 0.0f}}};

    for (const auto& [noteName, f0] : notes) {
        std::size_t controlsFlagged = 0;  // positive controls flagged at this note (gated >= 1 per note, ruling 2026-10-16)
        for (const RtFixture& fx : rtFixtures()) {
            const std::vector<float> in = rtInput(fx.controls, f0, fs, kSeconds, false);
            for (const auto& [b0, b1] : directions) {
                // References: b held at the start value and at the end value, same blocks (phase-aligned).
                // Group 3 gate ruling 2026-10-16 (spec "Differential click count"): a detection counts only
                // if neither held render has one within +-N; the end setting's steady detections are not
                // transition artefacts.
                Rectifier ref;
                rtPrepare(ref, fs, b0);
                const std::vector<float> yRef = rtRender(ref, in, kBlock);
                Rectifier refEnd;
                rtPrepare(refEnd, fs, b1);
                const std::vector<float> yRefEnd = rtRender(refEnd, in, kBlock);

                for (const bool ramp : {false, true}) {
                    Rectifier r;
                    rtPrepare(r, fs, b0);
                    std::vector<float> L = in;
                    std::vector<float> R = in;
                    rtRenderStereo(r, L, R, kBlock, {}, [&, from = b0, to = b1](std::size_t pos) {
                        if (pos < kEvent)
                            return;
                        if (!ramp) {
                            if (pos == kEvent)
                                r.setBlend(to);
                            return;
                        }
                        if (pos > kEvent + kRampSamples)
                            return;
                        // setBlend every 16 samples, linearly interpolated over 20 ms.
                        const double t = std::min(1.0, static_cast<double>(pos - kEvent) /
                                                           static_cast<double>(kRampSamples));
                        r.setBlend(static_cast<float>(static_cast<double>(from) +
                                                      (static_cast<double>(to) - static_cast<double>(from)) * t));
                    });
                    const sct::ClickCount cc =
                        sct::differentialClicks(L.data(), yRef.data(), yRefEnd.data(), L.size(), fs, f0);
                    INFO(noteName << ", " << fx.name << ", b " << b0 << " -> " << b1 << ", "
                                  << (ramp ? "20 ms ramp" : "single step") << ": counted " << cc.counted
                                  << " (guarded out " << cc.guardedOut << ", transition detections " << cc.transitionDetections
                                  << ", reference detections " << cc.referenceDetections << ", window "
                                  << cc.windowN << ")");
                    CHECK(cc.counted == 0u);
                }

                if (b0 != 0.0f)
                    continue;
                // Positive control: the same 0 -> 1 step with the smoother bypassed (probe) at the
                // input's negative peak must count. The literal minimum of a saw-like input sits next
                // to its edge, where the reference has its own detection inside the ±N window, so a
                // click there is masked by the differential rule by construction; the control
                // therefore takes the most negative sample at least 2·N away from every edge.
                const std::size_t at = rtNegativePeakAwayFromEdges(in, kEvent, fs, f0);
                Rectifier pc;
                rtPrepare(pc, fs, 0.0f);
                std::vector<float> L = in;
                std::vector<float> R = in;
                rtRenderStereo(pc, L, R, kBlock, {at}, [&](std::size_t pos) {
                    if (pos == at)
                        detail::RectifierProbe::snapBlend(pc, 1.0f);
                });
                const sct::ClickCount cc =
                    sct::differentialClicks(L.data(), yRef.data(), yRefEnd.data(), L.size(), fs, f0);
                // Recorded per cell; gated per note below (ruling 2026-10-16: the spec asks for the control
                // per SC arm, and a pulse-like growl at C2 has no countable snap sample away from its edges).
                WARN(noteName << ", " << fx.name << " positive control at sample " << at << " (x = " << in[at]
                              << "): counted " << cc.counted << " (guarded out " << cc.guardedOut << ", transition detections "
                              << cc.transitionDetections << ", reference detections " << cc.referenceDetections
                              << ")" << (cc.counted >= 1u ? " - FLAGGED" : " - not flagged (recorded)"));
                if (cc.counted >= 1u)
                    ++controlsFlagged;
            }
        }
        INFO(noteName << ": positive controls flagged over the fixtures = " << controlsFlagged);
        CHECK(controlsFlagged >= 1u);
    }
}

// =============================================================================
// SC-016 Rectifier arm: no allocation on the audio thread
// =============================================================================

TEST_CASE("Rectifier_NoAllocation", "[processors][profundum][rectifier]") {
    constexpr double fs = 48000.0;
    constexpr std::size_t kN = 512;
    Rectifier r;
    rtPrepare(r, fs, 0.5f);
    std::vector<float> srcL(kN);
    std::vector<float> srcR(kN);
    for (std::size_t k = 0; k < kN; ++k) {
        const double ph = hft::kTwoPi * kRtE1Hz * static_cast<double>(k) / fs;
        srcL[k] = static_cast<float>(0.35 * std::sin(ph));
        srcR[k] = static_cast<float>(0.30 * std::sin(ph + 0.4));
    }
    std::vector<float> L = srcL;
    std::vector<float> R = srcR;

    std::size_t observed = 0;
    float sink = 0.0f;
    {
        [[maybe_unused]] const TestHelpers::AllocationScope scope;
        for (int i = 0; i < 64; ++i) {
            r.setBlend((i % 2 == 0) ? 0.9f : 0.2f);
            std::copy(srcL.begin(), srcL.end(), L.begin());
            std::copy(srcR.begin(), srcR.end(), R.begin());
            r.processBlock(L.data(), R.data(), kN);
            sink += L[kN - 1] + R[kN - 1];
        }
        r.reset();
        // AllocationScope latches in its destructor, so the live figure is read while it is open.
        observed = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }
    INFO("allocations observed: " << observed);
    CHECK(detail::isFinite(sink));
    REQUIRE(observed == 0u);

    // Positive control: the detector sees an allocation inside an identical scope.
    std::size_t control = 0;
    int probeObserved = 0;
    {
        [[maybe_unused]] const TestHelpers::AllocationScope scope;
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        int* probe = new int(42);
        volatile int* probeSink = probe;  // defeat new/delete elision
        probeObserved = *probeSink;
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        delete probe;
        control = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }
    CHECK(probeObserved == 42);
    CHECK(control >= 1u);
}

// =============================================================================
// SC-018 Rectifier arm: block-partition invariance and determinism
// =============================================================================

TEST_CASE("Rectifier_PartitionInvariant", "[processors][profundum][rectifier]") {
    constexpr double fs = 48000.0;
    constexpr std::size_t kEvent = 20000;
    const std::vector<float> in = rtInput(SpectralShapeRecipe::kGrowl, kRtE1Hz, fs, 1.0, false);

    const auto renderWith = [&](Rectifier& r, std::size_t block) {
        std::vector<float> L = in;
        std::vector<float> R = in;
        rtRenderStereo(r, L, R, block, {kEvent}, [&](std::size_t pos) {
            if (pos == kEvent)
                r.setBlend(0.8f);
        });
        return L;
    };

    Rectifier r0;
    rtPrepare(r0, fs, 0.0f);
    const std::vector<float> base = renderWith(r0, 512);

    constexpr std::array<std::size_t, 5> kBlocks{1, 17, 32, 64, 512};
    for (const std::size_t block : kBlocks) {
        Rectifier r;
        rtPrepare(r, fs, 0.0f);
        const std::vector<float> y = renderWith(r, block);
        INFO("block " << block);
        CHECK(rtBitEqual(y, base));
    }

    // Two renders from reset() are bit-identical (reset snaps the smoother to the pending blend).
    {
        Rectifier r;
        rtPrepare(r, fs, 0.0f);
        const std::vector<float> first = renderWith(r, 512);
        r.setBlend(0.0f);
        r.reset();
        const std::vector<float> second = renderWith(r, 512);
        CHECK(rtBitEqual(first, second));
        CHECK(rtBitEqual(first, base));
    }

    // prepare(48k) -> prepare(96k) -> prepare(48k) equals a fresh 48 kHz render.
    {
        Rectifier r;
        rtPrepare(r, fs, 0.3f);
        (void)renderWith(r, 512);
        r.prepare(96000.0, 512);
        (void)rtRender(r, in, 256);
        r.setBlend(0.0f);
        r.prepare(fs, 512);
        const std::vector<float> y = renderWith(r, 512);
        CHECK(rtBitEqual(y, base));
    }
}

// =============================================================================
// SC-019 Rectifier arm: sample rates, finite and bounded output
// =============================================================================

TEST_CASE("Rectifier_SampleRates", "[processors][profundum][rectifier]") {
    constexpr std::array<double, 5> kRates{44100.0, 48000.0, 88200.0, 96000.0, 192000.0};
    constexpr std::array<float, 3> kBlends{0.0f, 0.5f, 1.0f};
    for (const double fs : kRates) {
        const std::vector<float> in = rtInput(SpectralShapeRecipe::kSawAnchor, kRtE1Hz, fs, 1.0, false);
        for (const float b : kBlends) {
            Rectifier r;
            rtPrepare(r, fs, b);
            const std::vector<float> y = rtRender(r, in);
            bool allFinite = true;
            float maxAbs = 0.0f;
            for (const float v : y) {
                if (!detail::isFinite(v))
                    allFinite = false;
                else
                    maxAbs = std::max(maxAbs, std::abs(v));
            }
            INFO("fs = " << fs << ", b = " << b << ": max |y| = " << maxAbs << ", pre-ceiling peak "
                         << r.preCeilingPeak() << ", factor " << r.oversamplingFactor());
            CHECK(allFinite);
            CHECK(maxAbs <= Rectifier::kOutputCeiling);
            CHECK(r.preCeilingPeak() <= Rectifier::kOutputCeiling);
            CHECK(r.stateFinite());
        }
    }
}

// =============================================================================
// Lifecycle, edge cases, mono link
// =============================================================================

TEST_CASE("Rectifier_Lifecycle", "[processors][profundum][rectifier]") {
    constexpr double fs = 48000.0;
    const std::vector<float> in = rtInput(SpectralShapeRecipe::kSawAnchor, kRtE1Hz, fs, 0.5, false);
    const std::size_t n = in.size();

    SECTION("unprepared processBlock leaves the buffers unchanged") {
        Rectifier r;
        r.setBlend(0.7f);
        std::vector<float> L = in;
        std::vector<float> R = in;
        r.processBlock(L.data(), R.data(), n);
        CHECK(rtBitEqual(L, in));
        CHECK(rtBitEqual(R, in));
        r.processBlock(L.data(), L.data(), n);
        CHECK(rtBitEqual(L, in));
    }

    SECTION("null pointers and zero samples are no-ops") {
        Rectifier fresh;
        rtPrepare(fresh, fs, 0.5f);
        const std::vector<float> expected = rtRender(fresh, in);

        Rectifier r;
        rtPrepare(r, fs, 0.5f);
        std::vector<float> L = in;
        std::vector<float> R = in;
        r.processBlock(nullptr, R.data(), n);
        r.processBlock(L.data(), nullptr, n);
        r.processBlock(nullptr, nullptr, n);
        r.processBlock(L.data(), R.data(), 0);
        CHECK(rtBitEqual(L, in));
        CHECK(rtBitEqual(R, in));
        // State untouched: the next render equals a fresh one.
        CHECK(rtBitEqual(rtRender(r, in), expected));
    }

    SECTION("right == left equals channel 0 of a stereo render with L == R") {
        Rectifier mono;
        rtPrepare(mono, fs, 0.5f);
        std::vector<float> M = in;
        for (std::size_t pos = 0; pos < n; pos += 512)
            mono.processBlock(M.data() + pos, M.data() + pos, std::min<std::size_t>(512, n - pos));

        Rectifier stereo;
        rtPrepare(stereo, fs, 0.5f);
        std::vector<float> L = in;
        std::vector<float> R = in;
        rtRenderStereo(stereo, L, R, 512, {}, [](std::size_t) {});
        CHECK(rtBitEqual(M, L));
        CHECK(rtBitEqual(L, R));  // L == R input -> L == R output, bit-identical
        CHECK(detail::RectifierProbe::linked(stereo));
    }

    SECTION("a differing R input unlinks; reset() relinks") {
        Rectifier r;
        rtPrepare(r, fs, 0.5f);
        std::vector<float> L = in;
        std::vector<float> R = in;
        for (float& v : R)
            v *= 0.5f;
        rtRenderStereo(r, L, R, 512, {}, [](std::size_t) {});
        CHECK_FALSE(rtBitEqual(L, R));
        CHECK_FALSE(detail::RectifierProbe::linked(r));

        // Channel 0 is unaffected by channel 1's input.
        Rectifier ref;
        rtPrepare(ref, fs, 0.5f);
        CHECK(rtBitEqual(L, rtRender(ref, in)));

        float maxAbs = 0.0f;
        for (std::size_t k = 0; k < n; ++k)
            maxAbs = std::max({maxAbs, std::abs(L[k]), std::abs(R[k])});
        INFO("max |y| = " << maxAbs << ", preCeilingPeak = " << r.preCeilingPeak());
        CHECK(r.preCeilingPeak() >= maxAbs);

        r.reset();
        CHECK(detail::RectifierProbe::linked(r));
        std::vector<float> L2 = in;
        std::vector<float> R2 = in;
        rtRenderStereo(r, L2, R2, 512, {}, [](std::size_t) {});
        CHECK(rtBitEqual(L2, R2));
        CHECK(detail::RectifierProbe::linked(r));
        CHECK(rtBitEqual(L2, L));  // from reset() the render equals a fresh one
    }
}
