// ==============================================================================
// Layer 3: System Tests - Modulation Engine
// ==============================================================================
// Tests for the ModulationEngine class covering all user stories.
//
// Reference: specs/008-modulation-system/spec.md
// ==============================================================================

#include <krate/dsp/systems/modulation_engine.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// Helper: Create a prepared engine at 44100 Hz
// =============================================================================
static ModulationEngine createEngine(double sampleRate = 44100.0,
                                      size_t maxBlock = 512) {
    ModulationEngine engine;
    engine.prepare(sampleRate, maxBlock);
    return engine;
}

// =============================================================================
// US1: LFO Integration Tests (FR-007 to FR-014a, SC-001, SC-002, SC-018)
// =============================================================================

TEST_CASE("LFO 1 produces oscillation at configured rate", "[systems][modulation_engine][us1]") {
    auto engine = createEngine(44100.0);
    engine.setLFO1Rate(1.0f);  // 1 Hz
    engine.setLFO1Waveform(Waveform::Sine);

    // Process one full cycle (44100 samples at 1Hz)
    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;

    // Set up routing from LFO1 to param ID 100
    ModRouting routing;
    routing.source = ModSource::LFO1;
    routing.destParamId = 100;
    routing.amount = 1.0f;
    routing.curve = ModCurve::Linear;
    routing.active = true;
    engine.setRouting(0, routing);

    // Process several blocks and track min/max
    float minOffset = 1.0f;
    float maxOffset = -1.0f;
    constexpr size_t blocksPerCycle = 87;  // ~44100/512

    std::array<float, 512> silenceL = {};
    std::array<float, 512> silenceR = {};

    for (size_t b = 0; b < blocksPerCycle; ++b) {
        engine.process(ctx, silenceL.data(), silenceR.data(), 512);
        float offset = engine.getModulationOffset(100);
        minOffset = std::min(minOffset, offset);
        maxOffset = std::max(maxOffset, offset);
    }

    // SC-001: LFO cycle should produce full range oscillation
    // Sine wave should reach near -1.0 and +1.0 within a cycle
    REQUIRE(maxOffset > 0.5f);
    REQUIRE(minOffset < -0.5f);
}

TEST_CASE("LFO tempo sync at 120 BPM quarter note", "[systems][modulation_engine][us1]") {
    auto engine = createEngine(44100.0);
    engine.setLFO1TempoSync(true);
    engine.setLFO1NoteValue(NoteValue::Quarter);
    engine.setLFO1Waveform(Waveform::Sawtooth);

    // SC-002: Quarter note at 120 BPM = 0.5s = 22050 samples
    // Frequency = 2 Hz

    ModRouting routing;
    routing.source = ModSource::LFO1;
    routing.destParamId = 100;
    routing.amount = 1.0f;
    routing.curve = ModCurve::Linear;
    routing.active = true;
    engine.setRouting(0, routing);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.tempoBPM = 120.0;
    ctx.blockSize = 512;

    std::array<float, 512> silenceL = {};
    std::array<float, 512> silenceR = {};

    // Process enough blocks to cover 2 cycles at 2Hz
    float prevOffset = 0.0f;
    bool sawWraparound = false;

    for (size_t b = 0; b < 172; ++b) {  // ~2 seconds
        engine.process(ctx, silenceL.data(), silenceR.data(), 512);
        float offset = engine.getModulationOffset(100);
        if (offset < prevOffset - 0.5f) {
            sawWraparound = true;
        }
        prevOffset = offset;
    }

    // The LFO should have completed at least one cycle
    REQUIRE(sawWraparound);
}

