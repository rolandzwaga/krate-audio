// ==============================================================================
// Profundum Phase 5 (specs/profundum-phase5-shape-character)
// ==============================================================================
// OversampledResidualPath (plan S1 C-1, S2, S3; tasks.md T005):
//   constants (C-1), factor table (FR-068), res = 0 round trip == halfband pair bit-exact
//   (the SC-027 premise), residual notch depth, residual DC blocking, reset / finite and the
//   192 kHz DC-corner record.
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/primitives/polyphase_halfband_oversampler.h>
#include <krate/dsp/processors/harmonic_fold.h>
#include <krate/dsp/processors/oversampled_residual_path.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

using namespace Krate::DSP;

namespace {

/// xorshift32 uniform white noise in [-0.5, 0.5).
std::vector<float> orpNoise(std::size_t n, std::uint32_t seed) {
    std::vector<float> v(n);
    std::uint32_t s = seed;
    for (auto& x : v) {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        x = static_cast<float>(static_cast<double>(s) / 4294967296.0 - 0.5);
    }
    return v;
}

}  // namespace

// plan S1 C-1: HarmonicFold's notch constants are private (harmonic_fold.h:300-301), so they are
// asserted here literally; its DC corner is public (harmonic_fold.h:91).
TEST_CASE("OversampledResidualPath_Constants", "[processors][profundum][shape-character-parts]") {
    STATIC_REQUIRE(OversampledResidualPath::kNotchRatios == std::array<float, 3>{0.50f, 0.52f, 0.55f});
    STATIC_REQUIRE(OversampledResidualPath::kNotchQs == std::array<float, 3>{1.5f, 2.0f, 1.5f});
    STATIC_REQUIRE(OversampledResidualPath::kResidualDcHz == HarmonicFold::kDcCornerHz);
    STATIC_REQUIRE(OversampledResidualPath::kMaxFactor == std::size_t{4});
    STATIC_REQUIRE(OversampledResidualPath::kFourTimesLimitHz == 100000.0);
}

TEST_CASE("OversampledResidualPath_FactorTable", "[processors][profundum][shape-character-parts]") {
    for (const double fs : {44100.0, 48000.0, 88200.0, 96000.0}) {
        OversampledResidualPath p;
        p.prepare(fs);
        INFO("fs = " << fs);
        CHECK(p.factor() == std::size_t{4});
    }
    OversampledResidualPath p;
    p.prepare(192000.0);
    CHECK(p.factor() == std::size_t{2});
}

// SC-027 premise: with res == 0 the path is exactly a halfband round trip.
TEST_CASE("OversampledResidualPath_LinearRoundTripExact", "[processors][profundum][shape-character-parts]") {
    constexpr std::size_t kN = 8192;
    const auto x = orpNoise(kN, 0x1234u);
    for (const double fs : {48000.0, 192000.0}) {
        OversampledResidualPath path;
        path.prepare(fs);
        PolyphaseHalfbandOversampler refUp;
        PolyphaseHalfbandOversampler refDown;
        refUp.setFactor(path.factor());
        refDown.setFactor(path.factor());
        REQUIRE(refUp.factor() == path.factor());

        std::array<float, OversampledResidualPath::kMaxFactor> u{};
        std::array<float, OversampledResidualPath::kMaxFactor> ru{};
        const std::array<float, OversampledResidualPath::kMaxFactor> zeros{};
        std::size_t mismatches = 0;
        for (std::size_t i = 0; i < kN; ++i) {
            path.upsample(x[i], u.data());
            const float y = path.downsample(u.data(), zeros.data());
            refUp.upsample(x[i], ru.data());
            const float r = refDown.downsample(ru.data());
            if (std::bit_cast<std::uint32_t>(y) != std::bit_cast<std::uint32_t>(r))
                ++mismatches;
        }
        INFO("fs = " << fs << ", factor = " << path.factor());
        CHECK(mismatches == 0);
    }
}

TEST_CASE("OversampledResidualPath_ResidualNotches", "[processors][profundum][shape-character-parts]") {
    constexpr double fs = 48000.0;
    constexpr double kAmp = 0.5;
    const auto settle = static_cast<std::size_t>(0.5 * fs);
    const auto measure = static_cast<std::size_t>(0.25 * fs);
    for (const float ratio : OversampledResidualPath::kNotchRatios) {
        OversampledResidualPath p;
        p.prepare(fs);
        const std::size_t F = p.factor();
        const double osRate = fs * static_cast<double>(F);
        const double w = 2.0 * std::numbers::pi * static_cast<double>(ratio) * fs / osRate;
        std::array<float, OversampledResidualPath::kMaxFactor> lin{};
        std::array<float, OversampledResidualPath::kMaxFactor> res{};
        std::size_t k = 0;
        double sumSq = 0.0;
        for (std::size_t i = 0; i < settle + measure; ++i) {
            for (std::size_t j = 0; j < F; ++j, ++k)
                res[j] = static_cast<float>(kAmp * std::sin(w * static_cast<double>(k)));
            const float y = p.downsample(lin.data(), res.data());
            if (i >= settle)
                sumSq += static_cast<double>(y) * static_cast<double>(y);
        }
        const double outRms = std::sqrt(sumSq / static_cast<double>(measure));
        const double toneRms = kAmp / std::numbers::sqrt2;
        const double depthDb = 20.0 * std::log10(std::max(outRms, 1e-30) / toneRms);
        INFO("ratio = " << ratio << ", output re tone = " << depthDb << " dB");
        CHECK(depthDb <= -60.0);
    }
}

