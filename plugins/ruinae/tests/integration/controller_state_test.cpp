// ==============================================================================
// Integration Test: Controller State Synchronization
// ==============================================================================
// Verifies that Controller::setComponentState() synchronizes all parameters
// to match the Processor state stream.
//
// Reference: specs/045-plugin-shell/spec.md FR-012, US4
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "controller/controller.h"
#include "plugin_ids.h"

#include "public.sdk/source/common/memorystream.h"
#include "base/source/fstreamer.h"

#include <cmath>

using Catch::Approx;

// =============================================================================
// Helpers
// =============================================================================

static std::unique_ptr<Ruinae::Processor> makeProcessor() {
    auto p = std::make_unique<Ruinae::Processor>();
    p->initialize(nullptr);

    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    p->setupProcessing(setup);

    return p;
}

static Ruinae::Controller* makeControllerRaw() {
    auto* ctrl = new Ruinae::Controller();
    ctrl->initialize(nullptr);
    return ctrl;
}

// Subclass to expose protected processParameterChanges for testing
class TestableProcessor : public Ruinae::Processor {
public:
    using Ruinae::Processor::processParameterChanges;
};

// Mock single-value parameter queue for feeding parameter changes
class StateTestParamQueue : public Steinberg::Vst::IParamValueQueue {
public:
    StateTestParamQueue(Steinberg::Vst::ParamID id, double value)
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
class StateTestParamChanges : public Steinberg::Vst::IParameterChanges {
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

    void addChange(Steinberg::Vst::ParamID id, double value) {
        queues_.emplace_back(id, value);
    }

private:
    std::vector<StateTestParamQueue> queues_;
};

// =============================================================================
// Tests
// =============================================================================

TEST_CASE("Controller syncs default state from Processor", "[controller_state][integration]") {
    auto proc = makeProcessor();
    auto* ctrl = makeControllerRaw();

    // Save processor state
    Steinberg::MemoryStream stream;
    REQUIRE(proc->getState(&stream) == Steinberg::kResultTrue);

    // Sync controller
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(ctrl->setComponentState(&stream) == Steinberg::kResultTrue);

    // Verify key default parameters are synced
    // Master Gain default = 1.0 -> normalized 0.5
    CHECK(ctrl->getParamNormalized(Ruinae::kMasterGainId) == Approx(0.5).margin(0.01));

    // Polyphony default = 8 -> normalized (8-1)/15 = 7/15
    CHECK(ctrl->getParamNormalized(Ruinae::kPolyphonyId) == Approx(7.0 / 15.0).margin(0.01));

    // Soft Limit default = true -> 1.0
    CHECK(ctrl->getParamNormalized(Ruinae::kSoftLimitId) == Approx(1.0).margin(0.01));

    // OSC A Level default = 1.0
    CHECK(ctrl->getParamNormalized(Ruinae::kOscALevelId) == Approx(1.0).margin(0.01));

    // Amp Sustain default = 0.8
    CHECK(ctrl->getParamNormalized(Ruinae::kAmpEnvSustainId) == Approx(0.8).margin(0.01));

    proc->terminate();
    ctrl->terminate();
}

TEST_CASE("Controller syncs non-default state from Processor", "[controller_state][integration]") {
    // Create a processor with test-accessible parameter changes
    auto proc = std::make_unique<TestableProcessor>();
    proc->initialize(nullptr);

    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    proc->setupProcessing(setup);

    // Set non-default parameter values through the VST3 parameter change path.
    // All values are NORMALIZED (0.0 to 1.0) at the VST boundary.
    StateTestParamChanges paramChanges;

    // Master Gain 1.5 -> normalized 1.5/2.0 = 0.75
    paramChanges.addChange(Ruinae::kMasterGainId, 0.75);

    // Voice Mode = 1 (Mono) -> normalized 1.0
    paramChanges.addChange(Ruinae::kVoiceModeId, 1.0);

    // Polyphony = 4 -> normalized (4-1)/15 = 0.2
    paramChanges.addChange(Ruinae::kPolyphonyId, 3.0 / 15.0);

    // OSC A Level = 0.7
    paramChanges.addChange(Ruinae::kOscALevelId, 0.7);

    // Filter cutoff 1000.0 Hz -> normalized = log(1000/20)/log(1000)
    double cutoffNorm = std::log(1000.0 / 20.0) / std::log(1000.0);
    paramChanges.addChange(Ruinae::kFilterCutoffId, cutoffNorm);

    // Apply changes through the processor's parameter handling
    proc->processParameterChanges(&paramChanges);

    // Save the processor's state (in current format, always correct)
    Steinberg::MemoryStream stream;
    REQUIRE(proc->getState(&stream) == Steinberg::kResultTrue);

    // Sync controller with this state
    auto* ctrl = makeControllerRaw();
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(ctrl->setComponentState(&stream) == Steinberg::kResultTrue);

    // Verify the non-default values are synced
    CHECK(ctrl->getParamNormalized(Ruinae::kMasterGainId) == Approx(0.75).margin(0.01));
    CHECK(ctrl->getParamNormalized(Ruinae::kVoiceModeId) == Approx(1.0).margin(0.01));
    CHECK(ctrl->getParamNormalized(Ruinae::kPolyphonyId) == Approx(3.0 / 15.0).margin(0.01));
    CHECK(ctrl->getParamNormalized(Ruinae::kOscALevelId) == Approx(0.7).margin(0.01));
    CHECK(ctrl->getParamNormalized(Ruinae::kFilterCutoffId) == Approx(cutoffNorm).margin(0.02));

    proc->terminate();
    ctrl->terminate();
}

TEST_CASE("Controller handles empty stream in setComponentState", "[controller_state][integration]") {
    auto* ctrl = makeControllerRaw();
    Steinberg::MemoryStream emptyStream;

    // Should not crash and return kResultTrue
    auto result = ctrl->setComponentState(&emptyStream);
    CHECK(result == Steinberg::kResultTrue);

    ctrl->terminate();
}

TEST_CASE("Controller handles null stream in setComponentState", "[controller_state][integration]") {
    auto* ctrl = makeControllerRaw();

    // Null stream should return kResultFalse
    auto result = ctrl->setComponentState(nullptr);
    CHECK(result == Steinberg::kResultFalse);

    ctrl->terminate();
}

TEST_CASE("Controller round-trip: Processor save -> Controller load", "[controller_state][integration]") {
    auto proc = makeProcessor();
    auto* ctrl = makeControllerRaw();

    // Save default processor state
    Steinberg::MemoryStream stream;
    REQUIRE(proc->getState(&stream) == Steinberg::kResultTrue);

    // Load into controller
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(ctrl->setComponentState(&stream) == Steinberg::kResultTrue);

    // All parameters should have valid normalized values (0.0-1.0)
    Steinberg::int32 paramCount = ctrl->getParameterCount();
    for (Steinberg::int32 i = 0; i < paramCount; ++i) {
        Steinberg::Vst::ParameterInfo info{};
        ctrl->getParameterInfo(i, info);
        double norm = ctrl->getParamNormalized(info.id);
        INFO("Parameter " << info.id << " has normalized value " << norm);
        CHECK(norm >= 0.0);
        CHECK(norm <= 1.0);
    }

    proc->terminate();
    ctrl->terminate();
}
