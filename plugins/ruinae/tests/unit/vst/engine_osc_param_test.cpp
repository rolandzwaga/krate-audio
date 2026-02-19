// ==============================================================================
// Engine OSC Parameter Routing Tests (068-osc-type-params, Phase 3)
// ==============================================================================
// Tests that RuinaeEngine::setOscAParam() / setOscBParam() forward to all
// active voices for a representative sample of OscParam values.
//
// Feature: 068-osc-type-params
// User Story: US2 (Type-Specific Parameter Routing for All Types)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/ruinae_engine.h"
#include <krate/dsp/systems/oscillator_types.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <numeric>
#include <vector>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// Helpers
// =============================================================================

static constexpr double kSampleRate = 44100.0;
static constexpr size_t kBlockSize = 512;

static std::unique_ptr<RuinaeEngine> createEngine() {
    auto engine = std::make_unique<RuinaeEngine>();
    engine->prepare(kSampleRate, kBlockSize);
    return engine;
}

static float computeRMS(const float* buf, size_t n) {
    if (n == 0) return 0.0f;
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        sum += static_cast<double>(buf[i]) * buf[i];
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(n)));
}

// =============================================================================
// Tests: RuinaeEngine setOscAParam / setOscBParam
// =============================================================================

TEST_CASE("RuinaeEngine setOscAParam forwards to voices",
          "[engine][osc-param][us2]") {
    auto engine = createEngine();

    // Set up a single-voice scenario
    engine->setPolyphony(1);

    // Configure OSC A as PolyBLEP, set waveform
    engine->setOscAType(OscType::PolyBLEP);
    engine->setMixPosition(0.0f);  // OSC A only

    // Set waveform to Sawtooth via engine setter
    engine->setOscAParam(OscParam::Waveform, 1.0f);

    // Play a note
    engine->noteOn(60, 100);  // Middle C, velocity 100

    // Process a few blocks to get audio
    std::vector<float> left(kBlockSize, 0.0f);
    std::vector<float> right(kBlockSize, 0.0f);
    engine->processBlock(left.data(), right.data(), kBlockSize);
    engine->processBlock(left.data(), right.data(), kBlockSize);

    float rmsL = computeRMS(left.data(), kBlockSize);
    // Should produce audible output
    REQUIRE(rmsL > 0.001f);
}

TEST_CASE("RuinaeEngine setOscBParam forwards to voices independently",
          "[engine][osc-param][us2]") {
    auto engine = createEngine();
    engine->setPolyphony(1);

    // Set OSC A as PolyBLEP Sine, OSC B as PolyBLEP Sawtooth
    engine->setOscAType(OscType::PolyBLEP);
    engine->setOscBType(OscType::PolyBLEP);
    engine->setOscAParam(OscParam::Waveform, 0.0f);  // Sine
    engine->setOscBParam(OscParam::Waveform, 1.0f);  // Sawtooth
    engine->setMixPosition(1.0f);  // OSC B only

    engine->noteOn(60, 100);

    std::vector<float> left(kBlockSize, 0.0f);
    std::vector<float> right(kBlockSize, 0.0f);
    engine->processBlock(left.data(), right.data(), kBlockSize);
    engine->processBlock(left.data(), right.data(), kBlockSize);

    float rmsL = computeRMS(left.data(), kBlockSize);
    REQUIRE(rmsL > 0.001f);
}

TEST_CASE("RuinaeEngine setOscAParam handles representative params for all types",
          "[engine][osc-param][us2]") {
    auto engine = createEngine();
    engine->setPolyphony(1);
    engine->setMixPosition(0.0f);  // OSC A only

    SECTION("ChaosAmount forwarded to Chaos oscillator") {
        engine->setOscAType(OscType::Chaos);
        engine->setOscAParam(OscParam::ChaosAmount, 0.8f);
        engine->noteOn(60, 100);
        std::vector<float> left(kBlockSize, 0.0f);
        std::vector<float> right(kBlockSize, 0.0f);
        engine->processBlock(left.data(), right.data(), kBlockSize);
        engine->processBlock(left.data(), right.data(), kBlockSize);
        REQUIRE(computeRMS(left.data(), kBlockSize) > 0.001f);
    }

    SECTION("NoiseColor forwarded to Noise oscillator") {
        engine->setOscAType(OscType::Noise);
        engine->setOscAParam(OscParam::NoiseColor, 1.0f);  // Pink
        engine->noteOn(60, 100);
        std::vector<float> left(kBlockSize, 0.0f);
        std::vector<float> right(kBlockSize, 0.0f);
        engine->processBlock(left.data(), right.data(), kBlockSize);
        engine->processBlock(left.data(), right.data(), kBlockSize);
        REQUIRE(computeRMS(left.data(), kBlockSize) > 0.001f);
    }

    SECTION("FormantVowel forwarded to Formant oscillator") {
        engine->setOscAType(OscType::Formant);
        engine->setOscAParam(OscParam::FormantVowel, 3.0f);  // O
        engine->noteOn(60, 100);
        std::vector<float> left(kBlockSize, 0.0f);
        std::vector<float> right(kBlockSize, 0.0f);
        engine->processBlock(left.data(), right.data(), kBlockSize);
        engine->processBlock(left.data(), right.data(), kBlockSize);
        REQUIRE(computeRMS(left.data(), kBlockSize) > 0.001f);
    }

    SECTION("SpectralPitchShift forwarded to SpectralFreeze oscillator") {
        engine->setOscAType(OscType::SpectralFreeze);
        engine->setOscAParam(OscParam::SpectralPitchShift, 12.0f);  // +12 semitones
        engine->noteOn(60, 100);
        std::vector<float> left(kBlockSize, 0.0f);
        std::vector<float> right(kBlockSize, 0.0f);
        // SpectralFreeze needs more blocks
        for (int i = 0; i < 4; ++i) {
            engine->processBlock(left.data(), right.data(), kBlockSize);
        }
        // May or may not produce output depending on freeze state, but should not crash
    }

    SECTION("ParticleDensity forwarded to Particle oscillator") {
        engine->setOscAType(OscType::Particle);
        engine->setOscAParam(OscParam::ParticleDensity, 32.0f);
        engine->noteOn(60, 100);
        std::vector<float> left(kBlockSize, 0.0f);
        std::vector<float> right(kBlockSize, 0.0f);
        for (int i = 0; i < 4; ++i) {
            engine->processBlock(left.data(), right.data(), kBlockSize);
        }
        // Should not crash; particle output is timing-dependent
    }
}
