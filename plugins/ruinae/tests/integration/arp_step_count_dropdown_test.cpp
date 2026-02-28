// ==============================================================================
// Integration Test: Arp Step Count Dropdown Regression Guard
// ==============================================================================
// Comprehensive tests for the step count option lists in all 6 arp lanes.
// Simulates the exact normalization formula used by the dropdown callback
// and verifies the full flow:
//   Dropdown selection -> normalized value -> processParameterChanges ->
//   ArpeggiatorParams atomic -> state save/restore -> controller readback
//
// These tests exist specifically to prevent regressions in the step count
// dropdown functionality across all lanes.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "controller/controller.h"
#include "plugin_ids.h"
#include "parameters/arpeggiator_params.h"

#include "public.sdk/source/common/memorystream.h"
#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/vsttypes.h"

#include <vector>

using Catch::Approx;

// =============================================================================
// Test Infrastructure
// =============================================================================

// Expose processParameterChanges and applyParamsToEngine for testing
class StepCountTestableProcessor : public Ruinae::Processor {
public:
    using Ruinae::Processor::processParameterChanges;
    using Ruinae::Processor::applyParamsToEngine;
};

// Mock single-value parameter queue
class StepCountParamQueue : public Steinberg::Vst::IParamValueQueue {
public:
    StepCountParamQueue(Steinberg::Vst::ParamID id, double value)
        : paramId_(id), value_(value) {}

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    Steinberg::Vst::ParamID PLUGIN_API getParameterId() override { return paramId_; }
    Steinberg::int32 PLUGIN_API getPointCount() override { return 1; }

    Steinberg::tresult PLUGIN_API getPoint(
        Steinberg::int32 index,
        Steinberg::int32& sampleOffset,
        Steinberg::Vst::ParamValue& value) override {
        if (index != 0) return Steinberg::kResultFalse;
        sampleOffset = 0;
        value = value_;
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API addPoint(
        Steinberg::int32, Steinberg::Vst::ParamValue,
        Steinberg::int32&) override {
        return Steinberg::kResultFalse;
    }

private:
    Steinberg::Vst::ParamID paramId_;
    double value_;
};

// Mock parameter changes container
class StepCountParamChanges : public Steinberg::Vst::IParameterChanges {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    Steinberg::int32 PLUGIN_API getParameterCount() override {
        return static_cast<Steinberg::int32>(queues_.size());
    }

    Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(
        Steinberg::int32 index) override {
        if (index < 0 || index >= static_cast<Steinberg::int32>(queues_.size()))
            return nullptr;
        return &queues_[static_cast<size_t>(index)];
    }

    Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(
        const Steinberg::Vst::ParamID&, Steinberg::int32&) override {
        return nullptr;
    }

