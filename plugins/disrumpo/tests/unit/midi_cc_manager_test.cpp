// ==============================================================================
// MidiCCManager Unit Tests
// ==============================================================================
// T014: Tests for MIDI CC mapping management, MIDI Learn, 14-bit CC pairing
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "midi/midi_cc_manager.h"

using namespace Krate::Plugins;
using Steinberg::Vst::ParamID;

// =============================================================================
// Mapping CRUD Tests
// =============================================================================

TEST_CASE("MidiCCManager: Add and query global mapping", "[midi][mapping]") {
    MidiCCManager manager;

    manager.addGlobalMapping(74, 0x0F01, false);

    MidiCCMapping mapping;
    REQUIRE(manager.getMapping(74, mapping));
    REQUIRE(mapping.ccNumber == 74);
    REQUIRE(mapping.paramId == 0x0F01);
    REQUIRE(mapping.is14Bit == false);
    REQUIRE(mapping.isPerPreset == false);
}

TEST_CASE("MidiCCManager: Reverse lookup CC for param", "[midi][mapping]") {
    MidiCCManager manager;

    manager.addGlobalMapping(74, 0x0F01, false);

    uint8_t cc = 0;
    REQUIRE(manager.getCCForParam(0x0F01, cc));
    REQUIRE(cc == 74);
}

TEST_CASE("MidiCCManager: Remove global mapping", "[midi][mapping]") {
    MidiCCManager manager;

    manager.addGlobalMapping(74, 0x0F01, false);
    manager.removeGlobalMapping(74);

    MidiCCMapping mapping;
    REQUIRE_FALSE(manager.getMapping(74, mapping));
}

TEST_CASE("MidiCCManager: Remove mapping by param ID", "[midi][mapping]") {
    MidiCCManager manager;

    manager.addGlobalMapping(74, 0x0F01, false);
    manager.removeMappingsForParam(0x0F01);

    MidiCCMapping mapping;
    REQUIRE_FALSE(manager.getMapping(74, mapping));

    uint8_t cc = 0;
    REQUIRE_FALSE(manager.getCCForParam(0x0F01, cc));
}

TEST_CASE("MidiCCManager: Most recent mapping wins for same CC (FR-036)", "[midi][mapping]") {
    MidiCCManager manager;

    manager.addGlobalMapping(74, 0x0F01, false);
    manager.addGlobalMapping(74, 0x0F02, false);

    MidiCCMapping mapping;
    REQUIRE(manager.getMapping(74, mapping));
    REQUIRE(mapping.paramId == 0x0F02);

    // Previous param should no longer be mapped
    uint8_t cc = 0;
    REQUIRE_FALSE(manager.getCCForParam(0x0F01, cc));
}

TEST_CASE("MidiCCManager: Per-preset mapping overrides global (FR-034)", "[midi][mapping]") {
    MidiCCManager manager;

    manager.addGlobalMapping(74, 0x0F01, false);
    manager.addPresetMapping(74, 0x0F02, false);

    MidiCCMapping mapping;
    REQUIRE(manager.getMapping(74, mapping));
    REQUIRE(mapping.paramId == 0x0F02);
    REQUIRE(mapping.isPerPreset == true);
}

TEST_CASE("MidiCCManager: Clear preset mappings restores global", "[midi][mapping]") {
    MidiCCManager manager;

    manager.addGlobalMapping(74, 0x0F01, false);
    manager.addPresetMapping(74, 0x0F02, false);
    manager.clearPresetMappings();

    MidiCCMapping mapping;
    REQUIRE(manager.getMapping(74, mapping));
    REQUIRE(mapping.paramId == 0x0F01);
}

