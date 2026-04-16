// Pitch-envelope detector: synth a downward-sweeping sinusoid, assert
// detection fires.
#include "src/tone_shaper_fit.h"
#include "src/segmentation.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

TEST_CASE("Pitch env detector: downsweep 200->50 Hz / 30 ms triggers") {
    constexpr double sr = 44100.0;
    constexpr int N = static_cast<int>(0.5 * sr);
    std::vector<float> x(N);
    constexpr float pi = 3.14159265358979323846f;
    // Phase-accumulated sinusoid sweeping linearly from 200 -> 50 Hz over 30 ms,
    // then sustains at 50 Hz for the rest.
    float phase = 0.0f;
    const float sweepSec = 0.030f;
    for (int i = 0; i < N; ++i) {
        const float t = i / static_cast<float>(sr);
        const float f = (t < sweepSec)
            ? 200.0f + (50.0f - 200.0f) * (t / sweepSec)
            : 50.0f;
        phase += 2.0f * pi * f / static_cast<float>(sr);
        x[i] = 0.5f * std::sin(phase) * std::exp(-1.0f * t);
    }
    MembrumFit::SegmentedSample seg;
    seg.onsetSample = 0;
    seg.attackEndSample = static_cast<std::size_t>(0.020 * sr);
    seg.decayStartSample = static_cast<std::size_t>(0.005 * sr);
    seg.decayEndSample = N;

    const auto track = MembrumFit::trackPitch(x, seg, sr);
    REQUIRE(track.size() >= 4);
    float startHz = 0, endHz = 0, durSec = 0;
    REQUIRE(MembrumFit::detectPitchEnvelope(track, startHz, endHz, durSec));
    REQUIRE(startHz > endHz);
    REQUIRE(durSec  < 0.2f);
}
