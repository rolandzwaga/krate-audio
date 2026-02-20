// ==============================================================================
// OSC A/B Independence Tests (068-osc-type-params, Phase 7)
// ==============================================================================
// Tests that OSC A and OSC B are fully independent: parameter changes on one
// oscillator do NOT affect the other, both at the voice/DSP level and at the
// atomic storage level.
//
// Feature: 068-osc-type-params
// User Story: US6 (OSC B Parity)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/ruinae_voice.h"
#include "engine/ruinae_engine.h"
#include "parameters/osc_a_params.h"
#include "parameters/osc_b_params.h"
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
// T058: Voice-level OSC A/B independence
// =============================================================================

TEST_CASE("OSC A and OSC B are independently configurable at voice level",
          "[voice][osc-param][us6][independence]") {
    auto voice = createVoice();
    voice.setOscAType(OscType::PolyBLEP);
    voice.setOscBType(OscType::PolyBLEP);

    // Set OSC A to Sawtooth, OSC B to Square
    voice.setOscAParam(OscParam::Waveform, 1.0f);  // Sawtooth
    voice.setOscBParam(OscParam::Waveform, 2.0f);  // Square

    voice.noteOn(440.0f, 0.8f);

    // Capture OSC B output (mix fully to B)
    voice.setMixPosition(1.0f);
    processBlock(voice, 512);  // Settle
    auto oscBOutput1 = processBlock(voice, 2048);
    float bRMS1 = rms(oscBOutput1);

    // Now change OSC A waveform to Sine -- should NOT affect OSC B
    voice.setOscAParam(OscParam::Waveform, 0.0f);  // Sine

    processBlock(voice, 512);  // Settle
    auto oscBOutput2 = processBlock(voice, 2048);
    float bRMS2 = rms(oscBOutput2);

    // OSC B should produce non-silent output
    REQUIRE(bRMS1 > 0.01f);
    REQUIRE(bRMS2 > 0.01f);

    // OSC B output RMS should remain essentially the same after changing OSC A
    // (Allow small floating-point variation but not a waveform-level change)
    REQUIRE(bRMS2 == Approx(bRMS1).margin(0.01f));
}

TEST_CASE("OSC A waveform change does not alter OSC B waveform at engine level",
          "[engine][osc-param][us6][independence]") {
    auto engine = std::make_unique<RuinaeEngine>();
    engine->prepare(44100.0, 512);
    engine->setPolyphony(1);

    // Set both to PolyBLEP
    engine->setOscAType(OscType::PolyBLEP);
    engine->setOscBType(OscType::PolyBLEP);

    // Set OSC A to Sawtooth, OSC B to Square
    engine->setOscAParam(OscParam::Waveform, 1.0f);  // Sawtooth
    engine->setOscBParam(OscParam::Waveform, 2.0f);  // Square

    // Listen to OSC B only
    engine->setMixPosition(1.0f);

    engine->noteOn(60, 100);

    std::vector<float> left(512, 0.0f);
    std::vector<float> right(512, 0.0f);
    engine->processBlock(left.data(), right.data(), 512);  // Settle
    engine->processBlock(left.data(), right.data(), 512);

    float bRMS1 = 0.0f;
    {
        double sum = 0.0;
        for (size_t i = 0; i < 512; ++i)
            sum += static_cast<double>(left[i]) * left[i];
        bRMS1 = static_cast<float>(std::sqrt(sum / 512.0));
    }

    // Change OSC A to Sine -- must not affect OSC B
    engine->setOscAParam(OscParam::Waveform, 0.0f);  // Sine

    engine->processBlock(left.data(), right.data(), 512);
    engine->processBlock(left.data(), right.data(), 512);

    float bRMS2 = 0.0f;
    {
        double sum = 0.0;
        for (size_t i = 0; i < 512; ++i)
            sum += static_cast<double>(left[i]) * left[i];
        bRMS2 = static_cast<float>(std::sqrt(sum / 512.0));
    }

    REQUIRE(bRMS1 > 0.001f);
    REQUIRE(bRMS2 > 0.001f);
    REQUIRE(bRMS2 == Approx(bRMS1).margin(0.02f));
}

TEST_CASE("OSC A and OSC B can use different oscillator types independently",
          "[voice][osc-param][us6][independence]") {
    auto voice = createVoice();

    // Set OSC A to Chaos, OSC B to Noise -- completely different types
    voice.setOscAType(OscType::Chaos);
    voice.setOscBType(OscType::Noise);

    voice.setOscAParam(OscParam::ChaosAmount, 0.7f);
    voice.setOscBParam(OscParam::NoiseColor, 1.0f);  // Pink

    voice.noteOn(440.0f, 0.8f);

    // Both oscillators should produce output
    voice.setMixPosition(0.0f);  // OSC A only
    processBlock(voice, 512);
    auto oscAOutput = processBlock(voice, 2048);

    voice.setMixPosition(1.0f);  // OSC B only
    processBlock(voice, 512);
    auto oscBOutput = processBlock(voice, 2048);

    REQUIRE(rms(oscAOutput) > 0.001f);
    REQUIRE(rms(oscBOutput) > 0.001f);
}

// =============================================================================
// T059: Atomic storage independence
// =============================================================================

