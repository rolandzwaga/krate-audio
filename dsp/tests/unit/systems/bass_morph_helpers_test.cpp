// ==============================================================================
// Layer 3: System Tests - BassMorph render-fixture self-tests
//                          (Profundum Phase 8, specs/profundum-phase8-bass-morph)
// ==============================================================================
// tasks.md T007 (FR-070, FR-071; plan S5, S7.3, C-11; deviations 9, 10): the self-tests of
// bass_morph_test_helpers.h.
//   BassMorphHelpers_StaticEqualsFieldChain     A == B == a renderFieldChain-expressible state: the two fixtures
//                                               agree within TestUtils::kSampleTolerance (character, sum, out)
//   BassMorphHelpers_GridCalls                  advanceSamples == 0, 32, 64, ..., count ceil(total / 32), host
//                                               blocks 1, 7, 512
//   BassMorphHelpers_ChangeOnlyEnvelopeSetters  each ampEnv setter fires once at start and once per actual change
//   BassMorphHelpers_DescriptorsOnSines         dCxDb, eSubDb, dTMs on synthetic vectors
//   BassMorphHelpers_MuteCharacterKeepsTail     muteCharacter: character tap recorded, sum == Anchor tap bitwise
// FTZ / DAZ is set per render case (the components' precondition, plan S7.2).
// ==============================================================================

#include "bass_morph_test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <enable_ftz_daz.h>
#include <render_fingerprint.h>

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/systems/bass_morph_engine.h>
#include <krate/dsp/systems/morph_state.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

namespace {

namespace Bmt = Krate::DSP::BassMorphTest;
using Krate::DSP::BassMorphEngine;
using Krate::DSP::MorphState;

constexpr double kBmhFs = 48000.0;
constexpr double kBmhPi = 3.14159265358979323846264338327950288;

double bmhE1() { return static_cast<double>(Krate::DSP::ProfundumTest::anchorMidiHz(28)); }

/// max |x - y| over two equal-length vectors.
float bmhMaxAbsDiff(const std::vector<float>& x, const std::vector<float>& y) {
    REQUIRE(x.size() == y.size());
    float m = 0.0f;
    for (std::size_t i = 0; i < x.size(); ++i)
        m = std::max(m, std::fabs(x[i] - y[i]));
    return m;
}

float bmhMaxAbs(const std::vector<float>& x) {
    float m = 0.0f;
    for (const float v : x)
        m = std::max(m, std::fabs(v));
    return m;
}

/// amp · sin(2π·hz·i/fs) for i < toneSamples, then silence up to n.
std::vector<float> bmhTone(std::size_t n, std::size_t toneSamples, double fs, double hz, double amp) {
    std::vector<float> x(n, 0.0f);
    for (std::size_t i = 0; i < std::min(n, toneSamples); ++i)
        x[i] = static_cast<float>(amp * std::sin(2.0 * kBmhPi * hz * static_cast<double>(i) / fs));
    return x;
}

/// |H(f0)| in dB of MassDynamicsTest::subBandLowpass (4th-order Butterworth low-pass at float(1.5·f0), RBJ
/// bilinear with prewarp): the prewarped analog magnitude 1 / sqrt(1 + Ω^8), Ω = tan(π f0 / fs) / tan(π fc / fs).
double bmhSubBandLowpassGainDb(double f0, double fs) {
    const double fc = static_cast<double>(static_cast<float>(1.5 * f0));
    const double r = std::tan(kBmhPi * f0 / fs) / std::tan(kBmhPi * fc / fs);
    return -10.0 * std::log10(1.0 + std::pow(r, 8.0));
}

/// Both endpoints = showcase B, steady, E1, 48 kHz.
Bmt::MorphChainConfig bmhStaticShowcaseB(double seconds) {
    const auto pair = Bmt::showcasePair();
    Bmt::MorphChainConfig c{};
    c.a = pair.second;
    c.b = pair.second;
    c.f0 = bmhE1();
    c.fs = kBmhFs;
    c.seconds = seconds;
    c.ampEnvelope = false;
    return c;
}

}  // namespace

// -----------------------------------------------------------------------------
// 1. The morph fixture at a static state equals the Phase 7 chain (plan S7.3)
// -----------------------------------------------------------------------------