TEST_CASE("MidiCCManager: Clear all removes everything", "[midi][mapping]") {
    MidiCCManager manager;

    manager.addGlobalMapping(74, 0x0F01, false);
    manager.addPresetMapping(75, 0x0F02, false);
    manager.clearAll();

    MidiCCMapping mapping;
    REQUIRE_FALSE(manager.getMapping(74, mapping));
    REQUIRE_FALSE(manager.getMapping(75, mapping));
    REQUIRE_FALSE(manager.isLearning());
}

TEST_CASE("MidiCCManager: Get active mappings merges global and preset", "[midi][mapping]") {
    MidiCCManager manager;

    manager.addGlobalMapping(74, 0x0F01, false);
    manager.addGlobalMapping(75, 0x0F02, false);
    manager.addPresetMapping(74, 0x0F03, false);  // Override CC 74

    auto active = manager.getActiveMappings();
    REQUIRE(active.size() == 2);

    // CC 74 should be the preset mapping
    bool foundCC74 = false;
    bool foundCC75 = false;
    for (const auto& m : active) {
        if (m.ccNumber == 74) {
            REQUIRE(m.paramId == 0x0F03);
            foundCC74 = true;
        }
        if (m.ccNumber == 75) {
            REQUIRE(m.paramId == 0x0F02);
            foundCC75 = true;
        }
    }
    REQUIRE(foundCC74);
    REQUIRE(foundCC75);
}

// =============================================================================
// MIDI Learn Workflow Tests
// =============================================================================

TEST_CASE("MidiCCManager: MIDI Learn workflow", "[midi][learn]") {
    MidiCCManager manager;

    SECTION("start learn sets active state") {
        manager.startLearn(0x0F01);
        REQUIRE(manager.isLearning());
        REQUIRE(manager.getLearnTargetParamId() == 0x0F01);
    }

    SECTION("cancel learn clears state") {
        manager.startLearn(0x0F01);
        manager.cancelLearn();
        REQUIRE_FALSE(manager.isLearning());
        REQUIRE(manager.getLearnTargetParamId() == 0);
    }

    SECTION("receiving CC during learn creates mapping") {
        manager.startLearn(0x0F01);

        ParamID callbackParamId = 0;
        double callbackValue = -1.0;

        bool handled = manager.processCCMessage(74, 64,
            [&](ParamID id, double val) {
                callbackParamId = id;
                callbackValue = val;
            });

        REQUIRE(handled);
        REQUIRE_FALSE(manager.isLearning());

        // Mapping should now exist
        MidiCCMapping mapping;
        REQUIRE(manager.getMapping(74, mapping));
        REQUIRE(mapping.paramId == 0x0F01);

        // Callback should have been called with initial value
        REQUIRE(callbackParamId == 0x0F01);
        REQUIRE_THAT(callbackValue, Catch::Matchers::WithinAbs(64.0 / 127.0, 0.001));
    }

    SECTION("learning from CC 0-31 auto-enables 14-bit") {
        manager.startLearn(0x0F01);
        manager.processCCMessage(1, 64, nullptr);

        MidiCCMapping mapping;
        REQUIRE(manager.getMapping(1, mapping));
        REQUIRE(mapping.is14Bit == true);
    }

    SECTION("learning from CC 64+ does not enable 14-bit") {
        manager.startLearn(0x0F01);
        manager.processCCMessage(74, 64, nullptr);

        MidiCCMapping mapping;
        REQUIRE(manager.getMapping(74, mapping));
        REQUIRE(mapping.is14Bit == false);
    }

    SECTION("learning ignores LSB CCs (32-63)") {
        manager.startLearn(0x0F01);
        bool handled = manager.processCCMessage(33, 64, nullptr);

        // LSB CC should not create a mapping during learn
        REQUIRE_FALSE(handled);
        REQUIRE(manager.isLearning());
    }
}

// =============================================================================
// MIDI CC Processing Tests
// =============================================================================

