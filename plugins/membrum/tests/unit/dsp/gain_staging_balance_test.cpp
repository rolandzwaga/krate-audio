// ==============================================================================
// Gain-staging Step 4 (H-2): layer-vs-body balance
// ==============================================================================
// The modal body carries the timbre-defining parameters (Material, Size,
// StrikePos, damping); the parallel noise + click layers are broadband
// ACCENTS that must sit ~6 dB UNDER the body so they don't mask it. With the
// body normalised to its strike-peak budget (kBodyHeadroom = -6 dBFS), the
// layers should peak around -18 dBFS at mix=1.0. This test pins the
// standalone-gain calibration so a future change to NoiseLayer/ClickLayer
// can't silently re-bury the body -- including the cutoff-tracking correction
// (standaloneGain()) that holds the noise layer at -18 dBFS across the actual
// per-preset cutoffs, not just the reference cutoff.
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

TEST_CASE("Gain staging: noise standalone gain tracks cutoff across presets",
          "[membrum][dsp][gain_staging]")
{
    // Fix C (INVESTIGATION-snare-body): a single fixed standalone gain was
    // calibrated at cutoff norm 0.5 (~849 Hz). Presets running a higher cutoff
    // passed more broadband energy and shipped several dB hot -- the snare's
    // 0.72 (~3.2 kHz) sat ~5 dB above the -18 dBFS budget, level with the body.
    // standaloneGain() compensates so the layer holds ~-18 dBFS at every cutoff.
    struct Case { const char* name; float cutoff; };
    const Case cases[] = {
        {"reference", 0.50f},  // ~849 Hz
        {"snare",     0.72f},  // ~3.2 kHz
        {"cymbal",    0.82f},  // ~6.0 kHz
        {"hat",       0.88f},  // ~8.7 kHz
    };

    for (const auto& c : cases)
    {
        Membrum::NoiseLayer layer;
        layer.prepare(kSr, /*voiceId*/ 0);
        layer.setFilterMode(Krate::DSP::SVFMode::Lowpass);

        Membrum::NoiseLayerParams p{};
        p.mix = 1.0f; p.cutoff = c.cutoff; p.resonance = 0.3f; p.decay = 0.5f; p.color = 0.5f;
        layer.configure(p);
        layer.trigger(1.0f);

        const int n = 8192;
        std::vector<float> out(n, 0.0f);
        layer.processBlock(out.data(), n);

        const float rawPeak = blockPeak(out);
        const float effPeak = rawPeak * layer.standaloneGain();  // per-instance gain

        INFO(c.name << " cutoff=" << c.cutoff
             << " raw=" << dbfs(rawPeak) << " dBFS"
             << " gain=" << layer.standaloneGain()
             << " eff=" << dbfs(effPeak) << " dBFS");

        // Effective peak must stay in the -18 +/-6 dB window at EVERY cutoff,
        // not just the reference. Without the cutoff correction the snare/hat
        // cutoffs land well above -15 dBFS.
        CHECK(dbfs(effPeak) < -13.0f);
        CHECK(dbfs(effPeak) > -24.0f);
    }
}
