// ==============================================================================
// Integration Test: Arp Lane Parameter Flow (079-layout-framework)
// ==============================================================================
// Verifies velocity (and later gate) lane parameter round-trip:
//   Set a parameter via setParamNormalized(), read it back, verify value.
//
// This file is dedicated to arp lane parameter flow tests and will be
// extended in subsequent phases (US2-US6).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "controller/controller.h"
#include "plugin_ids.h"

#include "public.sdk/source/common/memorystream.h"
#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/vsttypes.h"

#include <vector>

using Catch::Approx;

// =============================================================================
// Helpers for state round-trip tests
// =============================================================================

// Expose processParameterChanges for feeding parameter values into the processor
class ArpFlowTestableProcessor : public Ruinae::Processor {
public:
    using Ruinae::Processor::processParameterChanges;
};

// Mock single-value parameter queue
class ArpFlowParamQueue : public Steinberg::Vst::IParamValueQueue {
public:
    ArpFlowParamQueue(Steinberg::Vst::ParamID id, double value)
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
class ArpFlowParamChanges : public Steinberg::Vst::IParameterChanges {
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
    std::vector<ArpFlowParamQueue> queues_;
};

static std::unique_ptr<ArpFlowTestableProcessor> makeArpFlowProcessor() {
    auto p = std::make_unique<ArpFlowTestableProcessor>();
    p->initialize(nullptr);

    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    p->setupProcessing(setup);

    return p;
}

// ==============================================================================
// T023: Velocity Lane Parameter Round-Trip (SC-007)
// ==============================================================================

TEST_CASE("VelocityLane_ParameterRoundTrip_ValuePreserved", "[arp][integration][velocity]") {
    Ruinae::Controller controller;
    auto result = controller.initialize(nullptr);
    REQUIRE(result == Steinberg::kResultOk);

    SECTION("Set velocity step 0 to 0.75, read back matches within 1e-6") {
        auto paramId = static_cast<Steinberg::Vst::ParamID>(
            Ruinae::kArpVelocityLaneStep0Id);
        controller.setParamNormalized(paramId, 0.75);

        auto* param = controller.getParameterObject(paramId);
        REQUIRE(param != nullptr);

        double readBack = param->getNormalized();
        REQUIRE(readBack == Approx(0.75).margin(1e-6));
    }

    SECTION("Set velocity step 15 to 0.0, read back matches") {
        auto paramId = static_cast<Steinberg::Vst::ParamID>(
            Ruinae::kArpVelocityLaneStep0Id + 15);
        controller.setParamNormalized(paramId, 0.0);

        auto* param = controller.getParameterObject(paramId);
        REQUIRE(param != nullptr);

        double readBack = param->getNormalized();
        REQUIRE(readBack == Approx(0.0).margin(1e-6));
    }

    SECTION("Set velocity step 31 to 1.0, read back matches") {
        auto paramId = static_cast<Steinberg::Vst::ParamID>(
            Ruinae::kArpVelocityLaneStep0Id + 31);
        controller.setParamNormalized(paramId, 1.0);

        auto* param = controller.getParameterObject(paramId);
        REQUIRE(param != nullptr);

        double readBack = param->getNormalized();
        REQUIRE(readBack == Approx(1.0).margin(1e-6));
    }

    SECTION("Set velocity lane length to 0.5 (midpoint), read back matches") {
        auto paramId = static_cast<Steinberg::Vst::ParamID>(
            Ruinae::kArpVelocityLaneLengthId);
        controller.setParamNormalized(paramId, 0.5);

        auto* param = controller.getParameterObject(paramId);
        REQUIRE(param != nullptr);

        double readBack = param->getNormalized();
        REQUIRE(readBack == Approx(0.5).margin(1e-6));
    }

    controller.terminate();
}

// ==============================================================================
// T083b: State Persistence Round-Trip (FR-035)
// ==============================================================================
// Verifies that velocity lane step values and lane length survive a full
// processor state save/restore cycle:
//   1. Set velocity steps 0-3 to 0.25, 0.5, 0.75, 1.0
//   2. Set velocity lane length to 8 (normalized = 7/31)
//   3. Save processor state
//   4. Create fresh processor + controller (defaults)
//   5. Restore state
//   6. Verify all values round-trip correctly
// Also verifies that collapse/expand UI state is NOT persisted.

TEST_CASE("VelocityLane_StatePersistence_RoundTrip", "[arp][integration][state][FR-035]") {
    // --- Step 1: Create processor and set non-default values ---
    auto proc1 = makeArpFlowProcessor();

    ArpFlowParamChanges changes;

    // Velocity steps 0-3 with distinct values
    changes.add(Ruinae::kArpVelocityLaneStep0Id, 0.25);
    changes.add(Ruinae::kArpVelocityLaneStep0Id + 1, 0.5);
    changes.add(Ruinae::kArpVelocityLaneStep0Id + 2, 0.75);
    changes.add(Ruinae::kArpVelocityLaneStep0Id + 3, 1.0);

    // Velocity lane length = 8 -> normalized = (8-1)/31 = 7/31
    double lengthNorm8 = 7.0 / 31.0;
    changes.add(Ruinae::kArpVelocityLaneLengthId, lengthNorm8);

    proc1->processParameterChanges(&changes);

    // --- Step 2: Save processor state ---
    Steinberg::MemoryStream stream;
    REQUIRE(proc1->getState(&stream) == Steinberg::kResultTrue);

    // --- Step 3: Create fresh processor (defaults) and restore state ---
    auto proc2 = makeArpFlowProcessor();
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(proc2->setState(&stream) == Steinberg::kResultTrue);

    // --- Step 4: Save state from restored processor ---
    Steinberg::MemoryStream stream2;
    REQUIRE(proc2->getState(&stream2) == Steinberg::kResultTrue);

    // --- Step 5: Sync controller from restored processor state ---
    Ruinae::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    stream2.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(controller.setComponentState(&stream2) == Steinberg::kResultTrue);

    // --- Step 6: Verify velocity step values round-trip ---
    SECTION("Velocity step 0 reads back 0.25") {
        double val = controller.getParamNormalized(Ruinae::kArpVelocityLaneStep0Id);
        CHECK(val == Approx(0.25).margin(1e-6));
    }

    SECTION("Velocity step 1 reads back 0.5") {
        double val = controller.getParamNormalized(
            static_cast<Steinberg::Vst::ParamID>(Ruinae::kArpVelocityLaneStep0Id + 1));
        CHECK(val == Approx(0.5).margin(1e-6));
    }

    SECTION("Velocity step 2 reads back 0.75") {
        double val = controller.getParamNormalized(
            static_cast<Steinberg::Vst::ParamID>(Ruinae::kArpVelocityLaneStep0Id + 2));
        CHECK(val == Approx(0.75).margin(1e-6));
    }

    SECTION("Velocity step 3 reads back 1.0") {
        double val = controller.getParamNormalized(
            static_cast<Steinberg::Vst::ParamID>(Ruinae::kArpVelocityLaneStep0Id + 3));
        CHECK(val == Approx(1.0).margin(1e-6));
    }

    SECTION("Velocity lane length reads back as 8 steps") {
        // RangeParameter 1-32, stepCount 31:
        // normalized = (8-1)/31 = 7/31 ~ 0.225806
        double val = controller.getParamNormalized(Ruinae::kArpVelocityLaneLengthId);
        CHECK(val == Approx(lengthNorm8).margin(1e-6));
    }

    SECTION("Collapsed state is NOT persisted (all lanes expand after restore)") {
        // The controller's getState/setState does not save any controller-specific
        // state (including UI collapse state). ArpLaneEditor::isCollapsed_ is a
        // transient UI property that defaults to false (expanded).
        // Verify controller getState/setState round-trip does not affect this.
        Steinberg::MemoryStream ctrlStream;
        REQUIRE(controller.getState(&ctrlStream) == Steinberg::kResultTrue);

        // Controller getState writes nothing meaningful -- setState should still
        // succeed and not change parameter state.
        Ruinae::Controller controller2;
        REQUIRE(controller2.initialize(nullptr) == Steinberg::kResultOk);
        ctrlStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
        controller2.setState(&ctrlStream);

        // Verify the collapse state is not a parameter -- there is no parameter ID
        // for collapse state, confirming it is ephemeral UI state that resets on
        // plugin reload (all lanes start expanded).
        // The ArpLaneEditor::isCollapsed_ defaults to false, verified by the
        // ArpLaneEditor unit tests in test_arp_lane_editor.cpp.
        // Here we verify the controller state round-trip does not corrupt
        // any velocity lane parameters:
        double step0 = controller2.getParamNormalized(Ruinae::kArpVelocityLaneStep0Id);
        // After controller setState (which is a no-op), params retain their
        // initialize() defaults (1.0 for velocity steps).
        CHECK(step0 == Approx(1.0).margin(1e-6));

        controller2.terminate();
    }

    proc1->terminate();
    proc2->terminate();
    controller.terminate();
}
