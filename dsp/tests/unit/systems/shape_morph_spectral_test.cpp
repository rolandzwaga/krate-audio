// ==============================================================================
// Profundum Phase 5 (specs/profundum-phase5-shape-character)
// ==============================================================================
// ShapeMorph spectral gates (tasks.md T012; plan S9.3, C-3, C-6, S6.5):
//   SC-001  ShapeMorph_RegimesPairwiseDistinct
//   SC-002  ShapeMorph_SweepContinuitySmoke, ShapeMorph_SweepContinuity [long]
//   SC-004  ShapeMorph_HarmonicResponseTracksKeyboard
//   SC-010  ShapeMorph_AliasingSmoke, ShapeMorph_AliasingE0E2 [long]      (Shape arm)
//   SC-011  ShapeMorph_DcBound                                             (Shape arm)
//   SC-012(f) ShapeMorph_RIsOneSmoke, ShapeMorph_RIsOne [long]             (Shape arm, C-3)
//   FR-056  ShapeMorph_LoudnessTableHarvest [.calibration]                 (C-6; run by T024)
//
// Fixture conventions: tasks.md rule 9 (input = HarmonicFoldTest::recipeAdditive at the
// -12 dBFS RMS calibration; Fold-100 = ShapeCharacterTest::fold100; 1 s settle; host block 512;
// L == R copies). Evidence: tasks.md rule 10.
// ==============================================================================

#include "shape_character_test_helpers.h"
#include "shape_morph_probe.h"

#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/core/closed_form_adaa2.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/processors/keytracked_tilt.h>
#include <krate/dsp/processors/oversampled_residual_path.h>
#include <krate/dsp/processors/rectifier.h>
#include <krate/dsp/systems/shape_morph.h>
#include <krate/dsp/systems/bass_character_stage.h>
#include <krate/dsp/systems/profundum_core.h>
#include <krate/dsp/processors/harmonic_fold.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace Krate::DSP;
using ShapeMorphProbe = Krate::DSP::detail::ShapeMorphProbe;

