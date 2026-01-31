// ==============================================================================
// MIDI Learn Integration Tests
// ==============================================================================
// T062: Tests for MIDI Learn workflow, mapping persistence, CC conflict resolution
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "midi/midi_cc_manager.h"
#include "plugin_ids.h"

using namespace Krate::Plugins;
using namespace Disrumpo;
using Steinberg::Vst::ParamID;

// =============================================================================
// MIDI Learn Workflow Integration Tests
// =============================================================================

TEST_CASE("MIDI Learn full workflow: start -> CC -> mapped", "[integration][midi_learn]") {
    MidiCCManager manager;

    auto sweepFreqId = makeSweepParamId(SweepParamType::kSweepFrequency);

    // Step 1: Start MIDI Learn for sweep frequency
    manager.startLearn(sweepFreqId);
    REQUIRE(manager.isLearning());
    REQUIRE(manager.getLearnTargetParamId() == sweepFreqId);

    // Step 2: Receive CC 74 (modulation wheel variant)
    ParamID callbackParamId = 0;
    double callbackValue = -1.0;

    bool handled = manager.processCCMessage(74, 64,
        [&](ParamID id, double val) {
            callbackParamId = id;
            callbackValue = val;
        });

    REQUIRE(handled);
    REQUIRE_FALSE(manager.isLearning());  // Learn mode deactivated
    REQUIRE(callbackParamId == sweepFreqId);

    // Step 3: Subsequent CC 74 messages control sweep frequency
    callbackValue = -1.0;
    manager.processCCMessage(74, 127,
        [&](ParamID id, double val) {
            callbackParamId = id;
            callbackValue = val;
        });

    REQUIRE(callbackParamId == sweepFreqId);
    REQUIRE_THAT(callbackValue, Catch::Matchers::WithinAbs(1.0, 0.001));
}

TEST_CASE("MIDI Learn cancel via Escape", "[integration][midi_learn]") {
    MidiCCManager manager;

    auto sweepFreqId = makeSweepParamId(SweepParamType::kSweepFrequency);
    manager.startLearn(sweepFreqId);
    REQUIRE(manager.isLearning());

    // Cancel
    manager.cancelLearn();
    REQUIRE_FALSE(manager.isLearning());

    // No mapping should exist
    MidiCCMapping mapping;
    REQUIRE_FALSE(manager.getMapping(74, mapping));
}

TEST_CASE("MIDI CC conflict resolution: most recent wins (FR-036)", "[integration][midi_learn]") {
    MidiCCManager manager;

    auto param1 = makeSweepParamId(SweepParamType::kSweepFrequency);
    auto param2 = makeSweepParamId(SweepParamType::kSweepWidth);

    // Map CC 74 to param1
    manager.addGlobalMapping(74, param1, false);

    // Map CC 74 to param2 (should override)
    manager.addGlobalMapping(74, param2, false);

    MidiCCMapping mapping;
    REQUIRE(manager.getMapping(74, mapping));
    REQUIRE(mapping.paramId == param2);

    // param1 should no longer be mapped
    uint8_t cc = 0;
    REQUIRE_FALSE(manager.getCCForParam(param1, cc));
}

TEST_CASE("Mapping persistence round-trip", "[integration][midi_learn]") {
    MidiCCManager manager;

    auto sweepFreqId = makeSweepParamId(SweepParamType::kSweepFrequency);
    auto sweepWidthId = makeSweepParamId(SweepParamType::kSweepWidth);

    manager.addGlobalMapping(74, sweepFreqId, false);
    manager.addGlobalMapping(1, sweepWidthId, true);

    // Serialize global
    auto data = manager.serializeGlobalMappings();

    // Create new manager and deserialize
    MidiCCManager restored;
    REQUIRE(restored.deserializeGlobalMappings(data.data(), data.size()));

    // Verify mappings restored
    MidiCCMapping mapping;
    REQUIRE(restored.getMapping(74, mapping));
    REQUIRE(mapping.paramId == sweepFreqId);

    REQUIRE(restored.getMapping(1, mapping));
    REQUIRE(mapping.paramId == sweepWidthId);
    REQUIRE(mapping.is14Bit == true);
}

TEST_CASE("Per-preset mapping overrides global when active", "[integration][midi_learn]") {
    MidiCCManager manager;

    auto globalParam = makeSweepParamId(SweepParamType::kSweepFrequency);
    auto presetParam = makeSweepParamId(SweepParamType::kSweepWidth);

    manager.addGlobalMapping(74, globalParam, false);
    manager.addPresetMapping(74, presetParam, false);

    // Preset mapping takes precedence
    ParamID callbackParamId = 0;
    manager.processCCMessage(74, 64,
        [&](ParamID id, double) { callbackParamId = id; });

    REQUIRE(callbackParamId == presetParam);

    // Clear preset mapping restores global
    manager.clearPresetMappings();
    callbackParamId = 0;
    manager.processCCMessage(74, 64,
        [&](ParamID id, double) { callbackParamId = id; });

    REQUIRE(callbackParamId == globalParam);
}

TEST_CASE("Global mappings persist across preset changes", "[integration][midi_learn]") {
    MidiCCManager manager;

    auto globalParam = makeSweepParamId(SweepParamType::kSweepFrequency);
    manager.addGlobalMapping(74, globalParam, false);

    // Simulate preset change: clear preset mappings only
    manager.clearPresetMappings();

    // Global mapping should still work
    MidiCCMapping mapping;
    REQUIRE(manager.getMapping(74, mapping));
    REQUIRE(mapping.paramId == globalParam);
}
