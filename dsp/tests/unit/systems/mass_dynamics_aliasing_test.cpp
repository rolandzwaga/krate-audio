// ==============================================================================
// Profundum Phase 6 (specs/profundum-phase6-mass-dynamics)
// Anti-aliasing at E0-E2: SC-010 (FR-042; plan S1 C-3); grids [long]
// ==============================================================================
// tasks.md T016. MassDynamics standalone (shaper -> compressor), processed one sample at a time so
// the per-sample gain taps (TransientShaper::currentGainDb + BassCompressor::currentGainDb) are the
// gain applied to that sample. Stereo L == R copies (tasks.md rule 9); L is measured.
//
// Settings (spec SC-010 row): shaper duration 5 ms (kMinDurationMs, the fastest), attack A in
// {+12, -12} dB (±max), sustain 0 dB; compressor amount 1 (the table's fastest attack, 10 ms) with
// the release pinned at its 50 ms floor through detail::MassDynamicsProbe::setFixedReleaseMs (the
// D-5 rule never releases faster than 50 ms, so the floor is the fastest gain motion the product
// can make); detector key = f0.
//
// Inputs: kSawAnchor (GATED) and ShapeCharacterTest::fold100(kSawAnchor) (RECORDED: user ruling
// 2026-10-18, spec SC-010 row / Clarifications "Group 9 close rulings" G9-1 -- the sample-rate-native
// gain folds the sidebands of an input whose content reaches Nyquist, and the 4x true-peak estimate of
// the Fold-100 wave wobbles with its sub-sample phase at E2) at E0, E1, E2. Three windows per
// cell: steady (from 1 s after onset), burst onset [onset, onset + 100 ms]
// (TransientShaperTest::burst, 250 ms pre-roll so the residual's 50 ms pre-taper fits), and the
// step transition [step, step + 100 ms] (TransientShaperTest::twoLevelStep, step at 2 s). For the
// Fold-100 input the fixture itself is folded.
//
// Two methods (plan S1 C-3, RULED 2026-10-17: kSc010Method = Residual gates; the spec-literal
// method is printed beside it, not gated):
//   Spec:     ProfundumTest::aliasedPower on in and out at the same frame, the largest power of two
//             <= the window ([1 s, 2 s], and each 100 ms window); A_out <= max(-80, A_in + 0.5). A
//             frame with f0 / binHz <= 17 (the ±8-bin lattice exclusion covers every bin) is
//             `vacuous`: printed, and under Spec its CHECK fails (FR-048, not a pass).
//   Residual: steady aliasedPower on one ProfundumTest::aliasFftLength(fs, f0) frame starting at
//             1 s, A_out <= max(-80, A_in + 0.5); MassDynamicsTest::aliasResidualDb <= -80 dBFS over
//             both moving windows; the hard 0 -> +12 dB positive control on a Fold-100 C3 saw reads
//             > -80 (M-2, 2026-10-18: on the plain saw it read -82.5 dBFS at 96 kHz, under the bar by
//             construction).
// FFT length and bin Hz are printed per cell for both methods. Every value std::setprecision(6).
// ==============================================================================

#include "mass_dynamics_test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/processors/spectral_shape_recipe.h>
#include <krate/dsp/processors/transient_shaper.h>
#include <krate/dsp/systems/mass_dynamics.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <memory>
#include <vector>

using namespace Krate::DSP;

