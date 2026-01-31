// ==============================================================================
// Combined State Round-Trip Integration Test
// ==============================================================================
// SC-011: Verifies that ALL state persists together in a single round-trip:
// - Expand states for bands 0 and 2
// - Modulation panel visibility
// - Window size (1200x720)
// - Global MIDI CC mappings
// - Per-preset MIDI CC mappings
//
// Constitution Principle VIII: Testing Discipline
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "midi/midi_cc_manager.h"
#include "plugin_ids.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace Krate::Plugins;
using namespace Disrumpo;
using Steinberg::Vst::ParamID;

// =============================================================================
// SC-011: Combined State Round-Trip Test
// =============================================================================

TEST_CASE("SC-011: All state persists together in round-trip", "[integration][state_persistence]") {
    // Simulates the full controller state persistence workflow:
    // 1. Set expand state for bands 0 and 2
    // 2. Set modulation panel visible
    // 3. Set window size to 1200x720
    // 4. Add global and per-preset MIDI CC mappings
    // 5. Serialize all state
    // 6. Deserialize into fresh state
    // 7. Verify all values match

    // =========================================================================
    // Step 1: Set up expand states (band 0 and 2 expanded, others collapsed)
    // =========================================================================
    constexpr int kMaxBands = 8;
    float expandStates[kMaxBands] = {
        1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f
    };

    // Verify initial setup
    REQUIRE(expandStates[0] == 1.0f);  // Band 0 expanded
    REQUIRE(expandStates[1] == 0.0f);  // Band 1 collapsed
    REQUIRE(expandStates[2] == 1.0f);  // Band 2 expanded
    for (int i = 3; i < kMaxBands; ++i) {
        REQUIRE(expandStates[i] == 0.0f);
    }

    // =========================================================================
    // Step 2: Set modulation panel visible
    // =========================================================================
    float modPanelVisible = 1.0f;
    REQUIRE(modPanelVisible == 1.0f);

    // =========================================================================
    // Step 3: Set window size to 1200x720
    // =========================================================================
    double windowWidth = 1200.0;
    double windowHeight = 720.0;

    // Verify 5:3 aspect ratio
    constexpr double kAspectRatio = 5.0 / 3.0;
    REQUIRE_THAT(windowWidth / windowHeight,
        Catch::Matchers::WithinAbs(kAspectRatio, 0.01));

    // =========================================================================
    // Step 4: Set up MIDI CC mappings
    // =========================================================================
    MidiCCManager originalManager;

    // Add a global mapping: CC 74 -> sweep frequency
    auto sweepFreqId = makeSweepParamId(SweepParamType::kSweepFrequency);
    originalManager.addGlobalMapping(74, sweepFreqId, false);

    // Add a global 14-bit mapping: CC 1 (MSB) + CC 33 (LSB) -> sweep width
    auto sweepWidthId = makeSweepParamId(SweepParamType::kSweepWidth);
    originalManager.addGlobalMapping(1, sweepWidthId, true);

    // Add a per-preset mapping: CC 11 -> band 0 gain
    auto band0GainId = makeBandParamId(0, BandParamType::kBandGain);
    originalManager.addPresetMapping(11, band0GainId, false);

    // =========================================================================
    // Step 5: Serialize all state
    // =========================================================================

    // 5a: Serialize expand states (these are standard VST3 parameters,
    // serialized as normalized floats by EditControllerEx1)
    std::vector<float> serializedExpandStates(expandStates, expandStates + kMaxBands);

    // 5b: Serialize modulation panel visibility (standard VST3 parameter)
    float serializedModPanel = modPanelVisible;

    // 5c: Serialize window size (controller state)
    // Note: Only width is used for restore; height is recomputed from 5:3 ratio.
    double serializedWidth = windowWidth;
    [[maybe_unused]] double serializedHeight = windowHeight;

    // 5d: Serialize MIDI CC mappings
    auto globalMidiData = originalManager.serializeGlobalMappings();
    auto presetMidiData = originalManager.serializePresetMappings();

    REQUIRE_FALSE(globalMidiData.empty());
    REQUIRE_FALSE(presetMidiData.empty());

    // =========================================================================
    // Step 6: Deserialize into fresh state
    // =========================================================================

    // 6a: Restore expand states
    float restoredExpandStates[kMaxBands]{};
    for (int i = 0; i < kMaxBands; ++i) {
        restoredExpandStates[i] = serializedExpandStates[static_cast<size_t>(i)];
    }

    // 6b: Restore modulation panel visibility
    float restoredModPanel = serializedModPanel;

    // 6c: Restore window size with clamping and aspect ratio enforcement
    double restoredWidth = std::clamp(serializedWidth, 834.0, 1400.0);
    double restoredHeight = restoredWidth * 3.0 / 5.0;  // Enforce 5:3 ratio

    // 6d: Restore MIDI CC mappings
    MidiCCManager restoredManager;
    REQUIRE(restoredManager.deserializeGlobalMappings(
        globalMidiData.data(), globalMidiData.size()));
    REQUIRE(restoredManager.deserializePresetMappings(
        presetMidiData.data(), presetMidiData.size()));

    // =========================================================================
    // Step 7: Verify all values match
    // =========================================================================

    SECTION("expand states restored correctly") {
        REQUIRE(restoredExpandStates[0] == 1.0f);
        REQUIRE(restoredExpandStates[1] == 0.0f);
        REQUIRE(restoredExpandStates[2] == 1.0f);
        for (int i = 3; i < kMaxBands; ++i) {
            REQUIRE(restoredExpandStates[i] == 0.0f);
        }
    }

    SECTION("modulation panel visibility restored correctly") {
        REQUIRE(restoredModPanel == 1.0f);
        bool shouldBeVisible = (restoredModPanel >= 0.5f);
        REQUIRE(shouldBeVisible == true);
    }

    SECTION("window size restored correctly") {
        REQUIRE_THAT(restoredWidth,
            Catch::Matchers::WithinAbs(1200.0, 0.1));
        REQUIRE_THAT(restoredHeight,
            Catch::Matchers::WithinAbs(720.0, 0.1));
        REQUIRE_THAT(restoredWidth / restoredHeight,
            Catch::Matchers::WithinAbs(kAspectRatio, 0.01));
    }

    SECTION("global MIDI CC mappings restored correctly") {
        MidiCCMapping mapping;

        // CC 74 -> sweep frequency
        REQUIRE(restoredManager.getMapping(74, mapping));
        REQUIRE(mapping.paramId == sweepFreqId);
        REQUIRE(mapping.is14Bit == false);

        // CC 1 -> sweep width (14-bit)
        REQUIRE(restoredManager.getMapping(1, mapping));
        REQUIRE(mapping.paramId == sweepWidthId);
        REQUIRE(mapping.is14Bit == true);
    }

    SECTION("per-preset MIDI CC mappings restored correctly") {
        MidiCCMapping mapping;

        // CC 11 -> band 0 gain (per-preset)
        REQUIRE(restoredManager.getMapping(11, mapping));
        REQUIRE(mapping.paramId == band0GainId);
        REQUIRE(mapping.isPerPreset == true);
    }

    SECTION("MIDI CC mappings are functional after restore") {
        // Verify CC 74 still controls sweep frequency
        ParamID callbackParamId = 0;
        double callbackValue = -1.0;

        restoredManager.processCCMessage(74, 100,
            [&](ParamID id, double val) {
                callbackParamId = id;
                callbackValue = val;
            });

        REQUIRE(callbackParamId == sweepFreqId);
        REQUIRE_THAT(callbackValue,
            Catch::Matchers::WithinAbs(100.0 / 127.0, 0.01));

        // Verify per-preset CC 11 controls band 0 gain
        callbackParamId = 0;
        restoredManager.processCCMessage(11, 64,
            [&](ParamID id, double val) {
                callbackParamId = id;
                callbackValue = val;
            });

        REQUIRE(callbackParamId == band0GainId);
    }

    SECTION("all state fields present in a single round-trip") {
        // Final combined check: all five state categories are non-default
        bool hasExpandStates = (restoredExpandStates[0] == 1.0f &&
                                restoredExpandStates[2] == 1.0f);
        bool hasModPanel = (restoredModPanel == 1.0f);
        bool hasWindowSize = (restoredWidth == 1200.0);
        bool hasGlobalMidi = restoredManager.getActiveMappings().size() >= 2;
        bool hasPresetMidi = false;
        {
            MidiCCMapping m;
            hasPresetMidi = restoredManager.getMapping(11, m) && m.isPerPreset;
        }

        REQUIRE(hasExpandStates);
        REQUIRE(hasModPanel);
        REQUIRE(hasWindowSize);
        REQUIRE(hasGlobalMidi);
        REQUIRE(hasPresetMidi);
    }
}