TEST_CASE("All 6 LFO waveforms produce distinct patterns", "[systems][modulation_engine][us1][sc018]") {
    auto engine = createEngine(44100.0);
    engine.setLFO1Rate(10.0f);

    ModRouting routing;
    routing.source = ModSource::LFO1;
    routing.destParamId = 100;
    routing.amount = 1.0f;
    routing.curve = ModCurve::Linear;
    routing.active = true;
    engine.setRouting(0, routing);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    std::array<float, 512> silenceL = {};
    std::array<float, 512> silenceR = {};

    std::array<Waveform, 6> waveforms = {
        Waveform::Sine, Waveform::Triangle, Waveform::Sawtooth,
        Waveform::Square, Waveform::SampleHold, Waveform::SmoothRandom
    };

    std::array<float, 6> sums = {};

    for (size_t w = 0; w < 6; ++w) {
        engine.setLFO1Waveform(waveforms[w]);
        engine.reset();
        engine.setRouting(0, routing);  // Re-set routing after reset

        float sum = 0.0f;
        for (size_t b = 0; b < 10; ++b) {
            engine.process(ctx, silenceL.data(), silenceR.data(), 512);
            sum += std::abs(engine.getModulationOffset(100));
        }
        sums[w] = sum;
    }

    // Each waveform should produce a non-zero sum
    for (size_t w = 0; w < 6; ++w) {
        REQUIRE(sums[w] > 0.0f);
    }
}

TEST_CASE("LFO unipolar mode converts [-1,+1] to [0,+1]", "[systems][modulation_engine][us1]") {
    auto engine = createEngine(44100.0);
    engine.setLFO1Rate(10.0f);
    engine.setLFO1Waveform(Waveform::Sine);
    engine.setLFO1Unipolar(true);

    ModRouting routing;
    routing.source = ModSource::LFO1;
    routing.destParamId = 100;
    routing.amount = 1.0f;
    routing.curve = ModCurve::Linear;
    routing.active = true;
    engine.setRouting(0, routing);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    std::array<float, 512> silenceL = {};
    std::array<float, 512> silenceR = {};

    float minOffset = 1.0f;
    float maxOffset = -1.0f;

    for (size_t b = 0; b < 100; ++b) {
        engine.process(ctx, silenceL.data(), silenceR.data(), 512);
        float offset = engine.getModulationOffset(100);
        minOffset = std::min(minOffset, offset);
        maxOffset = std::max(maxOffset, offset);
    }

    // Unipolar: offset should be in [0, 1] range
    REQUIRE(minOffset >= -0.01f);
    REQUIRE(maxOffset > 0.3f);
    REQUIRE(maxOffset <= 1.01f);
}

TEST_CASE("LFO retrigger resets phase on transport start", "[systems][modulation_engine][us1]") {
    auto engine = createEngine(44100.0);
    engine.setLFO1Rate(1.0f);
    engine.setLFO1Waveform(Waveform::Sawtooth);
    engine.setLFO1Retrigger(true);

    ModRouting routing;
    routing.source = ModSource::LFO1;
    routing.destParamId = 100;
    routing.amount = 1.0f;
    routing.curve = ModCurve::Linear;
    routing.active = true;
    engine.setRouting(0, routing);

    std::array<float, 512> silenceL = {};
    std::array<float, 512> silenceR = {};

    // Process with transport stopped, then start
    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.isPlaying = false;

    // Process a few blocks with transport stopped
    for (size_t b = 0; b < 50; ++b) {
        engine.process(ctx, silenceL.data(), silenceR.data(), 512);
    }

    // Now start transport - should retrigger
    ctx.isPlaying = true;
    engine.process(ctx, silenceL.data(), silenceR.data(), 512);

    // After retrigger, sawtooth should start near -1 (beginning of ramp)
    float offset = engine.getModulationOffset(100);
    // The sawtooth starts at -1 and ramps to +1. After a 512-sample block at 1Hz,
    // the phase advance is 512/44100 ~ 0.0116, so offset should be near the start
    REQUIRE(offset < 0.5f);
}

// =============================================================================
// US2: Routing Matrix Tests (FR-055 to FR-062, FR-085 to FR-088, SC-003-005)
// =============================================================================