TEST_CASE("OscAParams and OscBParams atomic storage is independent",
          "[params][osc-param][us6][independence]") {
    Ruinae::OscAParams oscA;
    Ruinae::OscBParams oscB;

    // Both should start with spec defaults
    REQUIRE(oscA.waveform.load() == 1);  // Sawtooth
    REQUIRE(oscB.waveform.load() == 1);  // Sawtooth

    SECTION("Setting OSC A waveform does not affect OSC B waveform") {
        oscA.waveform.store(0);   // Sine
        oscB.waveform.store(3);   // Pulse

        REQUIRE(oscA.waveform.load() == 0);
        REQUIRE(oscB.waveform.load() == 3);

        // Change A again -- B must remain unchanged
        oscA.waveform.store(4);   // Triangle
        REQUIRE(oscB.waveform.load() == 3);  // Still Pulse
    }

    SECTION("Setting OSC B pulseWidth does not affect OSC A pulseWidth") {
        oscA.pulseWidth.store(0.2f);
        oscB.pulseWidth.store(0.8f);

        REQUIRE(oscA.pulseWidth.load() == Approx(0.2f));
        REQUIRE(oscB.pulseWidth.load() == Approx(0.8f));

        oscB.pulseWidth.store(0.1f);
        REQUIRE(oscA.pulseWidth.load() == Approx(0.2f));  // Unchanged
    }

    SECTION("All type-specific fields are independent between A and B") {
        // Set all OSC A fields to non-default values
        oscA.waveform.store(0);
        oscA.pulseWidth.store(0.1f);
        oscA.phaseMod.store(0.5f);
        oscA.freqMod.store(-0.3f);
        oscA.pdWaveform.store(3);
        oscA.pdDistortion.store(0.9f);
        oscA.syncRatio.store(5.0f);
        oscA.syncWaveform.store(2);
        oscA.syncMode.store(1);
        oscA.syncAmount.store(0.3f);
        oscA.syncPulseWidth.store(0.2f);
        oscA.additivePartials.store(64);
        oscA.additiveTilt.store(-12.0f);
        oscA.additiveInharm.store(0.7f);
        oscA.chaosAttractor.store(2);
        oscA.chaosAmount.store(0.9f);
        oscA.chaosCoupling.store(0.4f);
        oscA.chaosOutput.store(1);
        oscA.particleScatter.store(8.0f);
        oscA.particleDensity.store(48.0f);
        oscA.particleLifetime.store(1000.0f);
        oscA.particleSpawnMode.store(2);
        oscA.particleEnvType.store(3);
        oscA.particleDrift.store(0.6f);
        oscA.formantVowel.store(4);
        oscA.formantMorph.store(3.5f);
        oscA.spectralPitch.store(12.0f);
        oscA.spectralTilt.store(-6.0f);
        oscA.spectralFormant.store(5.0f);
        oscA.noiseColor.store(3);

        // Verify all OSC B fields remain at their defaults
        REQUIRE(oscB.waveform.load() == 1);           // Sawtooth (default)
        REQUIRE(oscB.pulseWidth.load() == Approx(0.5f));
        REQUIRE(oscB.phaseMod.load() == Approx(0.0f));
        REQUIRE(oscB.freqMod.load() == Approx(0.0f));
        REQUIRE(oscB.pdWaveform.load() == 0);
        REQUIRE(oscB.pdDistortion.load() == Approx(0.0f));
        REQUIRE(oscB.syncRatio.load() == Approx(2.0f));
        REQUIRE(oscB.syncWaveform.load() == 1);
        REQUIRE(oscB.syncMode.load() == 0);
        REQUIRE(oscB.syncAmount.load() == Approx(1.0f));
        REQUIRE(oscB.syncPulseWidth.load() == Approx(0.5f));
        REQUIRE(oscB.additivePartials.load() == 16);
        REQUIRE(oscB.additiveTilt.load() == Approx(0.0f));
        REQUIRE(oscB.additiveInharm.load() == Approx(0.0f));
        REQUIRE(oscB.chaosAttractor.load() == 0);
        REQUIRE(oscB.chaosAmount.load() == Approx(0.5f));
        REQUIRE(oscB.chaosCoupling.load() == Approx(0.0f));
        REQUIRE(oscB.chaosOutput.load() == 0);
        REQUIRE(oscB.particleScatter.load() == Approx(3.0f));
        REQUIRE(oscB.particleDensity.load() == Approx(16.0f));
        REQUIRE(oscB.particleLifetime.load() == Approx(200.0f));
        REQUIRE(oscB.particleSpawnMode.load() == 0);
        REQUIRE(oscB.particleEnvType.load() == 0);
        REQUIRE(oscB.particleDrift.load() == Approx(0.0f));
        REQUIRE(oscB.formantVowel.load() == 0);
        REQUIRE(oscB.formantMorph.load() == Approx(0.0f));
        REQUIRE(oscB.spectralPitch.load() == Approx(0.0f));
        REQUIRE(oscB.spectralTilt.load() == Approx(0.0f));
        REQUIRE(oscB.spectralFormant.load() == Approx(0.0f));
        REQUIRE(oscB.noiseColor.load() == 0);
    }
}