TEST_CASE("MidiCCManager: Process 7-bit CC message", "[midi][process]") {
    MidiCCManager manager;
    manager.addGlobalMapping(74, 0x0F01, false);

    ParamID callbackParamId = 0;
    double callbackValue = -1.0;

    bool handled = manager.processCCMessage(74, 127,
        [&](ParamID id, double val) {
            callbackParamId = id;
            callbackValue = val;
        });

    REQUIRE(handled);
    REQUIRE(callbackParamId == 0x0F01);
    REQUIRE_THAT(callbackValue, Catch::Matchers::WithinAbs(1.0, 0.001));
}

TEST_CASE("MidiCCManager: Unmapped CC returns false", "[midi][process]") {
    MidiCCManager manager;

    bool handled = manager.processCCMessage(74, 64, nullptr);
    REQUIRE_FALSE(handled);
}

TEST_CASE("MidiCCManager: getMidiControllerAssignment returns mapped param", "[midi][mapping]") {
    MidiCCManager manager;
    manager.addGlobalMapping(74, 0x0F01, false);

    ParamID paramId = 0;
    REQUIRE(manager.getMidiControllerAssignment(74, paramId));
    REQUIRE(paramId == 0x0F01);
}

TEST_CASE("MidiCCManager: getMidiControllerAssignment returns false for unmapped", "[midi][mapping]") {
    MidiCCManager manager;

    ParamID paramId = 0;
    REQUIRE_FALSE(manager.getMidiControllerAssignment(74, paramId));
}

// =============================================================================
// 14-bit CC Pairing Tests
// =============================================================================

TEST_CASE("MidiCCManager: 14-bit CC combines MSB and LSB", "[midi][14bit]") {
    MidiCCManager manager;
    manager.addGlobalMapping(1, 0x0F01, true);  // CC 1 (MSB), CC 33 (LSB)

    ParamID callbackParamId = 0;
    double callbackValue = -1.0;

    // Send MSB first (CC 1, value 64 = 0x40)
    manager.processCCMessage(1, 64,
        [&](ParamID id, double val) {
            callbackParamId = id;
            callbackValue = val;
        });

    // MSB alone should give 7-bit value (FR-040 backwards compat)
    REQUIRE(callbackParamId == 0x0F01);
    REQUIRE_THAT(callbackValue, Catch::Matchers::WithinAbs(64.0 / 127.0, 0.001));

    // Now send LSB (CC 33, value 0 = 0x00)
    callbackValue = -1.0;
    manager.processCCMessage(33, 0,
        [&](ParamID id, double val) {
            callbackParamId = id;
            callbackValue = val;
        });

    // Combined: (64 << 7) | 0 = 8192, normalized = 8192/16383
    REQUIRE(callbackParamId == 0x0F01);
    REQUIRE_THAT(callbackValue, Catch::Matchers::WithinAbs(8192.0 / 16383.0, 0.001));
}

TEST_CASE("MidiCCManager: 14-bit CC full range", "[midi][14bit]") {
    MidiCCManager manager;
    manager.addGlobalMapping(1, 0x0F01, true);

    double callbackValue = -1.0;

    // Send MSB=127, LSB=127 -> max value
    manager.processCCMessage(1, 127, nullptr);
    manager.processCCMessage(33, 127,
        [&](ParamID, double val) { callbackValue = val; });

    // Combined: (127 << 7) | 127 = 16383, normalized = 16383/16383 = 1.0
    REQUIRE_THAT(callbackValue, Catch::Matchers::WithinAbs(1.0, 0.001));
}

TEST_CASE("MidiCCManager: 14-bit CC zero value", "[midi][14bit]") {
    MidiCCManager manager;
    manager.addGlobalMapping(1, 0x0F01, true);

    double callbackValue = -1.0;

    // Send MSB=0, LSB=0 -> min value
    manager.processCCMessage(1, 0, nullptr);
    manager.processCCMessage(33, 0,
        [&](ParamID, double val) { callbackValue = val; });

    REQUIRE_THAT(callbackValue, Catch::Matchers::WithinAbs(0.0, 0.001));
}

