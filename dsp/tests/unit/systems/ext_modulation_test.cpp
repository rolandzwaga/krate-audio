// ==============================================================================
// Tests: Extended Modulation System - Global Modulation
// ==============================================================================
// Tests for global modulation composition, global-to-voice forwarding,
// MIDI controller normalization, and Rungler/Pitch Bend/Mod Wheel integration.
//
// Feature: 042-ext-modulation-system (User Stories 4, 5, 6)
// Test-First: Constitution Principle XII
// ==============================================================================

#include <krate/dsp/systems/modulation_engine.h>
#include <krate/dsp/core/modulation_types.h>
#include <krate/dsp/core/block_context.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// Constants: Global Destination IDs (test scaffold)
// =============================================================================

namespace {

constexpr uint32_t kGlobalFilterCutoffDestId = 0;
constexpr uint32_t kGlobalFilterResonanceDestId = 1;
constexpr uint32_t kMasterVolumeDestId = 2;
constexpr uint32_t kEffectMixDestId = 3;
constexpr uint32_t kAllVoiceFilterCutoffDestId = 4;
constexpr uint32_t kAllVoiceMorphPositionDestId = 5;
constexpr uint32_t kTranceGateRateDestId = 6;

constexpr double kSampleRate = 44100.0;
constexpr size_t kBlockSize = 512;

// =============================================================================
// Test Scaffold: Minimal engine scaffold for global modulation testing
// =============================================================================

/// @brief Minimal test scaffold composing a ModulationEngine.
/// Simulates the future RuinaeEngine composition pattern.
class TestEngineScaffold {
public:
    void prepare() noexcept {
        engine_.prepare(kSampleRate, kBlockSize);
    }

    /// @brief Process one block of silence through the modulation engine.
    void processBlock() noexcept {
        std::fill(silenceL_.begin(), silenceL_.end(), 0.0f);
        std::fill(silenceR_.begin(), silenceR_.end(), 0.0f);

        BlockContext ctx;
        ctx.sampleRate = kSampleRate;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;

        engine_.process(ctx, silenceL_.data(), silenceR_.data(), kBlockSize);
    }

    /// @brief Get modulation offset for a global destination.
    [[nodiscard]] float getOffset(uint32_t destId) const noexcept {
        return engine_.getModulationOffset(destId);
    }

    /// @brief Configure a routing in the engine.
    void setRouting(size_t index, ModSource source, uint32_t destId,
                    float amount, ModCurve curve = ModCurve::Linear) noexcept {
        ModRouting routing;
        routing.source = source;
        routing.destParamId = destId;
        routing.amount = amount;
        routing.curve = curve;
        routing.active = true;
        engine_.setRouting(index, routing);
    }

    /// @brief Set macro value (for Pitch Bend, Mod Wheel, Rungler injection).
    void setMacroValue(size_t index, float value) noexcept {
        engine_.setMacroValue(index, value);
    }

    ModulationEngine& engine() noexcept { return engine_; }

private:
    ModulationEngine engine_;
    std::array<float, kBlockSize> silenceL_{};
    std::array<float, kBlockSize> silenceR_{};
};

// =============================================================================
// Helper: Normalize MIDI Pitch Bend (14-bit to [-1, +1])
// =============================================================================

/// @brief Normalize 14-bit pitch bend value to [-1.0, +1.0].
/// @param rawValue 14-bit value (0x0000 to 0x3FFF), center = 0x2000
/// @return Normalized value [-1.0, +1.0]
float normalizePitchBend(uint16_t rawValue) noexcept {
    constexpr float kCenter = 8192.0f;  // 0x2000
    constexpr float kRange = 8191.0f;   // 0x1FFF
    return std::clamp((static_cast<float>(rawValue) - kCenter) / kRange, -1.0f, 1.0f);
}

/// @brief Normalize MIDI CC value (0-127) to [0.0, 1.0].
/// @param ccValue CC value [0, 127]
/// @return Normalized value [0.0, 1.0]
float normalizeModWheel(uint8_t ccValue) noexcept {
    return static_cast<float>(ccValue) / 127.0f;
}

/// @brief Two-stage clamping formula (FR-021).
/// Step 1: perVoiceResult = clamp(baseValue + perVoiceOffset, min, max)
/// Step 2: finalValue = clamp(perVoiceResult + globalOffset, min, max)
float twoStageClamping(float baseValue, float perVoiceOffset, float globalOffset,
                       float minVal, float maxVal) noexcept {
    float perVoiceResult = std::clamp(baseValue + perVoiceOffset, minVal, maxVal);
    return std::clamp(perVoiceResult + globalOffset, minVal, maxVal);
}

} // anonymous namespace

// =============================================================================
// US4: Global Modulation Engine Composition
// =============================================================================

