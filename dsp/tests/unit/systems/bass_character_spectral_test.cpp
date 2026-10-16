// ==============================================================================
// Profundum Phase 5 (specs/profundum-phase5-shape-character)
// ==============================================================================
// Will hold: SC-005, SC-006 [long], SC-007, SC-010 [long], SC-011, SC-022, SC-028
// Filled by: T008, T011, T014, T015, T016 (plan S9.1).
// T008: helper self-tests for shape_character_test_helpers.h (SC-005 floor control,
//       C-2 / C-3 SHS candidates, descriptor sanity).
// T011: Character Bite and Tube smoke cells (h1 normalisation, SC-010 worst-cell aliasing,
//       SC-011 DC, SC-007 Amount movement) and the coupled Tone tilts (FR-055, C-8):
//       BassCharacter_BiteSmoke, BassCharacter_TubeSmoke, BassCharacter_ToneTiltsCoupled.
// T014: Character Fold and Crush smoke cells (FR-021, FR-051; plan S5.4, C-12, D-6):
//       BassCharacter_FoldSmoke (c_fold lin coefficient, SC-010 worst cell, SC-011, SC-007),
//       BassCharacter_CrushSmoke (SC-010 worst cell, SC-011, SC-007, determinism from reset()).
// T015: Character Fracture (FR-021, FR-033, FR-050; plan S5.4, S5.7, C-5, C-9, C-24):
//       BassCharacter_FractureSmoke (odd-f0/2 power, SC-010 on the n·f0/2 lattice, SC-011, SC-007),
//       BassCharacter_FracturePhasor (phasor start / rate-not-phase via the law, determinism),
//       BassCharacter_FractureObserver (C-5 hysteresis r 1 <-> 0.5).
// T016: Character spectral gates (C-7, C-8, C-9): BassCharacter_AlgorithmsPairwiseDistinct (SC-005),
//       BassCharacter_ToneMonotonicCentroidSmoke / BassCharacter_ToneMonotonicCentroid [long] (SC-006),
//       BassCharacter_AmountAudible / BassCharacter_AmountTrajectory [long] (SC-007),
//       BassCharacter_AliasingE0E2Smoke / BassCharacter_AliasingE0E2 [long] (SC-010),
//       ShapeCharacter_AliasGatePositiveControl (SC-010 control), BassCharacter_DcBound (SC-011),
//       ShapeCharacter_OutputBounded (SC-022 Character arm), BassCharacter_ToneStructure (SC-028).
// ==============================================================================

#include "bass_character_stage_probe.h"
#include "shape_character_test_helpers.h"

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
#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

using namespace Krate::DSP;

namespace {

namespace Sct = Krate::DSP::ShapeCharacterTest;

constexpr double kBcsFs = 48000.0;
constexpr double kBcsSeconds = 7.0;   // 1 s settle + the 262144-sample frame (rule 9)
constexpr std::size_t kBcsFrame = TestUtils::kLowFrequencyFftSize;

/// Mean power of x over the largest integer number of f0 periods (the "total" the floor
/// control's tone level is referenced to).
double bcsMeanPower(const std::vector<float>& x, double fs, double f0) {
    const std::size_t len = HarmonicFoldTest::integerPeriodLength(x.size(), fs, f0, 1.0);
    const float* p = x.data() + (x.size() - len);
    double sum = 0.0;
    for (std::size_t i = 0; i < len; ++i)
        sum += static_cast<double>(p[i]) * static_cast<double>(p[i]);
    return sum / static_cast<double>(len);
}

/// x plus a sine at `ratio`·f0 whose power is `relDb` re the mean power of x.
std::vector<float> bcsAddTone(const std::vector<float>& x, double fs, double f0, double ratio, double relDb) {
    const double amp = std::sqrt(2.0 * bcsMeanPower(x, fs, f0) * std::pow(10.0, relDb / 10.0));
    std::vector<float> y(x.size(), 0.0f);
    for (std::size_t n = 0; n < x.size(); ++n) {
        const double tone = amp * std::sin(HarmonicFoldTest::kTwoPi * ratio * f0 * static_cast<double>(n) / fs);
        y[n] = static_cast<float>(static_cast<double>(x[n]) + tone);
    }
    return y;
}

// -----------------------------------------------------------------------------
// T011 stage-render helpers
// -----------------------------------------------------------------------------

using BcsAlgo = BassCharacterStage::Algorithm;
using BcsProbe = Krate::DSP::detail::BassCharacterStageProbe;

constexpr double kBcsSettleSeconds = 1.0;  // rule 9 settle
constexpr double kBcsBarDbfs = -80.0;      // SC-010 aliasing and SC-011 DC bar
constexpr double kBcsH1BarDb = 0.25;       // T011 (i): h1 transfer on the calibration sine

const char* bcsAlgoName(BcsAlgo a) {
    switch (a) {
        case BcsAlgo::Tube:
            return "Tube";
        case BcsAlgo::Fold:
            return "Fold";
        case BcsAlgo::Rectify:
            return "Rectify";
        case BcsAlgo::Bite:
            return "Bite";
        case BcsAlgo::Crush:
            return "Crush";
        case BcsAlgo::Fracture:
            return "Fracture";
    }
    return "?";
}

/// L == R render of `in` through `s` in `block`-sized host blocks (the last may be shorter); returns L.
std::vector<float> bcsRender(BassCharacterStage& s, const std::vector<float>& in, std::size_t block = 512) {
    std::vector<float> L = in;
    std::vector<float> R = in;
    for (std::size_t pos = 0; pos < L.size(); pos += block) {
        const std::size_t n = std::min(block, L.size() - pos);
        s.processBlock(L.data() + pos, R.data() + pos, n);
    }
    return L;
}

/// A fresh stage configured before prepare (so prepare()'s reset() leaves it steady: no crossfade,
/// smoothers snapped, f0 keyed), rendering `in`.
std::vector<float> bcsRenderCell(BcsAlgo a, float amount, float tone, const std::vector<float>& in, double fs,
                                 double f0) {
    BassCharacterStage s;
    s.setAlgorithm(a);
    s.setAmount(amount);
    s.setTone(tone);
    s.setFundamentalHz(static_cast<float>(f0));
    s.prepare(fs, 512);
    return bcsRender(s, in);
}

/// Render length for an aliasing frame: settle + the alias frame + 50 ms margin.
double bcsAliasSeconds(double fs, double f0) {
    return kBcsSettleSeconds + static_cast<double>(HarmonicFoldTest::aliasFftLength(fs, f0)) / fs + 0.05;
}

/// SC-010 A: aliasedPower on the last aliasFftLength(fs, f0) samples (after the settle), dBFS.
double bcsAliasedDbfs(const std::vector<float>& y, double fs, double f0) {
    const std::size_t L = HarmonicFoldTest::aliasFftLength(fs, f0);
    REQUIRE(y.size() >= L + static_cast<std::size_t>(fs * kBcsSettleSeconds));
    return HarmonicFoldTest::aliasedPower(y.data() + (y.size() - L), L, fs, f0).aliasedDbfs;
}

/// SC-011 DC: mean over the largest integer number of f0 periods after the settle, dBFS.
double bcsDcDbfs(const std::vector<float>& y, double fs, double f0) {
    const auto settle = static_cast<std::size_t>(fs * kBcsSettleSeconds);
    REQUIRE(y.size() > settle);
    const std::size_t len = HarmonicFoldTest::integerPeriodLength(y.size() - settle, fs, f0, 1.0);
    const float* x = y.data() + (y.size() - len);
    double sum = 0.0;
    for (std::size_t k = 0; k < len; ++k)
        sum += static_cast<double>(x[k]);
    const double mean = sum / static_cast<double>(len);
    return 20.0 * std::log10(std::max(std::abs(mean), 1.0e-300));
}

/// 20·log10(|h1_out| / |h1_in|): goertzelPhasor at f0 over the same integer-period frame (after the settle).
double bcsH1TransferDb(const std::vector<float>& in, const std::vector<float>& out, double fs, double f0) {
    REQUIRE(in.size() == out.size());
    const auto settle = static_cast<std::size_t>(fs * kBcsSettleSeconds);
    REQUIRE(out.size() > settle);
    const std::size_t len = HarmonicFoldTest::integerPeriodLength(out.size() - settle, fs, f0, 1.0);
    const std::complex<double> ho = HarmonicFoldTest::goertzelPhasor(out.data() + (out.size() - len), len, fs, f0);
    const std::complex<double> hi = HarmonicFoldTest::goertzelPhasor(in.data() + (in.size() - len), len, fs, f0);
    REQUIRE(std::abs(hi) > 0.0);
    return 20.0 * std::log10(std::max(std::abs(ho), 1.0e-300) / std::abs(hi));
}

struct BcsAliasInput {
    const char* name;
    SpectralShapeRecipe::Controls controls;
    double f0;
};

/// SC-010 smoke at Fold-100 input cells, 44.1 kHz, Amount 1, Tone ∈ {0, 1}; modelWorstAliasDbfs is
/// the plan S10.3 model value, printed only.
void bcsAliasSmoke(BcsAlgo a, const std::vector<BcsAliasInput>& inputs, double modelWorstAliasDbfs) {
    const char* name = bcsAlgoName(a);
    {
        constexpr double fs = 44100.0;
        for (const BcsAliasInput& cell : inputs) {
            const std::vector<float> raw = Sct::fixture(cell.controls, cell.f0, fs, bcsAliasSeconds(fs, cell.f0));
            const std::vector<float> in = Sct::fold100(raw, fs, cell.f0);
            const double aIn = bcsAliasedDbfs(in, fs, cell.f0);
            for (const float tone : {0.0f, 1.0f}) {
                const std::vector<float> y = bcsRenderCell(a, 1.0f, tone, in, fs, cell.f0);
                const double aOut = bcsAliasedDbfs(y, fs, cell.f0);
                INFO(name << ", " << cell.name << ", 44.1 kHz, Amount 1, Tone " << tone << ": A = " << aOut
                          << " dBFS (A_in " << aIn << ", model worst " << modelWorstAliasDbfs << ", bar "
                          << kBcsBarDbfs << ")");
                CHECK(aOut <= kBcsBarDbfs);
            }
        }
    }
}

/// SC-011 smoke: raw kSawAnchor E1, 48 kHz, Amount 1, Tone 0.5.
void bcsDcSmoke(BcsAlgo a) {
    const char* name = bcsAlgoName(a);
    {
        const double f0 = Sct::kE1Hz;
        const std::vector<float> in = Sct::fixture(SpectralShapeRecipe::kSawAnchor, f0, kBcsFs, 2.2);
        const std::vector<float> y = bcsRenderCell(a, 1.0f, 0.5f, in, kBcsFs, f0);
        const double dc = bcsDcDbfs(y, kBcsFs, f0);
        INFO(name << ", raw saw E1, Amount 1: DC = " << dc << " dBFS (bar " << kBcsBarDbfs << ")");
        CHECK(dc <= kBcsBarDbfs);
    }
}

/// SC-007 smoke: kSawAnchor E1, Tone 0.5, Amount 0 vs 1. modelMovedDb / modelMovedOct are the plan
/// S10.4 model values, printed only.
void bcsAmountSmoke(BcsAlgo a, double modelMovedDb, double modelMovedOct) {
    const char* name = bcsAlgoName(a);
    {
        const double f0 = Sct::kE1Hz;
        const std::vector<float> in = Sct::fixture(SpectralShapeRecipe::kSawAnchor, f0, kBcsFs, kBcsSeconds);
        const std::vector<float> y0 = bcsRenderCell(a, 0.0f, 0.5f, in, kBcsFs, f0);
        const std::vector<float> y1 = bcsRenderCell(a, 1.0f, 0.5f, in, kBcsFs, f0);
        const Sct::Descriptors d0 = Sct::describe(y0.data(), y0.size(), kBcsFs, f0);
        const Sct::Descriptors d1 = Sct::describe(y1.data(), y1.size(), kBcsFs, f0);
        const double moved = Sct::amountMoved(d0, d1);
        const double dC = std::abs(d1.C - d0.C);
        INFO(name << ", saw E1, Amount 0 -> 1: moved " << moved << " dB, dC " << dC << " oct (model " << modelMovedDb
                  << " dB / " << modelMovedOct << " oct; THD " << d0.Thd << " -> " << d1.Thd << ", I " << d0.I
                  << " -> " << d1.I << ", Roe " << (d0.RoeNum - d0.RoeDen) << " -> " << (d1.RoeNum - d1.RoeDen)
                  << ")");
        CHECK((moved >= Sct::kSeparationDb || dC >= Sct::kSeparationOct));
    }
}

/// T011 smoke cells for one algorithm (Bite or Tube): (i) h1 transfer, (ii) the model's worst SC-010
/// cells at 44.1 kHz, (iii) SC-011 DC, (iv) SC-007 Amount movement. modelMovedDb / modelMovedOct are
/// the plan S10.4 model values, printed only.
void bcsCharacterSmoke(BcsAlgo a, double modelWorstAliasDbfs, double modelMovedDb, double modelMovedOct) {
    const char* name = bcsAlgoName(a);

    // (i) h1 normalisation: calibrationSine(48000, C2), Amount ∈ {0.25, 1}, Tone 0.5.
    {
        const double f0 = Sct::kC2Hz;
        const std::vector<float> in = HarmonicFoldTest::calibrationSine(kBcsFs, f0, 3.0);
        for (const float amount : {0.25f, 1.0f}) {
            const std::vector<float> y = bcsRenderCell(a, amount, 0.5f, in, kBcsFs, f0);
            const double db = bcsH1TransferDb(in, y, kBcsFs, f0);
            INFO(name << " h1 transfer at Amount " << amount << ": " << db << " dB (bar ±" << kBcsH1BarDb << ")");
            CHECK(std::abs(db) <= kBcsH1BarDb);
        }
    }

    // (ii) SC-010 smoke at the model's worst cells (plan S10.3): Fold-100 kSawAnchor E0 and
    // Fold-100 kGrowl E2, 44.1 kHz, Amount 1, Tone ∈ {0, 1}.
    bcsAliasSmoke(a,
                  {
                      {.name = "Fold-100 saw E0", .controls = SpectralShapeRecipe::kSawAnchor, .f0 = Sct::kE0Hz},
                      {.name = "Fold-100 growl E2", .controls = SpectralShapeRecipe::kGrowl, .f0 = Sct::kE2Hz},
                  },
                  modelWorstAliasDbfs);

    // (iii) SC-011 smoke: raw kSawAnchor E1, 48 kHz, Amount 1, Tone 0.5.
    bcsDcSmoke(a);

    // (iv) SC-007 smoke: kSawAnchor E1, Tone 0.5, Amount 0 vs 1.
    bcsAmountSmoke(a, modelMovedDb, modelMovedOct);
}

/// T014 (i): c_fold(A) = [h1 of P·sin(θ·sin φ)/sin(min(θ, π/2))] / P with θ = 0.1 + 2.4·A (C-12),
/// by a 4096-point midpoint sum in double — independent of the stage's 256-point table.
double bcsFoldLinearCoefficient(double amount) {
    constexpr std::size_t kN = 4096;
    constexpr double kPiD = 3.14159265358979323846;
    const double theta = 0.1 + 2.4 * amount;
    const double p = static_cast<double>(HarmonicFold::kCalibrationPeak);
    double sum = 0.0;
    for (std::size_t k = 0; k < kN; ++k) {
        const double phi = 2.0 * kPiD * (static_cast<double>(k) + 0.5) / static_cast<double>(kN);
        const double sn = std::sin(phi);
        sum += (p * std::sin(theta * sn) / std::sin(std::min(theta, 0.5 * kPiD))) * sn;
    }
    return ((2.0 / static_cast<double>(kN)) * sum) / p;
}

}  // namespace