TEST_CASE("BassMorphHelpers_StaticEqualsFieldChain", "[systems][profundum][bass-morph][fr-070]") {
    enableFTZDAZ();
    std::cout << std::setprecision(9);
    const MorphState b = Bmt::showcasePair().second;

    // What renderFieldChain cannot express is pinned to what it hard-codes (mapping recorded in run-log/T007.txt):
    // the Anchor is anchorControlsFor(level) (Sine, Lock, Controls{} punch / ghost), no Drop, no amp envelope,
    // no fold feed, Mass 50 (massAt empty).
    const Krate::DSP::AnchorOscillator::Controls fixed = Krate::DSP::MassDynamicsTest::anchorControlsFor(b.anchor.level);
    REQUIRE(b.anchor.waveform == fixed.waveform);
    REQUIRE(b.anchor.mode == fixed.mode);
    REQUIRE(Bmt::sameFloatBits(b.anchor.punchBoostDb, fixed.punchBoostDb));
    REQUIRE(Bmt::sameFloatBits(b.anchor.punchTimeMs, fixed.punchTimeMs));
    REQUIRE(Bmt::sameFloatBits(b.anchor.ghostTimeMs, fixed.ghostTimeMs));
    REQUIRE(b.dropAmountSt == 0.0f);
    REQUIRE(b.anchorFoldFeed == 0.0f);
    REQUIRE(b.mass == Krate::DSP::MassLaw::kNeutralMass);

    constexpr double kSeconds = 1.5;
    const Bmt::MorphChainConfig mc = bmhStaticShowcaseB(kSeconds);
    const Bmt::MorphChainTaps m = Bmt::renderMorphChain(mc);

    Krate::DSP::StereoFieldTest::FieldChainConfig fc{};
    fc.chain.base = b.recipe;
    fc.chain.f0 = mc.f0;
    fc.chain.fs = mc.fs;
    fc.chain.seconds = kSeconds;
    fc.chain.lawBase = Bmt::baseFor(b);   // depth / body from the recipe, Fold 80, Anchor level 0.7
    fc.chain.shaperAttackDb = b.transientAttackDb;
    fc.chain.shaperSustainDb = b.transientSustainDb;
    fc.field = b.field;                   // WIDE, Spread 1, the rest Controls{}
    fc.shapeOn = true;
    fc.shape = b.shape;                   // SHAPE 1
    fc.foldAmount = std::nullopt;         // the law's Fold at Mass 50 = the base's 80
    fc.algorithm = b.characterAlgorithm;  // Fold / 0.8 / 0.8
    fc.characterAmount = b.characterAmount;
    fc.characterTone = b.characterTone;
    fc.excitation = b.excitation;         // mix 1 / drive 1 / asymmetry 0.5
    const Krate::DSP::StereoFieldTest::FieldChainTaps f = Krate::DSP::StereoFieldTest::renderFieldChain(fc);

    REQUIRE(m.outL.size() == f.outL.size());
    REQUIRE(bmhMaxAbs(f.characterL) > 1e-3f);   // the comparison is not vacuous
    REQUIRE(bmhMaxAbs(f.outL) > 1e-3f);
    const float tol = Krate::DSP::TestUtils::kSampleTolerance;
    const float dCharL = bmhMaxAbsDiff(m.characterL, f.characterL);
    const float dCharR = bmhMaxAbsDiff(m.characterR, f.characterR);
    const float dSumL = bmhMaxAbsDiff(m.sumL, f.sumL);
    const float dSumR = bmhMaxAbsDiff(m.sumR, f.sumR);
    const float dOutL = bmhMaxAbsDiff(m.outL, f.outL);
    const float dOutR = bmhMaxAbsDiff(m.outR, f.outR);
    std::cout << "BMH_STATIC_EQUALS_FIELD_CHAIN,maxAbsDiff,characterL," << dCharL << ",characterR," << dCharR
              << ",sumL," << dSumL << ",sumR," << dSumR << ",outL," << dOutL << ",outR," << dOutR << ",tolerance,"
              << tol << "\n";
    CHECK(dCharL <= tol);
    CHECK(dCharR <= tol);
    CHECK(dSumL <= tol);
    CHECK(dSumR <= tol);
    CHECK(dOutL <= tol);
    CHECK(dOutR <= tol);
}

// -----------------------------------------------------------------------------
// 2. One advanceInterval per grid point (SC-010)
// -----------------------------------------------------------------------------