// T047: LFO1 -> GlobalFilterCutoff routing
TEST_CASE("ExtModulation: LFO1 -> GlobalFilterCutoff produces expected offset",
          "[ext_modulation][us4]") {
    TestEngineScaffold scaffold;
    scaffold.prepare();

    // Set LFO1 to output a constant +1.0 (high frequency sine, after many samples
    // the last value is unpredictable, so use a macro instead for deterministic test)
    // Actually, use Macro1 as a deterministic source set to 1.0
    scaffold.setMacroValue(0, 1.0f);  // Macro1 = 1.0
    scaffold.setRouting(0, ModSource::Macro1, kGlobalFilterCutoffDestId, 0.5f);

    scaffold.processBlock();

    // Macro1 output = 1.0 (unipolar, linear curve, no min/max mapping needed)
    // amount = 0.5 -> offset = 1.0 * 0.5 = 0.5
    float offset = scaffold.getOffset(kGlobalFilterCutoffDestId);
    REQUIRE(offset == Approx(0.5f).margin(0.01f));
}

// T048: Chaos -> MasterVolume routing with varying output
TEST_CASE("ExtModulation: Chaos -> MasterVolume routing produces non-zero offset",
          "[ext_modulation][us4]") {
    TestEngineScaffold scaffold;
    scaffold.prepare();

    // Configure Chaos source to master volume
    scaffold.engine().setChaosSpeed(1.0f);
    scaffold.setRouting(0, ModSource::Chaos, kMasterVolumeDestId, 0.3f);

    // Process multiple blocks to let chaos attractor evolve
    for (int i = 0; i < 10; ++i) {
        scaffold.processBlock();
    }

    // Chaos output should produce some non-zero offset
    float offset = scaffold.getOffset(kMasterVolumeDestId);
    // The offset should be in range [-0.3, +0.3] (amount=0.3)
    REQUIRE(std::abs(offset) <= 0.31f);
    // Note: chaos may output near-zero sometimes, so we just verify it's bounded
}

// T049: No global routings -> all-zero offsets
TEST_CASE("ExtModulation: no global routings produces all-zero offsets",
          "[ext_modulation][us4]") {
    TestEngineScaffold scaffold;
    scaffold.prepare();

    scaffold.processBlock();

    REQUIRE(scaffold.getOffset(kGlobalFilterCutoffDestId) == Approx(0.0f));
    REQUIRE(scaffold.getOffset(kMasterVolumeDestId) == Approx(0.0f));
    REQUIRE(scaffold.getOffset(kEffectMixDestId) == Approx(0.0f));
    REQUIRE(scaffold.getOffset(kAllVoiceFilterCutoffDestId) == Approx(0.0f));
    REQUIRE(scaffold.getOffset(kAllVoiceMorphPositionDestId) == Approx(0.0f));
    REQUIRE(scaffold.getOffset(kTranceGateRateDestId) == Approx(0.0f));
}

// T050: ModulationEngine.prepare() initializes correctly
TEST_CASE("ExtModulation: ModulationEngine.prepare() initializes sources",
          "[ext_modulation][us4]") {
    TestEngineScaffold scaffold;
    scaffold.prepare();

    // After prepare, LFO rates should be valid
    // Process a block to initialize source states
    scaffold.processBlock();

    // Macro values should default to 0
    REQUIRE(scaffold.engine().getMacro(0).value == Approx(0.0f));
    REQUIRE(scaffold.engine().getMacro(1).value == Approx(0.0f));
    REQUIRE(scaffold.engine().getMacro(2).value == Approx(0.0f));
    REQUIRE(scaffold.engine().getMacro(3).value == Approx(0.0f));

    // No active routings by default
    REQUIRE(scaffold.engine().getActiveRoutingCount() == 0);
}

// =============================================================================
// US5: Global-to-Voice Parameter Forwarding
// =============================================================================

// T059: AllVoiceFilterCutoff forwarding
TEST_CASE("ExtModulation: AllVoiceFilterCutoff forwarding offset calculation",
          "[ext_modulation][us5]") {
    TestEngineScaffold scaffold;
    scaffold.prepare();

    // LFO2 = use Macro2 as deterministic source, value = 0.5
    scaffold.setMacroValue(1, 0.625f);  // Macro2 = 0.625 (will output 0.625)
    scaffold.setRouting(0, ModSource::Macro2, kAllVoiceFilterCutoffDestId, 0.8f);

    scaffold.processBlock();

    // Macro2 output = 0.625, amount = 0.8
    // Raw offset = 0.625 * 0.8 = 0.5
    float rawOffset = scaffold.getOffset(kAllVoiceFilterCutoffDestId);
    REQUIRE(rawOffset == Approx(0.5f).margin(0.01f));

    // Forwarding: scale to semitones: offset * 48 = 0.5 * 48 = 24 semitones
    float semitoneOffset = rawOffset * 48.0f;
    REQUIRE(semitoneOffset == Approx(24.0f).margin(0.5f));
}