// -----------------------------------------------------------------------------
// T008 helper self-tests
// -----------------------------------------------------------------------------

// SC-005 positive control (OQ-S floor), spec literal.
TEST_CASE("SeparationBar_FloorControl", "[systems][profundum][shape-character][bass-character]") {
    const double f0 = Sct::kE1Hz;
    const std::vector<float> saw = Sct::fixture(SpectralShapeRecipe::kSawAnchor, f0, kBcsFs, kBcsSeconds);

    const std::vector<float> below = bcsAddTone(saw, kBcsFs, f0, 1.5, -70.0);
    const std::vector<float> ref = bcsAddTone(saw, kBcsFs, f0, 1.5, -130.0);
    const std::vector<float> above = bcsAddTone(saw, kBcsFs, f0, 1.5, -30.0);

    const Sct::Descriptors dBelow = Sct::describe(below.data(), below.size(), kBcsFs, f0);
    const Sct::Descriptors dRef = Sct::describe(ref.data(), ref.size(), kBcsFs, f0);
    const Sct::Descriptors dAbove = Sct::describe(above.data(), above.size(), kBcsFs, f0);

    const Sct::Separation sBelow = Sct::separate(dBelow, dRef);
    INFO("I(-70) = " << dBelow.I << " dB, I(-130) = " << dRef.I << " dB, maxDb = " << sBelow.maxDb << " ("
                     << sBelow.which << "), dC = " << sBelow.dC);
    CHECK(sBelow.distinct == false);

    const Sct::Separation sAbove = Sct::separate(dAbove, dRef);
    INFO("I(-30) = " << dAbove.I << " dB, maxDb = " << sAbove.maxDb << " (" << sAbove.which
                     << "), dC = " << sAbove.dC);
    CHECK(sAbove.distinct == true);
}

// Plan S1 C-2 table: kSineAnchor raw reads f0, S(f0/2)/S(f0) = −1.51 dB.
TEST_CASE("ShapeCharacterShs_SineReadsF0", "[systems][profundum][shape-character][bass-character]") {
    for (const double f0 : {Sct::kE1Hz, Sct::kC3Hz}) {
        const std::vector<float> x = Sct::fixture(SpectralShapeRecipe::kSineAnchor, f0, kBcsFs, kBcsSeconds);
        REQUIRE(x.size() >= kBcsFrame);
        const float* frame = x.data() + (x.size() - kBcsFrame);
        const Sct::ShsCandidates c = Sct::shsCandidates(frame, kBcsFs, f0);
        INFO("f0 = " << f0 << ", fHat = " << c.fHat << ", S(f/2) = " << c.sHalfDb << ", S(f) = " << c.sOneDb
                     << ", S(2f) = " << c.sDoubleDb << ", gap = " << c.bestMinusSecondDb);
        CHECK(c.readsF0);
        CHECK_FALSE(c.ambiguous);
        CHECK(std::abs((c.sHalfDb - c.sOneDb) - (-1.51)) <= 0.1);
        CHECK(c.fHat == ProfundumTest::anchorShsF0(frame, kBcsFs, f0));
    }
}

// Plan S10.1: full-wave kSineAnchor reads 2·f0 at +2.06 dB — unambiguous under the 1 dB rule (C-2).
TEST_CASE("ShapeCharacterShs_FullWaveSineReadsOctave", "[systems][profundum][shape-character][bass-character]") {
    const double f0 = Sct::kE1Hz;
    std::vector<float> x = Sct::fixture(SpectralShapeRecipe::kSineAnchor, f0, kBcsFs, kBcsSeconds);
    for (float& v : x)
        v = std::abs(v);
    const float* frame = x.data() + (x.size() - kBcsFrame);
    const Sct::ShsCandidates c = Sct::shsCandidates(frame, kBcsFs, f0);
    // f̂ = 2·f0, so S(2f0)/S(f0) = S(f̂)/S(f̂/2).
    const double ratioDb = c.sOneDb - c.sHalfDb;
    INFO("fHat/f0 = " << c.fHat / f0 << ", S(2f0)/S(f0) = " << ratioDb << " dB, gap = " << c.bestMinusSecondDb);
    CHECK(std::abs(Sct::centsBetween(c.fHat, 2.0 * f0)) <= 1.0);
    CHECK(std::abs(ratioDb - 2.06) <= 0.2);
    CHECK_FALSE(c.ambiguous);
    CHECK_FALSE(c.readsF0);
}

// Plan S1 C-3 premise: raw kGrowl reads 2·f0 (+3.43 dB at E1), so its cells are not gated.
TEST_CASE("ShapeCharacterShs_GrowlReadsOctave", "[systems][profundum][shape-character][bass-character]") {
    const double f0 = Sct::kE1Hz;
    const std::vector<float> x = Sct::fixture(SpectralShapeRecipe::kGrowl, f0, kBcsFs, kBcsSeconds);
    const float* frame = x.data() + (x.size() - kBcsFrame);
    const Sct::ShsCandidates c = Sct::shsCandidates(frame, kBcsFs, f0);
    INFO("fHat/f0 = " << c.fHat / f0 << ", S(f/2) = " << c.sHalfDb << ", S(f) = " << c.sOneDb
                      << ", S(2f) = " << c.sDoubleDb << ", gap = " << c.bestMinusSecondDb);
    CHECK(std::abs(Sct::centsBetween(c.fHat, 2.0 * f0)) <= 1.0);
    CHECK_FALSE(c.readsF0);
}

TEST_CASE("ShapeCharacterDescriptors_Saw", "[systems][profundum][shape-character][bass-character]") {
    const double f0 = Sct::kC2Hz;
    const std::vector<float> x = Sct::fixture(SpectralShapeRecipe::kSawAnchor, f0, kBcsFs, kBcsSeconds);
    const Sct::Descriptors d = Sct::describe(x.data(), x.size(), kBcsFs, f0);
    INFO("Thd = " << d.Thd << ", Elow = " << d.Elow << ", Ehigh = " << d.Ehigh << ", RoeNum = " << d.RoeNum
                  << ", RoeDen = " << d.RoeDen << ", I = " << d.I << ", C = " << d.C << ", L = " << d.L);
    CHECK(Krate::DSP::detail::isFinite(d.Thd));
    CHECK(Krate::DSP::detail::isFinite(d.Elow));
    CHECK(Krate::DSP::detail::isFinite(d.Ehigh));
    const Sct::Separation s = Sct::separate(d, d);
    CHECK(s.distinct == false);
}

// -----------------------------------------------------------------------------
// T011 Character Bite and Tube (FR-021, FR-055, FR-061; plan S5.4, C-8, C-10, C-11; deviation 9)
// -----------------------------------------------------------------------------

// Bite: y = P·ADAA2 diode(g·tp/P) / c_bite(A) at F·fs; model worst A −83.3 dBFS (E2 / 44.1 kHz),
// SC-007 6.27 dB (plan S10.3, S10.4).
TEST_CASE("BassCharacter_BiteSmoke", "[systems][profundum][shape-character][bass-character]") {
    bcsCharacterSmoke(BcsAlgo::Bite, -83.3, 6.27, 0.0);
}

// Tube: TubeStage at a branch-local 2x (8x total), input gain −6…+12 dB (C-10); model worst A
// −85.3 dBFS (E2 / 44.1 kHz), SC-007 4.89 dB / 0.40 oct (plan S10.3, S10.4).
TEST_CASE("BassCharacter_TubeSmoke", "[systems][profundum][shape-character][bass-character]") {
    bcsCharacterSmoke(BcsAlgo::Tube, -85.3, 4.89, 0.40);

    // Spec Edge Case: saturation pinned at 1, so the tube_stage.h bypass threshold (< 0.0001) is
    // never crossed — after prepare and after reset().
    BassCharacterStage s;
    s.setAlgorithm(BcsAlgo::Tube);
    s.setAmount(1.0f);
    s.setFundamentalHz(static_cast<float>(Sct::kC2Hz));
    s.prepare(kBcsFs, 512);
    for (const int ch : {0, 1})
        CHECK(BcsProbe::tubeSaturationAmount(s, ch) == 1.0f);

    const std::vector<float> in = HarmonicFoldTest::calibrationSine(kBcsFs, Sct::kC2Hz, 0.25);
    std::vector<float> L = in;
    std::vector<float> R = in;
    for (float& v : R)
        v *= 0.5f;  // unlinked, so both channels' branches run
    s.processBlock(L.data(), R.data(), L.size());
    s.setAmount(0.3f);
    s.reset();
    for (const int ch : {0, 1})
        CHECK(BcsProbe::tubeSaturationAmount(s, ch) == 1.0f);
}

// C-8 (FR-055): Tube and Bite Tone = pre tilt keyed at 1·f0 and post tilt at 2·f0, both following the
// channel's smoothed Tone; a running f0 change re-keys both.
TEST_CASE("BassCharacter_ToneTiltsCoupled", "[systems][profundum][shape-character][bass-character]") {
    const auto c2 = static_cast<float>(Sct::kC2Hz);
    const auto c3 = static_cast<float>(Sct::kC3Hz);
    for (const BcsAlgo a : {BcsAlgo::Tube, BcsAlgo::Bite}) {
        REQUIRE(BcsProbe::hasPreTilt(a));
        for (const float tone : {0.0f, 1.0f}) {
            BassCharacterStage s;
            s.setAlgorithm(a);
            s.setAmount(1.0f);
            s.setTone(tone);
            s.setFundamentalHz(c2);
            s.prepare(kBcsFs, 512);

            std::vector<float> L = HarmonicFoldTest::calibrationSine(kBcsFs, Sct::kC2Hz, 0.05);
            std::vector<float> R = L;
            s.processBlock(L.data(), R.data(), L.size());

            INFO(bcsAlgoName(a) << ", Tone " << tone);
            const KeytrackedTilt& pre = BcsProbe::preTilt(s, a, 0);
            const KeytrackedTilt& post = BcsProbe::postTilt(s, a, 0);
            CHECK(pre.pivotHz() == c2);
            CHECK(post.pivotHz() == 2.0f * c2);
            CHECK(pre.tone() == BcsProbe::smoothedTone(s, 0));
            CHECK(post.tone() == BcsProbe::smoothedTone(s, 0));
            CHECK(pre.tone() == tone);

            // A running f0 change re-keys both tilts (S5.3 step 1).
            s.setFundamentalHz(c3);
            std::vector<float> L2 = HarmonicFoldTest::calibrationSine(kBcsFs, Sct::kC3Hz, 0.05);
            std::vector<float> R2 = L2;
            s.processBlock(L2.data(), R2.data(), L2.size());
            CHECK(BcsProbe::preTilt(s, a, 0).pivotHz() == c3);
            CHECK(BcsProbe::postTilt(s, a, 0).pivotHz() == 2.0f * c3);
            CHECK(BcsProbe::preTilt(s, a, 0).tone() == BcsProbe::smoothedTone(s, 0));
            CHECK(BcsProbe::postTilt(s, a, 0).tone() == BcsProbe::smoothedTone(s, 0));
        }
    }
}

