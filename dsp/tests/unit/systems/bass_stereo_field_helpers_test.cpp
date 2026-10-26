// ==============================================================================
// Layer 3: System Tests - BassStereoField, helpers TU
//                          (Profundum Phase 7, specs/profundum-phase7-stereo-field)
// ==============================================================================
// Reference: specs/profundum-phase7-stereo-field/spec.md (descriptor block; FR-070 validation chain)
//            specs/profundum-phase7-stereo-field/plan.md (S10; C-12 chain equality, C-16 pan references)
//            specs/profundum-phase7-stereo-field/tasks.md T007 (deviations 7, 8)
//
// T007: the self-tests of bass_stereo_field_test_helpers.h.
//   BassStereoField_HelpersSelfTest     bandReading (Q, rho), the deviation-8 mask equivalence, the band mask,
//                                       widthDb, renderPanReference held == interpolated, goertzelComplex, and
//                                       restage at the field's own floor (deviation 7)
//   BassStereoField_ChainMatchesPhase6  plan S1 C-12: every Phase 7 stage off == MassDynamicsTest::renderChain
//   BassStereoField_FieldChainSmoke     the dirty arm (Tube) with the Anchor: taps finite, stage in the path,
//                                       one vector per grid point, SOLID from reset L == R
// ==============================================================================

#include "bass_stereo_field_test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/systems/bass_character_stage.h>
#include <krate/dsp/systems/bass_stereo_field.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <vector>

namespace {

using Krate::DSP::BassCharacterStage;
using Krate::DSP::BassStereoField;
namespace Sft = Krate::DSP::StereoFieldTest;

constexpr double kBsfFs = 48000.0;
constexpr double kBsfTwoPi = 6.283185307179586476925286766559;

/// amp · sin(2π·hz·i/fs + phase), rounded to float.
std::vector<float> bsfTone(std::size_t n, double fs, double hz, double amp, double phase = 0.0) {
    std::vector<float> x(n, 0.0f);
    for (std::size_t i = 0; i < n; ++i)
        x[i] = static_cast<float>(amp * std::sin(kBsfTwoPi * hz * static_cast<double>(i) / fs + phase));
    return x;
}

std::vector<float> bsfScaled(const std::vector<float>& x, float g) {
    std::vector<float> y(x.size(), 0.0f);
    for (std::size_t i = 0; i < x.size(); ++i)
        y[i] = x[i] * g;
    return y;
}

bool bsfAllFinite(const std::vector<float>& x) {
    return std::all_of(x.begin(), x.end(), [](float v) { return Krate::DSP::detail::isFinite(v); });
}

/// The dirty arm (SHAPE 1 -> Fold 100 -> character at `a`, Amount 1, Tone 0.5) on kSawAnchor with the Anchor,
/// tasks.md rule 10, at `f0` for `seconds`.
Sft::FieldChainConfig bsfDirtyAnchorConfig(BassCharacterStage::Algorithm a, double f0, double seconds) {
    Sft::FieldChainConfig cfg{};
    cfg.chain = Krate::DSP::MassDynamicsTest::ChainConfig{
        .base = Krate::DSP::SpectralShapeRecipe::kSawAnchor, .f0 = f0, .fs = kBsfFs, .seconds = seconds};
    cfg.shapeOn = true;
    cfg.shape = 1.0f;
    cfg.foldAmount = 100.0f;
    cfg.algorithm = a;
    cfg.characterAmount = 1.0f;
    cfg.characterTone = 0.5f;
    return cfg;
}

}  // namespace