namespace {

namespace SCT = Krate::DSP::ShapeCharacterTest;
namespace HFT = Krate::DSP::HarmonicFoldTest;

constexpr double kSmspFs = 48000.0;
constexpr double kSmspSeconds = 7.0;          // 1 s settle + the 262144-sample frame (rule 9)
constexpr double kSmspShortSeconds = 2.2;     // 1 s settle + >= 1 s of integer periods (L, DC)
constexpr std::size_t kSmspBlock = 512;       // rule 9 host block
constexpr std::size_t kSmspFrame = TestUtils::kLowFrequencyFftSize;
constexpr double kSmspAliasBarDbfs = -80.0;   // SC-010
constexpr double kSmspDcBarDbfs = -80.0;      // SC-011
constexpr double kSmspCentsBar = 1.0;         // SC-012(f)
constexpr std::array<float, 5> kSmspShapeGrid{0.0f, 0.25f, 0.5f, 0.75f, 1.0f};

struct SmspNote {
    const char* name;
    double hz;
};

struct SmspFixture {
    const char* name;
    SpectralShapeRecipe::Controls controls;
};

const SmspFixture kSmspSaw{.name = "kSawAnchor", .controls = SpectralShapeRecipe::kSawAnchor};
const SmspFixture kSmspTriangle{.name = "kTriangleAnchor", .controls = SpectralShapeRecipe::kTriangleAnchor};
const SmspFixture kSmspSine{.name = "kSineAnchor", .controls = SpectralShapeRecipe::kSineAnchor};
const SmspFixture kSmspGrowl{.name = "kGrowl", .controls = SpectralShapeRecipe::kGrowl};

struct SmspRender {
    std::vector<float> y;   ///< channel L of an L == R render
    int maxRegimes;         ///< probe: largest running-regime count over every processed sample
    bool stateFinite;
    float preCeilingPeak;
};

/// A fresh ShapeMorph at SHAPE `shape` (set before prepare, so the smoother starts there),
/// f0 set by setFundamentalHz, enabled (constructor default), rendering `in` as L == R in
/// 512-sample host blocks. `loudnessBypassed` turns the probe's FR-056 table seam on (harvest only).
SmspRender smspRender(const std::vector<float>& in, double fs, double f0, float shape,
                      bool loudnessBypassed = false) {
    ShapeMorph m;
    m.setShape(shape);
    m.setFundamentalHz(static_cast<float>(f0));
    m.prepare(fs, kSmspBlock);
    if (loudnessBypassed)
        ShapeMorphProbe::setSeamLoudnessBypassed(m, true);
    REQUIRE(ShapeMorphProbe::smoothedShape(m) == shape);
    std::vector<float> L = in;
    std::vector<float> R = in;
    const std::size_t total = L.size();
    for (std::size_t i = 0; i < total; i += kSmspBlock) {
        const std::size_t len = std::min(kSmspBlock, total - i);
        m.processBlock(L.data() + i, R.data() + i, len);
    }
    return SmspRender{.y = std::move(L),
                      .maxRegimes = ShapeMorphProbe::maxRunningRegimeCount(m),
                      .stateFinite = m.stateFinite(),
                      .preCeilingPeak = m.preCeilingPeak()};
}

/// The samples after the 1 s settle (rule 9).
std::vector<float> smspTail(const std::vector<float>& y, double fs) {
    const auto settle = static_cast<std::size_t>(std::llround(fs));
    REQUIRE(y.size() > settle);
    return {y.begin() + static_cast<std::ptrdiff_t>(settle), y.end()};
}

/// Spec L on the post-settle tail.
double smspLevelDb(const std::vector<float>& y, double fs, double f0) {
    const std::vector<float> t = smspTail(y, fs);
    return SCT::levelDb(t.data(), t.size(), fs, f0);
}

/// The fixture input, raw or Fold-100 (rule 9).
std::vector<float> smspInput(const SmspFixture& fx, double f0, double fs, double seconds, bool fold) {
    std::vector<float> x = SCT::fixture(fx.controls, f0, fs, seconds);
    return fold ? SCT::fold100(x, fs, f0) : x;
}

/// Render length for the aliasing frame: 1 s settle + the alias frame (rule 9).
double smspAliasSeconds(double fs, double f0) {
    return 1.0 + static_cast<double>(HFT::aliasFftLength(fs, f0)) / fs + 0.05;
}

struct SmspAliasCell {
    double aOut;    ///< output aliased power, dBFS
    double aIn;     ///< input aliased power (same mask, same frame), dBFS
    double g;       ///< h1 power gain out/in (BH7 single bin at f0 on the same frame)
    double aAdded;  ///< 10·log10(max(P_alias,out − G·P_alias,in, 0)), dBFS
};

/// SC-010 attribution on the last aliasFftLength(fs, f0) samples of `in` and `out`.
SmspAliasCell smspAliasCell(const std::vector<float>& in, const std::vector<float>& out, double fs, double f0) {
    REQUIRE(in.size() == out.size());
    const std::size_t L = HFT::aliasFftLength(fs, f0);
    REQUIRE(out.size() >= L + static_cast<std::size_t>(fs));  // the frame starts after the 1 s settle
    const float* yo = out.data() + (out.size() - L);
    const float* yi = in.data() + (in.size() - L);
    const HFT::AliasResult ao = HFT::aliasedPower(yo, L, fs, f0);
    const HFT::AliasResult ai = HFT::aliasedPower(yi, L, fs, f0);
    const std::complex<double> ho = HFT::goertzelPhasor(yo, L, fs, f0);
    const std::complex<double> hi = HFT::goertzelPhasor(yi, L, fs, f0);
    REQUIRE(std::norm(hi) > 0.0);
    const double g = std::norm(ho) / std::norm(hi);
    const double pOut = std::pow(10.0, ao.aliasedDbfs / 10.0);
    const double pIn = std::pow(10.0, ai.aliasedDbfs / 10.0);
    return SmspAliasCell{.aOut = ao.aliasedDbfs,
                         .aIn = ai.aliasedDbfs,
                         .g = g,
                         .aAdded = HFT::safeDb10(std::max(pOut - g * pIn, 0.0))};
}

/// Mean of the post-settle tail over its largest integer number of f0 periods (>= 1 s), dBFS.
double smspDcDb(const std::vector<float>& y, double fs, double f0) {
    const auto settle = static_cast<std::size_t>(std::llround(fs));
    REQUIRE(y.size() > settle);
    const std::size_t tail = y.size() - settle;
    const std::size_t len = HFT::integerPeriodLength(tail, fs, f0, 1.0);
    const float* x = y.data() + (y.size() - len);
    double sum = 0.0;
    for (std::size_t k = 0; k < len; ++k)
        sum += static_cast<double>(x[k]);
    const double mean = sum / static_cast<double>(len);
    return 20.0 * std::log10(std::max(std::abs(mean), 1.0e-300));
}

/// SHS candidates on the last 262144 samples (the analysis frame) of y.
SCT::ShsCandidates smspShs(const std::vector<float>& y, double fs, double f0) {
    REQUIRE(y.size() >= kSmspFrame + static_cast<std::size_t>(fs));
    return SCT::shsCandidates(y.data() + (y.size() - kSmspFrame), fs, f0);
}

std::string smspEvidenceDir() {
    return std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase5-shape-character/evidence/";
}

const char* smspRegimeName(float shape) {
    if (shape == 0.0f)
        return "ROUND";
    if (shape == 0.5f)
        return "HOLLOW";
    if (shape == 1.0f)
        return "BITE";
    return "crossfade";
}

}  // namespace

