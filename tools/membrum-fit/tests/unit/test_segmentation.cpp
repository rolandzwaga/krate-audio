// Segmentation: synthetic step + decay should yield onset near the step.
#include "src/segmentation.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

TEST_CASE("Segmentation: locates onset of a synthetic transient within 1024 samples") {
    constexpr double sr = 44100.0;
    constexpr int    N  = 8192;
    constexpr int    onset = 2048;
    std::vector<float> x(N, 0.0f);
    constexpr float pi = 3.14159265358979323846f;
    for (int i = onset; i < N; ++i) {
        const float t = (i - onset) / static_cast<float>(sr);
        x[i] = std::exp(-10.0f * t) * std::sin(2.0f * pi * 220.0f * t);
    }
    const auto seg = MembrumFit::segmentSample(x, sr);
    REQUIRE(static_cast<int>(seg.onsetSample) >= onset - 1024);
    REQUIRE(static_cast<int>(seg.onsetSample) <= onset + 1024);
    REQUIRE(MembrumFit::isSegmentationUsable(seg, sr));
}

TEST_CASE("Segmentation: short sample (<100ms decay) is rejected") {
    constexpr double sr = 44100.0;
    constexpr int N = static_cast<int>(0.05 * sr);  // 50 ms total
    std::vector<float> x(N);
    constexpr float pi = 3.14159265358979323846f;
    for (int i = 0; i < N; ++i) {
        x[i] = std::exp(-30.0f * i / static_cast<float>(sr))
             * std::sin(2.0f * pi * 220.0f * i / sr);
    }
    const auto seg = MembrumFit::segmentSample(x, sr);
    REQUIRE(!MembrumFit::isSegmentationUsable(seg, sr));
}