// Helper self-tests (plan S10; tasks.md T007, deviations 7 and 8).
TEST_CASE("BassStereoField_HelpersSelfTest", "[systems][profundum][stereo-field][helpers]") {
    std::cout << std::setprecision(6);

    // ---- bandReading: Q and rho on analytic pairs (spec descriptor block) ----
    {
        const std::size_t n = std::size_t{1} << 16;
        const std::vector<float> x = bsfTone(n, kBsfFs, 300.0, 0.5);
        const std::vector<float> zero(n, 0.0f);

        const Sft::BandReading hard = Sft::bandReading(x.data(), zero.data(), n, kBsfFs, 200.0, 400.0);
        INFO("hard pan Q " << hard.q);
        CHECK(std::abs(hard.q - (-3.0103)) <= 0.001);
        CHECK(hard.valid);

        const std::vector<float> neg = bsfScaled(x, -1.0f);
        const Sft::BandReading anti = Sft::bandReading(x.data(), neg.data(), n, kBsfFs, 200.0, 400.0);
        INFO("anti-phase Q " << anti.q << " rho " << anti.rho);
        CHECK(anti.q <= -100.0);
        CHECK(anti.q >= Sft::kFloorDb);
        CHECK(std::abs(anti.rho - (-1.0)) <= 1e-9);

        const Sft::BandReading same = Sft::bandReading(x.data(), x.data(), n, kBsfFs, 200.0, 400.0);
        INFO("L == R Q " << same.q << " rho " << same.rho);
        CHECK(std::abs(same.q) <= 1e-9);
        CHECK(std::abs(same.rho - 1.0) <= 1e-12);

        const std::vector<float> half = bsfScaled(x, 0.5f);
        const Sft::BandReading scaled = Sft::bandReading(x.data(), half.data(), n, kBsfFs, 200.0, 400.0);
        INFO("R = 0.5 L: Q " << scaled.q << " rho " << scaled.rho);
        CHECK(std::abs(scaled.rho - 1.0) <= 1e-9);
        CHECK(std::abs(scaled.q - (-0.4576)) <= 0.001);
        std::cout << "BSF_HELPERS,bandReading,hard_q," << hard.q << ",anti_q," << anti.q << ",anti_rho," << anti.rho
                  << ",same_q," << same.q << ",same_rho," << same.rho << ",half_q," << scaled.q << ",half_rho,"
                  << scaled.rho << "\n";
    }

    // ---- deviation 8: the Parseval reading equals the zero-phase-masked time-domain reading ----
    {
        const std::size_t n = (std::size_t{1} << 16) + 1000;
        std::vector<float> L(n, 0.0f);
        std::vector<float> R(n, 0.0f);
        for (std::size_t i = 0; i < n; ++i) {
            const double t = static_cast<double>(i) / kBsfFs;
            L[i] = static_cast<float>(0.4 * std::sin(kBsfTwoPi * 60.0 * t) + 0.3 * std::sin(kBsfTwoPi * 300.0 * t));
            R[i] = static_cast<float>(0.3 * std::sin(kBsfTwoPi * 300.0 * t + 0.3));
        }
        const double fLo = 0.0;
        const double fHi = 120.0;
        const Sft::BandEnergies e = Sft::bandEnergies(L.data(), R.data(), n, kBsfFs, fLo, fHi);
        const Sft::BandReading r = Sft::bandReading(L.data(), R.data(), n, kBsfFs, fLo, fHi);
        REQUIRE(e.length == (std::size_t{1} << 16));

        // Time domain: the same taper, FFT, zero the bins outside [fLo, fHi) (and their mirrors), inverse FFT.
        const std::size_t W = e.length;
        const std::vector<double> w = Sft::hannWindow(W);
        std::vector<std::complex<double>> A(W);
        std::vector<std::complex<double>> B(W);
        for (std::size_t k = 0; k < W; ++k) {
            A[k] = std::complex<double>(static_cast<double>(L[k]) * w[k], 0.0);
            B[k] = std::complex<double>(static_cast<double>(R[k]) * w[k], 0.0);
        }
        Krate::DSP::ProfundumTest::fftDouble(A);
        Krate::DSP::ProfundumTest::fftDouble(B);
        for (std::size_t k = 0; k < W; ++k) {
            const std::size_t kk = k <= W / 2 ? k : W - k;
            const double f = static_cast<double>(kk) * kBsfFs / static_cast<double>(W);
            if (f < fLo || f >= fHi) {
                A[k] = std::complex<double>(0.0, 0.0);
                B[k] = std::complex<double>(0.0, 0.0);
            }
        }
        Krate::DSP::MassDynamicsTest::inverseFftDouble(A);
        Krate::DSP::MassDynamicsTest::inverseFftDouble(B);
        double ll = 0.0;
        double rr = 0.0;
        double lr = 0.0;
        for (std::size_t k = 0; k < W; ++k) {
            const double a = A[k].real();
            const double b = B[k].real();
            ll += a * a;
            rr += b * b;
            lr += a * b;
        }
        const double rhoT = lr / std::sqrt(ll * rr);
        const double meanT = 0.5 * (ll + rr) / e.sumW2;
        INFO("ll " << e.ll << " vs " << ll << ", rr " << e.rr << " vs " << rr << ", lr " << e.lr << " vs " << lr
                   << ", rho " << r.rho << " vs " << rhoT);
        REQUIRE(ll > 0.0);
        REQUIRE(rr > 0.0);
        CHECK(std::abs(e.ll - ll) <= 1e-9 * ll);
        CHECK(std::abs(e.rr - rr) <= 1e-9 * rr);
        CHECK(std::abs(e.lr - lr) <= 1e-9 * std::sqrt(ll * rr));
        CHECK(std::abs(r.meanEnergy - meanT) <= 1e-9 * meanT);
        CHECK(std::abs(r.rho - rhoT) <= 1e-9);
        std::cout << "BSF_HELPERS,maskEquivalence,ll_rel," << std::abs(e.ll - ll) / ll << ",rr_rel,"
                  << std::abs(e.rr - rr) / rr << ",rho_diff," << std::abs(r.rho - rhoT) << "\n";
    }

    // ---- the band mask passes an in-band tone (0.01 dB) and removes an out-of-band one (>= 120 dB) ----
    {
        const std::size_t n = std::size_t{1} << 17;
        const double amp = 0.5;
        const double ref = 0.5 * amp * amp;
        const std::vector<float> in = bsfTone(n, kBsfFs, 100.0, amp);
        const std::vector<float> out = bsfTone(n, kBsfFs, 1000.0, amp);
        const Sft::BandReading pass = Sft::bandReading(in.data(), in.data(), n, kBsfFs, 50.0, 200.0);
        const Sft::BandReading stop = Sft::bandReading(out.data(), out.data(), n, kBsfFs, 50.0, 200.0);
        const double passDb = 10.0 * std::log10(pass.meanEnergy / ref);
        const double stopDb = 10.0 * std::log10(std::max(stop.meanEnergy, 1e-300) / ref);
        INFO("in-band " << passDb << " dB, out-of-band " << stopDb << " dB");
        CHECK(std::abs(passDb) <= 0.01);
        CHECK(stopDb <= -120.0);
        std::cout << "BSF_HELPERS,bandMask,pass_db," << passDb << ",stop_db," << stopDb << "\n";
    }

    // ---- widthDb of a constant-power pan p = 0.5 (bank: angle = pi/4 + p*pi/4) ----
    {
        constexpr std::size_t kFrame = Krate::DSP::TestUtils::kLowFrequencyFftSize;
        const double f0 = 100.0;
        const int N = 8;
        const double theta = 0.25 * kBsfTwoPi * 0.5 * (1.0 + 0.5);   // pi/4 + 0.5 * pi/4
        std::vector<float> L(kFrame, 0.0f);
        std::vector<float> R(kFrame, 0.0f);
        for (std::size_t i = 0; i < kFrame; ++i) {
            double x = 0.0;
            for (int h = 1; h <= N; ++h)
                x += 0.2 / static_cast<double>(h) *
                     std::sin(kBsfTwoPi * static_cast<double>(h) * f0 * static_cast<double>(i) / kBsfFs);
            L[i] = static_cast<float>(std::cos(theta) * x);
            R[i] = static_cast<float>(std::sin(theta) * x);
        }
        const double expected = 20.0 * std::log10(std::abs(std::tan(0.25 * kBsfTwoPi * 0.5 * 0.5)));   // tan(pi p / 4)
        const std::vector<double> wdb = Sft::widthDb(L.data(), R.data(), kBsfFs, f0, N);
        REQUIRE(wdb.size() == static_cast<std::size_t>(N));
        double worst = 0.0;
        for (int h = 1; h <= N; ++h) {
            const double v = wdb[static_cast<std::size_t>(h - 1)];
            INFO("harmonic " << h << " W " << v << " expected " << expected);
            CHECK(std::abs(v - (-7.6555)) <= 0.01);
            worst = std::max(worst, std::abs(v - expected));
        }
        std::cout << "BSF_HELPERS,widthDb,expected," << expected << ",worst_abs_err," << worst << "\n";
    }

    // ---- renderPanReference: a constant vector gives held == interpolated (plan S1 C-16) ----
    {
        const std::size_t total = 9600;
        const auto f0At = [](std::size_t) { return 41.2; };
        const std::vector<Sft::FittedHarmonic> harmonics{
            {.amplitude = 0.3, .phase = 0.1}, {.amplitude = 0.2, .phase = -0.4},
            {.amplitude = 0.1, .phase = 1.2}, {.amplitude = 0.05, .phase = 2.0}};
        std::array<float, Krate::DSP::kMaxPartials> v{};
        for (std::size_t k = 1; k < harmonics.size(); ++k)
            v[k] = (k % 2 == 0) ? -0.4f : 0.4f;
        const std::vector<std::array<float, Krate::DSP::kMaxPartials>> vectors(total / Sft::kGrid, v);
        const Sft::PanReference held =
            Sft::renderPanReference(kBsfFs, total, f0At, harmonics, vectors, Sft::PanReferenceMode::Held);
        const Sft::PanReference interp =
            Sft::renderPanReference(kBsfFs, total, f0At, harmonics, vectors, Sft::PanReferenceMode::Interpolated);
        REQUIRE(held.L.size() == total);
        REQUIRE(interp.R.size() == total);
        double maxDiff = 0.0;
        double maxLR = 0.0;
        for (std::size_t i = 0; i < total; ++i) {
            maxDiff = std::max({maxDiff, std::abs(held.L[i] - interp.L[i]), std::abs(held.R[i] - interp.R[i])});
            maxLR = std::max(maxLR, std::abs(held.L[i] - held.R[i]));
        }
        INFO("held vs interpolated " << maxDiff << ", |L - R| " << maxLR);
        CHECK(maxDiff <= 1e-12);
        REQUIRE(maxLR > 1e-3);   // non-vacuity: the vector pans
        std::cout << "BSF_HELPERS,panReference,max_diff," << maxDiff << ",max_lr," << maxLR << "\n";
    }

    // ---- goertzelComplex: amplitude and sine phase of a 41.2 Hz tone over whole periods ----
    {
        const double hz = 41.2;
        const double period = kBsfFs / hz;
        std::size_t bestM = 400;
        double bestFrac = 1.0;
        for (std::size_t m = 400; m <= 1200; ++m) {
            const double s = static_cast<double>(m) * period;
            const double frac = std::abs(s - std::round(s));
            if (frac < bestFrac) {
                bestFrac = frac;
                bestM = m;
            }
        }
        const auto len = static_cast<std::size_t>(std::llround(static_cast<double>(bestM) * period));
        const std::vector<float> x = bsfTone(len, kBsfFs, hz, 0.3, 0.7);
        const Sft::ToneFit fit = Sft::goertzelComplex(x.data(), len, kBsfFs, hz);
        INFO("periods " << bestM << " (frac " << bestFrac << "), amplitude " << fit.amplitude << ", phase " << fit.phase);
        CHECK(std::abs(fit.amplitude - 0.3) <= 1e-6);
        CHECK(std::abs(fit.phase - 0.7) <= 1e-6);
        std::cout << "BSF_HELPERS,goertzelComplex,amplitude," << std::setprecision(10) << fit.amplitude << ",phase,"
                  << fit.phase << std::setprecision(6) << "\n";
    }

    // ---- deviation 7: restage at the field's own floor reproduces the chain's stage and output taps ----
    {
        const Sft::FieldChainConfig cfg =
            bsfDirtyAnchorConfig(BassCharacterStage::Algorithm::Tube, Krate::DSP::ShapeCharacterTest::kE1Hz, 1.0);
        const Sft::FieldChainTaps t = Sft::renderFieldChain(cfg);
        const Sft::FieldChainTaps r = Sft::restage(t, cfg, BassStereoField::kSideFloorGain);
        CHECK(Sft::bitEqual(r.stageOutL, t.stageOutL));
        CHECK(Sft::bitEqual(r.stageOutR, t.stageOutR));
        CHECK(Sft::bitEqual(r.outL, t.outL));
        CHECK(Sft::bitEqual(r.outR, t.outR));
        REQUIRE_FALSE(Sft::bitEqual(t.stageOutL, t.dynL));   // non-vacuity: the stage changes the signal
        // A different floor must change the stage output (the restaged field really uses floorGain).
        const Sft::FieldChainTaps lower = Sft::restage(t, cfg, 0.1f);
        CHECK_FALSE(Sft::bitEqual(lower.stageOutL, t.stageOutL));
        std::cout << "BSF_HELPERS,restage,own_floor_bit_equal,"
                  << (Sft::bitEqual(r.stageOutL, t.stageOutL) && Sft::bitEqual(r.outR, t.outR) ? 1 : 0) << "\n";
    }
}