// -----------------------------------------------------------------------------
// T014 Character Fold and Crush (FR-021, FR-032, FR-051, FR-055; plan S5.4, C-12, D-6)
// -----------------------------------------------------------------------------

// Fold: WavefolderProcessor (Serge) at θ = 0.1 + 2.4·A (θ_max 2.5 rad, C-12), makeup P/sin(min(θ, π/2)),
// lin = c_fold(A)·u; model worst A −85.7 dBFS (Fold-100 growl E2 / 44.1 kHz), SC-007 15.9 dB (plan S10.3,
// S10.4).
TEST_CASE("BassCharacter_FoldSmoke", "[systems][profundum][shape-character][bass-character]") {
    // Plan S1 C-12: θ_max = 2.5 rad at the calibration peak.
    STATIC_REQUIRE(BassCharacterStage::kFoldThetaMin == 0.1f);
    STATIC_REQUIRE(BassCharacterStage::kFoldThetaMax == 2.5f);

    // (i) the branch's lin coefficient equals c_fold(A) (S5.4), computed here by a 4096-point midpoint sum.
    for (const float amount : {0.0f, 0.5f, 1.0f}) {
        BassCharacterStage s;
        s.setAlgorithm(BcsAlgo::Fold);
        s.setAmount(amount);
        s.setFundamentalHz(static_cast<float>(Sct::kC2Hz));
        s.prepare(kBcsFs, 512);
        REQUIRE(BcsProbe::smoothedAmount(s, 0) == amount);
        (void)BcsProbe::runBranch(s, BcsAlgo::Fold, 0, 0.0f);  // configures the branch at the smoothed Amount
        const double expected = bcsFoldLinearCoefficient(static_cast<double>(amount));
        const auto got = static_cast<double>(BcsProbe::branchLinearCoefficient(s, BcsAlgo::Fold, 0));
        INFO("Fold c_fold at Amount " << amount << ": branch " << got << ", 4096-point sum " << expected);
        CHECK(std::abs(got - expected) <= 1.0e-4);
    }

    // (ii) SC-010 smoke at the model's worst cell (plan S10.3): Fold-100 kGrowl E2, 44.1 kHz.
    bcsAliasSmoke(BcsAlgo::Fold,
                  {{.name = "Fold-100 growl E2", .controls = SpectralShapeRecipe::kGrowl, .f0 = Sct::kE2Hz}},
                  -85.7);

    // (iii) SC-011 smoke.
    bcsDcSmoke(BcsAlgo::Fold);

    // (iv) SC-007 smoke (model 15.9 dB).
    bcsAmountSmoke(BcsAlgo::Fold, 15.9, 0.0);
}

// Crush: pitch-synchronous hold (two parity-keyed engines, B-spline steps) + BitCrusher at 12 − 8·A bits,
// dither 0 (FR-051, D-6); model worst A −100.5 dBFS (Fold-100 growl E2 / 44.1 kHz), SC-007 55.7 dB (I)
// (plan S10.3, S10.4).
TEST_CASE("BassCharacter_CrushSmoke", "[systems][profundum][shape-character][bass-character]") {
    // (i) SC-010 smoke at the model's worst cell.
    bcsAliasSmoke(BcsAlgo::Crush,
                  {{.name = "Fold-100 growl E2", .controls = SpectralShapeRecipe::kGrowl, .f0 = Sct::kE2Hz}},
                  -100.5);

    // (ii) SC-011 smoke.
    bcsDcSmoke(BcsAlgo::Crush);

    // (iii) SC-007 smoke (model 55.7 dB, I).
    bcsAmountSmoke(BcsAlgo::Crush, 55.7, 0.0);

    // (iv) noiseless and deterministic (D-6): two renders from reset() are bit-identical (dither 0,
    // no RNG used), at a fractional (0.6) and the full (1) Amount.
    const double f0 = Sct::kE1Hz;
    const std::vector<float> in = Sct::fixture(SpectralShapeRecipe::kSawAnchor, f0, kBcsFs, 0.5);
    for (const float amount : {0.6f, 1.0f}) {
        BassCharacterStage s;
        s.setAlgorithm(BcsAlgo::Crush);
        s.setAmount(amount);
        s.setFundamentalHz(static_cast<float>(f0));
        s.prepare(kBcsFs, 512);
        const std::vector<float> y1 = bcsRender(s, in);
        s.reset();
        const std::vector<float> y2 = bcsRender(s, in);
        INFO("Crush at Amount " << amount << ": two renders from reset()");
        REQUIRE(y1.size() == y2.size());
        CHECK(std::memcmp(y1.data(), y2.data(), y1.size() * sizeof(float)) == 0);
        // Not vacuous: the stage processed (output differs from the input).
        CHECK(std::memcmp(y1.data(), in.data(), in.size() * sizeof(float)) != 0);
    }
}

// -----------------------------------------------------------------------------
// T015 Character Fracture (FR-021, FR-033, FR-050; plan S5.4 Fracture, S5.7, C-5, C-9, C-24)
// -----------------------------------------------------------------------------

namespace {

/// Power at the odd multiples of f0/2 re total (dB): BH7 bins within ±8 of every (2k+1)·f0/2 < fs/2,
/// on the last aliasFftLength(fs, f0/2) samples of y (SC-024(b) measure; T015 (i)).
double bcsOddHalfLatticeDb(const std::vector<float>& y, double fs, double f0) {
    const double half = 0.5 * f0;
    const std::size_t L = HarmonicFoldTest::aliasFftLength(fs, half);
    REQUIRE(y.size() >= L);
    const std::vector<double> p = HarmonicFoldTest::bh7PowerSpectrum(y.data() + (y.size() - L), L);
    const double binHz = fs / static_cast<double>(L);
    const std::size_t top = L / 2;
    std::vector<char> inLattice(top + 1, 0);
    for (int k = 0; (2.0 * static_cast<double>(k) + 1.0) * half < 0.5 * fs; ++k) {
        const double centre = (2.0 * static_cast<double>(k) + 1.0) * half / binHz;
        const double lo = std::max(0.0, std::ceil(centre - 8.0));
        const double hi = std::min(static_cast<double>(top), std::floor(centre + 8.0));
        for (auto b = static_cast<std::size_t>(lo); static_cast<double>(b) <= hi; ++b)
            inLattice[b] = 1;
    }
    double odd = 0.0;
    double total = 0.0;
    for (std::size_t b = 0; b <= top; ++b) {
        total += p[b];
        if (inLattice[b] != 0)
            odd += p[b];
    }
    REQUIRE(total > 0.0);
    return HarmonicFoldTest::safeDb10(odd / total);
}

/// ln cosh in double (the TanhADAA antiderivative, overflow-safe form).
double bcsLnCosh(double x) {
    const double ax = std::abs(x);
    return ax - 0.69314718055994530942 + std::log1p(std::exp(-2.0 * ax));
}

/// Plan S5.4 Fracture law (C-5) in double with std::sin / std::tanh and an exact double ADAA1
/// (midpoint fallback below 1e-5, first sample naive), at F·fs. u[n] is the F-rate input; the f0 step
/// to f0b happens before sample nStep. resetPhaseAtStep is the negative control (a phasor that jumps
/// to 0 at the step). Returns y BEFORE the post tilt (what the probe's runBranch returns).
std::vector<double> bcsFractureReference(const std::vector<float>& u, double rate, float amount, float f0a, float f0b,
                                         std::size_t nStep, bool resetPhaseAtStep) {
    constexpr double kPiD = 3.14159265358979323846;
    const auto p = static_cast<double>(HarmonicFold::kCalibrationPeak);
    const auto a = static_cast<double>(amount);
    const double g = 0.5 + 1.5 * a;
    const double beta = 0.5 * a;
    double phase = 0.0;
    double inc = static_cast<double>(f0a) / rate;
    double vPrev = 0.0;
    bool has = false;
    std::vector<double> y(u.size(), 0.0);
    for (std::size_t n = 0; n < u.size(); ++n) {
        if (n == nStep) {
            inc = static_cast<double>(f0b) / rate;
            if (resetPhaseAtStep)
                phase = 0.0;
        }
        const double s = std::sin(kPiD * phase);
        const double sMid = std::sin(kPiD * (phase - 0.5 * inc));
        const double sigma = std::tanh(4.0 * s) / std::tanh(4.0);
        const double v = g * (static_cast<double>(u[n]) * ((1.0 - a) + a * sigma) / p + beta * s);
        double t = 0.0;
        if (!has) {
            has = true;
            t = std::tanh(v);
        } else {
            const double dv = v - vPrev;
            t = (std::abs(dv) < 1.0e-5) ? std::tanh(0.5 * (v + vPrev)) : (bcsLnCosh(v) - bcsLnCosh(vPrev)) / dv;
        }
        vPrev = v;
        y[n] = (p / g) * (t - std::tanh(g * beta * sMid));
        phase += inc;
        if (phase >= 2.0)
            phase -= 2.0;
    }
    return y;
}

/// c_frac(A) = the h1 coefficient of (P/g)·tanh(g·sin φ) re P, g = 0.5 + 1.5·A, by a 4096-point
/// midpoint sum in double — independent of the stage's 256-point table.
double bcsFractureLinearCoefficient(double amount) {
    constexpr std::size_t kN = 4096;
    constexpr double kPiD = 3.14159265358979323846;
    const double g = 0.5 + 1.5 * amount;
    double sum = 0.0;
    for (std::size_t k = 0; k < kN; ++k) {
        const double phi = 2.0 * kPiD * (static_cast<double>(k) + 0.5) / static_cast<double>(kN);
        const double sn = std::sin(phi);
        sum += (std::tanh(g * sn) / g) * sn;
    }
    return (2.0 / static_cast<double>(kN)) * sum;
}

}  // namespace

// Fracture: C-5 combined law (polarity alternation + bias-flip tanh on the ÷2 phasor, TanhADAA);
// model: odd-f0/2 power 0.0 dB re total at Amount 1 (SC-024(b)), worst A −99.3 dBFS on the n·f0/2
// lattice (Fold-100 growl E2 / 44.1 kHz, C-9), SC-007 60 dB (I) (plan S10.2–S10.4).
TEST_CASE("BassCharacter_FractureSmoke", "[systems][profundum][shape-character][bass-character]") {
    // (i) SC-024(b) preview: Fold-100 kSawAnchor E1, 48 kHz, Amount 1, Tone 0.5 -> the power at the odd
    // multiples of f0/2 is >= −40 dB re total.
    {
        const double f0 = Sct::kE1Hz;
        const double seconds =
            kBcsSettleSeconds + static_cast<double>(HarmonicFoldTest::aliasFftLength(kBcsFs, 0.5 * f0)) / kBcsFs + 0.05;
        const std::vector<float> raw = Sct::fixture(SpectralShapeRecipe::kSawAnchor, f0, kBcsFs, seconds);
        const std::vector<float> in = Sct::fold100(raw, kBcsFs, f0);
        const std::vector<float> y = bcsRenderCell(BcsAlgo::Fracture, 1.0f, 0.5f, in, kBcsFs, f0);
        const double oddOut = bcsOddHalfLatticeDb(y, kBcsFs, f0);
        const double oddIn = bcsOddHalfLatticeDb(in, kBcsFs, f0);
        INFO("Fracture, Fold-100 saw E1, Amount 1, Tone 0.5: odd-f0/2 power " << oddOut << " dB re total (input "
                                                                             << oddIn << " dB; model 0.0, bar -40)");
        CHECK(oddOut >= -40.0);
    }

    // (ii) SC-010 smoke on the n·f0/2 lattice (C-9): Fold-100 kGrowl E2, 44.1 kHz, Amount 1, Tone ∈ {0, 1}.
    {
        constexpr double fs = 44100.0;
        const double f0 = Sct::kE2Hz;
        const double half = 0.5 * f0;
        const std::size_t L = HarmonicFoldTest::aliasFftLength(fs, half);
        const double seconds = kBcsSettleSeconds + static_cast<double>(L) / fs + 0.05;
        const std::vector<float> raw = Sct::fixture(SpectralShapeRecipe::kGrowl, f0, fs, seconds);
        const std::vector<float> in = Sct::fold100(raw, fs, f0);
        REQUIRE(in.size() >= L + static_cast<std::size_t>(fs * kBcsSettleSeconds));
        const double aIn = HarmonicFoldTest::aliasedPower(in.data() + (in.size() - L), L, fs, half).aliasedDbfs;
        for (const float tone : {0.0f, 1.0f}) {
            const std::vector<float> y = bcsRenderCell(BcsAlgo::Fracture, 1.0f, tone, in, fs, f0);
            const double aHalf = HarmonicFoldTest::aliasedPower(y.data() + (y.size() - L), L, fs, half).aliasedDbfs;
            const double aFull = bcsAliasedDbfs(y, fs, f0);  // the n·f0 mask: recorded, not gated (C-9)
            INFO("Fracture, Fold-100 growl E2, 44.1 kHz, Amount 1, Tone " << tone << ": A(n·f0/2) = " << aHalf
                                                                         << " dBFS (A_in " << aIn
                                                                         << ", model worst -99.3, bar " << kBcsBarDbfs
                                                                         << "); n·f0 mask value " << aFull
                                                                         << " dBFS (recorded)");
            CHECK(aHalf <= kBcsBarDbfs);
        }
    }

    // (iii) SC-011 smoke: raw kSawAnchor E1, 48 kHz, Amount 1, Tone 0.5. The output's period is 2/f0, so
    // the mean runs over an integer number of f0/2 periods (an odd count of f0 periods would leave a
    // fraction of the f0/2 subharmonic in the mean).
    {
        const double f0 = Sct::kE1Hz;
        const std::vector<float> in = Sct::fixture(SpectralShapeRecipe::kSawAnchor, f0, kBcsFs, 2.2);
        const std::vector<float> y = bcsRenderCell(BcsAlgo::Fracture, 1.0f, 0.5f, in, kBcsFs, f0);
        const double dc = bcsDcDbfs(y, kBcsFs, 0.5 * f0);
        INFO("Fracture, raw saw E1, Amount 1: DC = " << dc << " dBFS over whole f0/2 periods (bar " << kBcsBarDbfs
                                                     << ")");
        CHECK(dc <= kBcsBarDbfs);
    }

    // (iv) SC-007 smoke: kSawAnchor E1, Tone 0.5, Amount 0 vs 1 (model 60 dB, I).
    bcsAmountSmoke(BcsAlgo::Fracture, 60.0, 0.0);
}