// =============================================================================
// SC-001: ROUND, HOLLOW and BITE are pairwise distinct (OQ-S bar)
// =============================================================================

TEST_CASE("ShapeMorph_RegimesPairwiseDistinct", "[systems][profundum][shape-character][shape-morph]") {
    // Model margin x1.48: saw HOLLOW-BITE 4.44 dB THD (plan S10.6).
    constexpr std::array<float, 3> kAnchors{0.0f, 0.5f, 1.0f};
    const std::array<SmspNote, 2> notes{{{"E1", SCT::kE1Hz}, {"C2", SCT::kC2Hz}}};
    constexpr std::array<std::pair<std::size_t, std::size_t>, 3> kPairs{{{0u, 1u}, {1u, 2u}, {0u, 2u}}};
    for (const SmspFixture& fx : {kSmspSaw, kSmspGrowl}) {
        for (const SmspNote& note : notes) {
            const std::vector<float> in = SCT::fixture(fx.controls, note.hz, kSmspFs, kSmspSeconds);
            const double levelIn = smspLevelDb(in, kSmspFs, note.hz);
            std::array<SCT::Descriptors, 3> d{};
            for (std::size_t i = 0; i < kAnchors.size(); ++i) {
                const SmspRender r = smspRender(in, kSmspFs, note.hz, kAnchors[i]);
                REQUIRE(r.stateFinite);
                REQUIRE(r.maxRegimes <= 2);
                std::vector<float> t = smspTail(r.y, kSmspFs);
                SCT::levelMatch(t, levelIn, kSmspFs, note.hz);   // level-matched to the input's L
                d[i] = SCT::describe(t.data(), t.size(), kSmspFs, note.hz);
            }
            for (const auto& [a, b] : kPairs) {
                const SCT::Separation s = SCT::separate(d[a], d[b]);
                INFO(fx.name << ", " << note.name << ": " << smspRegimeName(kAnchors[a]) << " vs "
                             << smspRegimeName(kAnchors[b]) << ": maxDb = " << s.maxDb << " (" << s.which
                             << "), dC = " << s.dC << " oct; THD " << d[a].Thd << " / " << d[b].Thd << ", Elow "
                             << d[a].Elow << " / " << d[b].Elow << ", Ehigh " << d[a].Ehigh << " / " << d[b].Ehigh
                             << ", I " << d[a].I << " / " << d[b].I << ", C " << d[a].C << " / " << d[b].C);
                CHECK(s.distinct);
            }
        }
    }
}

// =============================================================================
// SC-002: the SHAPE sweep is continuous; equal loudness (FR-056, C-6)
// =============================================================================