    void add(Steinberg::Vst::ParamID id, double value) {
        queues_.emplace_back(id, value);
    }

private:
    std::vector<StepCountParamQueue> queues_;
};

static std::unique_ptr<StepCountTestableProcessor> makeTestProcessor() {
    auto p = std::make_unique<StepCountTestableProcessor>();
    p->initialize(nullptr);

    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    p->setupProcessing(setup);

    return p;
}

// =============================================================================
// Dropdown normalization formula (matches arp_lane_header.h::openLengthDropdown)
// =============================================================================
// This is the EXACT formula the dropdown uses to normalize step counts.
// If this formula changes in arp_lane_header.h, it must change here too.
static double dropdownNormalize(int steps) {
    return static_cast<double>(steps - 1) / 31.0;
}

// Denormalization formula (matches handleArpParamChange)
static int processorDenormalize(double normalized) {
    return std::clamp(static_cast<int>(1.0 + std::round(normalized * 31.0)), 1, 32);
}

// =============================================================================
// Lane configuration for parameterized tests
// =============================================================================
struct LaneConfig {
    const char* name;
    Steinberg::Vst::ParamID lengthParamId;
};

static const LaneConfig kAllLanes[] = {
    {"Velocity",  Ruinae::kArpVelocityLaneLengthId},
    {"Gate",      Ruinae::kArpGateLaneLengthId},
    {"Pitch",     Ruinae::kArpPitchLaneLengthId},
    {"Modifier",  Ruinae::kArpModifierLaneLengthId},
    {"Ratchet",   Ruinae::kArpRatchetLaneLengthId},
    {"Condition", Ruinae::kArpConditionLaneLengthId},
};

// ==============================================================================
// TEST: Normalization/Denormalization Formula Consistency
// ==============================================================================
// Verifies that the dropdown normalization formula and the processor
// denormalization formula are exact inverses for ALL 32 step counts.
// This is the most fundamental regression guard.

TEST_CASE("StepCount_NormDenorm_AllSteps_RoundTrip",
          "[arp][step-count][regression]") {
    for (int steps = 1; steps <= 32; ++steps) {
        double normalized = dropdownNormalize(steps);
        int recovered = processorDenormalize(normalized);
        INFO("Step count " << steps << " -> normalized " << normalized
             << " -> recovered " << recovered);
        REQUIRE(recovered == steps);
    }
}

// ==============================================================================
// TEST: Controller Parameter Round-Trip for ALL Lanes, ALL Step Counts
// ==============================================================================
// For each of the 6 lanes, set the length parameter using the dropdown's
// normalization formula, then read it back via the controller. This verifies
// the RangeParameter quantization doesn't corrupt the value.

TEST_CASE("StepCount_ControllerRoundTrip_AllLanes_AllSteps",
          "[arp][step-count][controller][regression]") {
    Ruinae::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    for (const auto& lane : kAllLanes) {
        SECTION(std::string(lane.name) + " lane - all 32 step counts") {
            for (int steps = 1; steps <= 32; ++steps) {
                double normalized = dropdownNormalize(steps);

                controller.setParamNormalized(lane.lengthParamId, normalized);

                auto* param = controller.getParameterObject(lane.lengthParamId);
                REQUIRE(param != nullptr);

                double readBack = param->getNormalized();
                int readBackSteps = processorDenormalize(readBack);

                INFO(lane.name << " lane: step count " << steps
                     << " normalized to " << normalized
                     << " read back as " << readBack
                     << " (steps=" << readBackSteps << ")");
                REQUIRE(readBackSteps == steps);
            }
        }
    }

    controller.terminate();
}

// ==============================================================================
// TEST: Processor Receives Correct Lane Length from Dropdown
// ==============================================================================
// Simulates the dropdown callback flow: normalize step count, feed through
// processParameterChanges, save state, load into controller, verify.
// This tests the full host -> processor -> state -> controller round-trip.

TEST_CASE("StepCount_ProcessorRoundTrip_AllLanes_KeyValues",
          "[arp][step-count][processor][regression]") {

    // Test a representative set of step counts
    const int testSteps[] = {1, 2, 4, 8, 12, 16, 24, 31, 32};

    for (const auto& lane : kAllLanes) {
        for (int steps : testSteps) {
            SECTION(std::string(lane.name) + " lane - " + std::to_string(steps) + " steps") {
                auto proc = makeTestProcessor();

                double normalized = dropdownNormalize(steps);
                StepCountParamChanges changes;
                changes.add(lane.lengthParamId, normalized);
                proc->processParameterChanges(&changes);

                // Save processor state
                Steinberg::MemoryStream stream;
                REQUIRE(proc->getState(&stream) == Steinberg::kResultTrue);

                // Load into controller
                Ruinae::Controller controller;
                REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

                stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
                REQUIRE(controller.setComponentState(&stream) == Steinberg::kResultTrue);

                // Verify lane length reads back correctly
                double readBack = controller.getParamNormalized(lane.lengthParamId);
                int readBackSteps = processorDenormalize(readBack);

                INFO(lane.name << " lane: set " << steps << " steps"
                     << " (normalized=" << normalized << ")"
                     << " -> readback=" << readBack
                     << " (steps=" << readBackSteps << ")");
                REQUIRE(readBackSteps == steps);

                controller.terminate();
                proc->terminate();
            }
        }
    }
}

// ==============================================================================
// TEST: All 32 Step Counts Through Full Processor Round-Trip
// ==============================================================================
// For each lane, exhaustively test all 32 step counts through the full
// processor save/restore/controller round-trip. This is the definitive
// regression guard for the step count dropdown.

TEST_CASE("StepCount_ProcessorRoundTrip_AllLanes_All32Steps",
          "[arp][step-count][processor][exhaustive][regression]") {

    for (const auto& lane : kAllLanes) {
        SECTION(std::string(lane.name) + " lane - exhaustive 32 step counts") {
            for (int steps = 1; steps <= 32; ++steps) {
                auto proc = makeTestProcessor();

                double normalized = dropdownNormalize(steps);
                StepCountParamChanges changes;
                changes.add(lane.lengthParamId, normalized);
                proc->processParameterChanges(&changes);

                // Save and restore
                Steinberg::MemoryStream stream;
                REQUIRE(proc->getState(&stream) == Steinberg::kResultTrue);

                Ruinae::Controller controller;
                REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

                stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
                REQUIRE(controller.setComponentState(&stream) == Steinberg::kResultTrue);

                double readBack = controller.getParamNormalized(lane.lengthParamId);
                int readBackSteps = processorDenormalize(readBack);

                INFO(lane.name << " lane step count " << steps);
                CHECK(readBackSteps == steps);

                controller.terminate();
                proc->terminate();
            }
        }
    }
}

// ==============================================================================
// TEST: Multiple Lane Lengths Changed Simultaneously
// ==============================================================================
// Verifies that changing all 6 lane lengths in the same parameter block
// doesn't cause crosstalk or overwrites.

TEST_CASE("StepCount_AllLanesSimultaneous_NoCrosstalk",
          "[arp][step-count][regression]") {
    auto proc = makeTestProcessor();

    // Set each lane to a DIFFERENT step count
    StepCountParamChanges changes;
    changes.add(Ruinae::kArpVelocityLaneLengthId, dropdownNormalize(4));   // 4 steps
    changes.add(Ruinae::kArpGateLaneLengthId, dropdownNormalize(8));       // 8 steps
    changes.add(Ruinae::kArpPitchLaneLengthId, dropdownNormalize(12));     // 12 steps
    changes.add(Ruinae::kArpModifierLaneLengthId, dropdownNormalize(16));  // 16 steps
    changes.add(Ruinae::kArpRatchetLaneLengthId, dropdownNormalize(24));   // 24 steps
    changes.add(Ruinae::kArpConditionLaneLengthId, dropdownNormalize(32)); // 32 steps

    proc->processParameterChanges(&changes);

    // Save and restore
    Steinberg::MemoryStream stream;
    REQUIRE(proc->getState(&stream) == Steinberg::kResultTrue);

    Ruinae::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(controller.setComponentState(&stream) == Steinberg::kResultTrue);

    // Verify each lane independently
    auto checkLane = [&](Steinberg::Vst::ParamID paramId, int expectedSteps,
                         const char* name) {
        double readBack = controller.getParamNormalized(paramId);
        int readBackSteps = processorDenormalize(readBack);
        INFO(name << " lane: expected " << expectedSteps << " got " << readBackSteps);
        CHECK(readBackSteps == expectedSteps);
    };

    checkLane(Ruinae::kArpVelocityLaneLengthId, 4, "Velocity");
    checkLane(Ruinae::kArpGateLaneLengthId, 8, "Gate");
    checkLane(Ruinae::kArpPitchLaneLengthId, 12, "Pitch");
    checkLane(Ruinae::kArpModifierLaneLengthId, 16, "Modifier");
    checkLane(Ruinae::kArpRatchetLaneLengthId, 24, "Ratchet");
    checkLane(Ruinae::kArpConditionLaneLengthId, 32, "Condition");

    controller.terminate();
    proc->terminate();
}

// ==============================================================================
// TEST: Step Count Survives Preset Load (loadComponentStateWithNotify)
// ==============================================================================
// Simulates the preset browser's load flow: save state from one processor,
// load it into another processor, then into a controller using the same
// path as loadComponentStateWithNotify.

TEST_CASE("StepCount_PresetLoadRoundTrip_AllLanes",
          "[arp][step-count][preset][regression]") {
    // Create source processor with non-default lane lengths
    auto proc1 = makeTestProcessor();

    StepCountParamChanges changes;
    changes.add(Ruinae::kArpVelocityLaneLengthId, dropdownNormalize(5));
    changes.add(Ruinae::kArpGateLaneLengthId, dropdownNormalize(10));
    changes.add(Ruinae::kArpPitchLaneLengthId, dropdownNormalize(15));
    changes.add(Ruinae::kArpModifierLaneLengthId, dropdownNormalize(20));
    changes.add(Ruinae::kArpRatchetLaneLengthId, dropdownNormalize(25));
    changes.add(Ruinae::kArpConditionLaneLengthId, dropdownNormalize(30));
    proc1->processParameterChanges(&changes);

    // Save state
    Steinberg::MemoryStream stream1;
    REQUIRE(proc1->getState(&stream1) == Steinberg::kResultTrue);

    // Load into a FRESH processor (simulates preset load)
    auto proc2 = makeTestProcessor();
    stream1.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(proc2->setState(&stream1) == Steinberg::kResultTrue);

    // Save from restored processor
    Steinberg::MemoryStream stream2;
    REQUIRE(proc2->getState(&stream2) == Steinberg::kResultTrue);

    // Load into controller (simulates setComponentState after preset load)
    Ruinae::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    stream2.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(controller.setComponentState(&stream2) == Steinberg::kResultTrue);

    // Verify all lane lengths survived the double round-trip
    auto checkLane = [&](Steinberg::Vst::ParamID paramId, int expectedSteps,
                         const char* name) {
        double readBack = controller.getParamNormalized(paramId);
        int readBackSteps = processorDenormalize(readBack);
        INFO(name << " lane: expected " << expectedSteps << " got " << readBackSteps);
        CHECK(readBackSteps == expectedSteps);
    };

    checkLane(Ruinae::kArpVelocityLaneLengthId, 5, "Velocity");
    checkLane(Ruinae::kArpGateLaneLengthId, 10, "Gate");
    checkLane(Ruinae::kArpPitchLaneLengthId, 15, "Pitch");
    checkLane(Ruinae::kArpModifierLaneLengthId, 20, "Modifier");
    checkLane(Ruinae::kArpRatchetLaneLengthId, 25, "Ratchet");
    checkLane(Ruinae::kArpConditionLaneLengthId, 30, "Condition");

    controller.terminate();
    proc1->terminate();
    proc2->terminate();
}

// ==============================================================================
// TEST: Controller setParamNormalized Syncs to Denormalized Step Count
// ==============================================================================
// Verifies that when the host calls setParamNormalized (e.g., after performEdit),
// the controller's parameter object stores the value correctly and can be
// denormalized to the expected integer step count.

TEST_CASE("StepCount_ControllerSetParam_DenormalizesCorrectly",
          "[arp][step-count][controller][regression]") {
    Ruinae::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    // Test boundary values for each lane
    struct TestCase {
        const char* desc;
        Steinberg::Vst::ParamID paramId;
        int steps;
    };

    const TestCase cases[] = {
        // Velocity lane boundaries
        {"Velocity min=1",  Ruinae::kArpVelocityLaneLengthId, 1},
        {"Velocity mid=16", Ruinae::kArpVelocityLaneLengthId, 16},
        {"Velocity max=32", Ruinae::kArpVelocityLaneLengthId, 32},

        // Gate lane boundaries
        {"Gate min=1",  Ruinae::kArpGateLaneLengthId, 1},
        {"Gate mid=16", Ruinae::kArpGateLaneLengthId, 16},
        {"Gate max=32", Ruinae::kArpGateLaneLengthId, 32},

        // Pitch lane boundaries
        {"Pitch min=1",  Ruinae::kArpPitchLaneLengthId, 1},
        {"Pitch mid=16", Ruinae::kArpPitchLaneLengthId, 16},
        {"Pitch max=32", Ruinae::kArpPitchLaneLengthId, 32},

        // Modifier lane boundaries
        {"Modifier min=1",  Ruinae::kArpModifierLaneLengthId, 1},
        {"Modifier mid=16", Ruinae::kArpModifierLaneLengthId, 16},
        {"Modifier max=32", Ruinae::kArpModifierLaneLengthId, 32},

        // Ratchet lane boundaries
        {"Ratchet min=1",  Ruinae::kArpRatchetLaneLengthId, 1},
        {"Ratchet mid=16", Ruinae::kArpRatchetLaneLengthId, 16},
        {"Ratchet max=32", Ruinae::kArpRatchetLaneLengthId, 32},

        // Condition lane boundaries
        {"Condition min=1",  Ruinae::kArpConditionLaneLengthId, 1},
        {"Condition mid=16", Ruinae::kArpConditionLaneLengthId, 16},
        {"Condition max=32", Ruinae::kArpConditionLaneLengthId, 32},
    };

    for (const auto& tc : cases) {
        SECTION(tc.desc) {
            double normalized = dropdownNormalize(tc.steps);
            controller.setParamNormalized(tc.paramId, normalized);

            auto* param = controller.getParameterObject(tc.paramId);
            REQUIRE(param != nullptr);

            double readBack = param->getNormalized();
            int readBackSteps = processorDenormalize(readBack);

            REQUIRE(readBackSteps == tc.steps);
        }
    }

    controller.terminate();
}

// ==============================================================================
// TEST: Default Lane Lengths Are 16 (Regression Guard)
// ==============================================================================
// Verifies that all 6 lanes default to 16 steps after initialization.
// A previous regression had lanes defaulting to 1 step.

TEST_CASE("StepCount_DefaultsTo16_AllLanes",
          "[arp][step-count][defaults][regression]") {
    Ruinae::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    for (const auto& lane : kAllLanes) {
        SECTION(std::string(lane.name) + " lane defaults to 16 steps") {
            auto* param = controller.getParameterObject(lane.lengthParamId);
            REQUIRE(param != nullptr);

            double defaultNorm = param->getNormalized();
            int defaultSteps = processorDenormalize(defaultNorm);

            INFO(lane.name << " lane default: normalized=" << defaultNorm
                 << " steps=" << defaultSteps);
            REQUIRE(defaultSteps == 16);
        }
    }

    controller.terminate();
}

// ==============================================================================
// TEST: Processor Default Lane Lengths Are 16
// ==============================================================================
// Verifies the processor's atomic lane lengths default to 16 by saving
// a fresh processor's state and checking the controller reads 16 for all lanes.

TEST_CASE("StepCount_ProcessorDefaults_AllLanes16",
          "[arp][step-count][processor][defaults][regression]") {
    auto proc = makeTestProcessor();

    // Save default state
    Steinberg::MemoryStream stream;
    REQUIRE(proc->getState(&stream) == Steinberg::kResultTrue);

    Ruinae::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(controller.setComponentState(&stream) == Steinberg::kResultTrue);

    for (const auto& lane : kAllLanes) {
        double readBack = controller.getParamNormalized(lane.lengthParamId);
        int readBackSteps = processorDenormalize(readBack);
        INFO(lane.name << " lane processor default");
        CHECK(readBackSteps == 16);
    }

    controller.terminate();
    proc->terminate();
}

// ==============================================================================
// TEST: Step Count 1 is Selectable (Previous Regression: kMinSteps=2)
// ==============================================================================
// A previous regression had kMinSteps=2 which prevented selecting 1 step.

TEST_CASE("StepCount_MinValue1_AllLanes",
          "[arp][step-count][boundary][regression]") {
    for (const auto& lane : kAllLanes) {
        SECTION(std::string(lane.name) + " lane accepts step count 1") {
            auto proc = makeTestProcessor();

            double normalized = dropdownNormalize(1); // = 0.0
            REQUIRE(normalized == Approx(0.0).margin(1e-12));

            StepCountParamChanges changes;
            changes.add(lane.lengthParamId, normalized);
            proc->processParameterChanges(&changes);

            Steinberg::MemoryStream stream;
            REQUIRE(proc->getState(&stream) == Steinberg::kResultTrue);

            Ruinae::Controller controller;
            REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

            stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
            REQUIRE(controller.setComponentState(&stream) == Steinberg::kResultTrue);

            double readBack = controller.getParamNormalized(lane.lengthParamId);
            int readBackSteps = processorDenormalize(readBack);

            INFO(lane.name << " lane: step count 1");
            REQUIRE(readBackSteps == 1);

            controller.terminate();
            proc->terminate();
        }
    }
}
