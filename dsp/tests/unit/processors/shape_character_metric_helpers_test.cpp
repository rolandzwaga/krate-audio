// ==============================================================================
// Profundum Phase 5 (specs/profundum-phase5-shape-character)
// ==============================================================================
// Self-tests of shape_character_metric_helpers.h (tasks.md T006, deviation 3):
// the differential click count, the D-11 ratio, centroid C, inharmonic
// fraction I, level L and the seeded noise generator.
//
// Layer rule: no systems/ include, directly or through a helper.
// ==============================================================================

#include "shape_character_metric_helpers.h"

#include <krate/dsp/processors/spectral_shape_recipe.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

using namespace Krate::DSP;
namespace SCT = Krate::DSP::ShapeCharacterTest;
namespace HFT = Krate::DSP::HarmonicFoldTest;

namespace {

// Distinct names from the systems helper's kE0Hz…kC3Hz (T008 includes the metric header).
constexpr double kMetricE1Hz = 41.20344461410875;   // MIDI 28
constexpr double kMetricC2Hz = 65.40639132514966;   // MIDI 36
constexpr double kMetricFs = 48000.0;

std::vector<float> metricSine(double fs, double hz, double peak, std::size_t n) {
    std::vector<float> x(n, 0.0f);
    for (std::size_t k = 0; k < n; ++k)
        x[k] = static_cast<float>(peak * std::sin(HFT::kTwoPi * hz * static_cast<double>(k) / fs));
    return x;
}

double maxAbs(const std::vector<float>& x) {
    double m = 0.0;
    for (const float v : x)
        m = std::max(m, std::abs(static_cast<double>(v)));
    return m;
}

}  // namespace

TEST_CASE("ShapeCharacterMetrics_StepIsCounted", "[processors][profundum][shape-character-parts]") {
    const auto n = static_cast<std::size_t>(0.5 * kMetricFs);
    const std::vector<float> reference = metricSine(kMetricFs, kMetricE1Hz, 0.5, n);
    std::vector<float> transition = reference;
    for (std::size_t k = 12000; k < n; ++k)
        transition[k] += 0.5f;

    const SCT::ClickCount c = SCT::differentialClicks(transition.data(), reference.data(), n, kMetricFs, kMetricE1Hz);
    INFO("counted " << c.counted << ", guarded out " << c.guardedOut << ", transition detections " << c.transitionDetections
                    << ", reference detections " << c.referenceDetections << ", N " << c.windowN);
    CHECK(c.windowN == static_cast<std::size_t>(std::floor(kMetricFs / kMetricE1Hz / 8.0)));
    CHECK(c.counted >= 1);
}

TEST_CASE("ShapeCharacterMetrics_IdenticalIsZero", "[processors][profundum][shape-character-parts]") {
    const std::vector<float> saw = HFT::recipeAdditive(SpectralShapeRecipe::kSawAnchor, kMetricE1Hz, kMetricFs, 0.5);
    const SCT::ClickCount c = SCT::differentialClicks(saw.data(), saw.data(), saw.size(), kMetricFs, kMetricE1Hz);
    // Spec premise: the detector flags the steady fixture 12–115 times per 0.5 s (recorded, not gated).
    INFO("reference detections " << c.referenceDetections << " per 0.5 s (spec premise 12-115), N " << c.windowN);
    CHECK(c.transitionDetections == c.referenceDetections);
    CHECK(c.counted == 0);
}

TEST_CASE("ShapeCharacterMetrics_D11Ratio", "[processors][profundum][shape-character-parts]") {
    const auto n = static_cast<std::size_t>(0.25 * kMetricFs);
    const std::vector<float> xa = metricSine(kMetricFs, kMetricE1Hz, 0.5, n);
    const std::vector<float> xb = metricSine(kMetricFs, 3.0 * kMetricE1Hz, 0.5, n);

    // 10 ms linear crossfade a -> b starting at sample 4000.
    const std::size_t start = 4000;
    const auto fade = static_cast<std::size_t>(0.010 * kMetricFs);
    std::vector<float> ga(n, 0.0f);
    std::vector<float> gb(n, 0.0f);
    std::vector<float> y(n, 0.0f);
    for (std::size_t k = 0; k < n; ++k) {
        double t = 0.0;
        if (k >= start + fade)
            t = 1.0;
        else if (k > start)
            t = static_cast<double>(k - start) / static_cast<double>(fade);
        ga[k] = static_cast<float>(1.0 - t);
        gb[k] = static_cast<float>(t);
        y[k] = static_cast<float>(static_cast<double>(ga[k]) * static_cast<double>(xa[k])
                                  + static_cast<double>(gb[k]) * static_cast<double>(xb[k]));
    }

    const double Da = SCT::steadyDeltaMax(xa.data(), 1, n);
    const double Db = SCT::steadyDeltaMax(xb.data(), 1, n);
    const double maxXa = maxAbs(xa);
    const double maxXb = maxAbs(xb);
    REQUIRE(Da > 0.0);
    REQUIRE(Db > Da);

    const double clean = SCT::d11Ratio(y.data(), ga.data(), gb.data(), Da, Db, maxXa, maxXb, 1, n);
    INFO("clean crossfade D-11 ratio " << clean);
    CHECK(clean <= 1.0 + 1e-6);
    CHECK(clean > 0.5);   // the helper is not vacuous: steady segments read their own max delta

    std::vector<float> spiked = y;
    spiked[start + fade / 2] += 0.1f;
    const double spike = SCT::d11Ratio(spiked.data(), ga.data(), gb.data(), Da, Db, maxXa, maxXb, 1, n);
    INFO("spiked D-11 ratio " << spike);
    CHECK(spike > 1.0);
}