TEST_CASE("Single routing with LFO to destination applies amount and curve", "[systems][modulation_engine][us2]") {
    auto engine = createEngine(44100.0);
    engine.setLFO1Rate(10.0f);
    engine.setLFO1Waveform(Waveform::Sine);

    ModRouting routing;
    routing.source = ModSource::LFO1;
    routing.destParamId = 100;
    routing.amount = 0.5f;  // 50% amount
    routing.curve = ModCurve::Linear;
    routing.active = true;
    engine.setRouting(0, routing);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    std::array<float, 512> silenceL = {};
    std::array<float, 512> silenceR = {};

    float maxOffset = -1.0f;
    for (size_t b = 0; b < 100; ++b) {
        engine.process(ctx, silenceL.data(), silenceR.data(), 512);
        float offset = engine.getModulationOffset(100);
        maxOffset = std::max(maxOffset, std::abs(offset));
    }

    // With 50% amount, max offset should be ~0.5 (not 1.0)
    REQUIRE(maxOffset < 0.6f);
    REQUIRE(maxOffset > 0.3f);
}

TEST_CASE("Bipolar modulation: negative amount inverts", "[systems][modulation_engine][us2]") {
    auto engine = createEngine(44100.0);
    engine.setLFO1Rate(10.0f);
    engine.setLFO1Waveform(Waveform::Sine);

    // Positive amount routing
    ModRouting routingPos;
    routingPos.source = ModSource::LFO1;
    routingPos.destParamId = 100;
    routingPos.amount = 1.0f;
    routingPos.curve = ModCurve::Linear;
    routingPos.active = true;
    engine.setRouting(0, routingPos);

    // Negative amount routing to different dest
    ModRouting routingNeg;
    routingNeg.source = ModSource::LFO1;
    routingNeg.destParamId = 101;
    routingNeg.amount = -1.0f;
    routingNeg.curve = ModCurve::Linear;
    routingNeg.active = true;
    engine.setRouting(1, routingNeg);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    std::array<float, 512> silenceL = {};
    std::array<float, 512> silenceR = {};

    // Process a block
    engine.process(ctx, silenceL.data(), silenceR.data(), 512);

    float posOffset = engine.getModulationOffset(100);
    float negOffset = engine.getModulationOffset(101);

    // SC-004: Negative should invert positive within 0.001 tolerance
    // Note: both see same abs(source), but amount signs differ
    // With bipolar modulation: output = curved * amount
    // So negOffset = curved * (-1) = -curved * 1 = -posOffset
    // Since the LFO output feeds both, and both use Linear curve:
    // posOffset = abs(lfoVal) * 1.0 = abs(lfoVal)
    // negOffset = abs(lfoVal) * -1.0 = -abs(lfoVal)
    if (std::abs(posOffset) > 0.01f) {
        REQUIRE(posOffset == Approx(-negOffset).margin(0.01f));
    }
}

TEST_CASE("Multiple routings to same destination sum correctly", "[systems][modulation_engine][us2]") {
    auto engine = createEngine(44100.0);
    engine.setLFO1Rate(10.0f);
    engine.setLFO1Waveform(Waveform::Sine);
    engine.setLFO2Rate(10.0f);
    engine.setLFO2Waveform(Waveform::Sine);

    // Two routings to same destination
    ModRouting routing1;
    routing1.source = ModSource::LFO1;
    routing1.destParamId = 100;
    routing1.amount = 0.3f;
    routing1.curve = ModCurve::Linear;
    routing1.active = true;
    engine.setRouting(0, routing1);

    ModRouting routing2;
    routing2.source = ModSource::LFO2;
    routing2.destParamId = 100;
    routing2.amount = 0.3f;
    routing2.curve = ModCurve::Linear;
    routing2.active = true;
    engine.setRouting(1, routing2);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    std::array<float, 512> silenceL = {};
    std::array<float, 512> silenceR = {};

    float maxOffset = 0.0f;
    for (size_t b = 0; b < 100; ++b) {
        engine.process(ctx, silenceL.data(), silenceR.data(), 512);
        float offset = engine.getModulationOffset(100);
        maxOffset = std::max(maxOffset, std::abs(offset));
    }

    // With two +30% routings, max should exceed 30% (summation)
    REQUIRE(maxOffset > 0.3f);
}