TEST_CASE("ShapeMorph_SweepContinuitySmoke", "[systems][profundum][shape-character][shape-morph]") {
    const double f0 = SCT::kE1Hz;
    const std::vector<float> in = SCT::fixture(SpectralShapeRecipe::kSawAnchor, f0, kSmspFs, kSmspShortSeconds);
    const double levelIn = smspLevelDb(in, kSmspFs, f0);
    std::ostringstream levels;
    levels << "kSawAnchor E1, L_in = " << levelIn << " dBFS; L_out per SHAPE:";
    for (int p = 0; p <= 10; ++p) {
        const float shape = static_cast<float>(p) / 10.0f;
        const SmspRender r = smspRender(in, kSmspFs, f0, shape);
        bool allFinite = true;
        float peak = 0.0f;
        for (const float v : r.y) {
            if (!Krate::DSP::detail::isFinite(v))
                allFinite = false;
            else
                peak = std::max(peak, std::abs(v));
        }
        const double level = smspLevelDb(r.y, kSmspFs, f0);
        levels << ' ' << shape << ": " << level << ';';
        INFO("SHAPE " << shape << ": peak " << peak << ", preCeilingPeak " << r.preCeilingPeak << ", L = " << level
                      << " dBFS, max running regimes " << r.maxRegimes);
        CHECK(allFinite);
        CHECK(peak <= 6.0f);
        CHECK(r.maxRegimes <= 2);
        CHECK(r.stateFinite);
        CHECK(Krate::DSP::detail::isFinite(level));
    }
    INFO(levels.str());
    SUCCEED();
}

TEST_CASE("ShapeMorph_SweepContinuity", "[systems][profundum][shape-character][shape-morph][long]") {
    // SC-002 under C-6: per-step bars on kSawAnchor; +-2.5 dB of the sweep mean on kSawAnchor,
    // kTriangleAnchor and kSineAnchor at each note; kGrowl's swing recorded (plan S1 C-6).
    constexpr int kPositions = 101;
    constexpr double kStepLevelDb = 0.5;
    constexpr double kStepCentroidOct = 0.1;
    constexpr double kStepEnergyDb = 1.0;
    constexpr double kLoudnessBandDb = 2.5;
    const std::array<SmspNote, 3> notes{{{"E1", SCT::kE1Hz}, {"C2", SCT::kC2Hz}, {"C3", SCT::kC3Hz}}};

    const std::string dir = smspEvidenceDir();
    std::filesystem::create_directories(dir);
    std::ofstream csv(dir + "sc002-growl.csv");
    REQUIRE(csv.is_open());
    csv << "note,shape,L_out_dbfs,L_in_dbfs,L_out_minus_in_db,L_rel_sweep_mean_db\n";

    for (const SmspNote& note : notes) {
        for (const SmspFixture& fx : {kSmspSaw, kSmspTriangle, kSmspSine, kSmspGrowl}) {
            const std::vector<float> in = SCT::fixture(fx.controls, note.hz, kSmspFs, kSmspSeconds);
            const double levelIn = smspLevelDb(in, kSmspFs, note.hz);
            std::vector<SCT::Descriptors> d;
            d.reserve(static_cast<std::size_t>(kPositions));
            for (int p = 0; p < kPositions; ++p) {
                const float shape = static_cast<float>(p) / 100.0f;
                const SmspRender r = smspRender(in, kSmspFs, note.hz, shape);
                if (r.maxRegimes > 2 || !r.stateFinite) {
                    FAIL_CHECK(fx.name << ", " << note.name << ", SHAPE " << shape << ": max running regimes "
                                       << r.maxRegimes << ", stateFinite " << r.stateFinite);
                }
                const std::vector<float> t = smspTail(r.y, kSmspFs);
                d.push_back(SCT::describe(t.data(), t.size(), kSmspFs, note.hz));
            }

            double mean = 0.0;
            for (const SCT::Descriptors& x : d)
                mean += x.L;
            mean /= static_cast<double>(kPositions);
            double lo = d[0].L;
            double hi = d[0].L;
            for (const SCT::Descriptors& x : d) {
                lo = std::min(lo, x.L);
                hi = std::max(hi, x.L);
            }

            const bool isSaw = fx.name == kSmspSaw.name;
            const bool isGrowl = fx.name == kSmspGrowl.name;
            if (isSaw) {
                double worstL = 0.0;
                double worstC = 0.0;
                double worstLow = 0.0;
                double worstHigh = 0.0;
                for (int p = 1; p < kPositions; ++p) {
                    const SCT::Descriptors& a = d[static_cast<std::size_t>(p - 1)];
                    const SCT::Descriptors& b = d[static_cast<std::size_t>(p)];
                    const double dL = std::abs(b.L - a.L);
                    const double dC = std::abs(b.C - a.C);
                    const double dLow = std::abs(b.Elow - a.Elow);
                    const double dHigh = std::abs(b.Ehigh - a.Ehigh);
                    worstL = std::max(worstL, dL);
                    worstC = std::max(worstC, dC);
                    worstLow = std::max(worstLow, dLow);
                    worstHigh = std::max(worstHigh, dHigh);
                    if (dL > kStepLevelDb || dC > kStepCentroidOct || dLow > kStepEnergyDb || dHigh > kStepEnergyDb) {
                        FAIL_CHECK("SC-002 step " << (p - 1) << " -> " << p << ", kSawAnchor " << note.name
                                                  << ": |dL| = " << dL << " dB, |dC| = " << dC << " oct, |dElow| = "
                                                  << dLow << " dB, |dEhigh| = " << dHigh << " dB");
                    }
                }
                INFO("kSawAnchor " << note.name << " worst steps: |dL| " << worstL << " dB, |dC| " << worstC
                                   << " oct, |dElow| " << worstLow << " dB, |dEhigh| " << worstHigh << " dB");
                CHECK(worstL <= kStepLevelDb);
                CHECK(worstC <= kStepCentroidOct);
                CHECK(worstLow <= kStepEnergyDb);
                CHECK(worstHigh <= kStepEnergyDb);
            }

            if (isGrowl) {
                // Recorded, not gated (C-6).
                for (int p = 0; p < kPositions; ++p) {
                    const double L = d[static_cast<std::size_t>(p)].L;
                    csv << note.name << ',' << static_cast<double>(p) / 100.0 << ',' << L << ',' << levelIn << ','
                        << (L - levelIn) << ',' << (L - mean) << '\n';
                }
                csv << note.name << ",swing," << (lo - mean) << ',' << (hi - mean) << ',' << (hi - lo) << ",\n";
            } else {
                INFO(fx.name << ", " << note.name << ": L_in " << levelIn << " dBFS, sweep mean " << mean
                             << " dBFS, min " << (lo - mean) << " dB, max " << (hi - mean) << " dB re mean");
                CHECK(lo - mean >= -kLoudnessBandDb);
                CHECK(hi - mean <= kLoudnessBandDb);
            }
        }
    }
}