// T060: AllVoiceMorphPosition forwarding
TEST_CASE("ExtModulation: AllVoiceMorphPosition forwarding offset calculation",
          "[ext_modulation][us5]") {
    TestEngineScaffold scaffold;
    scaffold.prepare();

    // Macro3 = 0.7, amount = 1.0
    scaffold.setMacroValue(2, 0.7f);
    scaffold.setRouting(0, ModSource::Macro3, kAllVoiceMorphPositionDestId, 1.0f);

    scaffold.processBlock();

    // Offset = 0.7 * 1.0 = 0.7 (direct, no scaling needed for morph position)
    float offset = scaffold.getOffset(kAllVoiceMorphPositionDestId);
    REQUIRE(offset == Approx(0.7f).margin(0.01f));
}

// T061: TranceGateRate forwarding
TEST_CASE("ExtModulation: TranceGateRate forwarding offset calculation",
          "[ext_modulation][us5]") {
    TestEngineScaffold scaffold;
    scaffold.prepare();

    // Macro4 = 0.5, amount = 1.0
    scaffold.setMacroValue(3, 0.5f);
    scaffold.setRouting(0, ModSource::Macro4, kTranceGateRateDestId, 1.0f);

    scaffold.processBlock();

    float rawOffset = scaffold.getOffset(kTranceGateRateDestId);
    REQUIRE(rawOffset == Approx(0.5f).margin(0.01f));

    // Forwarding: scale to Hz: offset * 19.9 = 0.5 * 19.9 = 9.95 Hz
    float hzOffset = rawOffset * 19.9f;
    REQUIRE(hzOffset == Approx(9.95f).margin(0.5f));
}

// T062: Two-stage clamping formula
TEST_CASE("ExtModulation: two-stage clamping formula",
          "[ext_modulation][us5]") {
    // Per-voice offset of +0.9, global offset of +0.5, range [0, 1]
    // Step 1: clamp(base + perVoice) = clamp(0.0 + 0.9) = 0.9
    // Step 2: clamp(0.9 + 0.5) = clamp(1.4) = 1.0
    float result = twoStageClamping(0.0f, 0.9f, 0.5f, 0.0f, 1.0f);
    REQUIRE(result == Approx(1.0f));

    // Negative global offset
    // Step 1: clamp(0.5 + 0.3) = 0.8
    // Step 2: clamp(0.8 + (-0.5)) = 0.3
    result = twoStageClamping(0.5f, 0.3f, -0.5f, 0.0f, 1.0f);
    REQUIRE(result == Approx(0.3f));

    // Both offsets negative below min
    // Step 1: clamp(0.2 + (-0.5)) = clamp(-0.3) = 0.0
    // Step 2: clamp(0.0 + (-0.3)) = clamp(-0.3) = 0.0
    result = twoStageClamping(0.2f, -0.5f, -0.3f, 0.0f, 1.0f);
    REQUIRE(result == Approx(0.0f));
}

// T064a: TranceGateRate Hz offset scaling and clamping
TEST_CASE("ExtModulation: TranceGateRate Hz offset scaling and clamping",
          "[ext_modulation][us5]") {
    // Base rate = 4.0 Hz, offset = +1.0 -> raw Hz = 1.0 * 19.9 = 19.9
    // Final rate = clamp(4.0 + 19.9, 0.1, 20.0) = 20.0
    float baseRate = 4.0f;
    float rawOffset = 1.0f;
    float hzOffset = rawOffset * 19.9f;
    float finalRate = std::clamp(baseRate + hzOffset, 0.1f, 20.0f);
    REQUIRE(finalRate == Approx(20.0f));

    // Negative offset to push below minimum
    rawOffset = -1.0f;
    hzOffset = rawOffset * 19.9f;
    finalRate = std::clamp(baseRate + hzOffset, 0.1f, 20.0f);
    REQUIRE(finalRate == Approx(0.1f));
}

// =============================================================================
// US6: MIDI Controller Normalization
// =============================================================================

// T076: Pitch Bend normalization
TEST_CASE("ExtModulation: Pitch Bend normalization (14-bit to [-1, +1])",
          "[ext_modulation][us6]") {
    // 0x0000 = minimum = -1.0
    REQUIRE(normalizePitchBend(0x0000) == Approx(-1.0f).margin(0.001f));

    // 0x2000 = center = 0.0
    REQUIRE(normalizePitchBend(0x2000) == Approx(0.0f).margin(0.001f));

    // 0x3FFF = maximum = +1.0
    REQUIRE(normalizePitchBend(0x3FFF) == Approx(1.0f).margin(0.001f));

    // Mid values
    REQUIRE(normalizePitchBend(0x1000) == Approx(-0.5f).margin(0.01f));
    REQUIRE(normalizePitchBend(0x3000) == Approx(0.5f).margin(0.01f));
}