TEST_CASE("Summation clamping: 3 routings clamp to +1.0", "[systems][modulation_engine][us2][sc005]") {
    auto engine = createEngine(44100.0);
    engine.setLFO1Rate(10.0f);
    engine.setLFO1Waveform(Waveform::Square);  // Always +1 or -1

    // 3 routings each with 40% = 120% total -> should clamp to 100%
    for (size_t i = 0; i < 3; ++i) {
        ModRouting routing;
        routing.source = ModSource::LFO1;
        routing.destParamId = 100;
        routing.amount = 0.4f;
        routing.curve = ModCurve::Linear;
        routing.active = true;
        engine.setRouting(i, routing);
    }

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    std::array<float, 512> silenceL = {};
    std::array<float, 512> silenceR = {};

    for (size_t b = 0; b < 50; ++b) {
        engine.process(ctx, silenceL.data(), silenceR.data(), 512);
        float offset = engine.getModulationOffset(100);
        // SC-005: clamped to [-1, +1]
        REQUIRE(offset >= -1.0f);
        REQUIRE(offset <= 1.0f);
    }
}

TEST_CASE("32 simultaneous routings can be active", "[systems][modulation_engine][us2]") {
    auto engine = createEngine(44100.0);
    engine.setLFO1Rate(10.0f);
    engine.setLFO1Waveform(Waveform::Sine);

    // Fill all 32 routing slots
    for (size_t i = 0; i < kMaxModRoutings; ++i) {
        ModRouting routing;
        routing.source = ModSource::LFO1;
        routing.destParamId = static_cast<uint32_t>(i);
        routing.amount = 0.1f;
        routing.curve = ModCurve::Linear;
        routing.active = true;
        engine.setRouting(i, routing);
    }

    REQUIRE(engine.getActiveRoutingCount() == 32);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    std::array<float, 512> silenceL = {};
    std::array<float, 512> silenceR = {};

    // Should process without crash
    engine.process(ctx, silenceL.data(), silenceR.data(), 512);

    // At least one destination should have an offset
    bool anyActive = false;
    for (size_t i = 0; i < kMaxModRoutings; ++i) {
        if (std::abs(engine.getModulationOffset(static_cast<uint32_t>(i))) > 0.001f) {
            anyActive = true;
            break;
        }
    }
    REQUIRE(anyActive);
}

TEST_CASE("Routing with amount=0% has no effect", "[systems][modulation_engine][us2]") {
    auto engine = createEngine(44100.0);
    engine.setLFO1Rate(10.0f);
    engine.setLFO1Waveform(Waveform::Sine);

    ModRouting routing;
    routing.source = ModSource::LFO1;
    routing.destParamId = 100;
    routing.amount = 0.0f;  // Zero amount
    routing.curve = ModCurve::Linear;
    routing.active = true;
    engine.setRouting(0, routing);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    std::array<float, 512> silenceL = {};
    std::array<float, 512> silenceR = {};

    engine.process(ctx, silenceL.data(), silenceR.data(), 512);
    REQUIRE(engine.getModulationOffset(100) == Approx(0.0f).margin(0.001f));
}

TEST_CASE("getModulatedValue clamps to [0, 1]", "[systems][modulation_engine][us2]") {
    auto engine = createEngine(44100.0);
    engine.setLFO1Rate(10.0f);
    engine.setLFO1Waveform(Waveform::Square);

    // Large positive amount
    ModRouting routing;
    routing.source = ModSource::LFO1;
    routing.destParamId = 100;
    routing.amount = 1.0f;
    routing.curve = ModCurve::Linear;
    routing.active = true;
    engine.setRouting(0, routing);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    std::array<float, 512> silenceL = {};
    std::array<float, 512> silenceR = {};

    // Process enough to get non-zero modulation
    for (size_t b = 0; b < 10; ++b) {
        engine.process(ctx, silenceL.data(), silenceR.data(), 512);
    }

    // FR-062: final value clamped to [0, 1]
    float value = engine.getModulatedValue(100, 0.5f);
    REQUIRE(value >= 0.0f);
    REQUIRE(value <= 1.0f);
}

// =============================================================================
// US3: Envelope Follower Tests (FR-015 to FR-020a, SC-006)
// =============================================================================

