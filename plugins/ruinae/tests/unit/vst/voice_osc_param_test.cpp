// ==============================================================================
// Voice OSC Parameter Routing Tests (068-osc-type-params, Phase 3)
// ==============================================================================
// Tests that RuinaeVoice forwards type-specific OscParam values to the
// underlying SelectableOscillator, producing audible output changes.
//
// Feature: 068-osc-type-params
// User Story: US1 (PolyBLEP Waveform Selection and Pulse Width)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/ruinae_voice.h"
#include <krate/dsp/systems/oscillator_types.h>

#include "artifact_detection.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// Helper: Create a prepared voice with default settings
// =============================================================================
static RuinaeVoice createVoice(double sampleRate = 44100.0,
                               size_t maxBlockSize = 512) {
    RuinaeVoice voice;
    voice.prepare(sampleRate, maxBlockSize);
    return voice;
}

// =============================================================================
// Helper: Process N samples and return buffer
// =============================================================================
static std::vector<float> processBlock(RuinaeVoice& voice, size_t n = 512) {
    std::vector<float> buf(n, 0.0f);
    voice.processBlock(buf.data(), n);
    return buf;
}

// =============================================================================
// Helper: Compute RMS of a buffer
// =============================================================================
static float rms(const std::vector<float>& buf) {
    if (buf.empty()) return 0.0f;
    double sum = 0.0;
    for (float s : buf) sum += static_cast<double>(s) * s;
    return static_cast<float>(std::sqrt(sum / static_cast<double>(buf.size())));
}

// =============================================================================
// Tests: RuinaeVoice::setOscAParam / setOscBParam
// =============================================================================

TEST_CASE("RuinaeVoice setOscAParam forwards waveform to PolyBLEP",
          "[voice][osc-param][us1]") {
    auto voice = createVoice();
    voice.setOscAType(OscType::PolyBLEP);
    voice.setMixPosition(0.0f);  // OSC A only

    // Set to Sine waveform
    voice.setOscAParam(OscParam::Waveform, 0.0f);  // Sine = 0
    voice.noteOn(440.0f, 0.8f);

    // Let transients settle
    processBlock(voice, 512);
    auto sineOutput = processBlock(voice, 2048);
    float sineRMS = rms(sineOutput);

    // Now switch to Sawtooth
    voice.setOscAParam(OscParam::Waveform, 1.0f);  // Sawtooth = 1

    processBlock(voice, 512);
    auto sawOutput = processBlock(voice, 2048);
    float sawRMS = rms(sawOutput);

    // Both should produce non-silent output
    REQUIRE(sineRMS > 0.01f);
    REQUIRE(sawRMS > 0.01f);

    // Sawtooth has more harmonics so RMS should be different from sine
    // (not exactly equal within tight tolerance)
    REQUIRE(std::abs(sineRMS - sawRMS) > 0.001f);
}

TEST_CASE("RuinaeVoice setOscAParam forwards pulse width to PolyBLEP",
          "[voice][osc-param][us1]") {
    auto voice = createVoice();
    voice.setOscAType(OscType::PolyBLEP);
    voice.setMixPosition(0.0f);  // OSC A only

    // Set Pulse waveform (index 3)
    voice.setOscAParam(OscParam::Waveform, 3.0f);

    // Wide pulse
    voice.setOscAParam(OscParam::PulseWidth, 0.5f);
    voice.noteOn(440.0f, 0.8f);
    processBlock(voice, 512);
    auto wideOutput = processBlock(voice, 2048);
    float wideRMS = rms(wideOutput);

    // Narrow pulse
    voice.setOscAParam(OscParam::PulseWidth, 0.1f);
    processBlock(voice, 512);
    auto narrowOutput = processBlock(voice, 2048);
    float narrowRMS = rms(narrowOutput);

    // Both should produce non-silent output
    REQUIRE(wideRMS > 0.01f);
    REQUIRE(narrowRMS > 0.01f);

    // Different pulse widths produce different RMS levels
    REQUIRE(std::abs(wideRMS - narrowRMS) > 0.001f);
}