TEST_CASE("ShapeCharacterMetrics_Centroid", "[processors][profundum][shape-character-parts]") {
    const auto n = static_cast<std::size_t>(2.0 * kMetricFs);
    REQUIRE(n >= HFT::aliasFftLength(kMetricFs, kMetricC2Hz));

    const std::vector<float> h4 = metricSine(kMetricFs, 4.0 * kMetricC2Hz, 0.5, n);
    const double c4 = SCT::centroidOct(h4.data(), n, kMetricFs, kMetricC2Hz);
    INFO("C of a sine at 4 f0: " << c4 << " oct");
    CHECK(std::abs(c4 - 2.0) <= 0.01);

    const std::vector<float> h1 = metricSine(kMetricFs, kMetricC2Hz, 0.5, n);
    const double c1 = SCT::centroidOct(h1.data(), n, kMetricFs, kMetricC2Hz);
    INFO("C of a sine at f0: " << c1 << " oct");
    CHECK(std::abs(c1) <= 0.01);
}

TEST_CASE("ShapeCharacterMetrics_InharmonicFraction", "[processors][profundum][shape-character-parts]") {
    const std::vector<float> saw = HFT::recipeAdditive(SpectralShapeRecipe::kSawAnchor, kMetricE1Hz, kMetricFs, 3.0);
    REQUIRE(saw.size() >= HFT::aliasFftLength(kMetricFs, kMetricE1Hz));

    const double iSaw = SCT::inharmonicFractionDb(saw.data(), saw.size(), kMetricFs, kMetricE1Hz);
    INFO("I of the saw: " << iSaw << " dB");
    CHECK(iSaw <= -100.0);

    // A sine at 1.5 f0, 30 dB below the saw's RMS (peak = sqrt(2) * RMS).
    const double sawDb = SCT::levelDb(saw.data(), saw.size(), kMetricFs, kMetricE1Hz);
    const double peak = std::sqrt(2.0) * std::pow(10.0, (sawDb - 30.0) / 20.0);
    std::vector<float> mixed(saw.size(), 0.0f);
    for (std::size_t k = 0; k < saw.size(); ++k)
        mixed[k] = static_cast<float>(
            static_cast<double>(saw[k])
            + peak * std::sin(HFT::kTwoPi * 1.5 * kMetricE1Hz * static_cast<double>(k) / kMetricFs));
    const double iMixed = SCT::inharmonicFractionDb(mixed.data(), mixed.size(), kMetricFs, kMetricE1Hz);
    INFO("saw level " << sawDb << " dBFS; I with a -30 dB tone at 1.5 f0: " << iMixed << " dB");
    CHECK(std::abs(iMixed - (-30.0)) <= 1.0);
}

TEST_CASE("ShapeCharacterMetrics_NoiseAndLevel", "[processors][profundum][shape-character-parts]") {
    const auto n = static_cast<std::size_t>(2.0 * kMetricFs);
    const std::vector<float> a = SCT::whiteNoise(n, 0x1234u, -20.0);
    const std::vector<float> b = SCT::whiteNoise(n, 0x1234u, -20.0);
    const std::vector<float> c = SCT::whiteNoise(n, 0x5678u, -20.0);
    CHECK(a == b);   // deterministic per seed
    CHECK(a != c);

    double sum = 0.0;
    for (const float v : a)
        sum += static_cast<double>(v) * static_cast<double>(v);
    const double wholeDb = HFT::safeDb10(sum / static_cast<double>(n));
    CHECK(std::abs(wholeDb - (-20.0)) <= 1e-4);

    // levelDb reads the tail of whole periods; noise at -20 dBFS stays within 0.1 dB of it.
    const double tailDb = SCT::levelDb(a.data(), n, kMetricFs, kMetricC2Hz);
    INFO("levelDb of -20 dBFS noise: " << tailDb);
    CHECK(std::abs(tailDb - (-20.0)) <= 0.1);

    // A sine of peak P reads 20·log10(P/√2) over whole periods.
    const std::vector<float> s = metricSine(kMetricFs, kMetricC2Hz, 0.5, n);
    const double sineDb = SCT::levelDb(s.data(), n, kMetricFs, kMetricC2Hz);
    CHECK(std::abs(sineDb - 20.0 * std::log10(0.5 / std::sqrt(2.0))) <= 0.01);
}