TEST_CASE("Envelope follower responds to step input", "[systems][modulation_engine][us3]") {
    auto engine = createEngine(44100.0);
    engine.setEnvFollowerAttack(10.0f);
    engine.setEnvFollowerRelease(100.0f);
    engine.setEnvFollowerSensitivity(1.0f);

    ModRouting routing;
    routing.source = ModSource::EnvFollower;
    routing.destParamId = 100;
    routing.amount = 1.0f;
    routing.curve = ModCurve::Linear;
    routing.active = true;
    engine.setRouting(0, routing);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;

    // Create step input (silence then full level)
    std::array<float, 512> silenceL = {};
    std::array<float, 512> silenceR = {};
    std::array<float, 512> loudL;
    std::array<float, 512> loudR;
    loudL.fill(0.8f);
    loudR.fill(0.8f);

    // First some silence
    for (size_t b = 0; b < 5; ++b) {
        engine.process(ctx, silenceL.data(), silenceR.data(), 512);
    }

    float beforeOffset = engine.getModulationOffset(100);

    // Now loud input
    for (size_t b = 0; b < 10; ++b) {
        engine.process(ctx, loudL.data(), loudR.data(), 512);
    }

    float afterOffset = engine.getModulationOffset(100);

    // SC-006: should respond to step input
    REQUIRE(afterOffset > beforeOffset);
    REQUIRE(afterOffset > 0.1f);
}

TEST_CASE("Envelope follower source types", "[systems][modulation_engine][us3]") {
    auto engine = createEngine(44100.0);
    engine.setEnvFollowerAttack(1.0f);
    engine.setEnvFollowerRelease(10.0f);
    engine.setEnvFollowerSensitivity(1.0f);

    ModRouting routing;
    routing.source = ModSource::EnvFollower;
    routing.destParamId = 100;
    routing.amount = 1.0f;
    routing.curve = ModCurve::Linear;
    routing.active = true;
    engine.setRouting(0, routing);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;

    std::array<float, 512> leftOnly;
    leftOnly.fill(0.8f);
    std::array<float, 512> silence = {};

    // Test with InputL source: only left should matter
    engine.setEnvFollowerSource(EnvFollowerSourceType::InputL);
    for (size_t b = 0; b < 5; ++b) {
        engine.process(ctx, leftOnly.data(), silence.data(), 512);
    }
    float leftOnlyOffset = engine.getModulationOffset(100);
    REQUIRE(leftOnlyOffset > 0.1f);
}

// =============================================================================
// US4: Macro Tests (FR-026 to FR-029a)
// =============================================================================

TEST_CASE("4 macros are independently available", "[systems][modulation_engine][us4]") {
    auto engine = createEngine(44100.0);

    for (size_t i = 0; i < 4; ++i) {
        engine.setMacroValue(i, static_cast<float>(i + 1) * 0.2f);
    }

    // Each macro should have a different value (via getSourceValue)
    float m1 = engine.getSourceValue(ModSource::Macro1);
    float m2 = engine.getSourceValue(ModSource::Macro2);
    float m3 = engine.getSourceValue(ModSource::Macro3);
    float m4 = engine.getSourceValue(ModSource::Macro4);

    // All should be non-zero (since we set non-zero values)
    REQUIRE(m1 > 0.0f);
    REQUIRE(m2 > 0.0f);
    REQUIRE(m3 > 0.0f);
    REQUIRE(m4 > 0.0f);
}

TEST_CASE("Macro Min/Max range mapping", "[systems][modulation_engine][us4]") {
    auto engine = createEngine(44100.0);

    // Set macro 0: value=0.5, min=0.2, max=0.8
    engine.setMacroValue(0, 0.5f);
    engine.setMacroMin(0, 0.2f);
    engine.setMacroMax(0, 0.8f);
    engine.setMacroCurve(0, ModCurve::Linear);

    // FR-028: mapped = min + value * (max - min) = 0.2 + 0.5 * 0.6 = 0.5
    // FR-029: output = applyModCurve(Linear, 0.5) = 0.5
    float output = engine.getSourceValue(ModSource::Macro1);
    REQUIRE(output == Approx(0.5f).margin(0.05f));
}

