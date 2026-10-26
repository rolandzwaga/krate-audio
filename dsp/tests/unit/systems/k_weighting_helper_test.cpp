// ==============================================================================
// Profundum Phase 6 (specs/profundum-phase6-mass-dynamics)
// BS.1770 K-weighting helper reference test (FR-050; SC-026)
// T003: validates tests/test_helpers/k_weighting.h against the published
// BS.1770-4 48 kHz coefficient tables and the -3.01 LKFS 997 Hz reference.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/processors/period_peak_detector.h>
#include <krate/dsp/processors/transient_shaper.h>
#include <krate/dsp/systems/mass_dynamics.h>
#include <krate/dsp/systems/mass_law.h>
#include <krate/dsp/systems/profundum_core.h>
#include <krate/dsp/systems/anchor_oscillator.h>
#include <krate/dsp/processors/harmonic_fold.h>
#include <krate/dsp/processors/true_peak_limiter.h>
#include <krate/dsp/primitives/dc_blocker.h>

#include <k_weighting.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <vector>

namespace {

using Krate::DSP::TestUtils::KWeightingCoefficients;
using Krate::DSP::TestUtils::kWeightedLevelDb;
using Krate::DSP::TestUtils::kWeightingCoefficients;

constexpr double kKwTwoPi = 6.28318530717958647692;

/// Full-scale (amplitude 1.0) sine, phase computed in double, stored as float.
std::vector<float> makeKwSine(double freqHz, double fs, double seconds) {
    const auto n = static_cast<std::size_t>(std::llround(seconds * fs));
    std::vector<float> x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = static_cast<float>(
            std::sin(kKwTwoPi * freqHz * static_cast<double>(i) / fs));
    }
    return x;
}

/// 10 s sine, K-weighted level measured from round(1.0 * fs).
double kwSineLevelDb(double freqHz, double fs) {
    const auto x = makeKwSine(freqHz, fs, 10.0);
    const auto from = static_cast<std::size_t>(std::llround(1.0 * fs));
    return kWeightedLevelDb(x.data(), x.size(), fs, from);
}

void checkKwCoeffs(const char* name, const std::array<double, 3>& got,
                   const std::array<double, 3>& want) {
    for (std::size_t i = 0; i < 3; ++i) {
        const double err = std::abs(got[i] - want[i]);
        std::cout << "  " << name << "[" << i << "] got " << std::setprecision(15)
                  << got[i] << " want " << want[i] << " |err| "
                  << std::setprecision(6) << err << "\n";
        INFO(name << "[" << i << "] got " << got[i] << " want " << want[i]);
        CHECK(err <= 1e-6);
    }
}

}  // namespace

TEST_CASE("KWeighting_ReferenceLevels", "[systems][profundum][k-weighting]") {
    std::cout << "KWeighting_ReferenceLevels (SC-026)\n";

    // (1) Coefficients at 48 kHz vs the published BS.1770-4 tables, 1e-6 abs.
    const KWeightingCoefficients c = kWeightingCoefficients(48000.0);
    checkKwCoeffs("b1", c.b1, {1.53512485958697, -2.69169618940638, 1.19839281085285});
    checkKwCoeffs("a1", c.a1, {1.0, -1.69065929318241, 0.73248077421585});
    checkKwCoeffs("b2", c.b2, {1.0, -2.0, 1.0});
    checkKwCoeffs("a2", c.a2, {1.0, -1.99004745483398, 0.99007225036621});

    std::cout << std::setprecision(6);

    // (2) Full-scale 997 Hz sine at 48 kHz reads -3.01 +/- 0.1 LKFS
    //     (model -3.010, plan S12.1).
    const double ref48 = kwSineLevelDb(997.0, 48000.0);
    std::cout << "  997 Hz @ 48000 Hz: " << ref48
              << " LKFS (want -3.01 +/- 0.1)\n";
    CHECK(std::abs(ref48 - (-3.01)) <= 0.1);

    // (3) An E1 sine of the same amplitude reads at least 1 dB lower
    //     (model -5.32 dB re 997 Hz).
    const double e1 = kwSineLevelDb(41.20344, 48000.0);
    std::cout << "  E1 41.20344 Hz @ 48000 Hz: " << e1 << " LKFS, re 997 Hz "
              << (e1 - ref48) << " dB (want <= -1)\n";
    CHECK(e1 <= ref48 - 1.0);

    // (4) The coefficient derivation works at any rate: 44.1 kHz and 96 kHz
    //     read within 0.1 dB of the 48 kHz reading.
    for (const double fs : {44100.0, 96000.0}) {
        const double r = kwSineLevelDb(997.0, fs);
        std::cout << "  997 Hz @ " << fs << " Hz: " << r << " LKFS, re 48 kHz "
                  << (r - ref48) << " dB (want |d| <= 0.1)\n";
        INFO("fs = " << fs);
        CHECK(std::abs(r - ref48) <= 0.1);
    }
}