// plan S1 C-12: with every Phase 7 stage off the chain is Phase 6's renderChain, bit for bit.
TEST_CASE("BassStereoField_ChainMatchesPhase6", "[systems][profundum][stereo-field][helpers]") {
    Sft::FieldChainConfig cfg{};
    cfg.chain = Krate::DSP::MassDynamicsTest::ChainConfig{.base = Krate::DSP::SpectralShapeRecipe::kSawAnchor,
                                                          .f0 = Krate::DSP::ShapeCharacterTest::kE1Hz,
                                                          .fs = kBsfFs,
                                                          .seconds = 2.0};
    cfg.bypassField = true;
    cfg.shapeOn = false;
    cfg.algorithm.reset();
    cfg.foldAmount.reset();
    cfg.excitation = Krate::DSP::ProfundumCore::kDefaultExcitation;

    const Sft::FieldChainTaps a = Sft::renderFieldChain(cfg);
    const Krate::DSP::MassDynamicsTest::ChainTaps b = Krate::DSP::MassDynamicsTest::renderChain(cfg.chain);

    REQUIRE(a.outL.size() == b.outL.size());
    CHECK(Sft::bitEqual(a.outL, b.outL));
    CHECK(Sft::bitEqual(a.outR, b.outR));
    CHECK(Sft::bitEqual(a.coreL, b.coreL));
    CHECK(Sft::bitEqual(a.coreR, b.coreR));
    CHECK(Sft::bitEqual(a.foldL, b.foldL));
    CHECK(Sft::bitEqual(a.foldR, b.foldR));
    CHECK(Sft::bitEqual(a.dynL, b.dynL));
    CHECK(Sft::bitEqual(a.dynR, b.dynR));
    // Non-vacuity: the chain sounds.
    double peak = 0.0;
    for (const float v : a.outL)
        peak = std::max(peak, static_cast<double>(std::abs(v)));
    REQUIRE(peak > 1e-3);
    std::cout << "BSF_HELPERS,chainMatchesPhase6,out_peak," << peak << "\n";
}

