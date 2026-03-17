// ==============================================================================
// Integration Test: Voice Modulation All Targets Regression
// ==============================================================================
// Verifies that ALL per-voice modulation destinations actually affect audio
// output when routed. Regression test for bug where only FilterCutoff target
// worked; FilterResonance, DistortionDrive, TranceGateDepth, OscAPitch, and
// OscBPitch modulation offsets were computed but never applied.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/ruinae_voice.h"
#include <krate/dsp/core/pitch_utils.h>
#include <krate/dsp/systems/voice_mod_types.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>
#include <vector>

using namespace Krate::DSP;

// =============================================================================
// Helpers
// =============================================================================

namespace {

constexpr double kSampleRate = 44100.0;
constexpr size_t kBlockSize = 512;
constexpr size_t kNumBlocks = 20;

/// Create a prepared voice, configure it, set up modulation, trigger a note,
/// and render kNumBlocks of audio.
std::vector<float> renderVoiceWithMod(
    std::function<void(RuinaeVoice&)> configure,
    std::function<void(RuinaeVoice&)> modRoute)
{
    RuinaeVoice voice;
    voice.prepare(kSampleRate, kBlockSize);

    // Default sawtooth on both oscs for harmonic-rich content
    voice.setOscAType(OscType::PolyBLEP);
    voice.setOscBType(OscType::PolyBLEP);

    if (configure) configure(voice);
    if (modRoute) modRoute(voice);

    // Trigger note
    voice.noteOn(60, 0.8f);

    // Render
    std::vector<float> output(kBlockSize * kNumBlocks, 0.0f);
    for (size_t b = 0; b < kNumBlocks; ++b) {
        voice.processBlock(output.data() + b * kBlockSize, kBlockSize);
    }

    return output;
}

float computeRMS(const std::vector<float>& buf) {
    if (buf.empty()) return 0.0f;
    double sum = 0.0;
    for (float s : buf) {
        sum += static_cast<double>(s) * s;
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(buf.size())));
}

bool outputsDiffer(const std::vector<float>& a, const std::vector<float>& b,
                   float threshold = 1e-6f) {
    size_t len = std::min(a.size(), b.size());
    for (size_t i = 0; i < len; ++i) {
        if (std::abs(a[i] - b[i]) > threshold) return true;
    }
    return false;
}

/// Configure Env3 with fast attack and full sustain for reliable modulation.
void configureModEnvFullSustain(RuinaeVoice& voice) {
    auto& env = voice.getModEnvelope();
    env.setAttack(1.0f);
    env.setDecay(10.0f);
    env.setSustain(1.0f);
    env.setRelease(100.0f);
}

void setupModRoute(RuinaeVoice& voice, VoiceModSource source,
                   VoiceModDest dest, float amount, float scale) {
    VoiceModRoute route;
    route.source = source;
    route.destination = dest;
    route.amount = amount;
    voice.setModRoute(0, route);
    voice.setModRouteScale(dest, scale);
}

} // anonymous namespace

// =============================================================================
// Regression: FilterResonance modulation affects audio
// =============================================================================

TEST_CASE("Voice mod route Env3 -> FilterResonance affects audio output",
          "[voice-mod][regression]") {
    auto baseline = renderVoiceWithMod(
        [](RuinaeVoice& v) {
            v.setFilterCutoff(400.0f);
            v.setFilterResonance(1.0f);
        },
        nullptr);

    auto modulated = renderVoiceWithMod(
        [](RuinaeVoice& v) {
            v.setFilterCutoff(400.0f);
            v.setFilterResonance(1.0f);
        },
        [](RuinaeVoice& v) {
            setupModRoute(v, VoiceModSource::Env3, VoiceModDest::FilterResonance,
                          1.0f, 20.0f);
            configureModEnvFullSustain(v);
        });

    REQUIRE(outputsDiffer(baseline, modulated));

    float rmsBase = computeRMS(baseline);
    float rmsMod = computeRMS(modulated);
    INFO("RMS baseline: " << rmsBase << ", RMS modulated: " << rmsMod);
    REQUIRE(rmsMod != Catch::Approx(rmsBase).margin(0.001f));
}

// =============================================================================
// Regression: DistortionDrive modulation affects audio
// =============================================================================

