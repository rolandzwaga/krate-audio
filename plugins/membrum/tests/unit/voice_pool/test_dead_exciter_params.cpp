// ==============================================================================
// Dead-parameter regression tests (correctness-audit findings 3, 4, 5)
// ==============================================================================
// FM Ratio (kExciterFMRatioId), Feedback Amount (kExciterFeedbackAmountId), and
// Friction Pressure (kExciterFrictionPressureId) were registered, dispatched,
// stored into PadConfig, and persisted -- but applyPadConfigToSlot() never
// forwarded them to any voice, so they had ZERO audio effect.
//
// Each test renders the SAME pad/exciter/body twice through the full
// VoicePool -> DrumVoice -> ExciterBank -> exciter path, changing ONLY the
// parameter under test, and asserts the rendered audio differs. These tests
// FAIL on the pre-fix code (identical output) and PASS once the parameter is
// plumbed through applyPadConfigToSlot.
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "voice_pool/voice_pool.h"
#include "dsp/pad_config.h"
#include "plugin_ids.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace Membrum;

namespace {

// Render `numSamples` of a single note (MIDI 36 = pad 0) into a mono buffer,
// applying one secondary-exciter parameter value. A fresh pool guarantees
// deterministic per-voice RNG seeds, so the ONLY difference between two
// renders is the parameter value.
std::vector<float> renderPad0(ExciterType exciter,
                              int          padOffset,
                              float        paramValue,
                              float        velocity,
                              int          numSamples)
{
    constexpr double kSR        = 44100.0;
    constexpr int    kBlockSize = 256;

    VoicePool pool;
    pool.prepare(kSR, kBlockSize);
    pool.setPadConfigSelector(0, kPadExciterType, static_cast<int>(exciter));
    pool.setPadConfigField(0, padOffset, paramValue);
    pool.noteOn(36, velocity);

    std::vector<float> mono;
    mono.reserve(static_cast<std::size_t>(numSamples));
    std::vector<float> outL(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> outR(static_cast<std::size_t>(kBlockSize), 0.0f);

    int rendered = 0;
    while (rendered < numSamples)
    {
        const int n = std::min(kBlockSize, numSamples - rendered);
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        pool.processBlock(outL.data(), outR.data(), n);
        for (int i = 0; i < n; ++i)
            mono.push_back(outL[static_cast<std::size_t>(i)]);
        rendered += n;
    }
    return mono;
}

float peakAbs(const std::vector<float>& b)
{
    float p = 0.0f;
    for (float s : b) p = std::max(p, std::fabs(s));
    return p;
}

// Mean absolute difference between two equal-length buffers.
float meanAbsDiff(const std::vector<float>& a, const std::vector<float>& b)
{
    REQUIRE(a.size() == b.size());
    double acc = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
        acc += std::fabs(static_cast<double>(a[i]) - static_cast<double>(b[i]));
    return static_cast<float>(acc / static_cast<double>(a.size()));
}

} // namespace

TEST_CASE("Dead-param: FM Ratio changes FMImpulse output (audit finding 3)",
          "[membrum][voice_pool][dead_param][fmratio]")
{
    constexpr int kN = 2048;
    // norm 0.0 -> ratio 1.0 ; norm 1.0 -> ratio 4.0 (plugin_ids.h: 1.0..4.0).
    const auto lo = renderPad0(ExciterType::FMImpulse, kPadFMRatio, 0.0f, 0.9f, kN);
    const auto hi = renderPad0(ExciterType::FMImpulse, kPadFMRatio, 1.0f, 0.9f, kN);

    REQUIRE(peakAbs(lo) > 1e-4f);
    REQUIRE(peakAbs(hi) > 1e-4f);
    // Different modulator ratios produce audibly different inharmonic spectra.
    CHECK(meanAbsDiff(lo, hi) > 1e-4f);
}

TEST_CASE("Dead-param: Feedback Amount changes Feedback exciter output (audit finding 4)",
          "[membrum][voice_pool][dead_param][feedback]")
{
    constexpr int kN = 2048;
    // Moderate velocity so the drive floor (amount=1) clearly exceeds the
    // velocity-only baseline (amount=0).
    const auto lo = renderPad0(ExciterType::Feedback, kPadFeedbackAmount, 0.0f, 0.3f, kN);
    const auto hi = renderPad0(ExciterType::Feedback, kPadFeedbackAmount, 1.0f, 0.3f, kN);

    REQUIRE(peakAbs(lo) > 1e-4f);
    REQUIRE(peakAbs(hi) > 1e-4f);
    CHECK(meanAbsDiff(lo, hi) > 1e-4f);
}

TEST_CASE("Dead-param: Friction Pressure changes Friction exciter output (audit finding 5)",
          "[membrum][voice_pool][dead_param][friction]")
{
    constexpr int kN = 2048;
    const auto lo = renderPad0(ExciterType::Friction, kPadFrictionPressure, 0.0f, 0.6f, kN);
    const auto hi = renderPad0(ExciterType::Friction, kPadFrictionPressure, 1.0f, 0.6f, kN);

    REQUIRE(peakAbs(lo) > 1e-4f);
    REQUIRE(peakAbs(hi) > 1e-4f);
    CHECK(meanAbsDiff(lo, hi) > 1e-4f);
}

TEST_CASE("Dead-param: FM Ratio amount=0 default preserves the 1.4 bell ratio",
          "[membrum][voice_pool][dead_param][fmratio][default]")
{
    // The per-pad PadConfig default for fmRatio must map to the documented
    // 1.4 Chowning-bell ratio (norm (1.4-1.0)/3.0 = 0.133333), matching the
    // global proxy default -- so plumbing the parameter does not silently
    // change the default FM voice timbre.
    PadConfig cfg{};
    CHECK(cfg.fmRatio == Catch::Approx(0.133333f).margin(1e-5f));
}