// FR-050 Edge Cases + C-24: the ÷2 phasor starts at 0 after reset(), and an f0 step moves its rate,
// never its phase. φ is read directly through the probe's fracturePhase (added by T022 for the T015
// DEFECT line): it starts at 0 and every step is in [0, C3/(F·fs) + 1e-6]. It is also verified
// through the law: the branch output (probe runBranch, y before the post tilt) must match the plan
// S5.4 law evaluated in double with a phasor that starts at 0 and is continuous across the E1 -> C3
// step (|φ[n] − φ[n−1]| ≤ C3/(F·fs) by construction), and must NOT match the same law with a phasor
// that jumps to 0 at the step (negative control). Tolerances from a float-rounded model of this
// branch (T015 run log; TanhADAA in float, C-24 rotor and rational tanh): RMS error 2–5e-5 (isolated
// TanhADAA cancellation spikes up to 1.7e-3), so the RMS bar is 5e-4; the control's RMS error is
// 0.05–0.12.
TEST_CASE("BassCharacter_FracturePhasor", "[systems][profundum][shape-character][bass-character]") {
    constexpr double kRmsBar = 5.0e-4;
    constexpr double kControlMin = 1.0e-2;
    constexpr std::size_t kN1 = 3000;  // F-samples at E1 before the step (φ ≈ 0.64 at 48 kHz, F = 4)
    constexpr std::size_t kN2 = 3000;  // F-samples at C3 after it
    const auto e1 = static_cast<float>(Sct::kE1Hz);
    const auto c3 = static_cast<float>(Sct::kC3Hz);
    const auto p = static_cast<double>(HarmonicFold::kCalibrationPeak);

    for (const float amount : {0.25f, 0.75f, 1.0f}) {  // table grid points (A·64 integral): c_lin is exact
        BassCharacterStage s;
        s.setAlgorithm(BcsAlgo::Fracture);
        s.setAmount(amount);
        s.setTone(0.5f);
        s.setFundamentalHz(e1);
        s.prepare(kBcsFs, 512);
        REQUIRE(BcsProbe::smoothedAmount(s, 0) == amount);
        REQUIRE(BcsProbe::current(s) == BcsAlgo::Fracture);
        REQUIRE_FALSE(BcsProbe::bypassed(s));  // so setFundamentalHz pushes to the running branch
        const double rate = kBcsFs * static_cast<double>(s.oversamplingFactor());

        // F-rate input: 0.8·P sine following E1, then C3, phase-continuous.
        std::vector<float> u(kN1 + kN2, 0.0f);
        double ph = 0.0;
        for (std::size_t n = 0; n < u.size(); ++n) {
            u[n] = static_cast<float>(0.8 * p * std::sin(HarmonicFoldTest::kTwoPi * ph));
            ph += static_cast<double>(n < kN1 ? e1 : c3) / rate;
        }

        // phi[n] = φ the n-th F-sample uses (probe fracturePhase); phi[u.size()] = φ after the last one.
        std::vector<double> phi(u.size() + 1, 0.0);
        const auto render = [&]() {
            std::vector<BassCharacterStage::BranchOut> out(u.size(), BassCharacterStage::BranchOut{0.0f, 0.0f});
            for (std::size_t n = 0; n < u.size(); ++n) {
                if (n == kN1)
                    s.setFundamentalHz(c3);
                phi[n] = BcsProbe::fracturePhase(s, 0);
                out[n] = BcsProbe::runBranch(s, BcsAlgo::Fracture, 0, u[n]);
            }
            phi[u.size()] = BcsProbe::fracturePhase(s, 0);
            return out;
        };
        // The literal phasor check (tasks.md T015): φ starts at 0 after reset(), stays in [0, 2), and every
        // per-sample step (wrapped at 2) is in [0, C3/(F·fs) + 1e-6] — so the E1 -> C3 re-key moves the
        // increment, never the value.
        const double incBound = static_cast<double>(c3) / rate + 1.0e-6;
        const auto checkPhasor = [&](const char* which) {
            double maxStep = 0.0;
            double minStep = 2.0;
            bool inRange = true;
            for (std::size_t n = 0; n < phi.size(); ++n) {
                inRange = inRange && phi[n] >= 0.0 && phi[n] < 2.0;
                if (n == 0)
                    continue;
                double d = phi[n] - phi[n - 1];
                if (d < 0.0)
                    d += 2.0;
                maxStep = std::max(maxStep, d);
                minStep = std::min(minStep, d);
            }
            INFO("Fracture phasor (" << which << ") at Amount " << amount << ": phi[0] " << phi[0] << ", step range ["
                                     << minStep << ", " << maxStep << "] (bound " << incBound << ")");
            CHECK(phi[0] == 0.0);
            CHECK(inRange);
            CHECK(minStep >= 0.0);
            CHECK(maxStep <= incBound);
        };
        const std::vector<BassCharacterStage::BranchOut> first = render();
        checkPhasor("first render");

        const std::vector<double> ref = bcsFractureReference(u, rate, amount, e1, c3, kN1, false);
        const std::vector<double> jump = bcsFractureReference(u, rate, amount, e1, c3, kN1, true);
        const auto rmsErr = [&](const std::vector<double>& r, std::size_t lo, std::size_t hi) {
            double acc = 0.0;
            for (std::size_t n = lo; n < hi; ++n) {
                const double d = static_cast<double>(first[n].y) - r[n];
                acc += d * d;
            }
            return std::sqrt(acc / static_cast<double>(hi - lo));
        };
        const double errStart = rmsErr(ref, 0, 256);  // φ starts at 0 after reset()
        const double errPre = rmsErr(ref, 0, kN1);
        const double errPost = rmsErr(ref, kN1, kN1 + kN2);
        const double errJump = rmsErr(jump, kN1, kN1 + kN2);

        // lin = (1 − a)·c_frac(A)·(u + u_prev)/2 (S5.4), c_frac by an independent 4096-point sum.
        const double cFrac = bcsFractureLinearCoefficient(static_cast<double>(amount));
        double linErr = 0.0;
        for (std::size_t n = 0; n < u.size(); ++n) {
            const double uPrev = (n == 0) ? 0.0 : static_cast<double>(u[n - 1]);
            const double expected =
                (1.0 - static_cast<double>(amount)) * cFrac * 0.5 * (static_cast<double>(u[n]) + uPrev);
            linErr = std::max(linErr, std::abs(static_cast<double>(first[n].lin) - expected));
        }
        const double linCoef = static_cast<double>(BcsProbe::branchLinearCoefficient(s, BcsAlgo::Fracture, 0));

        INFO("Fracture phasor at Amount " << amount << ": RMS err first 256 " << errStart << ", pre-step " << errPre
                                          << ", post-step " << errPost << " (bar " << kRmsBar
                                          << "); phase-jump control post-step " << errJump << " (must be >= "
                                          << kControlMin << "); max lin err " << linErr << ", c_lin " << linCoef
                                          << " vs " << (1.0 - static_cast<double>(amount)) * cFrac);
        CHECK(errStart <= kRmsBar);
        CHECK(errPre <= kRmsBar);
        CHECK(errPost <= kRmsBar);
        CHECK(errJump >= kControlMin);
        CHECK(linErr <= 1.0e-6);
        CHECK(std::abs(linCoef - (1.0 - static_cast<double>(amount)) * cFrac) <= 1.0e-6);

        // Two renders from reset() are bit-identical (φ, rotor, ADAA and u_prev all restart).
        s.setFundamentalHz(e1);
        s.reset();
        const std::vector<BassCharacterStage::BranchOut> second = render();
        checkPhasor("after reset()");
        REQUIRE(first.size() == second.size());
        bool identical = true;
        for (std::size_t n = 0; n < first.size(); ++n) {
            const bool sameY = std::bit_cast<std::uint32_t>(first[n].y) == std::bit_cast<std::uint32_t>(second[n].y);
            const bool sameLin =
                std::bit_cast<std::uint32_t>(first[n].lin) == std::bit_cast<std::uint32_t>(second[n].lin);
            identical = identical && sameY && sameLin;
        }
        CHECK(identical);
    }
}

// C-5 observer (S5.7): Fracture r = 0.5 while flipped; hysteresis edges kFractureFlipEdges at Tone 0.5.
TEST_CASE("BassCharacter_FractureObserver", "[systems][profundum][shape-character][bass-character]") {
    const BassCharacterStage::FlipEdges edges = BassCharacterStage::kFractureFlipEdges[2];  // Tone 0.5
    REQUIRE(edges.lo < edges.hi);
    constexpr int kSteps = 200;  // Amount in 0.005 steps over [0, 1]

    BassCharacterStage s;
    s.setAlgorithm(BcsAlgo::Fracture);
    s.setTone(0.5f);
    s.prepare(kBcsFs, 512);
    REQUIRE(BcsProbe::target(s) == BcsAlgo::Fracture);

    s.setAmount(0.0f);
    CHECK(s.perceivedPitchRatio() == 1.0f);

    // Up: 1 -> 0.5 exactly once, at the first Amount >= hi.
    int flipsUp = 0;
    float flipUpAt = -1.0f;
    float firstAtOrAboveHi = -1.0f;
    float prev = s.perceivedPitchRatio();
    for (int i = 0; i <= kSteps; ++i) {
        const float a = static_cast<float>(i) * 0.005f;
        s.setAmount(a);
        const float r = s.perceivedPitchRatio();
        CHECK((r == 1.0f || r == 0.5f));
        if (firstAtOrAboveHi < 0.0f && a >= edges.hi)
            firstAtOrAboveHi = a;
        if (r != prev) {
            ++flipsUp;
            if (r == 0.5f)
                flipUpAt = a;
        }
        prev = r;
    }
    INFO("up: flips " << flipsUp << " at Amount " << flipUpAt << ", first Amount >= hi (" << edges.hi << ") "
                      << firstAtOrAboveHi);
    CHECK(flipsUp == 1);
    CHECK(flipUpAt == firstAtOrAboveHi);
    CHECK(s.perceivedPitchRatio() == 0.5f);

    // Down: 0.5 -> 1 exactly once, at the first Amount <= lo.
    int flipsDown = 0;
    float flipDownAt = -1.0f;
    float firstAtOrBelowLo = -1.0f;
    prev = s.perceivedPitchRatio();
    for (int i = kSteps; i >= 0; --i) {
        const float a = static_cast<float>(i) * 0.005f;
        s.setAmount(a);
        const float r = s.perceivedPitchRatio();
        if (firstAtOrBelowLo < 0.0f && a <= edges.lo)
            firstAtOrBelowLo = a;
        if (r != prev) {
            ++flipsDown;
            if (r == 1.0f)
                flipDownAt = a;
        }
        prev = r;
    }
    INFO("down: flips " << flipsDown << " at Amount " << flipDownAt << ", first Amount <= lo (" << edges.lo << ") "
                        << firstAtOrBelowLo);
    CHECK(flipsDown == 1);
    CHECK(flipDownAt == firstAtOrBelowLo);
    CHECK(s.perceivedPitchRatio() == 1.0f);  // Amount 0

    // reset() forgets the hysteresis: flipped, then an Amount inside the band keeps 0.5 until reset() -> 1.
    s.setAmount(1.0f);
    REQUIRE(s.perceivedPitchRatio() == 0.5f);
    const float inside = 0.5f * (edges.lo + edges.hi);
    s.setAmount(inside);
    CHECK(s.perceivedPitchRatio() == 0.5f);
    s.reset();
    CHECK(s.perceivedPitchRatio() == 1.0f);
}