TEST_CASE("MidiCCManager: 14-bit CC provides 16384 steps resolution", "[midi][14bit]") {
    MidiCCManager manager;
    manager.addGlobalMapping(1, 0x0F01, true);

    // T082: Verify 14-bit resolution = 16,384 distinct steps (0 to 16383)
    // Test a few representative values across the full range
    struct TestCase {
        uint8_t msb;
        uint8_t lsb;
        uint16_t expectedCombined;
    };

    TestCase cases[] = {
        {0, 0, 0},          // Minimum
        {0, 1, 1},          // One step above minimum
        {64, 0, 8192},      // Midpoint
        {64, 1, 8193},      // One step above midpoint
        {127, 126, 16382},  // One step below maximum
        {127, 127, 16383},  // Maximum
    };

    for (const auto& tc : cases) {
        double callbackValue = -1.0;

        manager.processCCMessage(1, tc.msb, nullptr);  // Send MSB
        manager.processCCMessage(33, tc.lsb,            // Send LSB
            [&](ParamID, double val) { callbackValue = val; });

        double expectedNorm = static_cast<double>(tc.expectedCombined) / 16383.0;
        REQUIRE_THAT(callbackValue, Catch::Matchers::WithinAbs(expectedNorm, 0.0001));
    }
}

TEST_CASE("MidiCCManager: 14-bit only valid for CC 0-31", "[midi][14bit]") {
    MidiCCManager manager;

    // Trying to create 14-bit mapping for CC 74 (outside 0-31 range)
    manager.addGlobalMapping(74, 0x0F01, true);

    MidiCCMapping mapping;
    REQUIRE(manager.getMapping(74, mapping));
    REQUIRE(mapping.is14Bit == false);  // Should be forced to false for CC >= 32
}

// =============================================================================
// Serialization Tests
// =============================================================================

TEST_CASE("MidiCCManager: Serialize and deserialize global mappings", "[midi][serialization]") {
    MidiCCManager manager;
    manager.addGlobalMapping(74, 0x0F01, false);
    manager.addGlobalMapping(1, 0x0F02, true);

    auto data = manager.serializeGlobalMappings();
    REQUIRE(data.size() > 4);  // At least header + some data

    MidiCCManager manager2;
    REQUIRE(manager2.deserializeGlobalMappings(data.data(), data.size()));

    MidiCCMapping mapping;
    REQUIRE(manager2.getMapping(74, mapping));
    REQUIRE(mapping.paramId == 0x0F01);
    REQUIRE(mapping.is14Bit == false);

    REQUIRE(manager2.getMapping(1, mapping));
    REQUIRE(mapping.paramId == 0x0F02);
    REQUIRE(mapping.is14Bit == true);
}

TEST_CASE("MidiCCManager: Serialize and deserialize preset mappings", "[midi][serialization]") {
    MidiCCManager manager;
    manager.addPresetMapping(74, 0x0F01, false);

    auto data = manager.serializePresetMappings();

    MidiCCManager manager2;
    REQUIRE(manager2.deserializePresetMappings(data.data(), data.size()));

    MidiCCMapping mapping;
    REQUIRE(manager2.getMapping(74, mapping));
    REQUIRE(mapping.paramId == 0x0F01);
}

TEST_CASE("MidiCCManager: Deserialize empty data", "[midi][serialization]") {
    MidiCCManager manager;

    REQUIRE_FALSE(manager.deserializeGlobalMappings(nullptr, 0));

    uint8_t emptyData[4] = {0, 0, 0, 0};  // count = 0
    REQUIRE(manager.deserializeGlobalMappings(emptyData, 4));
}

TEST_CASE("MidiCCManager: Deserialize too-small data fails", "[midi][serialization]") {
    MidiCCManager manager;

    uint8_t smallData[2] = {1, 0};
    REQUIRE_FALSE(manager.deserializeGlobalMappings(smallData, 2));
}