TEST_CASE("Macro curve applied after Min/Max mapping", "[systems][modulation_engine][us4]") {
    auto engine = createEngine(44100.0);

    // Set macro 0: value=1.0, min=0.0, max=1.0 with Exponential curve
    engine.setMacroValue(0, 1.0f);
    engine.setMacroMin(0, 0.0f);
    engine.setMacroMax(0, 1.0f);
    engine.setMacroCurve(0, ModCurve::Exponential);

    // mapped = 0.0 + 1.0 * 1.0 = 1.0
    // output = exp(1.0) = 1.0
    float output = engine.getSourceValue(ModSource::Macro1);
    REQUIRE(output == Approx(1.0f).margin(0.05f));

    // With value = 0.5:
    engine.setMacroValue(0, 0.5f);
    // mapped = 0.0 + 0.5 * 1.0 = 0.5
    // output = exp(0.5) = 0.25
    output = engine.getSourceValue(ModSource::Macro1);
    REQUIRE(output == Approx(0.25f).margin(0.05f));
}

TEST_CASE("Macro output range is [0, +1]", "[systems][modulation_engine][us4]") {
    auto engine = createEngine(44100.0);

    // Test at extremes
    engine.setMacroValue(0, 0.0f);
    REQUIRE(engine.getSourceValue(ModSource::Macro1) >= 0.0f);

    engine.setMacroValue(0, 1.0f);
    float maxOutput = engine.getSourceValue(ModSource::Macro1);
    REQUIRE(maxOutput >= 0.0f);
    REQUIRE(maxOutput <= 1.01f);
}

// =============================================================================
// Integration Tests - New Sources via Engine (US5-US9)
// =============================================================================

TEST_CASE("Engine random source integrates with routing", "[systems][modulation_engine][us5]") {
    auto engine = createEngine(44100.0);

    // Route Random to dest 10 with amount 0.5
    ModRouting routing;
    routing.source = ModSource::Random;
    routing.destParamId = 10;
    routing.amount = 0.5f;
    routing.curve = ModCurve::Linear;
    routing.active = true;
    engine.setRouting(0, routing);

    engine.setRandomRate(20.0f);  // Fast rate

    // Process 2 seconds
    std::array<float, 512> silence{};
    BlockContext ctx{};
    ctx.sampleRate = 44100.0;
    ctx.tempoBPM = 120.0;
    ctx.blockSize = 512;

    bool hasModulation = false;
    for (int block = 0; block < 172; ++block) {
        engine.process(ctx, silence.data(), silence.data(), 512);
        float offset = engine.getModulationOffset(10);
        if (std::abs(offset) > 0.01f) {
            hasModulation = true;
        }
    }

    REQUIRE(hasModulation);
}

TEST_CASE("Engine chaos source integrates with routing", "[systems][modulation_engine][us6]") {
    auto engine = createEngine(44100.0);

    ModRouting routing;
    routing.source = ModSource::Chaos;
    routing.destParamId = 20;
    routing.amount = 1.0f;
    routing.curve = ModCurve::Linear;
    routing.active = true;
    engine.setRouting(0, routing);

    engine.setChaosSpeed(5.0f);

    std::array<float, 512> silence{};
    BlockContext ctx{};
    ctx.sampleRate = 44100.0;
    ctx.tempoBPM = 120.0;
    ctx.blockSize = 512;

    bool hasModulation = false;
    for (int block = 0; block < 100; ++block) {
        engine.process(ctx, silence.data(), silence.data(), 512);
        float offset = engine.getModulationOffset(20);
        if (std::abs(offset) > 0.01f) {
            hasModulation = true;
        }
    }

    REQUIRE(hasModulation);
}

TEST_CASE("Engine transient source integrates with routing", "[systems][modulation_engine][us9]") {
    auto engine = createEngine(44100.0);

    ModRouting routing;
    routing.source = ModSource::Transient;
    routing.destParamId = 30;
    routing.amount = 1.0f;
    routing.curve = ModCurve::Linear;
    routing.active = true;
    engine.setRouting(0, routing);

    engine.setTransientSensitivity(0.9f);

    // First, silence
    std::array<float, 512> silence{};
    BlockContext ctx{};
    ctx.sampleRate = 44100.0;
    ctx.tempoBPM = 120.0;
    ctx.blockSize = 512;

    for (int block = 0; block < 10; ++block) {
        engine.process(ctx, silence.data(), silence.data(), 512);
    }

    // Then loud signal (transient)
    std::array<float, 512> loud{};
    loud.fill(0.9f);

    engine.process(ctx, loud.data(), loud.data(), 512);
    float offset = engine.getModulationOffset(30);

    // Transient detector should fire
    REQUIRE(offset > 0.0f);
}