TEST_CASE("BassMorphHelpers_GridCalls", "[systems][profundum][bass-morph][fr-070][sc-010]") {
    enableFTZDAZ();
    for (const std::size_t hostBlock : {std::size_t{1}, std::size_t{7}, std::size_t{512}}) {
        Bmt::MorphChainConfig c{};
        c.f0 = bmhE1();
        c.fs = kBmhFs;
        c.seconds = 1.0;
        c.hostBlock = hostBlock;
        c.ampEnvelope = false;
        c.automation.push_back(Bmt::MorphEvent{.sample = 4800, .kind = Bmt::MorphEvent::Kind::Position, .value = 1.0f});
        const Bmt::MorphChainTaps t = Bmt::renderMorphChain(c);
        const std::size_t total = t.outL.size();
        REQUIRE(total == 48000);
        INFO("hostBlock " << hostBlock);
        CHECK(t.advanceSamples.size() == (total + 31) / 32);
        CHECK(t.resolvedPerInterval.size() == t.advanceSamples.size());
        std::size_t wrong = 0;
        for (std::size_t i = 0; i < t.advanceSamples.size(); ++i) {
            if (t.advanceSamples[i] != 32 * i)
                ++wrong;
        }
        CHECK(wrong == 0);
        std::cout << "BMH_GRID_CALLS,hostBlock," << hostBlock << ",calls," << t.advanceSamples.size() << ",wrong,"
                  << wrong << "\n";
    }
}

// -----------------------------------------------------------------------------
// 3. ADSR setters are change-only (deviation 9)
// -----------------------------------------------------------------------------

TEST_CASE("BassMorphHelpers_ChangeOnlyEnvelopeSetters", "[systems][profundum][bass-morph][fr-070]") {
    enableFTZDAZ();
    MorphState a{};
    a.ampEnv.decayCurve = 0.7f;
    MorphState b = a;
    b.ampEnv.decayCurve = -0.7f;
    REQUIRE(Bmt::differingLeaves(a, b).size() == 1);

    Bmt::MorphChainConfig c{};
    c.a = a;
    c.b = b;
    c.f0 = bmhE1();
    c.fs = kBmhFs;
    c.seconds = 1.0;
    c.rampMs = 40.0f;
    c.ampEnvelope = true;
    c.automation.push_back(Bmt::MorphEvent{.sample = 24000, .kind = Bmt::MorphEvent::Kind::Position, .value = 1.0f});
    const Bmt::MorphChainTaps t = Bmt::renderMorphChain(c);

    std::size_t changes = 0;
    for (std::size_t i = 1; i < t.resolvedPerInterval.size(); ++i) {
        if (!Bmt::sameFloatBits(t.resolvedPerInterval[i].state.ampEnv.decayCurve,
                                t.resolvedPerInterval[i - 1].state.ampEnv.decayCurve))
            ++changes;
    }
    REQUIRE(Bmt::sameFloatBits(t.resolvedPerInterval.back().state.ampEnv.decayCurve, -0.7f));   // the morph landed
    REQUIRE(changes > 1);   // non-vacuous: the 40 ms ramp moves the curve over many intervals

    const auto decay = static_cast<std::size_t>(Bmt::AmpEnvLeaf::DecayCurve);
    std::cout << "BMH_ENV_SETTERS,decayCurveChanges," << changes;
    for (std::size_t i = 0; i < Bmt::kAmpEnvLeafCount; ++i)
        std::cout << ",leaf" << i << "," << t.ampEnvSetterCalls[i];
    std::cout << "\n";
    for (std::size_t i = 0; i < Bmt::kAmpEnvLeafCount; ++i) {
        INFO("ampEnv leaf " << i);
        if (i == decay)
            CHECK(t.ampEnvSetterCalls[i] == 1 + changes);
        else
            CHECK(t.ampEnvSetterCalls[i] == 1);
    }
}

// -----------------------------------------------------------------------------
// 4. The descriptor readers on synthetic vectors (no chain)
// -----------------------------------------------------------------------------