// =============================================================================
// SC-004: Shape keytracks — |H(n)| at C1 and C3 within 1.5 dB
// =============================================================================

TEST_CASE("ShapeMorph_HarmonicResponseTracksKeyboard", "[systems][profundum][shape-character][shape-morph]") {
    constexpr int kHarmonics = 16;
    constexpr double kBarDb = 1.5;
    const std::array<SmspNote, 2> notes{{{"C1", SCT::kC1Hz}, {"C3", SCT::kC3Hz}}};
    std::array<std::vector<float>, 2> inputs;
    for (std::size_t k = 0; k < notes.size(); ++k)
        inputs[k] = SCT::fixture(SpectralShapeRecipe::kSawAnchor, notes[k].hz, kSmspFs, kSmspSeconds);

    for (const float shape : kSmspShapeGrid) {
        std::array<std::vector<double>, 2> h;
        for (std::size_t k = 0; k < notes.size(); ++k) {
            const double f0 = notes[k].hz;
            const SmspRender r = smspRender(inputs[k], kSmspFs, f0, shape);
            REQUIRE(r.stateFinite);
            const std::vector<float>& in = inputs[k];
            REQUIRE(in.size() >= kSmspFrame + static_cast<std::size_t>(kSmspFs));
            const std::vector<double> wIn =
                HFT::renderedHarmonicPowers(in.data() + (in.size() - kSmspFrame), kSmspFs, f0, kHarmonics);
            const std::vector<double> wOut =
                HFT::renderedHarmonicPowers(r.y.data() + (r.y.size() - kSmspFrame), kSmspFs, f0, kHarmonics);
            h[k].assign(static_cast<std::size_t>(kHarmonics), 0.0);
            for (int n = 1; n <= kHarmonics; ++n) {
                const auto i = static_cast<std::size_t>(n - 1);
                REQUIRE(wIn[i] > 0.0);
                h[k][i] = HFT::safeDb10(wOut[i] / wIn[i]);
            }
        }
        for (int n = 1; n <= kHarmonics; ++n) {
            const double nd = static_cast<double>(n);
            if (nd * notes[0].hz >= 0.45 * kSmspFs || nd * notes[1].hz >= 0.45 * kSmspFs)
                continue;
            const auto i = static_cast<std::size_t>(n - 1);
            const double dH = std::abs(h[0][i] - h[1][i]);
            INFO("SHAPE " << shape << ", h" << n << ": |H| C1 = " << h[0][i] << " dB, C3 = " << h[1][i]
                          << " dB, |dH| = " << dH << " dB");
            CHECK(dH <= kBarDb);
        }
    }
}

