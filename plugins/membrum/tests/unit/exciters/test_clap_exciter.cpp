// ==============================================================================
// ClapExciter contract tests (HAND-CLAP-PLAN Section 5)
// ==============================================================================
// Contract invariants:
//   (1) multi-burst: >= 3 distinct envelope humps in the first 45 ms
//   (2) one-shot: returns exactly 0.0 once past the burst train; isActive()
//       goes false
//   (3) bit-identity: same voiceId + same velocity => sample-identical output
//       (deterministic re-seed on trigger, FR-124)
//   (4) allocation-free trigger + process
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "dsp/exciters/clap_exciter.h"
#include "exciter_test_helpers.h"

#include <allocation_detector.h>

#include <algorithm>
#include <cmath>
#include <vector>

using membrum_exciter_tests::isFiniteSample;

namespace {

// Rectified peak-hold envelope follower (~1 ms release) for hump counting.
std::vector<float> envelopeOf(const std::vector<float>& x, double sr) {
    const float a = static_cast<float>(std::exp(-1.0 / (0.001 * sr)));
    std::vector<float> env(x.size());
    float state = 0.0f;
    for (size_t i = 0; i < x.size(); ++i) {
        const float rect = std::fabs(x[i]);
        state = rect > state ? rect : rect + a * (state - rect);
        env[i] = state;
    }
    return env;
}

// Count local maxima above relThresh * globalPeak, merged within minSepSec.
int countHumps(const std::vector<float>& env, double sr, double windowSec,
               float relThresh, double minSepSec) {
    const size_t n = std::min(env.size(), static_cast<size_t>(windowSec * sr));
    float peak = 0.0f;
    for (size_t i = 0; i < n; ++i) peak = std::max(peak, env[i]);
    const float thresh = relThresh * peak;
    const auto minSep = static_cast<size_t>(minSepSec * sr);
    int count = 0;
    size_t last = 0;
    bool haveLast = false;
    for (size_t i = 1; i + 1 < n; ++i) {
        if (env[i] < thresh) continue;
        if (env[i] >= env[i - 1] && env[i] > env[i + 1]) {
            if (!haveLast || i - last >= minSep) {
                ++count;
                last = i;
                haveLast = true;
            }
        }
    }
    return count;
}

}  // namespace

TEST_CASE("ClapExciter: >= 3 envelope humps in first 45 ms, then exact silence",
          "[membrum][exciter][clap][burst]")
{
    constexpr double kSR = 48000.0;
    constexpr int kSamples = 2400;  // 50 ms -- past the ~42 ms burst train
    Membrum::ClapExciter exc;
    exc.prepare(kSR, 0);
    exc.trigger(1.0f);

    std::vector<float> out(kSamples);
    bool finite = true;
    for (int i = 0; i < kSamples; ++i) {
        out[static_cast<size_t>(i)] = exc.process(0.0f);
        if (!isFiniteSample(out[static_cast<size_t>(i)])) finite = false;
    }
    REQUIRE(finite);

    const auto env = envelopeOf(out, kSR);
    const int humps = countHumps(env, kSR, 0.045, 0.25f, 0.005);
    INFO("envelope humps in first 45 ms: " << humps);
    CHECK(humps >= 3);

    // One-shot contract: past the train the exciter is inactive and returns
    // exactly 0.0.
    CHECK_FALSE(exc.isActive());
    for (int i = 0; i < 64; ++i)
        CHECK(exc.process(0.0f) == 0.0f);
}

TEST_CASE("ClapExciter: bit-identical output for same voiceId + velocity (FR-124)",
          "[membrum][exciter][clap][deterministic]")
{
    constexpr double kSR = 48000.0;
    constexpr int kSamples = 2400;
    Membrum::ClapExciter a, b;
    a.prepare(kSR, 7);
    b.prepare(kSR, 7);

    // Perturb `a` first: a stolen/choked voice must still render identically
    // after retrigger (deterministic re-seed).
    a.trigger(0.3f);
    for (int i = 0; i < 100; ++i) (void)a.process(0.0f);

    a.trigger(0.8f);
    b.trigger(0.8f);
    for (int i = 0; i < kSamples; ++i) {
        const float sa = a.process(0.0f);
        const float sb = b.process(0.0f);
        if (sa != sb) {
            INFO("diverged at sample " << i << ": " << sa << " vs " << sb);
            REQUIRE(sa == sb);
        }
    }
    SUCCEED("bit-identical over full train");
}

TEST_CASE("ClapExciter: trigger + process is allocation-free",
          "[membrum][exciter][clap][alloc]")
{
    Membrum::ClapExciter exc;
    exc.prepare(48000.0, 0);
    {
        TestHelpers::AllocationScope scope;
        exc.trigger(0.8f);
        for (int i = 0; i < 2400; ++i)
            (void)exc.process(0.0f);
        CHECK(scope.getAllocationCount() == 0);
    }
}