// -----------------------------------------------------------------------------
// T016 Character spectral gates (SC-005, SC-006, SC-007, SC-010, SC-011, SC-022, SC-028;
// plan S1 C-7, C-8, C-9)
// -----------------------------------------------------------------------------

namespace {

constexpr std::array<BcsAlgo, 6> kBcsAlgos{BcsAlgo::Tube, BcsAlgo::Fold,  BcsAlgo::Rectify,
                                           BcsAlgo::Bite, BcsAlgo::Crush, BcsAlgo::Fracture};

struct BcsNote {
    const char* name;
    double hz;
};

struct BcsFixtureRef {
    const char* name;
    SpectralShapeRecipe::Controls controls;
    bool spanGated;  ///< SC-006 span gate applies (kSawAnchor only, C-7)
};

const std::array<BcsFixtureRef, 2> kBcsSawSine{{
    {.name = "saw", .controls = SpectralShapeRecipe::kSawAnchor, .spanGated = true},
    {.name = "sine", .controls = SpectralShapeRecipe::kSineAnchor, .spanGated = false},
}};

const std::array<BcsFixtureRef, 2> kBcsSawGrowl{{
    {.name = "saw", .controls = SpectralShapeRecipe::kSawAnchor, .spanGated = true},
    {.name = "growl", .controls = SpectralShapeRecipe::kGrowl, .spanGated = false},
}};

std::string bcsEvidenceDir() {
    return std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase5-shape-character/evidence/";
}

/// Probe seams applied before prepare (each defaults to off).
struct BcsSeams {
    bool naiveBaseRate = false;
    bool preToneNeutral = false;
    bool postToneNeutral = false;
};

/// Configures `s` before prepare (so prepare()'s reset() leaves it steady), then prepares it.
void bcsSetUp(BassCharacterStage& s, BcsAlgo a, float amount, float tone, double fs, double f0,
              const BcsSeams& seams = {}) {
    s.setAlgorithm(a);
    s.setAmount(amount);
    s.setTone(tone);
    s.setFundamentalHz(static_cast<float>(f0));
    BcsProbe::setSeamNaiveBaseRate(s, seams.naiveBaseRate);
    BcsProbe::setSeamPreToneNeutral(s, seams.preToneNeutral);
    BcsProbe::setSeamPostToneNeutral(s, seams.postToneNeutral);
    s.prepare(fs, 512);
}

/// The last `need` samples of x (all of x when shorter).
std::vector<float> bcsTail(const std::vector<float>& x, std::size_t need) {
    const std::size_t n = std::min(need, x.size());
    return {x.end() - static_cast<std::ptrdiff_t>(n), x.end()};
}

/// SC-010 cell with attribution (spec SC-010 row), the mask on the `latticeHz` lattice (f0 for every
/// algorithm except Fracture, f0/2 for Fracture, C-9). G is the h1 power gain at f0 on the same frame.
/// aNf0Mask is the n·f0-mask value (recorded for Fracture, equal to aOut otherwise).
struct BcsAliasCell {
    double aOut;
    double aIn;
    double g;
    double aAdded;
    double aNf0Mask;
};

BcsAliasCell bcsAliasCell(const std::vector<float>& in, const std::vector<float>& out, double fs, double f0,
                          double latticeHz) {
    REQUIRE(in.size() == out.size());
    const std::size_t L = HarmonicFoldTest::aliasFftLength(fs, latticeHz);
    REQUIRE(out.size() >= L + static_cast<std::size_t>(fs * kBcsSettleSeconds));
    const float* yo = out.data() + (out.size() - L);
    const float* yi = in.data() + (in.size() - L);
    const HarmonicFoldTest::AliasResult ao = HarmonicFoldTest::aliasedPower(yo, L, fs, latticeHz);
    const HarmonicFoldTest::AliasResult ai = HarmonicFoldTest::aliasedPower(yi, L, fs, latticeHz);
    const std::complex<double> ho = HarmonicFoldTest::goertzelPhasor(yo, L, fs, f0);
    const std::complex<double> hi = HarmonicFoldTest::goertzelPhasor(yi, L, fs, f0);
    REQUIRE(std::norm(hi) > 0.0);
    const double g = std::norm(ho) / std::norm(hi);
    const double pOut = std::pow(10.0, ao.aliasedDbfs / 10.0);
    const double pIn = std::pow(10.0, ai.aliasedDbfs / 10.0);
    double nf0 = ao.aliasedDbfs;
    if (latticeHz != f0)
        nf0 = bcsAliasedDbfs(out, fs, f0);
    return BcsAliasCell{.aOut = ao.aliasedDbfs,
                        .aIn = ai.aliasedDbfs,
                        .g = g,
                        .aAdded = HarmonicFoldTest::safeDb10(std::max(pOut - g * pIn, 0.0)),
                        .aNf0Mask = nf0};
}

/// The SC-010 / SC-011 lattice of an algorithm (C-9; Fracture's output period is 2/f0).
double bcsLatticeHz(BcsAlgo a, double f0) { return a == BcsAlgo::Fracture ? 0.5 * f0 : f0; }

/// Render length (samples) for the SC-010 frame of algorithm a: settle + aliasFftLength on its lattice
/// + 50 ms.
std::size_t bcsAliasSamples(BcsAlgo a, double fs, double f0) {
    const double seconds = kBcsSettleSeconds
                           + static_cast<double>(HarmonicFoldTest::aliasFftLength(fs, bcsLatticeHz(a, f0))) / fs
                           + 0.05;
    return static_cast<std::size_t>(std::ceil(seconds * fs));
}

// ---- SC-006 harness ----------------------------------------------------------

constexpr std::array<std::size_t, 3> kBcsEpsBlocks{1, 64, 512};
constexpr std::size_t kBcsEpsOffsets = 8;
constexpr double kBcsTauCapOct = 0.005;       // τ = min(2ε, 0.005) (SC-006)
constexpr double kBcsEpsHarnessOct = 0.0025;  // ε above this is a harness failure, not a bar failure

/// A raw fixture long enough for an SC-006 render started at any of the 8 sub-period offsets.
struct BcsCentroidSource {
    std::vector<float> src;
    std::size_t renderLen;
};

BcsCentroidSource bcsCentroidSource(const SpectralShapeRecipe::Controls& c, double fs, double f0) {
    const double period = fs / f0;
    std::vector<float> src = Sct::fixture(c, f0, fs, bcsAliasSeconds(fs, f0) + 2.0 / f0);
    const auto spare = static_cast<std::size_t>(std::ceil(period)) + 1;
    REQUIRE(src.size() > spare);
    const std::size_t len = src.size() - spare;
    return BcsCentroidSource{.src = std::move(src), .renderLen = len};
}

/// C (octaves above f0) of one render started `offset` samples into the source, in `block`-sized blocks.
double bcsCentroidOf(BcsAlgo a, float amount, float tone, const BcsCentroidSource& s, std::size_t offset,
                     std::size_t block, double fs, double f0) {
    REQUIRE(offset + s.renderLen <= s.src.size());
    const auto first = s.src.begin() + static_cast<std::ptrdiff_t>(offset);
    const std::vector<float> in(first, first + static_cast<std::ptrdiff_t>(s.renderLen));
    BassCharacterStage st;
    bcsSetUp(st, a, amount, tone, fs, f0);
    const std::vector<float> y = bcsRender(st, in, block);
    return Sct::centroidOct(y.data(), y.size(), fs, f0);
}

struct BcsCentroidCell {
    double c;    ///< reference render: offset 0, block 512
    double eps;  ///< max − min of C over 8 sub-period window offsets × block sizes {1, 64, 512}
};

/// ε is the measurement noise floor over the WINDOW phase (SC-006 "(window phase)"): per block size the
/// whole source is rendered once from sample 0, and the 8 centroid frames end 0, period/8, …, 7·period/8
/// samples after renderLen (source spare >= period + 1). T022 harness fix: the earlier form re-rendered
/// from a source shifted by the offset, which also moved the input against the stage's phasors (Crush θ,
/// Fracture φ start at 0 on reset), so for those two algorithms ε measured the onset alignment (up to
/// 1.97 oct on Fracture, Amount 1) instead of window noise (tasks.md "Defects found", T022).
BcsCentroidCell bcsCentroidCell(BcsAlgo a, float amount, float tone, const BcsCentroidSource& s, double fs,
                                double f0) {
    const double period = fs / f0;
    double lo = 1.0e300;
    double hi = -1.0e300;
    double ref = 0.0;
    for (const std::size_t block : kBcsEpsBlocks) {
        BassCharacterStage st;
        bcsSetUp(st, a, amount, tone, fs, f0);
        const std::vector<float> y = bcsRender(st, s.src, block);
        for (std::size_t k = 0; k < kBcsEpsOffsets; ++k) {
            const auto offset = static_cast<std::size_t>(
                std::llround(static_cast<double>(k) * period / static_cast<double>(kBcsEpsOffsets)));
            REQUIRE(s.renderLen + offset <= y.size());
            const double c = Sct::centroidOct(y.data(), s.renderLen + offset, fs, f0);
            if (k == 0 && block == 512)
                ref = c;
            lo = std::min(lo, c);
            hi = std::max(hi, c);
        }
    }
    return BcsCentroidCell{.c = ref, .eps = hi - lo};
}

/// The 11 Tone steps 0, 0.1, …, 1.
float bcsToneStep(int i) { return static_cast<float>(i) / 10.0f; }

// ---- SC-028 tilt measurement ---------------------------------------------------

/// 20·log10|H(hz)| of a copy of `tilt` by a sine probe: reset, 0.3 s of a 0.5-peak sine (double phase),
/// RMS ratio over the largest integer number of periods in the last 0.2 s (the first 0.1 s settles).
double bcsTiltSineDb(KeytrackedTilt tilt, double fs, double hz) {
    tilt.reset();
    const auto n = static_cast<std::size_t>(0.3 * fs);
    const auto settle = static_cast<std::size_t>(0.1 * fs);
    std::vector<float> x(n, 0.0f);
    std::vector<float> y(n, 0.0f);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = static_cast<float>(0.5 * std::sin(HarmonicFoldTest::kTwoPi * hz * static_cast<double>(i) / fs));
        y[i] = tilt.process(x[i]);
    }
    const std::size_t len = HarmonicFoldTest::integerPeriodLength(n - settle, fs, hz, 0.1);
    double sx = 0.0;
    double sy = 0.0;
    for (std::size_t i = n - len; i < n; ++i) {
        sx += static_cast<double>(x[i]) * static_cast<double>(x[i]);
        sy += static_cast<double>(y[i]) * static_cast<double>(y[i]);
    }
    REQUIRE(sx > 0.0);
    return HarmonicFoldTest::safeDb10(sy / sx);
}

/// Impulse response of a copy of `tilt` (reset first), 16384 samples (the slowest pole, 1·C1 = 32.7 Hz
/// at 48 kHz, decays by e^-70 over that length).
std::vector<double> bcsTiltImpulse(KeytrackedTilt tilt) {
    constexpr std::size_t kLen = 16384;
    tilt.reset();
    std::vector<double> h(kLen, 0.0);
    for (std::size_t k = 0; k < kLen; ++k)
        h[k] = static_cast<double>(tilt.process(k == 0 ? 1.0f : 0.0f));
    return h;
}

/// 20·log10|H(hz)| from an impulse response (DTFT in double, rotating phasor re-anchored every 1024).
double bcsIrDb(const std::vector<double>& h, double fs, double hz) {
    const double om = HarmonicFoldTest::kTwoPi * hz / fs;
    const double cr = std::cos(om);
    const double ci = -std::sin(om);
    double pr = 1.0;
    double pi = 0.0;
    double re = 0.0;
    double im = 0.0;
    for (std::size_t k = 0; k < h.size(); ++k) {
        re += h[k] * pr;
        im += h[k] * pi;
        if (((k + 1) & 1023u) == 0) {
            const double ang = -om * static_cast<double>(k + 1);
            pr = std::cos(ang);
            pi = std::sin(ang);
        } else {
            const double t = pr * cr - pi * ci;
            pi = pr * ci + pi * cr;
            pr = t;
        }
    }
    return HarmonicFoldTest::safeDb10(re * re + im * im);
}

/// 20·log10|H(n·f0)| for n = 1..count, measured on a copy of `tilt` (0 dB where n·f0 >= fs/2).
std::vector<double> bcsTiltHarmonicDb(const KeytrackedTilt& tilt, double fs, double f0, std::size_t count) {
    const std::vector<double> h = bcsTiltImpulse(tilt);
    std::vector<double> db(count, 0.0);
    for (std::size_t i = 0; i < count; ++i) {
        const double hz = static_cast<double>(i + 1) * f0;
        if (hz < 0.5 * fs)
            db[i] = bcsIrDb(h, fs, hz);
    }
    return db;
}

/// De-tilted THD and R_oe parts (re total), as Sct::describe computes them, from w_n / |H(n·f0)|².
struct BcsThdRoe {
    double thd;
    double roeNum;
    double roeDen;
};

