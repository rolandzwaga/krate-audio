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
#include "processor/processor.h"
#include "plugin_ids.h"
#include <krate/dsp/core/pitch_utils.h>
#include <krate/dsp/systems/voice_mod_types.h>

#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "test_helpers/reverb_metrics.h"
#include "vst_event_list.h"
#include "vst_param_changes.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
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
// R-039: the voice-tab UI index 7 ("Spectral Tilt") reaches the spectral tilt
// =============================================================================
// The controller sends the UI's ModDestination index; VoiceModDest has
// OscALevel at 7 and SpectralTilt at 9. A raw cast turned "Spectral Tilt" into
// an OSC A level route.

namespace {

/// Render a fresh processor through the same IMessage path the controller uses:
/// optional Velocity -> UI destination 7 route, then C4 held for kBlocks blocks.
/// Returns the left channel.
std::vector<float> renderProcessorWithUiDest7(bool routed, double mixModeNorm,
                                              double oscALevelNorm) {
    constexpr size_t kProcBlock = 512;
    constexpr int kBlocks = 40;

    auto proc = std::make_unique<Ruinae::Processor>();
    proc->initialize(nullptr);
    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = kSampleRate;
    setup.maxSamplesPerBlock = static_cast<Steinberg::int32>(kProcBlock);
    proc->setupProcessing(setup);
    proc->setActive(true);

    if (routed) {
        auto msg = Steinberg::owned(new Steinberg::Vst::HostMessage());
        msg->setMessageID("VoiceModRouteUpdate");
        auto* attrs = msg->getAttributes();
        attrs->setInt("slotIndex", 0);
        attrs->setInt("source", static_cast<Steinberg::int64>(VoiceModSource::Velocity));
        attrs->setInt("destination", 7);  // UI index 7 = "Spectral Tilt"
        attrs->setFloat("amount", 1.0);
        attrs->setInt("curve", 0);
        attrs->setFloat("smoothMs", 0.0);
        attrs->setInt("scale", 2);
        attrs->setInt("bypass", 0);
        attrs->setInt("active", 1);
        proc->notify(msg);
    }

    Krate::Test::ParameterChanges params;
    params.addChange(Ruinae::kMixerModeId, mixModeNorm);
    params.addChange(Ruinae::kMixerPositionId, 0.5);
    params.addChange(Ruinae::kOscALevelId, oscALevelNorm);
    Krate::Test::ParameterChanges noParams;
    Krate::Test::EventList events;
    events.addNoteOn(60, 0.8f);

    std::vector<float> outL(kProcBlock, 0.0f);
    std::vector<float> outR(kProcBlock, 0.0f);
    float* channels[2] = {outL.data(), outR.data()};
    Steinberg::Vst::AudioBusBuffers outBus{};
    outBus.numChannels = 2;
    outBus.channelBuffers32 = channels;

    Steinberg::Vst::ProcessData data{};
    data.processMode = Steinberg::Vst::kRealtime;
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.numSamples = static_cast<Steinberg::int32>(kProcBlock);
    data.numOutputs = 1;
    data.outputs = &outBus;
    data.inputParameterChanges = &params;
    data.inputEvents = &events;

    std::vector<float> rendered;
    rendered.reserve(kProcBlock * kBlocks);
    for (int b = 0; b < kBlocks; ++b) {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        proc->process(data);
        rendered.insert(rendered.end(), outL.begin(), outL.end());
        if (b == 0) {
            events.clear();
            data.inputParameterChanges = &noParams;
        }
    }
    proc->setActive(false);
    proc->terminate();
    return rendered;
}

}  // namespace

TEST_CASE("Voice route UI destination 7 modulates spectral tilt, not OSC A level",
          "[integration][modmatrix][voice]") {
    using Krate::DSP::TestUtils::spectralCentroidHz;

    SECTION("SpectralMorph: the route moves the spectral centroid") {
        constexpr double kSpectralMorph = 1.0;
        const auto unrouted = renderProcessorWithUiDest7(false, kSpectralMorph, 1.0);
        const auto routed = renderProcessorWithUiDest7(true, kSpectralMorph, 1.0);

        // Measure the held tail, well past the morph filter's FFT latency.
        const size_t tailStart = unrouted.size() / 2;
        const std::vector<float> tailUnrouted(unrouted.begin() + static_cast<std::ptrdiff_t>(tailStart),
                                              unrouted.end());
        const std::vector<float> tailRouted(routed.begin() + static_cast<std::ptrdiff_t>(tailStart),
                                            routed.end());
        REQUIRE(computeRMS(tailUnrouted) > 1e-4f);
        REQUIRE(computeRMS(tailRouted) > 1e-4f);

        const double centroidUnrouted = spectralCentroidHz(tailUnrouted, kSampleRate);
        const double centroidRouted = spectralCentroidHz(tailRouted, kSampleRate);
        INFO("centroid unrouted " << centroidUnrouted << " Hz, routed " << centroidRouted << " Hz");
        // Velocity 0.8 x 12 dB/oct = +9.6 dB/oct of tilt: the spectrum brightens.
        CHECK(centroidRouted > centroidUnrouted * 1.1);
    }

    SECTION("CrossfadeMix with OSC A level 0.5: the route leaves the level alone") {
        constexpr double kCrossfade = 0.0;
        const auto unrouted = renderProcessorWithUiDest7(false, kCrossfade, 0.5);
        const auto routed = renderProcessorWithUiDest7(true, kCrossfade, 0.5);
        const float rmsUnrouted = computeRMS(unrouted);
        const float rmsRouted = computeRMS(routed);
        INFO("RMS unrouted " << rmsUnrouted << ", routed " << rmsRouted);
        REQUIRE(rmsUnrouted > 1e-4f);
        CHECK(rmsRouted == Catch::Approx(rmsUnrouted).epsilon(1e-4));
    }
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