TEST_CASE("BassMorphHelpers_DescriptorsOnSines", "[systems][profundum][bass-morph][fr-070]") {
    constexpr double kF0 = 41.2;
    const std::size_t n = static_cast<std::size_t>(std::llround(Krate::DSP::MassDynamicsTest::kSettleSeconds * kBmhFs))
                          + Krate::DSP::TestUtils::kLowFrequencyFftSize + 4096;
    std::cout << std::setprecision(9);

    // D_cx of a pure sine.
    {
        const std::vector<float> x = bmhTone(n, n, kBmhFs, kF0, 0.5);
        const double d = Bmt::dCxDb(x, x, kF0, kBmhFs);
        std::cout << "BMH_DESCRIPTORS,dCxDb_pure_sine," << d << "\n";
        // The relative floor (reading refinement 2026-10-19): n >= 2 floored at -60 dB below w_1, so a pure sine reads
        // exactly kDescriptorFloorRelDb.
        CHECK(std::abs(d - Bmt::kDescriptorFloorRelDb) <= 1e-6);
    }

    // E_sub of a sine of amplitude a: the f0 reader (mean of subBand(...).dftDb as RMS dB, reading refinement
    // 2026-10-19) reads 20·log10(a/√2) with no filter gain; the recorded low-pass reader (eSubLpDb) carries the
    // 4th-order Butterworth's |H(f0)| = -0.166 dB (run-log/T007.txt).
    {
        const double lpDb = bmhSubBandLowpassGainDb(kF0, kBmhFs);
        for (const double amp : {0.1, 0.5, 1.0}) {
            const std::vector<float> x = bmhTone(n, n, kBmhFs, kF0, amp);
            const double e = Bmt::eSubDb(x, x, kF0, kBmhFs);
            const double eLp = Bmt::eSubLpDb(x, x, kF0, kBmhFs);
            const double expected = 20.0 * std::log10(amp / std::sqrt(2.0));
            std::cout << "BMH_DESCRIPTORS,eSubDb,amp," << amp << ",read," << e << ",expected," << expected
                      << ",lpRead," << eLp << ",lpGainDb," << lpDb << ",diff," << (e - expected) << "\n";
            INFO("amp " << amp << " eSub " << e << " expected " << expected);
            CHECK(std::abs(e - expected) <= 0.05);
            CHECK(std::abs(eLp - (expected + lpDb)) <= 0.05);
        }
    }

    // D_t of a constant tone of length T followed by silence.
    {
        const double periodMs = 1000.0 / kF0;
        for (const double T : {0.5, 1.0}) {
            const auto tone = static_cast<std::size_t>(std::llround(T * kBmhFs));
            const std::vector<float> x = bmhTone(tone + static_cast<std::size_t>(kBmhFs), tone, kBmhFs, kF0, 0.5);
            const double d = Bmt::dTMs(x, x, kF0, kBmhFs);
            std::cout << "BMH_DESCRIPTORS,dTMs,T," << T << ",read," << d << ",expected," << (T * 1000.0)
                      << ",periodMs," << periodMs << "\n";
            INFO("T " << T << " D_t " << d);
            CHECK(std::abs(d - T * 1000.0) <= periodMs);
        }
    }
}

// -----------------------------------------------------------------------------
// 5. muteCharacter keeps the tail running on the Anchor alone (SC-016)
// -----------------------------------------------------------------------------

TEST_CASE("BassMorphHelpers_MuteCharacterKeepsTail", "[systems][profundum][bass-morph][fr-070][sc-016]") {
    enableFTZDAZ();
    Bmt::MorphChainConfig c = bmhStaticShowcaseB(1.5);
    c.muteCharacter = true;
    const Bmt::MorphChainTaps t = Bmt::renderMorphChain(c);

    const float charPeak = std::max(bmhMaxAbs(t.characterL), bmhMaxAbs(t.characterR));
    const float anchorPeak = bmhMaxAbs(t.anchorL);
    std::cout << "BMH_MUTE_CHARACTER,characterPeak," << charPeak << ",anchorPeak," << anchorPeak << ",outPeak,"
              << bmhMaxAbs(t.outL) << "\n";
    CHECK(charPeak > 1e-3f);     // the tap is still recorded
    CHECK(anchorPeak > 1e-3f);   // non-vacuous: the Anchor sounds
    CHECK(Krate::DSP::StereoFieldTest::bitEqual(t.sumL, t.anchorL));
    CHECK(Krate::DSP::StereoFieldTest::bitEqual(t.sumR, t.anchorR));
}