TEST_CASE("Voice mod route Env3 -> DistortionDrive affects audio output",
          "[voice-mod][regression]") {
    auto baseline = renderVoiceWithMod(
        [](RuinaeVoice& v) {
            v.setDistortionType(RuinaeDistortionType::Wavefolder);
            v.setDistortionDrive(0.1f);
            v.setDistortionMix(1.0f);
        },
        nullptr);

    auto modulated = renderVoiceWithMod(
        [](RuinaeVoice& v) {
            v.setDistortionType(RuinaeDistortionType::Wavefolder);
            v.setDistortionDrive(0.1f);
            v.setDistortionMix(1.0f);
        },
        [](RuinaeVoice& v) {
            setupModRoute(v, VoiceModSource::Env3, VoiceModDest::DistortionDrive,
                          1.0f, 0.8f);
            configureModEnvFullSustain(v);
        });

    REQUIRE(outputsDiffer(baseline, modulated));

    float rmsBase = computeRMS(baseline);
    float rmsMod = computeRMS(modulated);
    INFO("RMS baseline: " << rmsBase << ", RMS modulated: " << rmsMod);
    REQUIRE(rmsMod != Catch::Approx(rmsBase).margin(0.001f));
}

// =============================================================================
// Regression: TranceGateDepth modulation affects audio
// =============================================================================

TEST_CASE("Voice mod route Env3 -> TranceGateDepth affects audio output",
          "[voice-mod][regression]") {
    auto baseline = renderVoiceWithMod(
        [](RuinaeVoice& v) {
            v.setTranceGateEnabled(true);
            v.setTranceGateDepth(1.0f);
            v.setTranceGateRate(8.0f);
            for (int i = 0; i < 16; ++i) {
                v.setTranceGateStep(i, (i % 2 == 0) ? 1.0f : 0.0f);
            }
        },
        nullptr);

    auto modulated = renderVoiceWithMod(
        [](RuinaeVoice& v) {
            v.setTranceGateEnabled(true);
            v.setTranceGateDepth(1.0f);
            v.setTranceGateRate(8.0f);
            for (int i = 0; i < 16; ++i) {
                v.setTranceGateStep(i, (i % 2 == 0) ? 1.0f : 0.0f);
            }
        },
        [](RuinaeVoice& v) {
            // Negative: Env3 at sustain pushes depth to 0 → gate bypassed
            setupModRoute(v, VoiceModSource::Env3, VoiceModDest::TranceGateDepth,
                          -1.0f, 1.0f);
            configureModEnvFullSustain(v);
        });

    // Primary assertion: modulation route must produce different output
    REQUIRE(outputsDiffer(baseline, modulated));
}

// =============================================================================
// Regression: OscAPitch modulation affects audio
// =============================================================================

TEST_CASE("Voice mod route Env3 -> OscAPitch affects audio output",
          "[voice-mod][regression]") {
    auto baseline = renderVoiceWithMod(
        [](RuinaeVoice&) {},
        nullptr);

    auto modulated = renderVoiceWithMod(
        [](RuinaeVoice&) {},
        [](RuinaeVoice& v) {
            setupModRoute(v, VoiceModSource::Env3, VoiceModDest::OscAPitch,
                          1.0f, 12.0f);
            configureModEnvFullSustain(v);
        });

    REQUIRE(outputsDiffer(baseline, modulated));
}

// =============================================================================
// Regression: OscBPitch modulation affects audio
// =============================================================================

TEST_CASE("Voice mod route Env3 -> OscBPitch affects audio output",
          "[voice-mod][regression]") {
    auto baseline = renderVoiceWithMod(
        [](RuinaeVoice& v) {
            v.setMixPosition(0.5f);
        },
        nullptr);

    auto modulated = renderVoiceWithMod(
        [](RuinaeVoice& v) {
            v.setMixPosition(0.5f);
        },
        [](RuinaeVoice& v) {
            setupModRoute(v, VoiceModSource::Env3, VoiceModDest::OscBPitch,
                          1.0f, 7.0f);
            configureModEnvFullSustain(v);
        });

    REQUIRE(outputsDiffer(baseline, modulated));
}

// =============================================================================
// Sanity: FilterCutoff still works (existing functionality)
// =============================================================================

TEST_CASE("Voice mod route Env3 -> FilterCutoff affects audio output (sanity)",
          "[voice-mod][regression]") {
    auto baseline = renderVoiceWithMod(
        [](RuinaeVoice& v) {
            v.setFilterCutoff(200.0f);
        },
        nullptr);

    auto modulated = renderVoiceWithMod(
        [](RuinaeVoice& v) {
            v.setFilterCutoff(200.0f);
        },
        [](RuinaeVoice& v) {
            setupModRoute(v, VoiceModSource::Env3, VoiceModDest::FilterCutoff,
                          1.0f, 48.0f);
            configureModEnvFullSustain(v);
        });

    REQUIRE(outputsDiffer(baseline, modulated));
}