TEST_CASE("OversampledResidualPath_ResidualDcBlocked", "[processors][profundum][shape-character-parts]") {
    constexpr double fs = 48000.0;
    OversampledResidualPath p;
    p.prepare(fs);
    std::array<float, OversampledResidualPath::kMaxFactor> lin{};
    std::array<float, OversampledResidualPath::kMaxFactor> res{};
    res.fill(0.1f);
    const auto settle = static_cast<std::size_t>(fs);
    constexpr std::size_t kWindow = 4800;
    double sum = 0.0;
    for (std::size_t i = 0; i < settle + kWindow; ++i) {
        const float y = p.downsample(lin.data(), res.data());
        if (i >= settle)
            sum += static_cast<double>(y);
    }
    const double mean = sum / static_cast<double>(kWindow);
    INFO("mean of the last 4800 samples = " << mean);
    CHECK(std::abs(mean) <= 1e-4);
}

TEST_CASE("OversampledResidualPath_ResetAndFinite", "[processors][profundum][shape-character-parts]") {
    const auto x = orpNoise(4096, 0xBEEFu);
    for (const double fs : {48000.0, 192000.0}) {
        OversampledResidualPath p;
        p.prepare(fs);
        std::array<float, OversampledResidualPath::kMaxFactor> u{};
        std::array<float, OversampledResidualPath::kMaxFactor> res{};
        for (const float v : x) {
            p.upsample(v, u.data());
            for (std::size_t j = 0; j < p.factor(); ++j)
                res[j] = 0.5f * u[j] * u[j];
            (void)p.downsample(u.data(), res.data());
        }
        CHECK(p.stateFinite());

        p.reset();
        CHECK(p.stateFinite());
        res.fill(0.0f);
        std::size_t nonZero = 0;
        for (std::size_t i = 0; i < 1024; ++i) {
            p.upsample(0.0f, u.data());
            const float y = p.downsample(u.data(), res.data());
            if (y != 0.0f)
                ++nonZero;
        }
        INFO("fs = " << fs);
        CHECK(nonZero == 0);
    }

    // Record: the effective residual DC corner at 192 kHz. DCBlocker clamps R to 0.9999
    // (dc_blocker.h:236), so the 2 Hz request becomes about 3.06 Hz there (plan S3). Measured
    // from the decay of the residual step response: y[n + N] / y[n] = R^N.
    constexpr double fs = 192000.0;
    OversampledResidualPath p;
    p.prepare(fs);
    std::array<float, OversampledResidualPath::kMaxFactor> lin{};
    std::array<float, OversampledResidualPath::kMaxFactor> res{};
    res.fill(1.0f);
    constexpr std::size_t kA = 2000;
    constexpr std::size_t kB = 12000;
    double ya = 0.0;
    double yb = 0.0;
    for (std::size_t i = 0; i <= kB; ++i) {
        const float y = p.downsample(lin.data(), res.data());
        if (i == kA)
            ya = y;
        if (i == kB)
            yb = y;
    }
    REQUIRE(ya > 0.0);
    REQUIRE(yb > 0.0);
    const double R = std::pow(yb / ya, 1.0 / static_cast<double>(kB - kA));
    const double cornerHz = -std::log(R) * fs / (2.0 * std::numbers::pi);
    INFO("effective residual DC corner at 192 kHz = " << cornerHz << " Hz (requested "
         << OversampledResidualPath::kResidualDcHz << " Hz)");
    CHECK(cornerHz == Catch::Approx(3.06).margin(0.1));
}

// primeDc (FR-026 / FR-054 pre-roll, user ruling 2026-10-16): after a DC residual has been running, priming the
// blocker to that mean makes the next outputs ~0 at once instead of draining over the 2 Hz time constant.
TEST_CASE("OversampledResidualPath_PrimeDc", "[processors][profundum][shape-character-parts]") {
    constexpr double fs = 48000.0;
    OversampledResidualPath p;
    p.prepare(fs);
    std::array<float, OversampledResidualPath::kMaxFactor> lin{};
    std::array<float, OversampledResidualPath::kMaxFactor> res{};
    res.fill(0.7f);
    float before = 0.0f;
    for (std::size_t i = 0; i < 2000; ++i)
        before = p.downsample(lin.data(), res.data());
    // Un-primed: 2000 samples (42 ms) into a 2 Hz blocker, most of the DC is still passing.
    REQUIRE(before > 0.4f);
    REQUIRE(p.dcInput() == Catch::Approx(0.7).margin(1e-4));
    p.primeDc(p.dcInput());
    float worst = 0.0f;
    for (std::size_t i = 0; i < 200; ++i)
        worst = std::max(worst, std::abs(p.downsample(lin.data(), res.data())));
    INFO("max |output| over 200 samples after primeDc: " << worst << " (before priming: " << before << ")");
    CHECK(worst < 1e-3f);
    CHECK(p.stateFinite());
}
