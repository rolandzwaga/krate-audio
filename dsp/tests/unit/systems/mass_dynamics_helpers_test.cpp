// ==============================================================================
// Profundum Phase 6 (specs/profundum-phase6-mass-dynamics)
// mass_dynamics_test_helpers.h self-tests (FR-031-FR-034; deviations 5, 8)
// tasks.md T011. Tag [systems][profundum][mass-dynamics]. Every measured value is printed with
// std::setprecision(6).
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
#include <iomanip>
#include <iostream>
#include <span>
#include <vector>

using namespace Krate::DSP;

namespace {

namespace mdh = Krate::DSP::MassDynamicsTest;
namespace mdhTs = Krate::DSP::TransientShaperTest;
namespace mdhSc = Krate::DSP::ShapeCharacterTest;

/// T011 test 5 (user ruling 2026-10-17, spec SC-005(c) row / Clarifications "Group 6 gate ruling"): the
/// one-period window is resampled onto an integer grid before its DFT, so the sine arm's E_low / THD gate
/// against describe() like the saw's, both readings clamped at the OQ-S -60 dB floor; the sine's raw THD
/// must also sit at or below this floor margin (the rectangular estimator read -47 ... -73 dB).
constexpr double kMdhSineRawThdCeilingDb = -70.0;

constexpr double kMdhFs = 48000.0;

bool mdhValid(float f) { return detail::isFinite(f) && f > 0.0f; }

float mdhFirstVoice(std::span<const float> f0s) {
    for (const float f : f0s)
        if (mdhValid(f))
            return f;
    return 0.0f;
}

float mdhLastVoice(std::span<const float> f0s) {
    float last = 0.0f;
    for (const float f : f0s)
        if (mdhValid(f))
            last = f;
    return last;
}

float mdhMaxVoice(std::span<const float> f0s) {
    float best = 0.0f;
    for (const float f : f0s)
        if (mdhValid(f) && f > best)
            best = f;
    return best;
}

bool mdhSameBits(float a, float b) { return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b); }

bool mdhVectorsBitEqual(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
}

}  // namespace

// FR-018, FR-035, SC-024(d) teeth: the engine-side key rule.
TEST_CASE("MassDynamicsHelpers_LowestSoundingF0", "[systems][profundum][mass-dynamics]") {
    const auto e1 = static_cast<float>(mdhTs::kE1Hz);
    const auto c3 = static_cast<float>(mdhTs::kC3Hz);
    const float nan = std::bit_cast<float>(0x7FC00000u);
    const float inf = std::bit_cast<float>(0x7F800000u);

    struct Row {
        std::vector<float> in;
        float expected;
    };
    const std::vector<Row> rows{
        {.in = {e1, c3}, .expected = e1},
        {.in = {c3, e1}, .expected = e1},
        {.in = {c3, 0.0f, e1}, .expected = e1},
        {.in = {nan, c3}, .expected = c3},
        {.in = {}, .expected = 0.0f},
        {.in = {-1.0f, 0.0f}, .expected = 0.0f},
        {.in = {inf, e1}, .expected = e1},
    };

    bool firstDisagrees = false;
    bool lastDisagrees = false;
    bool maxDisagrees = false;
    for (std::size_t r = 0; r < rows.size(); ++r) {
        const std::span<const float> in(rows[r].in);
        const float got = mdh::lowestSoundingF0(in);
        INFO("row " << r);
        CHECK(mdhSameBits(got, rows[r].expected));
        firstDisagrees = firstDisagrees || !mdhSameBits(mdhFirstVoice(in), rows[r].expected);
        lastDisagrees = lastDisagrees || !mdhSameBits(mdhLastVoice(in), rows[r].expected);
        maxDisagrees = maxDisagrees || !mdhSameBits(mdhMaxVoice(in), rows[r].expected);
    }
    // The table has teeth: a first-voice, last-voice or maximum rule fails at least one row.
    CHECK(firstDisagrees);
    CHECK(lastDisagrees);
    CHECK(maxDisagrees);
}

