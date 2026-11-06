// =============================================================================
// Smoke test for the Disrumpo processor render rig (fix plan T1.00)
// =============================================================================

#include "../test_support/disrumpo_rig.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace Disrumpo::TestSupport;

TEST_CASE("DisrumpoRig renders a block and reports level", "[disrumpo][test-support]") {
    constexpr double kSampleRate = 48000.0;
    constexpr double kTwoPi = 6.283185307179586476925;
    constexpr size_t kLen = 24000;  // 0.5 s
    const double amp = std::pow(10.0, -12.0 / 20.0);

    std::vector<float> in(kLen);
    for (size_t i = 0; i < kLen; ++i)
        in[i] = static_cast<float>(amp * std::sin(kTwoPi * 1000.0 * static_cast<double>(i) / kSampleRate));

    Rig rig(kSampleRate);
    std::vector<float> outL, outR;
    rig.render(in, in, outL, outR);

    REQUIRE(outL.size() == kLen);
    REQUIRE(outR.size() == kLen);

    const double rmsDb = toDb(std::max(rmsOf(outL.data(), kLen), rmsOf(outR.data(), kLen)));
    std::printf("DisrumpoRig smoke: output RMS = %.2f dBFS\n", rmsDb);

    REQUIRE(Krate::DSP::detail::isFinite(rmsDb));
    REQUIRE(rmsDb > -60.0);
}