namespace {

namespace mda = Krate::DSP::MassDynamicsTest;
namespace mdaTs = Krate::DSP::TransientShaperTest;
namespace mdaSc = Krate::DSP::ShapeCharacterTest;
namespace mdaPt = Krate::DSP::ProfundumTest;

// plan S1 C-3 (user ruling 2026-10-17, spec SC-010 row / Clarifications "Plan S1 rulings").
enum class Sc010Method : std::uint8_t { Spec, Residual };
constexpr Sc010Method kSc010Method = Sc010Method::Residual;

constexpr double kMdaBarDbfs = -80.0;           // SC-010 (the Phase 2 / Phase 5 bar)
constexpr double kMdaInputMarginDb = 0.5;       // SC-010: A_out <= max(-80, A_in + 0.5)
constexpr double kMdaVacuousBinsPerF0 = 17.0;   // f0 / binHz <= 17: the ±8-bin exclusion covers every bin

constexpr float kMdaDurationMs = TransientShaper::kMinDurationMs;   // 5 ms, the fastest (D-3)
constexpr float kMdaSustainDb = 0.0f;
constexpr float kMdaCompAmount = 1.0f;
constexpr float kMdaFixedReleaseMs = BassCompressor::kMinReleaseMs;   // 50 ms release floor (D-5)
constexpr std::array<float, 2> kMdaAttacksDb{TransientShaper::kMaxGainDb, TransientShaper::kMinGainDb};

constexpr double kMdaSteadyStartS = 1.0;
constexpr double kMdaSteadyWindowS = 1.0;     // the spec method's [1 s, 2 s]
constexpr double kMdaMovingWindowS = 0.1;     // [t0, t0 + 100 ms]
constexpr double kMdaBurstPreRollS = 0.25;    // >= the residual's 50 ms pre-taper
constexpr double kMdaBurstHoldS = 0.5;        // >= 100 ms window + 50 ms post-taper
constexpr double kMdaStepAtS = 2.0;           // tasks.md T016: step at 2 s
constexpr double kMdaStepHighS = 0.5;
constexpr double kMdaPositiveControlStepDb = 12.0;
constexpr double kMdaPositiveControlT0S = 0.5;

struct MdaNote {
    const char* name;
    double hz;
};

const std::array<MdaNote, 3> kMdaNotes{{
    {.name = "E0", .hz = mdaTs::kE0Hz},
    {.name = "E1", .hz = mdaTs::kE1Hz},
    {.name = "E2", .hz = mdaTs::kE2Hz},
}};

struct MdaRender {
    std::vector<float> y;        ///< L output
    std::vector<float> gainDb;   ///< shaper gain + compressor gain (dB) applied to each sample
};

/// MassDynamics at the SC-010 settings, one sample at a time, L == R copies.
MdaRender mdaRenderDynamics(const std::vector<float>& x, double fs, double f0, float attackDb) {
    auto md = std::make_unique<MassDynamics>();
    // Before prepare: prepare applies them and reset() snaps the gain smoothers (T011 decision 2).
    md->setTransientAttackDb(attackDb);
    md->setTransientSustainDb(kMdaSustainDb);
    md->setTransientDurationMs(kMdaDurationMs);
    md->setCompressionAmount(kMdaCompAmount);
    md->setDetectorKeyHz(static_cast<float>(f0));
    md->prepare(fs, 512);
    detail::MassDynamicsProbe::setFixedReleaseMs(detail::MassDynamicsProbe::compressor(*md), kMdaFixedReleaseMs);

    MdaRender r;
    r.y.assign(x.size(), 0.0f);
    r.gainDb.assign(x.size(), 0.0f);
    for (std::size_t i = 0; i < x.size(); ++i) {
        float l = x[i];
        float rr = x[i];
        md->processBlock(&l, &rr, 1);
        r.y[i] = l;
        r.gainDb[i] = md->shaper().currentGainDb() + md->compressor().currentGainDb();
    }
    return r;
}

/// The largest power of two <= n (n >= 1).
std::size_t mdaLargestPow2AtMost(std::size_t n) {
    REQUIRE(n >= 1);
    std::size_t p = 1;
    while (p * 2 <= n)
        p *= 2;
    return p;
}

struct MdaSpecReading {
    std::size_t frame = 0;
    double binHz = 0.0;
    bool vacuous = false;
    double aIn = 0.0;
    double aOut = 0.0;
    bool pass = false;
};

/// The spec-literal reading of one window starting at `start` of `windowSamples` samples.
MdaSpecReading mdaSpecReading(const std::vector<float>& in, const std::vector<float>& out, std::size_t start,
                              std::size_t windowSamples, double fs, double f0) {
    MdaSpecReading s;
    s.frame = mdaLargestPow2AtMost(windowSamples);
    REQUIRE(start + s.frame <= in.size());
    REQUIRE(start + s.frame <= out.size());
    s.binHz = fs / static_cast<double>(s.frame);
    s.vacuous = f0 / s.binHz <= kMdaVacuousBinsPerF0;
    s.aIn = mdaPt::aliasedPower(in.data() + start, s.frame, fs, f0).aliasedDbfs;
    s.aOut = mdaPt::aliasedPower(out.data() + start, s.frame, fs, f0).aliasedDbfs;
    s.pass = !s.vacuous && s.aOut <= std::max(kMdaBarDbfs, s.aIn + kMdaInputMarginDb);
    return s;
}

void mdaPrintSpec(const char* label, const MdaSpecReading& s) {
    std::cout << "    spec " << label << ": frame " << s.frame << ", bin " << s.binHz << " Hz, A_in " << s.aIn
              << " dBFS, A_out " << s.aOut << " dBFS" << (s.vacuous ? " VACUOUS" : "")
              << (s.pass ? " pass" : " FAIL") << "\n";
}

/// The hard 0 -> +12 dB positive control on a Fold-100 C3 kSawAnchor (plan S1 C-3; M-2 2026-10-18):
/// the residual must see the aliasing a non-band-limited gain step adds. The Fold-100 input has
/// content up to Nyquist at every rate; on the plain saw the control read -75.8 / -76.5 / -82.5 dBFS
/// at 44.1 / 48 / 96 kHz.
double mdaPositiveControlDb(double fs) {
    const double f0 = mdaTs::kC3Hz;
    const std::vector<float> x =
        mdaSc::fold100(HarmonicFoldTest::recipeAdditive(SpectralShapeRecipe::kSawAnchor, f0, fs, 1.0), fs, f0);
    const std::size_t n = x.size();
    const std::size_t t0 = mdaTs::secondsToSamples(fs, kMdaPositiveControlT0S);
    std::vector<float> g(n, 0.0f);
    std::vector<float> y(n, 0.0f);
    for (std::size_t i = 0; i < n; ++i) {
        g[i] = (i < t0) ? 0.0f : static_cast<float>(kMdaPositiveControlStepDb);
        y[i] = static_cast<float>(static_cast<double>(x[i]) * std::pow(10.0, static_cast<double>(g[i]) / 20.0));
    }
    return mda::aliasResidualDb(x.data(), g.data(), y.data(), n, fs, t0);
}

/// One SC-010 cell: every window, both methods, gated by kSc010Method.
void mdaRunCell(double fs, const MdaNote& note, bool folded, float attackDb) {
    const double f0 = note.hz;
    const char* inputName = folded ? "fold100(kSawAnchor)" : "kSawAnchor";
    INFO("SC-010 cell fs " << fs << " " << note.name << " " << inputName << " A " << attackDb << " dB");

    const auto steadyStart = mdaTs::secondsToSamples(fs, kMdaSteadyStartS);
    const auto steadyWindow = mdaTs::secondsToSamples(fs, kMdaSteadyWindowS);
    const auto movingWindow = mdaTs::secondsToSamples(fs, kMdaMovingWindowS);
    const std::size_t residualSteadyLen = mdaPt::aliasFftLength(fs, f0);

    // ---- steady ------------------------------------------------------------------------------
    const std::size_t steadyNeed = steadyStart + std::max(steadyWindow, residualSteadyLen) + 1;
    std::vector<float> steadyIn = HarmonicFoldTest::recipeAdditive(SpectralShapeRecipe::kSawAnchor, f0, fs,
                                                                   static_cast<double>(steadyNeed) / fs + 0.01);
    if (folded)
        steadyIn = mdaSc::fold100(steadyIn, fs, f0);
    REQUIRE(steadyIn.size() >= steadyNeed);
    const MdaRender steady = mdaRenderDynamics(steadyIn, fs, f0, attackDb);

    // ---- burst onset ---------------------------------------------------------------------------
    std::vector<float> burstIn = mdaTs::burst(mdaTs::Tone::Saw, f0, fs, kMdaBurstHoldS, kMdaBurstPreRollS, 0.0);
    if (folded)
        burstIn = mdaSc::fold100(burstIn, fs, f0);
    const std::size_t onset = mdaTs::onsetIndex(fs, kMdaBurstPreRollS);
    const MdaRender burst = mdaRenderDynamics(burstIn, fs, f0, attackDb);

    // ---- step transition ---------------------------------------------------------------------
    std::vector<float> stepIn = mdaTs::twoLevelStep(mdaTs::Tone::Saw, f0, fs, kMdaStepAtS, kMdaStepHighS);
    if (folded)
        stepIn = mdaSc::fold100(stepIn, fs, f0);
    const std::size_t stepAt = mdaTs::secondsToSamples(fs, kMdaStepAtS);
    const MdaRender step = mdaRenderDynamics(stepIn, fs, f0, attackDb);

    // ---- Spec method (printed; gated only under Sc010Method::Spec) ----------------------------
    const MdaSpecReading specSteady = mdaSpecReading(steadyIn, steady.y, steadyStart, steadyWindow, fs, f0);
    const MdaSpecReading specBurst = mdaSpecReading(burstIn, burst.y, onset, movingWindow, fs, f0);
    const MdaSpecReading specStep = mdaSpecReading(stepIn, step.y, stepAt, movingWindow, fs, f0);

    // ---- Residual method (plan S1 C-3; the gate) --------------------------------------------
    REQUIRE(steadyStart + residualSteadyLen <= steadyIn.size());
    const double resSteadyIn = mdaPt::aliasedPower(steadyIn.data() + steadyStart, residualSteadyLen, fs, f0).aliasedDbfs;
    const double resSteadyOut =
        mdaPt::aliasedPower(steady.y.data() + steadyStart, residualSteadyLen, fs, f0).aliasedDbfs;
    const bool resSteadyPass = resSteadyOut <= std::max(kMdaBarDbfs, resSteadyIn + kMdaInputMarginDb);
    const double resBurst =
        mda::aliasResidualDb(burstIn.data(), burst.gainDb.data(), burst.y.data(), burstIn.size(), fs, onset);
    const double resStep =
        mda::aliasResidualDb(stepIn.data(), step.gainDb.data(), step.y.data(), stepIn.size(), fs, stepAt);
    const std::size_t resMovingLen = mda::aliasResidualFftLength(fs);

    const auto [gMinIt, gMaxIt] = std::minmax_element(burst.gainDb.begin() + static_cast<std::ptrdiff_t>(onset),
                                                      burst.gainDb.begin() +
                                                          static_cast<std::ptrdiff_t>(onset + movingWindow));
    std::cout << std::setprecision(6) << "[T016 SC-010] fs " << fs << " " << note.name << " (" << f0 << " Hz) "
              << inputName << " A " << attackDb << " dB, method "
              << (kSc010Method == Sc010Method::Residual ? "Residual" : "Spec") << " gates\n";
    std::cout << "    residual steady: frame " << residualSteadyLen << ", bin "
              << fs / static_cast<double>(residualSteadyLen) << " Hz, A_in " << resSteadyIn << " dBFS, A_out "
              << resSteadyOut << " dBFS" << (resSteadyPass ? " pass" : " FAIL") << "\n";
    std::cout << "    residual moving: FFT " << resMovingLen << " (8x domain), bin "
              << 8.0 * fs / static_cast<double>(resMovingLen) << " Hz; burst " << resBurst << " dBFS (gain "
              << *gMinIt << " .. " << *gMaxIt << " dB), step " << resStep << " dBFS (bar " << kMdaBarDbfs << ")\n";
    mdaPrintSpec("steady [1 s, 2 s]", specSteady);
    mdaPrintSpec("burst [0, 100 ms]", specBurst);
    mdaPrintSpec("step [step, step + 100 ms]", specStep);

    if constexpr (kSc010Method == Sc010Method::Residual) {
        if (folded) {
            // G9-1 (user ruling 2026-10-18): the Fold-100 fixture is recorded, an FR-048 measurement.
            WARN("SC-010 fs " << fs << " " << note.name << " " << inputName << " A " << attackDb
                              << " dB RECORDED (G9-1, FR-048 measurement): steady A_out " << resSteadyOut
                              << " dBFS (A_in " << resSteadyIn << "), burst " << resBurst << " dBFS, step " << resStep
                              << " dBFS (bar " << kMdaBarDbfs << ", not gated)");
        } else {
            CHECK(resSteadyPass);
            CHECK(resBurst <= kMdaBarDbfs);
            CHECK(resStep <= kMdaBarDbfs);
        }
    } else {
        CHECK(specSteady.pass);
        CHECK(specBurst.pass);
        CHECK(specStep.pass);
    }
}

/// The positive control at fs, printed and checked (plan S1 C-3).
void mdaRunPositiveControl(double fs) {
    const double pc = mdaPositiveControlDb(fs);
    std::cout << std::setprecision(6) << "[T016 SC-010] positive control fs " << fs
              << ": hard 0 -> +12 dB step on a Fold-100 C3 kSawAnchor, residual " << pc << " dBFS (must read > "
              << kMdaBarDbfs << ")\n";
    CHECK(pc > kMdaBarDbfs);
}

/// The full grid at one rate: {kSawAnchor, fold100} x {E0, E1, E2} x A {+12, -12}.
void mdaRunGrid(double fs) {
    mdaRunPositiveControl(fs);
    for (const bool folded : {false, true}) {
        for (const MdaNote& note : kMdaNotes) {
            for (const float a : kMdaAttacksDb)
                mdaRunCell(fs, note, folded, a);
        }
    }
}

}  // namespace

// SC-010 (FR-042; plan S1 C-3): per-push sibling of the [long] grids. 48 kHz, E1, kSawAnchor,
// A = +12, both methods (the E1 spec steady frame is not vacuous: 32768 points, 1.46 Hz bins).
TEST_CASE("MassDynamics_AliasingE0E2Smoke", "[systems][profundum][mass-dynamics]") {
    constexpr double fs = 48000.0;
    mdaRunPositiveControl(fs);
    mdaRunCell(fs, kMdaNotes[1], false, TransientShaper::kMaxGainDb);
}

// SC-010 (FR-042; plan S1 C-3): one shard per rate.
TEST_CASE("MassDynamics_AliasingE0E2_44k1", "[systems][profundum][mass-dynamics][long]") { mdaRunGrid(44100.0); }

// SC-010 (FR-042; plan S1 C-3): one shard per rate.
TEST_CASE("MassDynamics_AliasingE0E2_48k", "[systems][profundum][mass-dynamics][long]") { mdaRunGrid(48000.0); }

// SC-010 (FR-042; plan S1 C-3): one shard per rate.
TEST_CASE("MassDynamics_AliasingE0E2_96k", "[systems][profundum][mass-dynamics][long]") { mdaRunGrid(96000.0); }