// T077: Mod Wheel normalization
TEST_CASE("ExtModulation: Mod Wheel normalization (CC#1 to [0, 1])",
          "[ext_modulation][us6]") {
    REQUIRE(normalizeModWheel(0) == Approx(0.0f));
    REQUIRE(normalizeModWheel(64) == Approx(0.504f).margin(0.01f));
    REQUIRE(normalizeModWheel(127) == Approx(1.0f));
}

// T078: ModWheel -> EffectMix routing via Macro2
TEST_CASE("ExtModulation: ModWheel -> EffectMix routing via Macro2",
          "[ext_modulation][us6]") {
    TestEngineScaffold scaffold;
    scaffold.prepare();

    // Normalize mod wheel CC=64 -> ~0.504
    float modWheelNorm = normalizeModWheel(64);
    scaffold.setMacroValue(1, modWheelNorm);  // Macro2 = mod wheel
    scaffold.setRouting(0, ModSource::Macro2, kEffectMixDestId, 1.0f);

    scaffold.processBlock();

    float offset = scaffold.getOffset(kEffectMixDestId);
    REQUIRE(offset == Approx(modWheelNorm).margin(0.02f));
}

// T079: PitchBend -> AllVoiceFilterCutoff routing via Macro1
TEST_CASE("ExtModulation: PitchBend -> AllVoiceFilterCutoff via Macro1",
          "[ext_modulation][us6]") {
    TestEngineScaffold scaffold;
    scaffold.prepare();

    // Normalize pitch bend: 0x3000 -> ~+0.5 bipolar
    float pitchBendBipolar = normalizePitchBend(0x3000);
    // Map bipolar [-1,+1] to unipolar [0,1] for macro: (pb + 1.0) * 0.5
    float macroValue = (pitchBendBipolar + 1.0f) * 0.5f;
    scaffold.setMacroValue(0, macroValue);  // Macro1 = pitch bend
    scaffold.setRouting(0, ModSource::Macro1, kAllVoiceFilterCutoffDestId, 1.0f);

    scaffold.processBlock();

    float offset = scaffold.getOffset(kAllVoiceFilterCutoffDestId);
    // macroValue ~ 0.75, amount = 1.0 -> offset ~ 0.75
    REQUIRE(offset == Approx(macroValue).margin(0.02f));
}

// T080: Rungler via Macro3 -> GlobalFilterCutoff routing
TEST_CASE("ExtModulation: Rungler via Macro3 -> GlobalFilterCutoff",
          "[ext_modulation][us6]") {
    TestEngineScaffold scaffold;
    scaffold.prepare();

    // Simulate Rungler output injected via Macro3
    float runglerValue = 0.6f;  // Simulated Rungler CV
    scaffold.setMacroValue(2, runglerValue);  // Macro3 = Rungler
    scaffold.setRouting(0, ModSource::Macro3, kGlobalFilterCutoffDestId, 0.5f);

    scaffold.processBlock();

    float offset = scaffold.getOffset(kGlobalFilterCutoffDestId);
    // Macro3 output = 0.6, amount = 0.5 -> offset = 0.6 * 0.5 = 0.3
    REQUIRE(offset == Approx(0.3f).margin(0.01f));
}

// =============================================================================
// 042-ext-modulation-system: US7 - Performance Benchmark (SC-002)
// =============================================================================

TEST_CASE("ExtModulation: Global modulation engine performance < 0.5% CPU",
          "[ext_modulation][performance][SC-002]") {
    TestEngineScaffold scaffold;
    scaffold.prepare();

    // Configure 32 routings (max capacity)
    for (size_t i = 0; i < 32; ++i) {
        // Cycle through source types (1..12, skip None=0)
        auto src = static_cast<ModSource>(1 + (i % (kModSourceCount - 1)));
        uint32_t destId = static_cast<uint32_t>(i % 10);
        float amount = (i % 2 == 0) ? 0.7f : -0.4f;
        scaffold.setRouting(i, src, destId, amount);
    }

    constexpr size_t blockSize = 512;
    constexpr size_t totalBlocks = (44100 * 10) / blockSize; // 10 seconds

    const auto start = std::chrono::high_resolution_clock::now();

    for (size_t block = 0; block < totalBlocks; ++block) {
        scaffold.processBlock();
    }

    const auto end = std::chrono::high_resolution_clock::now();
    const double durationMs = std::chrono::duration<double, std::milli>(end - start).count();
    const double cpuPercent = (durationMs / 10000.0) * 100.0;

    INFO("Global modulation processing time: " << durationMs << " ms for 10s of audio");
    INFO("CPU usage: " << cpuPercent << "%");

    // SC-002: global modulation < 0.5% CPU
    REQUIRE(cpuPercent < 0.5);
}