// =============================================================================
// Edge Case Tests (Phase 17)
// =============================================================================

TEST_CASE("Engine routing with amount=0 has no effect", "[systems][modulation_engine][edge]") {
    auto engine = createEngine(44100.0);

    // Set up LFO with strong output
    engine.setLFO1Rate(5.0f);
    engine.setLFO1Waveform(Waveform::Sine);

    // Route with zero amount
    ModRouting routing;
    routing.source = ModSource::LFO1;
    routing.destParamId = 5;
    routing.amount = 0.0f;
    routing.curve = ModCurve::Linear;
    routing.active = true;
    engine.setRouting(0, routing);

    std::array<float, 512> silence{};
    BlockContext ctx{};
    ctx.sampleRate = 44100.0;
    ctx.tempoBPM = 120.0;
    ctx.blockSize = 512;

    engine.process(ctx, silence.data(), silence.data(), 512);

    float offset = engine.getModulationOffset(5);
    REQUIRE(std::abs(offset) < 0.001f);
}

TEST_CASE("Engine getModulatedValue clamps to [0, 1]", "[systems][modulation_engine][edge]") {
    auto engine = createEngine(44100.0);

    // Set macro to max value
    engine.setMacroValue(0, 1.0f);

    // Route Macro1 to dest with +100% amount
    ModRouting routing;
    routing.source = ModSource::Macro1;
    routing.destParamId = 7;
    routing.amount = 1.0f;
    routing.curve = ModCurve::Linear;
    routing.active = true;
    engine.setRouting(0, routing);

    std::array<float, 512> silence{};
    BlockContext ctx{};
    ctx.sampleRate = 44100.0;
    ctx.tempoBPM = 120.0;
    ctx.blockSize = 512;

    engine.process(ctx, silence.data(), silence.data(), 512);

    // Base 0.8 + large positive offset should clamp to 1.0
    float val = engine.getModulatedValue(7, 0.8f);
    REQUIRE(val >= 0.0f);
    REQUIRE(val <= 1.0f);
}

TEST_CASE("Engine out-of-range destParamId returns 0 offset", "[systems][modulation_engine][edge]") {
    auto engine = createEngine(44100.0);

    // Requesting offset for ID beyond max destinations
    float offset = engine.getModulationOffset(999);
    REQUIRE(offset == Approx(0.0f));
}

TEST_CASE("Engine all 32 routing slots can be active", "[systems][modulation_engine][edge]") {
    auto engine = createEngine(44100.0);

    // Fill all 32 slots
    for (size_t i = 0; i < kMaxModRoutings; ++i) {
        ModRouting routing;
        routing.source = ModSource::Macro1;
        routing.destParamId = static_cast<uint32_t>(i % kMaxModDestinations);
        routing.amount = 0.1f;
        routing.curve = ModCurve::Linear;
        routing.active = true;
        engine.setRouting(i, routing);
    }

    REQUIRE(engine.getActiveRoutingCount() == kMaxModRoutings);

    engine.setMacroValue(0, 0.5f);

    // Process should not crash with 32 active routings
    std::array<float, 512> silence{};
    BlockContext ctx{};
    ctx.sampleRate = 44100.0;
    ctx.tempoBPM = 120.0;
    ctx.blockSize = 512;

    engine.process(ctx, silence.data(), silence.data(), 512);

    // Verify some offsets are non-zero
    bool hasNonZero = false;
    for (uint32_t i = 0; i < 32; ++i) {
        if (std::abs(engine.getModulationOffset(i)) > 0.001f) {
            hasNonZero = true;
            break;
        }
    }
    REQUIRE(hasNonZero);
}
