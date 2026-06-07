// ==============================================================================
// Gain-staging Step 4 (H-2): layer-vs-body balance
// ==============================================================================
// The modal body carries the timbre-defining parameters (Material, Size,
// StrikePos, damping); the parallel noise + click layers are broadband
// ACCENTS that must sit ~6 dB UNDER the body so they don't mask it. With the
// body normalised to ~-12 dBFS (kBodyHeadroom), the layers should peak around
// -18 dBFS at mix=1.0. This test pins the standalone-gain calibration so a
// future change to NoiseLayer/ClickLayer can't silently re-bury the body.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "dsp/noise_layer.h"
#include "dsp/click_layer.h"

#include <cmath>
#include <vector>

namespace {

constexpr double kSr = 48000.0;

float blockPeak(const std::vector<float>& x)
{
    float p = 0.0f;
    for (float s : x) p = std::max(p, std::fabs(s));
    return p;
}

float dbfs(float lin) { return 20.0f * std::log10(std::max(lin, 1.0e-9f)); }

} // namespace

TEST_CASE("Gain staging: noise layer peaks ~6 dB under the -12 dBFS body",
          "[membrum][dsp][gain_staging]")
{
    Membrum::NoiseLayer layer;
    layer.prepare(kSr, /*voiceId*/ 0);
    layer.setFilterMode(Krate::DSP::SVFMode::Lowpass);  // matches DrumVoice path

    Membrum::NoiseLayerParams p{};
    p.mix = 1.0f; p.cutoff = 0.5f; p.resonance = 0.3f; p.decay = 0.5f; p.color = 0.5f;
    layer.configure(p);
    layer.trigger(1.0f);

    const int n = 8192;
    std::vector<float> out(n, 0.0f);
    layer.processBlock(out.data(), n);

    const float rawPeak = blockPeak(out);
    const float effPeak = rawPeak * Membrum::NoiseLayer::kStandaloneOutputGain;

    INFO("noise raw peak=" << rawPeak << " (" << dbfs(rawPeak) << " dBFS)");
    INFO("noise standalone gain=" << Membrum::NoiseLayer::kStandaloneOutputGain);
    INFO("noise effective peak=" << effPeak << " (" << dbfs(effPeak) << " dBFS)");

    // Target -18 dBFS (~0.126), tolerance +/-3 dB to allow for noise variance.
    REQUIRE(dbfs(effPeak) < -15.0f);  // at least ~3 dB under the -12 dBFS body
    REQUIRE(dbfs(effPeak) > -24.0f);  // not so quiet the accent disappears
}

TEST_CASE("Gain staging: click layer peaks ~6 dB under the -12 dBFS body",
          "[membrum][dsp][gain_staging]")
{
    Membrum::ClickLayer layer;
    layer.prepare(kSr, /*voiceId*/ 0);

    Membrum::ClickLayerParams p{};
    p.mix = 1.0f; p.contactMs = 0.5f; p.brightness = 0.5f;
    layer.configure(p);
    layer.trigger(1.0f);

    const int n = 4096;
    std::vector<float> out(n, 0.0f);
    layer.processBlock(out.data(), n);

    const float rawPeak = blockPeak(out);
    const float effPeak = rawPeak * Membrum::ClickLayer::kStandaloneOutputGain;

    INFO("click raw peak=" << rawPeak << " (" << dbfs(rawPeak) << " dBFS)");
    INFO("click standalone gain=" << Membrum::ClickLayer::kStandaloneOutputGain);
    INFO("click effective peak=" << effPeak << " (" << dbfs(effPeak) << " dBFS)");

    REQUIRE(dbfs(effPeak) < -15.0f);
    REQUIRE(dbfs(effPeak) > -24.0f);
}