// FR-070: the validation chain with the stage in the path.
TEST_CASE("BassStereoField_FieldChainSmoke", "[systems][profundum][stereo-field][helpers]") {
    Sft::FieldChainConfig cfg =
        bsfDirtyAnchorConfig(BassCharacterStage::Algorithm::Tube, Krate::DSP::ShapeCharacterTest::kE1Hz, 2.0);
    cfg.field = BassStereoField::Controls{};   // SPREAD at the defaults
    const Sft::FieldChainTaps t = Sft::renderFieldChain(cfg);

    for (const std::vector<float>* v :
         {&t.coreL, &t.coreR, &t.foldL, &t.foldR, &t.characterL, &t.characterR, &t.anchorL, &t.anchorR, &t.sumL,
          &t.sumR, &t.dynL, &t.dynR, &t.stageOutL, &t.stageOutR, &t.dcL, &t.dcR, &t.outL, &t.outR})
        CHECK(bsfAllFinite(*v));
    CHECK_FALSE(Sft::bitEqual(t.stageOutL, t.dynL));
    CHECK_FALSE(Sft::bitEqual(t.stageOutR, t.dynR));
    const std::size_t gridPoints = (t.outL.size() + Sft::kGrid - 1) / Sft::kGrid;
    CHECK(t.vectorPerInterval.size() == gridPoints);
    CHECK(t.offsetsPerInterval.size() == gridPoints);
    CHECK(t.lawCrossoverPerInterval.size() == gridPoints);
    CHECK(t.stageCrossoverPerInterval.size() == gridPoints);
    CHECK(t.h1DeliveredPerInterval.size() == gridPoints);
    REQUIRE_FALSE(Sft::bitEqual(t.coreL, t.coreR));   // SPREAD at Spread 0.5 pans the core

    Sft::FieldChainConfig solid = cfg;
    solid.field.mode = BassStereoField::ImageMode::Solid;
    const Sft::FieldChainTaps s = Sft::renderFieldChain(solid);
    CHECK(Sft::bitEqual(s.stageOutL, s.stageOutR));
    std::cout << "BSF_HELPERS,fieldChainSmoke,grid_points," << gridPoints << ",vectors," << t.vectorPerInterval.size()
              << ",solid_stage_lr_equal," << (Sft::bitEqual(s.stageOutL, s.stageOutR) ? 1 : 0) << "\n";
}