BcsThdRoe bcsDetiltedThdRoe(const std::vector<double>& w, const std::vector<double>& hDb) {
    REQUIRE(hDb.size() >= w.size());
    double total = 0.0;
    double harm = 0.0;
    double odd = 0.0;
    double even = 0.0;
    double w1 = 0.0;
    for (std::size_t i = 0; i < w.size(); ++i) {
        const std::size_t n = i + 1;
        const double p = w[i] / std::pow(10.0, hDb[i] / 10.0);
        total += p;
        if (n == 1) {
            w1 = p;
            continue;
        }
        harm += p;
        if ((n & 1u) != 0u)
            odd += p;
        else
            even += p;
    }
    REQUIRE(total > 0.0);
    return BcsThdRoe{.thd = HarmonicFoldTest::safeDb10(harm / std::max(w1, 1e-300)),
                     .roeNum = HarmonicFoldTest::safeDb10(odd / total),
                     .roeDen = HarmonicFoldTest::safeDb10(even / total)};
}

/// The OQ-S floor rule (Sct::amountMoved) restricted to THD and R_oe: the larger qualifying move, dB.
double bcsThdRoeMove(const BcsThdRoe& a, const BcsThdRoe& b) {
    double mv = 0.0;
    if (Sct::floorCounts(a.thd, b.thd))
        mv = std::max(mv, std::abs(Sct::floorClamp(a.thd) - Sct::floorClamp(b.thd)));
    if (Sct::floorCounts(a.roeNum, b.roeNum) || Sct::floorCounts(a.roeDen, b.roeDen))
        mv = std::max(mv, std::abs((Sct::floorClamp(a.roeNum) - Sct::floorClamp(a.roeDen))
                                   - (Sct::floorClamp(b.roeNum) - Sct::floorClamp(b.roeDen))));
    return mv;
}

constexpr double kBcsDriveSeconds = 0.1;

/// Drives `s` with kBcsDriveSeconds of a calibration sine at f0 (L == R, block 512). viaPointer: f0
/// arrives as a constant f0PerSample array (one entry per sample); otherwise the pointer is null.
void bcsDrive(BassCharacterStage& s, double fs, double f0, bool viaPointer) {
    std::vector<float> L = HarmonicFoldTest::calibrationSine(fs, f0, kBcsDriveSeconds);
    std::vector<float> R = L;
    const std::vector<float> f0s(L.size(), static_cast<float>(f0));
    for (std::size_t pos = 0; pos < L.size(); pos += 512) {
        const std::size_t n = std::min<std::size_t>(512, L.size() - pos);
        s.processBlock(L.data() + pos, R.data() + pos, n, viaPointer ? f0s.data() + pos : nullptr);
    }
}

/// SC-028(b): the running branch's tilt responses (dB) at n·f0, n = 1..16 (channel 0).
struct BcsTiltRows {
    std::vector<double> post;
    std::vector<double> pre;  ///< empty for algorithms without a pre tilt
};

BcsTiltRows bcsTiltRows(const BassCharacterStage& s, BcsAlgo a, double fs, double f0) {
    BcsTiltRows r;
    r.post = bcsTiltHarmonicDb(BcsProbe::postTilt(s, a, 0), fs, f0, 16);
    if (BcsProbe::hasPreTilt(a))
        r.pre = bcsTiltHarmonicDb(BcsProbe::preTilt(s, a, 0), fs, f0, 16);
    return r;
}

/// Largest per-harmonic |difference| (dB) over n ≤ 16 with n·f0 < 0.45·fs at both notes.
double bcsRowsMaxDiff(const std::vector<double>& a, const std::vector<double>& b, double fs, double f0a,
                      double f0b) {
    double worst = 0.0;
    for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
        const auto n = static_cast<double>(i + 1);
        if (n * f0a >= 0.45 * fs || n * f0b >= 0.45 * fs)
            break;
        worst = std::max(worst, std::abs(a[i] - b[i]));
    }
    return worst;
}

}  // namespace

// SC-005: the six algorithms are pairwise distinct (OQ-S bar) at Amount 1 / Tone 0.5, level-matched to
// the input level, on kSawAnchor and kSineAnchor at E1 and C2, 48 kHz (model weakest pair: saw Tube–Bite
// 5.62 dB R_oe, ×1.87; sine ×10.5; plan S10.4).
TEST_CASE("BassCharacter_AlgorithmsPairwiseDistinct", "[systems][profundum][shape-character][bass-character]") {
    const std::array<BcsNote, 2> notes{{{.name = "E1", .hz = Sct::kE1Hz}, {.name = "C2", .hz = Sct::kC2Hz}}};
    for (const BcsFixtureRef& fx : kBcsSawSine) {
        for (const BcsNote& note : notes) {
            const std::vector<float> in = Sct::fixture(fx.controls, note.hz, kBcsFs, kBcsSeconds);
            const double target = Sct::levelDb(in.data(), in.size(), kBcsFs, note.hz);
            std::vector<Sct::Descriptors> d;
            d.reserve(kBcsAlgos.size());
            for (const BcsAlgo a : kBcsAlgos) {
                std::vector<float> y = bcsRenderCell(a, 1.0f, 0.5f, in, kBcsFs, note.hz);
                Sct::levelMatch(y, target, kBcsFs, note.hz);
                d.push_back(Sct::describe(y.data(), y.size(), kBcsFs, note.hz));
            }
            for (std::size_t i = 0; i < kBcsAlgos.size(); ++i) {
                for (std::size_t j = i + 1; j < kBcsAlgos.size(); ++j) {
                    const Sct::Separation sep = Sct::separate(d[i], d[j]);
                    INFO(fx.name << " " << note.name << ": " << bcsAlgoName(kBcsAlgos[i]) << " vs "
                                 << bcsAlgoName(kBcsAlgos[j]) << ": maxDb " << sep.maxDb << " (" << sep.which
                                 << ", margin x" << sep.maxDb / Sct::kSeparationDb << "), dC " << sep.dC
                                 << " oct (margin x" << sep.dC / Sct::kSeparationOct << ")");
                    CHECK(sep.distinct);
                }
            }
        }
    }
}

// SC-006 smoke (C-7): Fold, kSawAnchor E1, Amount 1, Tone in 11 steps: every ΔC ≥ −0.005 oct (the τ cap)
// and span C(1) − C(0) ≥ 0.25 oct (model 0.45, plan S10.4).
TEST_CASE("BassCharacter_ToneMonotonicCentroidSmoke", "[systems][profundum][shape-character][bass-character]") {
    const double f0 = Sct::kE1Hz;
    const BcsCentroidSource src = bcsCentroidSource(SpectralShapeRecipe::kSawAnchor, kBcsFs, f0);
    std::array<double, 11> c{};
    for (int i = 0; i <= 10; ++i)
        c[static_cast<std::size_t>(i)] = bcsCentroidOf(BcsAlgo::Fold, 1.0f, bcsToneStep(i), src, 0, 512, kBcsFs, f0);
    for (std::size_t i = 0; i + 1 < c.size(); ++i) {
        INFO("Fold saw E1, Tone " << static_cast<double>(i) / 10.0 << " -> " << static_cast<double>(i + 1) / 10.0
                                  << ": C " << c[i] << " -> " << c[i + 1] << " oct");
        CHECK(c[i + 1] - c[i] >= -kBcsTauCapOct);
    }
    INFO("Fold saw E1 span C(1) - C(0) = " << c[10] - c[0] << " oct (model 0.45, bar 0.25)");
    CHECK(c[10] - c[0] >= Sct::kSeparationOct);
}

// SC-006 (C-7): every algorithm × Tone in 11 steps × Amount ∈ {0.5, 1} × {kSawAnchor, kSineAnchor} ×
// {E1, C2}, 48 kHz. Per Tone point, the spread of C over 8 sub-period window offsets × block sizes {1, 64,
// 512} (3 renders, 24 frames); ε of a sweep = the largest of its 11 spreads; τ = min(2ε, 0.005); ε > 0.0025 fails as
// "harness, not bar". Monotonicity (every step and the sum of negative steps ≥ −τ) gates both fixtures;
// the ≥ 0.25 oct span gates kSawAnchor; spans, ε and τ to evidence/sc006.csv.
TEST_CASE("BassCharacter_ToneMonotonicCentroid", "[systems][profundum][shape-character][bass-character][long]") {
    const std::string dir = bcsEvidenceDir();
    std::filesystem::create_directories(dir);
    std::ofstream csv(dir + "sc006.csv");
    REQUIRE(csv.is_open());
    csv << "algorithm,amount,fixture,note,eps_oct,tau_oct,span_oct,min_step_oct,neg_sum_oct,span_gated";
    for (int i = 0; i <= 10; ++i)
        csv << ",C_tone" << i;
    for (int i = 0; i <= 10; ++i)
        csv << ",eps_tone" << i;
    csv << ",pass\n";

    const std::array<BcsNote, 2> notes{{{.name = "E1", .hz = Sct::kE1Hz}, {.name = "C2", .hz = Sct::kC2Hz}}};
    for (const BcsFixtureRef& fx : kBcsSawSine) {
        for (const BcsNote& note : notes) {
            const BcsCentroidSource src = bcsCentroidSource(fx.controls, kBcsFs, note.hz);
            for (const BcsAlgo a : kBcsAlgos) {
                for (const float amount : {0.5f, 1.0f}) {
                    std::array<BcsCentroidCell, 11> cells{};
                    double eps = 0.0;
                    for (int i = 0; i <= 10; ++i) {
                        const auto ii = static_cast<std::size_t>(i);
                        cells[ii] = bcsCentroidCell(a, amount, bcsToneStep(i), src, kBcsFs, note.hz);
                        eps = std::max(eps, cells[ii].eps);
                    }
                    const double tau = std::min(2.0 * eps, kBcsTauCapOct);
                    double minStep = 1.0e300;
                    double negSum = 0.0;
                    for (std::size_t i = 0; i + 1 < cells.size(); ++i) {
                        const double step = cells[i + 1].c - cells[i].c;
                        minStep = std::min(minStep, step);
                        negSum += std::min(step, 0.0);
                    }
                    const double span = cells[10].c - cells[0].c;
                    const bool harnessOk = eps <= kBcsEpsHarnessOct;
                    const bool monotone = minStep >= -tau && negSum >= -tau;
                    const bool spanOk = !fx.spanGated || span >= Sct::kSeparationOct;
                    const bool pass = harnessOk && monotone && spanOk;

                    csv << bcsAlgoName(a) << ',' << amount << ',' << fx.name << ',' << note.name << ',' << eps << ','
                        << tau << ',' << span << ',' << minStep << ',' << negSum << ',' << (fx.spanGated ? 1 : 0);
                    for (const BcsCentroidCell& cell : cells)
                        csv << ',' << cell.c;
                    for (const BcsCentroidCell& cell : cells)
                        csv << ',' << cell.eps;
                    csv << ',' << (pass ? 1 : 0) << '\n';

                    INFO(bcsAlgoName(a) << ", Amount " << amount << ", " << fx.name << " " << note.name << ": eps "
                                        << eps << ", tau " << tau << ", min step " << minStep << ", neg sum "
                                        << negSum << ", span " << span << " oct"
                                        << (fx.spanGated ? " (gated >= 0.25)" : " (recorded, C-7)"));
                    if (!harnessOk)
                        FAIL_CHECK("SC-006 harness, not bar: eps " << eps << " oct > " << kBcsEpsHarnessOct);
                    CHECK(minStep >= -tau);
                    CHECK(negSum >= -tau);
                    if (fx.spanGated)
                        CHECK(span >= Sct::kSeparationOct);
                }
            }
        }
    }
}

// SC-007 endpoints: every algorithm, Amount 0 vs 1 at Tone 0.5, E1, 48 kHz, kSawAnchor and kSineAnchor:
// amountMoved ≥ 3 dB (THD, I, R_oe under the OQ-S floor) or ΔC ≥ 0.25 oct.
TEST_CASE("BassCharacter_AmountAudible", "[systems][profundum][shape-character][bass-character]") {
    const double f0 = Sct::kE1Hz;
    for (const BcsFixtureRef& fx : kBcsSawSine) {
        const std::vector<float> in = Sct::fixture(fx.controls, f0, kBcsFs, kBcsSeconds);
        for (const BcsAlgo a : kBcsAlgos) {
            const std::vector<float> y0 = bcsRenderCell(a, 0.0f, 0.5f, in, kBcsFs, f0);
            const std::vector<float> y1 = bcsRenderCell(a, 1.0f, 0.5f, in, kBcsFs, f0);
            const Sct::Descriptors d0 = Sct::describe(y0.data(), y0.size(), kBcsFs, f0);
            const Sct::Descriptors d1 = Sct::describe(y1.data(), y1.size(), kBcsFs, f0);
            const double moved = Sct::amountMoved(d0, d1);
            const double dC = std::abs(d1.C - d0.C);
            INFO(bcsAlgoName(a) << ", " << fx.name << " E1, Amount 0 -> 1: moved " << moved << " dB, dC " << dC
                                << " oct (THD " << d0.Thd << " -> " << d1.Thd << ", I " << d0.I << " -> " << d1.I
                                << ", Roe " << (d0.RoeNum - d0.RoeDen) << " -> " << (d1.RoeNum - d1.RoeDen) << ")");
            CHECK((moved >= Sct::kSeparationDb || dC >= Sct::kSeparationOct));
        }
    }
}

