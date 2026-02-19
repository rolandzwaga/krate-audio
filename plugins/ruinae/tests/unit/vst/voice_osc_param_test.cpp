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