// =============================================================================
// SC-010 (Shape arm): aliasing
// =============================================================================

TEST_CASE("ShapeMorph_AliasingSmoke", "[systems][profundum][shape-character][shape-morph]") {
    // Model: SHAPE 1, Fold-100 kGrowl, E2 / 44.1 kHz = -89.9 dBFS (plan S10.3b). SHAPE 0 has no model figure.
    constexpr double fs = 44100.0;
    const double f0 = SCT::kE2Hz;
    const std::vector<float> in = smspInput(kSmspGrowl, f0, fs, smspAliasSeconds(fs, f0), true);
    for (const float shape : {1.0f, 0.0f}) {
        const SmspRender r = smspRender(in, fs, f0, shape);
        const SmspAliasCell c = smspAliasCell(in, r.y, fs, f0);
        INFO("SHAPE " << shape << ", Fold-100 kGrowl, E2, 44.1 kHz: A = " << c.aOut << " dBFS, A_in = " << c.aIn
                      << ", G = " << c.g << ", A_added = " << c.aAdded);
        CHECK(c.aOut <= kSmspAliasBarDbfs);
        CHECK(r.stateFinite);
    }
}

TEST_CASE("ShapeMorph_AliasingE0E2", "[systems][profundum][shape-character][shape-morph][long]") {
    const std::string dir = smspEvidenceDir();
    std::filesystem::create_directories(dir);
    std::ofstream csv(dir + "sc010-shape.csv");
    REQUIRE(csv.is_open());
    csv << "shape,note,fs,fixture,variant,A_out_dbfs,A_in_dbfs,G_h1_power,A_added_dbfs,pass\n";

    const std::array<SmspNote, 3> notes{{{"E0", SCT::kE0Hz}, {"E1", SCT::kE1Hz}, {"E2", SCT::kE2Hz}}};
    constexpr std::array<double, 3> kRates{44100.0, 48000.0, 96000.0};
    double worst = -400.0;
    for (const SmspNote& note : notes) {
        const double f0 = note.hz;
        for (const double fs : kRates) {
            const double seconds = smspAliasSeconds(fs, f0);
            for (const SmspFixture& fx : {kSmspSaw, kSmspGrowl}) {
                const std::vector<float> raw = smspInput(fx, f0, fs, seconds, false);
                for (const bool fold : {false, true}) {
                    const std::vector<float> in = fold ? SCT::fold100(raw, fs, f0) : raw;
                    for (const float shape : kSmspShapeGrid) {
                        const SmspRender r = smspRender(in, fs, f0, shape);
                        const SmspAliasCell c = smspAliasCell(in, r.y, fs, f0);
                        const bool pass = c.aOut <= kSmspAliasBarDbfs;
                        worst = std::max(worst, c.aOut);
                        csv << shape << ',' << note.name << ',' << fs << ',' << fx.name << ','
                            << (fold ? "Fold-100" : "raw") << ',' << c.aOut << ',' << c.aIn << ',' << c.g << ','
                            << c.aAdded << ',' << (pass ? 1 : 0) << '\n';
                        if (!pass) {
                            if (fold && c.aAdded <= kSmspAliasBarDbfs) {
                                // Never passed silently (SC-010 attribution, FR-070).
                                FAIL_CHECK("FR-070 candidate: input residual - SHAPE "
                                           << shape << ", " << note.name << ", " << fs << " Hz, " << fx.name
                                           << ", Fold-100: A = " << c.aOut << " dBFS, A_in = " << c.aIn
                                           << ", G = " << c.g << ", A_added = " << c.aAdded);
                            } else {
                                FAIL_CHECK("SC-010 Shape cell above -80 dBFS - SHAPE "
                                           << shape << ", " << note.name << ", " << fs << " Hz, " << fx.name << ", "
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
    CHECK(worst <= kSmspAliasBarDbfs);
}

// =============================================================================
// SC-011 (Shape arm): DC
// =============================================================================

TEST_CASE("ShapeMorph_DcBound", "[systems][profundum][shape-character][shape-morph]") {
    const std::array<SmspNote, 3> notes{{{"E0", SCT::kE0Hz}, {"E1", SCT::kE1Hz}, {"E2", SCT::kE2Hz}}};
    for (const SmspNote& note : notes) {
        for (const SmspFixture& fx : {kSmspSaw, kSmspGrowl}) {
            const std::vector<float> raw = smspInput(fx, note.hz, kSmspFs, kSmspShortSeconds, false);
            for (const bool fold : {false, true}) {
                const std::vector<float> in = fold ? SCT::fold100(raw, kSmspFs, note.hz) : raw;
                for (const float shape : kSmspShapeGrid) {
                    const SmspRender r = smspRender(in, kSmspFs, note.hz, shape);
                    const double dcDb = smspDcDb(r.y, kSmspFs, note.hz);
                    INFO("SHAPE " << shape << ", " << note.name << ", " << fx.name << ", "
                                  << (fold ? "Fold-100" : "raw") << ": DC = " << dcDb << " dBFS");
                    CHECK(dcDb <= kSmspDcBarDbfs);
                }
            }
        }
    }
}

// =============================================================================
// SC-012(f) (Shape arm, C-3): r_ref = f̂_out / f̂_in = 1 within 1 cent; S6.5 contingency
// =============================================================================

TEST_CASE("ShapeMorph_RIsOneSmoke", "[systems][profundum][shape-character][shape-morph]") {
    const double f0 = SCT::kE1Hz;
    const std::vector<float> in = SCT::fixture(SpectralShapeRecipe::kSawAnchor, f0, kSmspFs, kSmspSeconds);
    const SmspRender r = smspRender(in, kSmspFs, f0, 0.5f);
    const SCT::ShsCandidates cIn = smspShs(in, kSmspFs, f0);
    const SCT::ShsCandidates cOut = smspShs(r.y, kSmspFs, f0);
    const double cents = SCT::centsBetween(cOut.fHat, cIn.fHat);
    INFO("SHAPE 0.5, raw kSawAnchor, E1: f_in = " << cIn.fHat << " Hz (gap " << cIn.bestMinusSecondDb
                                                  << " dB), f_out = " << cOut.fHat << " Hz (gap "
                                                  << cOut.bestMinusSecondDb << " dB), r_ref = " << cents << " cents");
    CHECK_FALSE(cIn.ambiguous);
    CHECK_FALSE(cOut.ambiguous);
    CHECK(std::abs(cents) <= kSmspCentsBar);
}

TEST_CASE("ShapeMorph_RIsOne", "[systems][profundum][shape-character][shape-morph][long]") {
    const std::string dir = smspEvidenceDir();
    std::filesystem::create_directories(dir);
    std::ofstream csv(dir + "sc012f-shape.csv");
    REQUIRE(csv.is_open());
    csv << "shape,fixture,variant,note,f0_hz,fhat_in_hz,gap_in_db,ambiguous_in,fhat_out_hz,gap_out_db,"
           "ambiguous_out,r_ref_cents,gated,pass\n";
    // C-3 extended to (f) (user ruling 2026-10-16, Clarifications "Group 10 close rulings"): a cell is gated only
    // when the input reads f0 unambiguously; kGrowl (reads 2·f0) is recorded with its readings.
    int recorded = 0;

    const std::array<SmspNote, 4> notes{
        {{"E0", SCT::kE0Hz}, {"E1", SCT::kE1Hz}, {"E2", SCT::kE2Hz}, {"C3", SCT::kC3Hz}}};
    int failing = 0;
    int cells = 0;
    for (const SmspFixture& fx : {kSmspSine, kSmspTriangle, kSmspSaw, kSmspGrowl}) {
        for (const SmspNote& note : notes) {
            const std::vector<float> raw = smspInput(fx, note.hz, kSmspFs, kSmspSeconds, false);
            for (const bool fold : {false, true}) {
                const std::vector<float> in = fold ? SCT::fold100(raw, kSmspFs, note.hz) : raw;
                const SCT::ShsCandidates cIn = smspShs(in, kSmspFs, note.hz);
                for (const float shape : kSmspShapeGrid) {
                    const SmspRender r = smspRender(in, kSmspFs, note.hz, shape);
                    const SCT::ShsCandidates cOut = smspShs(r.y, kSmspFs, note.hz);
                    const double cents = SCT::centsBetween(cOut.fHat, cIn.fHat);
                    const bool gated = cIn.readsF0;
                    const bool pass = !gated || (!cOut.ambiguous && std::abs(cents) <= kSmspCentsBar);
                    ++cells;
                    if (!gated)
                        ++recorded;
                    csv << shape << ',' << fx.name << ',' << (fold ? "Fold-100" : "raw") << ',' << note.name << ','
                        << note.hz << ',' << cIn.fHat << ',' << cIn.bestMinusSecondDb << ','
                        << (cIn.ambiguous ? 1 : 0) << ',' << cOut.fHat << ',' << cOut.bestMinusSecondDb << ','
                        << (cOut.ambiguous ? 1 : 0) << ',' << cents << ',' << (gated ? 1 : 0) << ',' << (pass ? 1 : 0)
                        << '\n';
                    if (!pass) {
                        ++failing;
                        FAIL_CHECK("S6.5 CONTINGENCY: SC-012(f) ShapeMorph cell fails (the plan returns to the "
                                   "user before any ShapeMorph observer is written) - SHAPE "
                                   << shape << ", " << fx.name << ", " << (fold ? "Fold-100" : "raw") << ", "
                                   << note.name << ": f_in = " << cIn.fHat << " Hz (gap " << cIn.bestMinusSecondDb
                                   << " dB, ambiguous " << cIn.ambiguous << "), f_out = " << cOut.fHat
                                   << " Hz (gap " << cOut.bestMinusSecondDb << " dB, ambiguous " << cOut.ambiguous
                                   << "), r_ref = " << cents << " cents");
                    }
                }
            }
        }
    }
    WARN("SC-012(f) ShapeMorph: " << cells << " cells, " << recorded << " recorded (input does not read f0; C-3), "
                                  << failing << " failing of the gated cells");
    CHECK(recorded == 40);  // the kGrowl cells: 2 variants x 4 notes x 5 SHAPE positions
    CHECK(failing == 0);
}

// =============================================================================
// FR-056 / C-6: the loudness table harvest (hidden; run by T024 at close)
// =============================================================================

TEST_CASE("ShapeMorph_LoudnessTableHarvest", "[systems][profundum][shape-character][shape-morph][.calibration]") {
    constexpr int kPositions = 101;
    const std::array<SmspNote, 3> notes{{{"E1", SCT::kE1Hz}, {"C2", SCT::kC2Hz}, {"C3", SCT::kC3Hz}}};
    std::vector<double> sumDelta(static_cast<std::size_t>(kPositions), 0.0);
    int count = 0;
    for (const SmspNote& note : notes) {
        for (const SmspFixture& fx : {kSmspSaw, kSmspTriangle, kSmspSine}) {
            const std::vector<float> in = SCT::fixture(fx.controls, note.hz, kSmspFs, kSmspSeconds);
            const double levelIn = smspLevelDb(in, kSmspFs, note.hz);
            for (int p = 0; p < kPositions; ++p) {
                const float shape = static_cast<float>(p) / 100.0f;
                const SmspRender r = smspRender(in, kSmspFs, note.hz, shape, true);   // seamLoudnessBypassed_
                REQUIRE(r.stateFinite);
                const double levelOut = smspLevelDb(r.y, kSmspFs, note.hz);
                REQUIRE(Krate::DSP::detail::isFinite(levelOut));
                sumDelta[static_cast<std::size_t>(p)] += levelOut - levelIn;
            }
            ++count;
        }
    }
    REQUIRE(count == 9);

    std::string line = "HARVEST kLoudnessTableDb {";
    for (int p = 0; p < kPositions; ++p) {
        const double v = -sumDelta[static_cast<std::size_t>(p)] / static_cast<double>(count);
        REQUIRE(Krate::DSP::detail::isFinite(v));
        std::array<char, 32> buf{};
        std::snprintf(buf.data(), buf.size(), "%.4f", v);
        if (p > 0)
            line += ", ";
        line += buf.data();
    }
    line += "}";
    std::cout << line << '\n';

    const std::string dir = smspEvidenceDir();
    std::filesystem::create_directories(dir);
    std::ofstream out(dir + "harvest-loudness.txt");
    REQUIRE(out.is_open());
    out << line << '\n';
    SUCCEED();
}