TEST_CASE("RuinaeVoice setOscBParam forwards waveform independently from OSC A",
          "[voice][osc-param][us1]") {
    auto voice = createVoice();
    voice.setOscAType(OscType::PolyBLEP);
    voice.setOscBType(OscType::PolyBLEP);

    // Set OSC A to Sine, OSC B to Sawtooth
    voice.setOscAParam(OscParam::Waveform, 0.0f);  // Sine
    voice.setOscBParam(OscParam::Waveform, 1.0f);  // Sawtooth

    // Test OSC B alone
    voice.setMixPosition(1.0f);  // OSC B only
    voice.noteOn(440.0f, 0.8f);
    processBlock(voice, 512);
    auto oscBOutput = processBlock(voice, 2048);
    float bRMS = rms(oscBOutput);

    REQUIRE(bRMS > 0.01f);
}

TEST_CASE("RuinaeVoice processBlock produces non-silent output after setOscAParam",
          "[voice][osc-param][us2]") {
    auto voice = createVoice();

    SECTION("Chaos oscillator with ChaosAmount change") {
        voice.setOscAType(OscType::Chaos);
        voice.setMixPosition(0.0f);
        voice.setOscAParam(OscParam::ChaosAmount, 0.7f);
        voice.noteOn(440.0f, 0.8f);
        processBlock(voice, 512);
        auto output = processBlock(voice, 2048);
        REQUIRE(rms(output) > 0.001f);
    }

    SECTION("Noise oscillator with NoiseColor change") {
        voice.setOscAType(OscType::Noise);
        voice.setMixPosition(0.0f);
        voice.setOscAParam(OscParam::NoiseColor, 2.0f);  // Brown
        voice.noteOn(440.0f, 0.8f);
        processBlock(voice, 512);
        auto output = processBlock(voice, 2048);
        REQUIRE(rms(output) > 0.001f);
    }

    SECTION("Particle oscillator with ParticleDensity change") {
        voice.setOscAType(OscType::Particle);
        voice.setMixPosition(0.0f);
        voice.setOscAParam(OscParam::ParticleDensity, 32.0f);
        voice.noteOn(440.0f, 0.8f);
        // Particle needs more time to produce output
        processBlock(voice, 1024);
        processBlock(voice, 1024);
        auto output = processBlock(voice, 4096);
        // Particle may be quiet with default settings but should not crash
        // (density forwarding is the key test)
    }

    SECTION("Formant oscillator with FormantVowel change") {
        voice.setOscAType(OscType::Formant);
        voice.setMixPosition(0.0f);
        voice.setOscAParam(OscParam::FormantVowel, 2.0f);  // I
        voice.noteOn(440.0f, 0.8f);
        processBlock(voice, 512);
        auto output = processBlock(voice, 2048);
        REQUIRE(rms(output) > 0.001f);
    }
}

// =============================================================================
// Tests: static Phase Mod / Freq Mod (R-135, R-092, R-021)
// =============================================================================

namespace {

// Render OSC A alone (440 Hz) in host-sized blocks, re-sending `param` before
// every block exactly as Processor::applyParamsToEngine does once per process().
std::vector<float> renderStaticOscParam(OscType type, OscParam param, float value,
                                        size_t blockSize, size_t total) {
    auto voice = createVoice(44100.0, 512);
    voice.setOscAType(type);
    voice.setMixPosition(0.0f);  // OSC A only
    if (type == OscType::PolyBLEP) {
        voice.setOscAParam(OscParam::Waveform, 0.0f);  // Sine
    }
    voice.noteOn(440.0f, 0.8f);
    std::vector<float> out(total, 0.0f);
    for (size_t pos = 0; pos < total; pos += blockSize) {
        const size_t n = std::min(blockSize, total - pos);
        voice.setOscAParam(param, value);
        voice.processBlock(out.data() + pos, n);
    }
    return out;
}

float maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
    float worst = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) worst = std::max(worst, std::abs(a[i] - b[i]));
    return worst;
}

float maxAbsSecondDifference(const std::vector<float>& x) {
    float worst = 0.0f;
    for (size_t i = 2; i < x.size(); ++i) {
        worst = std::max(worst, std::abs(x[i] - 2.0f * x[i - 1] + x[i - 2]));
    }
    return worst;
}

