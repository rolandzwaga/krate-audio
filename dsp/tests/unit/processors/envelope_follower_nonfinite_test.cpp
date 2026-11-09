// ==============================================================================
// Layer 2: Processor Tests - EnvelopeFollower, non-finite TU
// ==============================================================================
// Disrumpo audit finding D-013 (fix plan T2.17): one NaN or +Inf input sample
// latched envelope_ / squaredEnvelope_ for good (NaN fails both comparisons;
// Inf gives inf - inf = NaN in Amplitude/RMS and stays Inf in Peak), so every
// later getCurrentValue() was non-finite until reset().
//
// Built only into dsp_ieee_tests (-fno-fast-math): it injects NaN/+Inf built from
// bit patterns through a volatile. Finiteness is read only through
// detail::isFinite.
// ==============================================================================

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/processors/envelope_follower.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <utility>

using namespace Krate::DSP;

namespace {

float efnfFromBits(std::uint32_t bits) noexcept {
    volatile std::uint32_t sink = bits;
    return std::bit_cast<float>(static_cast<std::uint32_t>(sink));
}

} // namespace

TEST_CASE("EnvelopeFollower recovers after one NaN / one +Inf sample",
          "[envelope][nonfinite][D-013]") {
    const std::array<std::pair<DetectionMode, const char*>, 3> modes{
        {{DetectionMode::Amplitude, "Amplitude"}, {DetectionMode::RMS, "RMS"}, {DetectionMode::Peak, "Peak"}}};
    const std::array<std::pair<std::uint32_t, const char*>, 2> bads{{{0x7FC00000u, "NaN"}, {0x7F800000u, "+Inf"}}};

    for (const auto& [mode, modeName] : modes) {
        for (const auto& [bits, badName] : bads) {
            EnvelopeFollower env;
            env.prepare(48000.0, 512);
            env.setMode(mode);
            env.setAttackTime(10.0f);
            env.setReleaseTime(100.0f);
            for (int i = 0; i < 100; ++i) {
                (void)env.processSample(0.5f);
            }
            const float atBad = env.processSample(efnfFromBits(bits));
            for (int i = 0; i < 1000; ++i) {
                (void)env.processSample(0.0f);
            }
            const float value = env.getCurrentValue();
            std::printf("D-013 EnvelopeFollower %s, one %s then 1000 zeros: output at the bad sample %g, "
                        "getCurrentValue %g (finite: %s)\n",
                        modeName, badName, static_cast<double>(atBad), static_cast<double>(value),
                        detail::isFinite(value) ? "yes" : "no");
            INFO(modeName << " / " << badName << ": getCurrentValue = " << value);
            CHECK(detail::isFinite(atBad));
            REQUIRE(detail::isFinite(value));
            REQUIRE(value < 1e-3f);

            // The follower tracks again once finite input returns.
            for (int i = 0; i < 4800; ++i) {
                (void)env.processSample(0.5f);
            }
            REQUIRE(env.getCurrentValue() > 0.1f);
        }
    }
}