// SC-007 trajectory (recorded): every algorithm × Amount 0, 0.1, …, 1 at Tone 0.5, E1, 48 kHz, kSawAnchor
// and kSineAnchor, to evidence/sc007.csv (moves re Amount 0); the Amount-1 endpoint is re-asserted.
TEST_CASE("BassCharacter_AmountTrajectory", "[systems][profundum][shape-character][bass-character][long]") {
    const std::string dir = bcsEvidenceDir();
    std::filesystem::create_directories(dir);
    std::ofstream csv(dir + "sc007.csv");
    REQUIRE(csv.is_open());
    csv << "algorithm,fixture,amount,moved_db,dC_oct,THD_db,I_db,Roe_db,C_oct,L_dbfs\n";

    const double f0 = Sct::kE1Hz;
    for (const BcsFixtureRef& fx : kBcsSawSine) {
        const std::vector<float> in = Sct::fixture(fx.controls, f0, kBcsFs, kBcsSeconds);
        for (const BcsAlgo a : kBcsAlgos) {
            Sct::Descriptors d0{};
            for (int i = 0; i <= 10; ++i) {
                const float amount = static_cast<float>(i) / 10.0f;
                const std::vector<float> y = bcsRenderCell(a, amount, 0.5f, in, kBcsFs, f0);
                const Sct::Descriptors d = Sct::describe(y.data(), y.size(), kBcsFs, f0);
                if (i == 0)
                    d0 = d;
                const double moved = Sct::amountMoved(d0, d);
                const double dC = std::abs(d.C - d0.C);
                csv << bcsAlgoName(a) << ',' << fx.name << ',' << amount << ',' << moved << ',' << dC << ','
                    << d.Thd << ',' << d.I << ',' << (d.RoeNum - d.RoeDen) << ',' << d.C << ',' << d.L << '\n';
                REQUIRE(Krate::DSP::detail::isFinite(moved));
                if (i == 10) {
                    INFO(bcsAlgoName(a) << ", " << fx.name << " E1, Amount 1: moved " << moved << " dB, dC " << dC);
                    CHECK((moved >= Sct::kSeparationDb || dC >= Sct::kSeparationOct));
                }
            }
        }
    }
}

// SC-010 smoke sibling of BassCharacter_AliasingE0E2 (deviation 8): Rectify (the one algorithm without
// a per-algorithm aliasing smoke in this TU) at the model's worst cells, Fold-100 kSawAnchor E0 and
// Fold-100 kGrowl E2, 44.1 kHz, Amount 1, Tone ∈ {0, 1}, through the [long] grid's attribution cell.
TEST_CASE("BassCharacter_AliasingE0E2Smoke", "[systems][profundum][shape-character][bass-character]") {
    constexpr double fs = 44100.0;
    const std::array<BcsAliasInput, 2> cells{{
        {.name = "Fold-100 saw E0", .controls = SpectralShapeRecipe::kSawAnchor, .f0 = Sct::kE0Hz},
        {.name = "Fold-100 growl E2", .controls = SpectralShapeRecipe::kGrowl, .f0 = Sct::kE2Hz},
    }};
    for (const BcsAliasInput& cell : cells) {
        const std::size_t need = bcsAliasSamples(BcsAlgo::Rectify, fs, cell.f0);
        const std::vector<float> raw = Sct::fixture(cell.controls, cell.f0, fs, static_cast<double>(need) / fs + 0.01);
        const std::vector<float> in = bcsTail(Sct::fold100(raw, fs, cell.f0), need);
        for (const float tone : {0.0f, 1.0f}) {
            const std::vector<float> y = bcsRenderCell(BcsAlgo::Rectify, 1.0f, tone, in, fs, cell.f0);
            const BcsAliasCell c = bcsAliasCell(in, y, fs, cell.f0, cell.f0);
            INFO("Rectify, " << cell.name << ", 44.1 kHz, Amount 1, Tone " << tone << ": A = " << c.aOut
                             << " dBFS, A_in " << c.aIn << ", G " << c.g << ", A_added " << c.aAdded << " (bar "
                             << kBcsBarDbfs << ")");
            CHECK(c.aOut <= kBcsBarDbfs);
        }
    }
}

