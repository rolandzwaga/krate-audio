// ==============================================================================
// Modulation Audio Path Integration Tests (FR-093)
// ==============================================================================
// Verifies that modulation engine offsets are correctly applied to band
// processors via the getModulatedValue() pathway.
//
// Reference: specs/008-modulation-system/spec.md FR-063, FR-064, FR-093
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/systems/modulation_engine.h>
#include <krate/dsp/core/block_context.h>

#include "dsp/band_processor.h"
#include "plugin_ids.h"

#include <array>
#include <cmath>

using Catch::Approx;
using namespace Krate::DSP;

namespace {

constexpr double kTestSampleRate = 44100.0;
constexpr size_t kTestBlockSize = 512;

} // namespace

TEST_CASE("LFO modulation produces varying band gain", "[modulation][integration]") {
    ModulationEngine engine;
    engine.prepare(kTestSampleRate, kTestBlockSize);

    // Configure LFO1: fast sine wave
    engine.setLFO1Rate(10.0f);  // 10 Hz
    engine.setLFO1Waveform(Waveform::Sine);

    // Route LFO1 to Band 0 Gain with 100% amount
    ModRouting routing;
    routing.source = ModSource::LFO1;
    routing.destParamId = Disrumpo::ModDest::bandParam(0, Disrumpo::ModDest::kBandGain);
    routing.amount = 1.0f;
    routing.curve = ModCurve::Linear;
    routing.active = true;
    engine.setRouting(0, routing);

    std::array<float, kTestBlockSize> silenceL{};
    std::array<float, kTestBlockSize> silenceR{};
    BlockContext ctx{};
    ctx.sampleRate = kTestSampleRate;
    ctx.tempoBPM = 120.0;
    ctx.blockSize = kTestBlockSize;

    // Process several blocks and collect modulated values
    constexpr float kBaseGainNorm = 0.5f;  // 0 dB normalized
    float minModulated = 1.0f;
    float maxModulated = 0.0f;

    for (int block = 0; block < 90; ++block) {  // ~1 second
        engine.process(ctx, silenceL.data(), silenceR.data(), kTestBlockSize);
        float modulated = engine.getModulatedValue(
            Disrumpo::ModDest::bandParam(0, Disrumpo::ModDest::kBandGain),
            kBaseGainNorm);
        minModulated = std::min(minModulated, modulated);
        maxModulated = std::max(maxModulated, modulated);
    }

    // FR-064: Modulated value should vary significantly from base
    REQUIRE(maxModulated > kBaseGainNorm + 0.3f);
    REQUIRE(minModulated < kBaseGainNorm - 0.3f);

    // FR-062: Modulated value stays clamped to [0, 1]
    REQUIRE(minModulated >= 0.0f);
    REQUIRE(maxModulated <= 1.0f);
}

TEST_CASE("Sweep frequency modulation shifts center", "[modulation][integration]") {
    ModulationEngine engine;
    engine.prepare(kTestSampleRate, kTestBlockSize);

    // Configure Macro1 at mid position
    engine.setMacroValue(0, 0.5f);

    // Route Macro1 to Sweep Frequency with +50% amount
    ModRouting routing;
    routing.source = ModSource::Macro1;
    routing.destParamId = Disrumpo::ModDest::kSweepFrequency;
    routing.amount = 0.5f;
    routing.curve = ModCurve::Linear;
    routing.active = true;
    engine.setRouting(0, routing);

    std::array<float, kTestBlockSize> silenceL{};
    std::array<float, kTestBlockSize> silenceR{};
    BlockContext ctx{};
    ctx.sampleRate = kTestSampleRate;
    ctx.tempoBPM = 120.0;
    ctx.blockSize = kTestBlockSize;

    engine.process(ctx, silenceL.data(), silenceR.data(), kTestBlockSize);

    // Base sweep freq normalized = 0.5, macro output = 0.5, amount = 0.5
    // Offset = applyBipolarModulation(Linear, 0.5, 0.5) = 0.25
    // Modulated = 0.5 + 0.25 = 0.75
    constexpr float kBaseSweepFreqNorm = 0.5f;
    float modulated = engine.getModulatedValue(
        Disrumpo::ModDest::kSweepFrequency, kBaseSweepFreqNorm);

    REQUIRE(modulated > kBaseSweepFreqNorm);
    REQUIRE(modulated <= 1.0f);
}

TEST_CASE("Multiple routings to same destination sum correctly", "[modulation][integration]") {
    ModulationEngine engine;
    engine.prepare(kTestSampleRate, kTestBlockSize);

    // Set Macro1 and Macro2 to max
    engine.setMacroValue(0, 1.0f);
    engine.setMacroValue(1, 1.0f);

    // Route both macros to Band 0 Pan with +50% each
    ModRouting route1;
    route1.source = ModSource::Macro1;
    route1.destParamId = Disrumpo::ModDest::bandParam(0, Disrumpo::ModDest::kBandPan);
    route1.amount = 0.5f;
    route1.curve = ModCurve::Linear;
    route1.active = true;
    engine.setRouting(0, route1);

    ModRouting route2;
    route2.source = ModSource::Macro2;
    route2.destParamId = Disrumpo::ModDest::bandParam(0, Disrumpo::ModDest::kBandPan);
    route2.amount = 0.5f;
    route2.curve = ModCurve::Linear;
    route2.active = true;
    engine.setRouting(1, route2);

    std::array<float, kTestBlockSize> silenceL{};
    std::array<float, kTestBlockSize> silenceR{};
    BlockContext ctx{};
    ctx.sampleRate = kTestSampleRate;
    ctx.tempoBPM = 120.0;
    ctx.blockSize = kTestBlockSize;

    engine.process(ctx, silenceL.data(), silenceR.data(), kTestBlockSize);

    // FR-060: Both offsets should sum
    float offset = engine.getModulationOffset(
        Disrumpo::ModDest::bandParam(0, Disrumpo::ModDest::kBandPan));

    // Each macro: value=1.0, unipolar output ≈ 1.0, amount=0.5 → contribution ≈ 0.5
    // Two contributions → total offset ≈ 1.0
    REQUIRE(offset > 0.5f);

    // FR-061: Total offset clamped to [-1, +1]
    REQUIRE(offset >= -1.0f);
    REQUIRE(offset <= 1.0f);
}

TEST_CASE("No modulation when no routings active", "[modulation][integration]") {
    ModulationEngine engine;
    engine.prepare(kTestSampleRate, kTestBlockSize);

    // Configure LFO but don't route it
    engine.setLFO1Rate(5.0f);
    engine.setLFO1Waveform(Waveform::Sawtooth);

    std::array<float, kTestBlockSize> silenceL{};
    std::array<float, kTestBlockSize> silenceR{};
    BlockContext ctx{};
    ctx.sampleRate = kTestSampleRate;
    ctx.tempoBPM = 120.0;
    ctx.blockSize = kTestBlockSize;

    engine.process(ctx, silenceL.data(), silenceR.data(), kTestBlockSize);

    // All destinations should have zero offset
    for (uint32_t d = 0; d < 54; ++d) {
        REQUIRE(engine.getModulationOffset(d) == Approx(0.0f));
    }

    // getModulatedValue returns base unchanged
    REQUIRE(engine.getModulatedValue(0, 0.5f) == Approx(0.5f));
}