// SC-006(a)'s s(t) and M_sub (plan S1 C-10).
TEST_CASE("MassDynamicsHelpers_SubBand", "[systems][profundum][mass-dynamics]") {
    const double fs = kMdhFs;

    // An E1 sine amplitude-modulated by exactly 1.0 dB peak-to-peak at 0.5 Hz, 3 s.
    {
        const double f0 = mdhTs::kE1Hz;
        const std::size_t n = mdhTs::secondsToSamples(fs, 3.0);
        std::vector<float> x(n);
        const double peak = static_cast<double>(HarmonicFold::kCalibrationPeak);
        for (std::size_t i = 0; i < n; ++i) {
            const double t = static_cast<double>(i) / fs;
            const double gDb = 0.5 * std::sin(ProfundumTest::kTwoPi * 0.5 * t);
            x[i] = static_cast<float>(peak * std::sin(ProfundumTest::kTwoPi * f0 * t) * std::pow(10.0, gDb / 20.0));
        }
        const mdh::SubBand sb = mdh::subBand(x.data(), n, fs, f0);
        REQUIRE_FALSE(sb.dftDb.empty());
        REQUIRE(sb.lpRmsDb.size() == sb.dftDb.size());
        CHECK(static_cast<double>(sb.hop) <= sb.period / 8.0);
        const std::size_t b = mdhTs::secondsToSamples(fs, 0.5);
        const double m = mdh::mSubSamples(sb, sb.dftDb, b, n);
        const double mLp = mdh::mSubSamples(sb, sb.lpRmsDb, b, n);
        std::cout << std::setprecision(6) << "[T011 SubBand] AM E1: M_sub(dft) = " << m << " dB, M_sub(LP) = " << mLp
                  << " dB, hop = " << sb.hop << ", P = " << sb.period
                  << ", LP group delay = " << sb.lpGroupDelayPeriods << " periods (analog 0.361)\n";
        CHECK(std::abs(m - 1.0) <= 0.05);
    }

    // A steady kSawAnchor at E0, 5 s.
    {
        const double f0 = mdhTs::kE0Hz;
        const std::vector<float> x = HarmonicFoldTest::recipeAdditive(SpectralShapeRecipe::kSawAnchor, f0, fs, 5.0);
        const mdh::SubBand sb = mdh::subBand(x.data(), x.size(), fs, f0);
        const std::size_t b = mdhTs::secondsToSamples(fs, 1.0);
        const double m = mdh::mSubSamples(sb, sb.dftDb, b, x.size());
        const double mLp = mdh::mSubSamples(sb, sb.lpRmsDb, b, x.size());
        std::cout << std::setprecision(6) << "[T011 SubBand] steady saw E0: M_sub(dft) = " << m
                  << " dB, M_sub(LP) = " << mLp << " dB, LP group delay = " << sb.lpGroupDelayPeriods
                  << " periods (analog 0.361)\n";
        CHECK(m <= 0.01);
    }
}

// SC-010's moving-window residual method (plan S1 C-3, RULED).
TEST_CASE("MassDynamicsHelpers_AliasResidual", "[systems][profundum][mass-dynamics]") {
    const double fs = kMdhFs;
    const double f0 = mdhTs::kC3Hz;
    const std::vector<float> x = HarmonicFoldTest::recipeAdditive(SpectralShapeRecipe::kSawAnchor, f0, fs, 1.0);
    const std::size_t n = x.size();
    const std::size_t t0 = mdhTs::secondsToSamples(fs, 0.5);
    const std::size_t rampLen = mdhTs::secondsToSamples(fs, 0.1);

    const auto residualFor = [&](auto gainDbAt) {
        std::vector<float> g(n);
        std::vector<float> y(n);
        for (std::size_t i = 0; i < n; ++i) {
            g[i] = static_cast<float>(gainDbAt(i));
            y[i] = static_cast<float>(static_cast<double>(x[i]) * std::pow(10.0, static_cast<double>(g[i]) / 20.0));
        }
        return mdh::aliasResidualDb(x.data(), g.data(), y.data(), n, fs, t0);
    };

    const double constant = residualFor([](std::size_t) { return 6.0; });
    const double ramp = residualFor([&](std::size_t i) {
        if (i < t0)
            return 0.0;
        if (i < t0 + rampLen)
            return static_cast<double>(i - t0) / static_cast<double>(rampLen);
        return 1.0;
    });
    const double step = residualFor([&](std::size_t i) { return i < t0 ? 0.0 : 12.0; });

    const std::size_t len = mdh::aliasResidualFftLength(fs);
    std::cout << std::setprecision(6) << "[T011 AliasResidual] C3 saw: FFT length " << len << ", bin "
              << 8.0 * fs / static_cast<double>(len) << " Hz; constant +6 dB " << constant
              << " dBFS, 1 dB / 100 ms ramp " << ramp << " dBFS, 0 -> +12 dB step (positive control) " << step
              << " dBFS\n";
    CHECK(constant <= -120.0);
    CHECK(ramp <= -100.0);
    CHECK(step > -80.0);
}