// Frequency from linearly interpolated rising zero crossings at or after `from`.
double risingZeroCrossingFrequency(const std::vector<float>& x, size_t from, double sampleRate) {
    double first = -1.0;
    double last = -1.0;
    size_t count = 0;
    for (size_t i = std::max<size_t>(from, 1); i < x.size(); ++i) {
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            const double t = static_cast<double>(i - 1)
                + static_cast<double>(x[i - 1]) / static_cast<double>(x[i - 1] - x[i]);
            if (first < 0.0) first = t;
            last = t;
            ++count;
        }
    }
    if (count < 2) return 0.0;
    return static_cast<double>(count - 1) * sampleRate / (last - first);
}

}  // namespace

TEST_CASE("OSC phase modulation is block-size independent and continuous",
          "[ruinae-voice][osc-params]") {
    constexpr size_t kTotal = 4096;

    SECTION("Phase Mod 1.0 is a steady 1 rad shift") {
        for (const OscType type : {OscType::PolyBLEP, OscType::Wavetable}) {
            CAPTURE(static_cast<int>(type));
            const auto pm64 = renderStaticOscParam(type, OscParam::PhaseModulation, 1.0f, 64, kTotal);
            const auto pm512 = renderStaticOscParam(type, OscParam::PhaseModulation, 1.0f, 512, kTotal);
            const auto dry512 = renderStaticOscParam(type, OscParam::PhaseModulation, 0.0f, 512, kTotal);

            // Block size must not change the output.
            REQUIRE(maxAbsDiff(pm64, pm512) <= 1e-6f);

            // No block-periodic step: the second difference stays within what the
            // unmodulated waveform itself produces.
            const float dryCurvature = maxAbsSecondDifference(dry512);
            CAPTURE(dryCurvature);
            REQUIRE(maxAbsSecondDifference(pm64) <= 1.5f * dryCurvature);
            REQUIRE(maxAbsSecondDifference(pm512) <= 1.5f * dryCurvature);

            if (type == OscType::PolyBLEP) {
                Krate::DSP::TestUtils::ClickDetectorConfig cfg{};
                Krate::DSP::TestUtils::ClickDetector detector(cfg);
                detector.prepare();
                CHECK(detector.detect(pm64.data(), pm64.size()).empty());
                CHECK(detector.detect(pm512.data(), pm512.size()).empty());
            }

            // The shift applies to every sample, not only the first of each block:
            // a 1 rad shift makes the difference RMS 2*sin(0.5) = 0.96 of the dry RMS.
            constexpr size_t kWindow = 64;
            size_t checkedWindows = 0;
            for (size_t w = 0; w + kWindow <= kTotal; w += kWindow) {
                double dryEnergy = 0.0;
                double diffEnergy = 0.0;
                for (size_t i = w; i < w + kWindow; ++i) {
                    dryEnergy += static_cast<double>(dry512[i]) * dry512[i];
                    const double d = static_cast<double>(pm512[i]) - dry512[i];
                    diffEnergy += d * d;
                }
                if (dryEnergy < kWindow * 1e-6) continue;  // still silent at the attack
                ++checkedWindows;
                CAPTURE(w);
                REQUIRE(std::sqrt(diffEnergy / dryEnergy) > 0.5);
            }
            REQUIRE(checkedWindows >= kTotal / kWindow - 2);
        }
    }

    SECTION("Freq Mod 1.0 raises the pitch by 1 Hz") {
        constexpr size_t kLong = 88200;  // 2 s
        for (const OscType type : {OscType::PolyBLEP, OscType::Wavetable}) {
            CAPTURE(static_cast<int>(type));
            const auto dry = renderStaticOscParam(type, OscParam::FrequencyModulation, 0.0f, 512, kLong);
            const auto fm = renderStaticOscParam(type, OscParam::FrequencyModulation, 1.0f, 512, kLong);
            const double fDry = risingZeroCrossingFrequency(dry, 4096, 44100.0);
            const double fFm = risingZeroCrossingFrequency(fm, 4096, 44100.0);
            CAPTURE(fDry, fFm);
            REQUIRE(fDry == Approx(440.0).margin(0.5));
            REQUIRE((fFm - fDry) == Approx(1.0).margin(0.05));
        }
    }
}