// SC-010 Character: every algorithm × Amount 1 × Tone ∈ {0, 1} × {E0, E1, E2} × {44.1, 48, 96} kHz ×
// {kSawAnchor, kGrowl} × {raw, Fold-100}: A ≤ −80 dBFS (Fracture on the n·f0/2 lattice with the n·f0
// value recorded, C-9). A_in, G, A_added per cell to evidence/sc010-character.csv; a Fold-100 cell that
// fails only on the input residual is an FR-070 candidate and fails loudly (never passed silently).
TEST_CASE("BassCharacter_AliasingE0E2", "[systems][profundum][shape-character][bass-character][long]") {
    const std::string dir = bcsEvidenceDir();
    std::filesystem::create_directories(dir);
    std::ofstream csv(dir + "sc010-character.csv");
    REQUIRE(csv.is_open());
    csv << "algorithm,tone,note,fs,fixture,variant,lattice,A_out_dbfs,A_in_dbfs,G_h1_power,A_added_dbfs,"
           "A_nf0_mask_dbfs,pass\n";

    const std::array<BcsNote, 3> notes{
        {{.name = "E0", .hz = Sct::kE0Hz}, {.name = "E1", .hz = Sct::kE1Hz}, {.name = "E2", .hz = Sct::kE2Hz}}};
    constexpr std::array<double, 3> kRates{44100.0, 48000.0, 96000.0};
    double worst = -400.0;
    for (const BcsNote& note : notes) {
        const double f0 = note.hz;
        for (const double fs : kRates) {
            const std::size_t longest = bcsAliasSamples(BcsAlgo::Fracture, fs, f0);  // the f0/2 frame is the longest
            for (const BcsFixtureRef& fx : kBcsSawGrowl) {
                const std::vector<float> raw =
                    Sct::fixture(fx.controls, f0, fs, static_cast<double>(longest) / fs + 0.01);
                for (const bool fold : {false, true}) {
                    const std::vector<float> full = fold ? Sct::fold100(raw, fs, f0) : raw;
                    for (const BcsAlgo a : kBcsAlgos) {
                        const double lattice = bcsLatticeHz(a, f0);
                        const std::vector<float> in = bcsTail(full, bcsAliasSamples(a, fs, f0));
                        for (const float tone : {0.0f, 1.0f}) {
                            const std::vector<float> y = bcsRenderCell(a, 1.0f, tone, in, fs, f0);
                            const BcsAliasCell c = bcsAliasCell(in, y, fs, f0, lattice);
                            const bool pass = c.aOut <= kBcsBarDbfs;
                            worst = std::max(worst, c.aOut);
                            csv << bcsAlgoName(a) << ',' << tone << ',' << note.name << ',' << fs << ',' << fx.name
                                << ',' << (fold ? "Fold-100" : "raw") << ','
                                << (a == BcsAlgo::Fracture ? "n*f0/2" : "n*f0") << ',' << c.aOut << ',' << c.aIn
                                << ',' << c.g << ',' << c.aAdded << ',' << c.aNf0Mask << ',' << (pass ? 1 : 0)
                                << '\n';
                            if (!pass) {
                                if (fold && c.aAdded <= kBcsBarDbfs) {
                                    // Never passed silently (SC-010 attribution, FR-070).
                                    FAIL_CHECK("FR-070 candidate: input residual - "
                                               << bcsAlgoName(a) << ", Tone " << tone << ", " << note.name << ", "
                                               << fs << " Hz, " << fx.name << ", Fold-100: A = " << c.aOut
                                               << " dBFS, A_in = " << c.aIn << ", G = " << c.g
                                               << ", A_added = " << c.aAdded);
                                } else {
                                    FAIL_CHECK("SC-010 Character cell above -80 dBFS - "
                                               << bcsAlgoName(a) << ", Tone " << tone << ", " << note.name << ", "
                                               << fs << " Hz, " << fx.name << ", " << (fold ? "Fold-100" : "raw")
                                               << ": A = " << c.aOut << " dBFS, A_in = " << c.aIn
                                               << ", G = " << c.g << ", A_added = " << c.aAdded
                                               << ", n*f0 mask " << c.aNf0Mask);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    INFO("worst output A over the grid: " << worst << " dBFS");
    CHECK(worst <= kBcsBarDbfs);
}

// SC-010 positive control: Bite with seamNaiveBaseRate_ (no oversampling, no ADAA) on raw kSawAnchor E2 /
// 44.1 kHz, Amount 1, Tone 0.5 reads ABOVE −80 dBFS (model −48.0); the same cell with the seam off ≤ −80.
TEST_CASE("ShapeCharacter_AliasGatePositiveControl", "[systems][profundum][shape-character][bass-character]") {
    constexpr double fs = 44100.0;
    const double f0 = Sct::kE2Hz;
    const std::vector<float> in = Sct::fixture(SpectralShapeRecipe::kSawAnchor, f0, fs, bcsAliasSeconds(fs, f0));
    double aSeam = 0.0;
    double aReal = 0.0;
    {
        BassCharacterStage s;
        bcsSetUp(s, BcsAlgo::Bite, 1.0f, 0.5f, fs, f0, BcsSeams{.naiveBaseRate = true});
        const std::vector<float> y = bcsRender(s, in);
        aSeam = bcsAliasedDbfs(y, fs, f0);
    }
    {
        BassCharacterStage s;
        bcsSetUp(s, BcsAlgo::Bite, 1.0f, 0.5f, fs, f0);
        const std::vector<float> y = bcsRender(s, in);
        aReal = bcsAliasedDbfs(y, fs, f0);
    }
    INFO("Bite, raw saw E2, 44.1 kHz: naive base rate A = " << aSeam << " dBFS (model -48.0, must be > "
                                                             << kBcsBarDbfs << "); oversampled + ADAA A = " << aReal
                                                             << " dBFS (bar " << kBcsBarDbfs << ")");
    CHECK(aSeam > kBcsBarDbfs);
    CHECK(aReal <= kBcsBarDbfs);
}

// SC-011: the SC-010 Character grid at 48 kHz only (every algorithm × Amount 1 × Tone ∈ {0, 1} × {E0, E1,
// E2} × {kSawAnchor, kGrowl} × {raw, Fold-100}): DC ≤ −80 dBFS, the mean over whole periods after a 1 s
// settle (Fracture: whole f0/2 periods, its output period being 2/f0; T015 run log decision 2).
TEST_CASE("BassCharacter_DcBound", "[systems][profundum][shape-character][bass-character]") {
    constexpr double kSeconds = 2.2;
    const std::array<BcsNote, 3> notes{
        {{.name = "E0", .hz = Sct::kE0Hz}, {.name = "E1", .hz = Sct::kE1Hz}, {.name = "E2", .hz = Sct::kE2Hz}}};
    for (const BcsNote& note : notes) {
        for (const BcsFixtureRef& fx : kBcsSawGrowl) {
            const std::vector<float> raw = Sct::fixture(fx.controls, note.hz, kBcsFs, kSeconds);
            for (const bool fold : {false, true}) {
                const std::vector<float> in = fold ? Sct::fold100(raw, kBcsFs, note.hz) : raw;
                for (const BcsAlgo a : kBcsAlgos) {
                    for (const float tone : {0.0f, 1.0f}) {
                        const std::vector<float> y = bcsRenderCell(a, 1.0f, tone, in, kBcsFs, note.hz);
                        const double dc = bcsDcDbfs(y, kBcsFs, bcsLatticeHz(a, note.hz));
                        INFO(bcsAlgoName(a) << ", Tone " << tone << ", " << note.name << ", " << fx.name << ", "
                                            << (fold ? "Fold-100" : "raw") << ": DC = " << dc << " dBFS (bar "
                                            << kBcsBarDbfs << ")");
                        CHECK(dc <= kBcsBarDbfs);
                    }
                }
            }
        }
    }
}

// SC-022 (Character arm; the Shape arm is T019): inputs with peak 1 — Fold-100 kGrowl normalised to peak 1
// and full-scale sines, each at E0 and C3, 48 kHz — every algorithm × Amount ∈ {0, 0.5, 1} × Tone ∈
// {0, 0.5, 1}: |y| ≤ 6.0 (FR-069, D-9); preCeilingPeak() printed (whether the ceiling acted).
TEST_CASE("ShapeCharacter_OutputBounded", "[systems][profundum][shape-character][bass-character]") {
    constexpr double kSeconds = 0.5;
    constexpr float kCeiling = 6.0f;
    struct BoundInput {
        std::string name;
        double f0;
        std::vector<float> x;
    };
    std::vector<BoundInput> inputs;
    const std::array<BcsNote, 2> notes{{{.name = "E0", .hz = Sct::kE0Hz}, {.name = "C3", .hz = Sct::kC3Hz}}};
    for (const BcsNote& note : notes) {
        std::vector<float> growl =
            Sct::fold100(Sct::fixture(SpectralShapeRecipe::kGrowl, note.hz, kBcsFs, kSeconds), kBcsFs, note.hz);
        float peak = 0.0f;
        for (const float v : growl)
            peak = std::max(peak, std::abs(v));
        REQUIRE(peak > 0.0f);
        for (float& v : growl)
            v /= peak;
        inputs.push_back(
            BoundInput{.name = std::string("Fold-100 growl (peak 1) ") + note.name, .f0 = note.hz, .x = growl});
        inputs.push_back(BoundInput{.name = std::string("full-scale sine ") + note.name,
                                    .f0 = note.hz,
                                    .x = HarmonicFoldTest::calibrationSine(kBcsFs, note.hz, kSeconds, 1.0)});
    }
    for (const BoundInput& input : inputs) {
        for (const BcsAlgo a : kBcsAlgos) {
            for (const float amount : {0.0f, 0.5f, 1.0f}) {
                for (const float tone : {0.0f, 0.5f, 1.0f}) {
                    BassCharacterStage s;
                    bcsSetUp(s, a, amount, tone, kBcsFs, input.f0);
                    const std::vector<float> y = bcsRender(s, input.x);
                    float maxAbs = 0.0f;
                    bool bounded = true;
                    for (const float v : y) {
                        bounded = bounded && (std::abs(v) <= kCeiling);  // false on NaN too
                        maxAbs = std::max(maxAbs, std::abs(v));
                    }
                    INFO(bcsAlgoName(a) << ", " << input.name << ", Amount " << amount << ", Tone " << tone
                                        << ": max |y| " << maxAbs << ", preCeilingPeak " << s.preCeilingPeak()
                                        << " (ceiling " << kCeiling << ")");
                    CHECK(bounded);
                }
            }
        }
    }
}

// SC-028 (FR-055, C-8): Tone structure per algorithm.
TEST_CASE("BassCharacter_ToneStructure", "[systems][profundum][shape-character][bass-character]") {
    // (a) Neutral: at Tone 0.5 a copy of each tilt (post for all six, pre for Tube / Bite) is flat within
    // 0.1 dB from 20 Hz to 0.45·fs, by sine probes at 30 log-spaced frequencies. The tilts run at the base
    // rate (C-23), so fs is the base rate (spec SC-028(a) "0.45·fs").
    SECTION("(a) neutral at Tone 0.5") {
        for (const BcsAlgo a : kBcsAlgos) {
            BassCharacterStage s;
            bcsSetUp(s, a, 1.0f, 0.5f, kBcsFs, Sct::kC2Hz);
            bcsDrive(s, kBcsFs, Sct::kC2Hz, false);
            std::vector<std::pair<const char*, KeytrackedTilt>> tilts;
            tilts.emplace_back("post", BcsProbe::postTilt(s, a, 0));
            if (BcsProbe::hasPreTilt(a))
                tilts.emplace_back("pre", BcsProbe::preTilt(s, a, 0));
            for (const auto& entry : tilts) {
                REQUIRE(entry.second.tone() == 0.5f);
                double worst = 0.0;
                double worstHz = 0.0;
                for (int i = 0; i < 30; ++i) {
                    const double hz = 20.0 * std::pow(0.45 * kBcsFs / 20.0, static_cast<double>(i) / 29.0);
                    const double db = bcsTiltSineDb(entry.second, kBcsFs, hz);
                    if (std::abs(db) > worst) {
                        worst = std::abs(db);
                        worstHz = hz;
                    }
                }
                INFO(bcsAlgoName(a) << " " << entry.first << " tilt at Tone 0.5: worst |H| deviation " << worst
                                    << " dB at " << worstHz << " Hz (bar 0.1)");
                CHECK(worst <= 0.1);
            }
        }
    }

    // (b) Keytracked through processBlock: the running branch's tilt responses vs n (n ≤ 16) at C1 and C3
    // differ by ≤ 1.5 dB per harmonic at Tone ∈ {0, 0.25, 0.75, 1}. f0 arrives by setFundamentalHz (one
    // stage, C1 then C3), by a constant f0PerSample array (≠ the prepare default C2; one stage, C1 then
    // C3), and on fresh stages where the algorithm is switched after setFundamentalHz, the stage leaves
    // bypass after it, or reset() is called after it (S5.3 step 1 re-key rule).
    SECTION("(b) keytracked between C1 and C3") {
        const double c1 = Sct::kC1Hz;
        const double c3 = Sct::kC3Hz;
        enum class Route : std::uint8_t { Setter, Pointer, SwitchAfter, LeaveBypassAfter, ResetAfter };
        const std::array<std::pair<Route, const char*>, 5> routes{{{Route::Setter, "setFundamentalHz"},
                                                                    {Route::Pointer, "f0PerSample"},
                                                                    {Route::SwitchAfter, "switch after f0"},
                                                                    {Route::LeaveBypassAfter, "leave bypass after f0"},
                                                                    {Route::ResetAfter, "reset() after f0"}}};
        for (const BcsAlgo a : kBcsAlgos) {
            const BcsAlgo other = (a == BcsAlgo::Tube) ? BcsAlgo::Fold : BcsAlgo::Tube;
            for (const float tone : {0.0f, 0.25f, 0.75f, 1.0f}) {
                for (const auto& routeEntry : routes) {
                    const Route route = routeEntry.first;
                    std::array<BcsTiltRows, 2> rows{};
                    BassCharacterStage shared;  // Setter / Pointer: one stage, C1 then C3
                    if (route == Route::Setter || route == Route::Pointer) {
                        shared.setAlgorithm(a);
                        shared.setAmount(1.0f);
                        shared.setTone(tone);
                        shared.prepare(kBcsFs, 512);  // f0 = the prepare default (C2)
                        REQUIRE(shared.fundamentalHz() == BassCharacterStage::kDefaultFundamentalHz);
                    }
                    for (std::size_t k = 0; k < 2; ++k) {
                        const double f0 = (k == 0) ? c1 : c3;
                        const auto f0f = static_cast<float>(f0);
                        if (route == Route::Setter) {
                            shared.setFundamentalHz(f0f);
                            bcsDrive(shared, kBcsFs, f0, false);
                            rows[k] = bcsTiltRows(shared, a, kBcsFs, f0);
                        } else if (route == Route::Pointer) {
                            bcsDrive(shared, kBcsFs, f0, true);
                            REQUIRE(shared.fundamentalHz() == f0f);
                            rows[k] = bcsTiltRows(shared, a, kBcsFs, f0);
                        } else {
                            BassCharacterStage s;
                            s.setAlgorithm(route == Route::SwitchAfter ? other : a);
                            s.setAmount(route == Route::LeaveBypassAfter ? 0.0f : 1.0f);
                            s.setTone(tone);
                            s.prepare(kBcsFs, 512);
                            s.setFundamentalHz(f0f);
                            if (route == Route::SwitchAfter) {
                                s.setAlgorithm(a);
                            } else if (route == Route::LeaveBypassAfter) {
                                REQUIRE(BcsProbe::bypassed(s));
                                s.setAmount(1.0f);
                            } else {
                                s.reset();
                            }
                            bcsDrive(s, kBcsFs, f0, false);
                            REQUIRE(BcsProbe::current(s) == a);
                            REQUIRE_FALSE(BcsProbe::bypassed(s));
                            rows[k] = bcsTiltRows(s, a, kBcsFs, f0);
                        }
                    }
                    const double postDiff = bcsRowsMaxDiff(rows[0].post, rows[1].post, kBcsFs, c1, c3);
                    INFO(bcsAlgoName(a) << ", Tone " << tone << ", " << routeEntry.second
                                        << ": post tilt max |H_C1(n) - H_C3(n)| = " << postDiff
                                        << " dB (bar 1.5); h1 " << rows[0].post[0] << " / " << rows[1].post[0]
                                        << " dB, h16 " << rows[0].post[15] << " / " << rows[1].post[15] << " dB");
                    CHECK(postDiff <= 1.5);
                    if (BcsProbe::hasPreTilt(a)) {
                        const double preDiff = bcsRowsMaxDiff(rows[0].pre, rows[1].pre, kBcsFs, c1, c3);
                        INFO("pre tilt max |H_C1(n) - H_C3(n)| = " << preDiff << " dB (bar 1.5)");
                        CHECK(preDiff <= 1.5);
                    }
                }
            }
        }
    }

    // (c) Fold's Tone is a linear post filter: kSawAnchor E1, 48 kHz, Amount 1; w_n at Tone 0 and at Tone 1,
    // each divided by |H_post(n·f0)|² (measured on the probe's tilt copy at that Tone), agree for
    // n = 1…16 within 0.1 dB.
    SECTION("(c) Fold de-tilted outputs agree") {
        const double f0 = Sct::kE1Hz;
        const std::vector<float> in = Sct::fixture(SpectralShapeRecipe::kSawAnchor, f0, kBcsFs, kBcsSeconds);
        std::array<std::vector<double>, 2> detilted{};
        for (std::size_t k = 0; k < 2; ++k) {
            const float tone = (k == 0) ? 0.0f : 1.0f;
            BassCharacterStage s;
            bcsSetUp(s, BcsAlgo::Fold, 1.0f, tone, kBcsFs, f0);
            const std::vector<float> y = bcsRender(s, in);
            const Sct::Descriptors d = Sct::describe(y.data(), y.size(), kBcsFs, f0);
            const KeytrackedTilt post = BcsProbe::postTilt(s, BcsAlgo::Fold, 0);
            REQUIRE(post.tone() == tone);
            const std::vector<double> h = bcsTiltHarmonicDb(post, kBcsFs, f0, 16);
            REQUIRE(d.w.size() >= 16);
            for (std::size_t i = 0; i < 16; ++i)
                detilted[k].push_back(HarmonicFoldTest::safeDb10(d.w[i]) - h[i]);
        }
        for (std::size_t i = 0; i < 16; ++i) {
            INFO("Fold saw E1, h" << i + 1 << ": de-tilted Tone 0 " << detilted[0][i] << " dB, Tone 1 "
                                  << detilted[1][i] << " dB (bar 0.1)");
            CHECK(std::abs(detilted[0][i] - detilted[1][i]) <= 0.1);
        }
    }

    // (d) Pre-clip placement (C-8): Tube and Bite, kSawAnchor E1, 48 kHz, Amount 1. The de-tilted
    // (÷ |H_post(n·f0)|², every harmonic below fs/2) THD or R_oe differs ≥ 3 dB (OQ-S floor) between
    // Tone 0 and Tone 1; corroborated with the post tilt held neutral (seamPostToneNeutral_, ≥ 3 dB);
    // negative control: the pre tilt held neutral (seamPreToneNeutral_), post active → de-tilted
    // difference < 3 dB.
    SECTION("(d) Tube and Bite pre-clip placement") {
        const double f0 = Sct::kE1Hz;
        const std::vector<float> in = Sct::fixture(SpectralShapeRecipe::kSawAnchor, f0, kBcsFs, kBcsSeconds);
        const auto measure = [&in, f0](BcsAlgo a, float tone, const BcsSeams& seams) {
            BassCharacterStage s;
            bcsSetUp(s, a, 1.0f, tone, kBcsFs, f0, seams);
            const std::vector<float> y = bcsRender(s, in);
            const Sct::Descriptors d = Sct::describe(y.data(), y.size(), kBcsFs, f0);
            std::vector<double> h(d.w.size(), 0.0);  // post held neutral: nothing to de-tilt
            if (!seams.postToneNeutral)
                h = bcsTiltHarmonicDb(BcsProbe::postTilt(s, a, 0), kBcsFs, f0, d.w.size());
            return bcsDetiltedThdRoe(d.w, h);
        };
        for (const BcsAlgo a : {BcsAlgo::Tube, BcsAlgo::Bite}) {
            const BcsThdRoe g0 = measure(a, 0.0f, BcsSeams{});
            const BcsThdRoe g1 = measure(a, 1.0f, BcsSeams{});
            const double gate = bcsThdRoeMove(g0, g1);
            const BcsThdRoe p0 = measure(a, 0.0f, BcsSeams{.postToneNeutral = true});
            const BcsThdRoe p1 = measure(a, 1.0f, BcsSeams{.postToneNeutral = true});
            const double corroborate = bcsThdRoeMove(p0, p1);
            const BcsThdRoe n0 = measure(a, 0.0f, BcsSeams{.preToneNeutral = true});
            const BcsThdRoe n1 = measure(a, 1.0f, BcsSeams{.preToneNeutral = true});
            const double control = bcsThdRoeMove(n0, n1);
            INFO(bcsAlgoName(a) << " saw E1, Amount 1, Tone 0 vs 1: de-tilted move " << gate << " dB (THD " << g0.thd
                                << " -> " << g1.thd << ", Roe " << (g0.roeNum - g0.roeDen) << " -> "
                                << (g1.roeNum - g1.roeDen) << "); post held neutral " << corroborate
                                << " dB; pre held neutral (negative control) " << control << " dB (THD " << n0.thd
                                << " -> " << n1.thd << ")");
            CHECK(gate >= Sct::kSeparationDb);
            CHECK(corroborate >= Sct::kSeparationDb);
            CHECK(control < Sct::kSeparationDb);
        }
    }
}