// Plan C-11: the absolute 32-sample grid.
TEST_CASE("MassDynamicsHelpers_GridDriver", "[systems][profundum][mass-dynamics]") {
    constexpr std::size_t kTotal = 8804;
    for (const std::size_t hb : {std::size_t{1}, std::size_t{17}, std::size_t{64}, std::size_t{512}}) {
        INFO("host block " << hb);
        std::size_t next = 0;
        bool tiles = true;
        bool crosses = false;
        bool atGridOk = true;
        bool emptyRun = false;
        bool starts8704 = false;
        mdh::gridDriver(kTotal, hb, [&](std::size_t start, std::size_t len, bool atGrid) {
            if (start != next)
                tiles = false;
            if (len == 0)
                emptyRun = true;
            else if (start / mdh::kGrid != (start + len - 1) / mdh::kGrid)
                crosses = true;
            if (atGrid != (start % mdh::kGrid == 0))
                atGridOk = false;
            if (start == 8704)
                starts8704 = true;
            next = start + len;
        });
        CHECK(tiles);
        CHECK(next == kTotal);
        CHECK_FALSE(emptyRun);
        CHECK_FALSE(crosses);
        CHECK(atGridOk);
        CHECK(starts8704);
    }
}

// SC-005(c): one-period descriptors against describe().
TEST_CASE("MassDynamicsHelpers_PeriodDescriptors", "[systems][profundum][mass-dynamics]") {
    const double fs = kMdhFs;
    for (const double f0 : {mdhTs::kE1Hz, mdhTs::kC3Hz}) {
        for (const mdhTs::Tone tone : {mdhTs::Tone::Saw, mdhTs::Tone::Sine}) {
            const bool sine = tone == mdhTs::Tone::Sine;
            const std::vector<float> x = mdhTs::steadyTone(tone, f0, fs, mdh::descriptorSeconds(fs));
            const mdhSc::Descriptors d = mdhSc::describe(x.data(), x.size(), fs, f0);
            const double P = fs / f0;
            const auto need = static_cast<std::size_t>(std::floor(P)) + 1;
            const std::size_t start = x.size() - need - static_cast<std::size_t>(std::llround(3.0 * P));
            const mdh::PeriodDescriptors p = mdh::periodDescriptors(x.data(), start, fs, f0);
            INFO((sine ? "sine" : "saw") << " f0 " << f0);
            std::cout << std::setprecision(6) << "[T011 PeriodDescriptors] " << (sine ? "sine" : "saw") << " f0 = " << f0
                      << ": L " << p.L << " / " << d.L << ", E_low " << p.Elow << " / " << d.Elow << ", E_high "
                      << p.Ehigh << " / " << d.Ehigh << ", THD " << p.Thd << " / " << d.Thd << ", C " << p.C << " / "
                      << d.C << " oct, w1Frac " << p.w1Frac << " (period / describe)\n";
            CHECK(std::abs(p.L - d.L) <= 0.1);
            CHECK(std::abs(p.C - d.C) <= 0.005);
            CHECK(std::abs(mdhSc::floorClamp(p.Elow) - mdhSc::floorClamp(d.Elow)) <= 0.1);
            CHECK(std::abs(mdhSc::floorClamp(p.Thd) - mdhSc::floorClamp(d.Thd)) <= 0.1);
            if (sine)
                CHECK(p.Thd <= kMdhSineRawThdCeilingDb);
        }
    }
}

// ΔL_step on the -24 -> -12 dBFS step fixture.
TEST_CASE("MassDynamicsHelpers_DeltaLStep", "[systems][profundum][mass-dynamics]") {
    const double fs = kMdhFs;
    const double f0 = mdhTs::kE1Hz;
    const std::vector<float> x = mdhTs::twoLevelStep(mdhTs::Tone::Saw, f0, fs, 2.0, 2.0);
    const double d = mdh::deltaLStepDb(x, fs, f0, mdhTs::secondsToSamples(fs, 1.0), mdhTs::secondsToSamples(fs, 2.0),
                                       mdhTs::secondsToSamples(fs, 3.0), mdhTs::secondsToSamples(fs, 4.0));
    std::cout << std::setprecision(6) << "[T011 DeltaLStep] unprocessed saw E1: " << d << " dB\n";
    CHECK(std::abs(d - 12.0) <= 0.05);
}

// FR-031 - FR-033: the renderChain contract.
TEST_CASE("MassDynamicsHelpers_RenderChainContract", "[systems][profundum][mass-dynamics]") {
    constexpr std::size_t kN = 48000;
    CHECK(std::abs(mdh::descriptorSeconds(48000.0) - (1.0 + 262144.0 / 48000.0)) <= 1e-12);

    const mdh::ChainConfig withAnchor{.f0 = mdhTs::kE1Hz, .seconds = 1.0, .perSampleDynamicsTaps = true};
    mdh::ChainConfig withoutAnchor = withAnchor;
    withoutAnchor.attachAnchor = false;
    mdh::ChainConfig detached = withAnchor;
    detached.massLawDetached = true;

    const mdh::ChainTaps a = mdh::renderChain(withAnchor);
    const mdh::ChainTaps b = mdh::renderChain(withoutAnchor);
    const mdh::ChainTaps c = mdh::renderChain(detached);

    const auto tapsOf = [](const mdh::ChainTaps& t) {
        return std::array<const std::vector<float>*, 17>{&t.coreL, &t.coreR, &t.foldL, &t.foldR, &t.anchorL,
                                                          &t.anchorR, &t.sumL, &t.sumR, &t.dynL, &t.dynR,
                                                          &t.dcL, &t.dcR, &t.outL, &t.outR, &t.shaperGainDb,
                                                          &t.compGainDb, &t.compGrDb};
    };
    for (const std::vector<float>* v : tapsOf(a))
        CHECK(v->size() == kN);

    // Not vacuous: the core and the Anchor sound; without the Anchor its taps are 0.
    CHECK(std::any_of(a.coreL.begin(), a.coreL.end(), [](float s) { return s != 0.0f; }));
    CHECK(std::any_of(a.anchorL.begin(), a.anchorL.end(), [](float s) { return s != 0.0f; }));
    CHECK(std::all_of(b.anchorL.begin(), b.anchorL.end(), [](float s) { return s == 0.0f; }));

    // FR-032: sum == fold + anchor as a plain float add.
    std::vector<float> addL(kN);
    std::vector<float> addR(kN);
    for (std::size_t i = 0; i < kN; ++i) {
        addL[i] = a.foldL[i] + a.anchorL[i];
        addR[i] = a.foldR[i] + a.anchorR[i];
    }
    CHECK(mdhVectorsBitEqual(a.sumL, addL));
    CHECK(mdhVectorsBitEqual(a.sumR, addR));

    // FR-031: the Anchor does not feed back into the core.
    CHECK(mdhVectorsBitEqual(a.coreL, b.coreL));
    CHECK(mdhVectorsBitEqual(a.coreR, b.coreR));

    // Per-interval records.
    CHECK(a.a1PerInterval.size() == 1500);
    REQUIRE(a.anchorControlsPerInterval.size() == a.anchorLevelPerInterval.size());
    CHECK(a.anchorLevelPerInterval.size() == 1500);
    CHECK(a.massPerInterval.size() == 1500);
    CHECK(a.targetsPerInterval.size() == 1500);
    CHECK(a.cornerHzPerInterval.size() == 1500);
    bool levelsMatch = true;
    for (std::size_t k = 0; k < a.anchorLevelPerInterval.size(); ++k)
        levelsMatch = levelsMatch && mdhSameBits(a.anchorControlsPerInterval[k].level, a.anchorLevelPerInterval[k]);
    CHECK(levelsMatch);
    std::cout << std::setprecision(6) << "[T011 RenderChainContract] E1 a1 (last interval) = " << a.a1PerInterval.back()
              << ", anchor level = " << a.anchorLevelPerInterval.back() << "\n";

    // SC-023 at Mass 50: massLawDetached is memcmp-equal to the law render, tap for tap.
    const auto aTaps = tapsOf(a);
    const auto cTaps = tapsOf(c);
    for (std::size_t k = 0; k < aTaps.size(); ++k) {
        INFO("tap " << k);
        CHECK(mdhVectorsBitEqual(*aTaps[k], *cTaps[k]));
    }
}
