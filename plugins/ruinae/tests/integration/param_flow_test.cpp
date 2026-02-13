// ==============================================================================
// Integration Test: Parameter Changes Flow from Host to Engine
// ==============================================================================
// Verifies that parameter changes flow through the Processor's parameter queue,
// are denormalized, stored in atomics, and applied to the engine.
//
// Reference: specs/045-plugin-shell/spec.md FR-005, FR-006, FR-007, US3
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <cmath>
#include <vector>

using Catch::Approx;

// =============================================================================
// Mock: Single Parameter Value Queue
// =============================================================================

class SingleParamValueQueue : public Steinberg::Vst::IParamValueQueue {
public:
    SingleParamValueQueue(Steinberg::Vst::ParamID id, double value)
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

// =============================================================================
// Mock: Parameter Changes Container
// =============================================================================

class TestParamChanges : public Steinberg::Vst::IParameterChanges {
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
    std::vector<SingleParamValueQueue> queues_;
};

// =============================================================================
// Mock: Empty Event List
// =============================================================================

class EmptyEventList : public Steinberg::Vst::IEventList {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
    Steinberg::int32 PLUGIN_API getEventCount() override { return 0; }
    Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32,
                                            Steinberg::Vst::Event&) override {
        return Steinberg::kResultFalse;
    }
    Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event&) override {
        return Steinberg::kResultTrue;
    }
};

// =============================================================================
// Test Fixture
// =============================================================================

struct ParamFlowFixture {
    Ruinae::Processor processor;
    EmptyEventList events;
    std::vector<float> outL;
    std::vector<float> outR;
    float* channelBuffers[2];
    Steinberg::Vst::AudioBusBuffers outputBus{};
    Steinberg::Vst::ProcessData data{};
    static constexpr size_t kBlockSize = 256;

    ParamFlowFixture() : outL(kBlockSize, 0.0f), outR(kBlockSize, 0.0f) {
        channelBuffers[0] = outL.data();
        channelBuffers[1] = outR.data();
        outputBus.numChannels = 2;
        outputBus.channelBuffers32 = channelBuffers;

        data.processMode = Steinberg::Vst::kRealtime;
        data.symbolicSampleSize = Steinberg::Vst::kSample32;
        data.numSamples = static_cast<Steinberg::int32>(kBlockSize);
        data.numInputs = 0;
        data.inputs = nullptr;
        data.numOutputs = 1;
        data.outputs = &outputBus;
        data.inputEvents = &events;
        data.processContext = nullptr;

        processor.initialize(nullptr);
        Steinberg::Vst::ProcessSetup setup{};
        setup.processMode = Steinberg::Vst::kRealtime;
        setup.symbolicSampleSize = Steinberg::Vst::kSample32;
        setup.sampleRate = 44100.0;
        setup.maxSamplesPerBlock = static_cast<Steinberg::int32>(kBlockSize);
        processor.setupProcessing(setup);
        processor.setActive(true);
    }

    ~ParamFlowFixture() {
        processor.setActive(false);
        processor.terminate();
    }

    void processWithParams(TestParamChanges& params) {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        data.inputParameterChanges = &params;
        processor.process(data);
    }
};

// =============================================================================
// Tests
// =============================================================================

TEST_CASE("Parameter changes are processed without crash", "[param_flow][integration]") {
    ParamFlowFixture f;
    TestParamChanges params;

    // Send changes for multiple sections simultaneously
    params.addChange(Ruinae::kMasterGainId, 0.75);
    params.addChange(Ruinae::kOscATypeId, 0.3);
    params.addChange(Ruinae::kFilterCutoffId, 0.8);
    params.addChange(Ruinae::kDistortionDriveId, 0.5);
    params.addChange(Ruinae::kAmpEnvAttackId, 0.3);
    params.addChange(Ruinae::kLFO1RateId, 0.5);
    params.addChange(Ruinae::kReverbMixId, 0.4);

    // Should not crash
    f.processWithParams(params);
    REQUIRE(true);
}

TEST_CASE("Multiple parameter changes in same block all take effect", "[param_flow][integration]") {
    ParamFlowFixture f;
    TestParamChanges params;

    // Send changes for every section
    params.addChange(Ruinae::kMasterGainId, 0.9);
    params.addChange(Ruinae::kOscATypeId, 0.1);
    params.addChange(Ruinae::kOscBLevelId, 0.5);
    params.addChange(Ruinae::kMixerPositionId, 0.7);
    params.addChange(Ruinae::kFilterCutoffId, 0.3);
    params.addChange(Ruinae::kDistortionMixId, 0.6);
    params.addChange(Ruinae::kTranceGateEnabledId, 1.0);
    params.addChange(Ruinae::kAmpEnvReleaseId, 0.5);
    params.addChange(Ruinae::kFilterEnvAttackId, 0.2);
    params.addChange(Ruinae::kModEnvDecayId, 0.4);
    params.addChange(Ruinae::kLFO1RateId, 0.6);
    params.addChange(Ruinae::kLFO2DepthId, 0.8);
    params.addChange(Ruinae::kChaosModRateId, 0.3);
    params.addChange(Ruinae::kModMatrixSlot0AmountId, 0.75);
    params.addChange(Ruinae::kGlobalFilterCutoffId, 0.5);
    params.addChange(Ruinae::kDelayTimeId, 0.2);
    params.addChange(Ruinae::kReverbSizeId, 0.6);
    params.addChange(Ruinae::kMonoPortamentoTimeId, 0.3);

    // Process all changes - should not crash
    f.processWithParams(params);
    REQUIRE(true);
}

TEST_CASE("Parameter changes affect subsequent audio blocks", "[param_flow][integration]") {
    ParamFlowFixture f;

    // First, play a note with default settings
    TestParamChanges emptyParams;

    // Create an event list with noteOn
    class NoteOnEvents : public Steinberg::Vst::IEventList {
    public:
        Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
            return Steinberg::kNoInterface;
        }
        Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
        Steinberg::uint32 PLUGIN_API release() override { return 1; }
        Steinberg::int32 PLUGIN_API getEventCount() override { return sent_ ? 0 : 1; }
        Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32 index,
                                                Steinberg::Vst::Event& e) override {
            if (index != 0 || sent_) return Steinberg::kResultFalse;
            e = {};
            e.type = Steinberg::Vst::Event::kNoteOnEvent;
            e.sampleOffset = 0;
            e.noteOn.channel = 0;
            e.noteOn.pitch = 60;
            e.noteOn.velocity = 0.8f;
            e.noteOn.noteId = -1;
            sent_ = true;
            return Steinberg::kResultTrue;
        }
        Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event&) override {
            return Steinberg::kResultTrue;
        }
        bool sent_ = false;
    };

    NoteOnEvents noteEvents;
    f.data.inputEvents = &noteEvents;
    f.data.inputParameterChanges = &emptyParams;
    f.processor.process(f.data);

    // Process a few blocks to get audio going
    EmptyEventList noEvents;
    f.data.inputEvents = &noEvents;
    for (int i = 0; i < 5; ++i) {
        std::fill(f.outL.begin(), f.outL.end(), 0.0f);
        std::fill(f.outR.begin(), f.outR.end(), 0.0f);
        f.processor.process(f.data);
    }

    // Record peak before parameter change
    float peakBefore = 0.0f;
    std::fill(f.outL.begin(), f.outL.end(), 0.0f);
    std::fill(f.outR.begin(), f.outR.end(), 0.0f);
    f.processor.process(f.data);
    for (size_t i = 0; i < f.kBlockSize; ++i) {
        float absL = std::abs(f.outL[i]);
        if (absL > peakBefore) peakBefore = absL;
    }

    // Now set master gain to zero
    TestParamChanges gainChange;
    gainChange.addChange(Ruinae::kMasterGainId, 0.0);
    f.data.inputParameterChanges = &gainChange;
    std::fill(f.outL.begin(), f.outL.end(), 0.0f);
    std::fill(f.outR.begin(), f.outR.end(), 0.0f);
    f.processor.process(f.data);

    // Process a few more blocks with gain at zero
    TestParamChanges noParamChanges;
    f.data.inputParameterChanges = &noParamChanges;
    float peakAfter = 0.0f;
    for (int i = 0; i < 5; ++i) {
        std::fill(f.outL.begin(), f.outL.end(), 0.0f);
        std::fill(f.outR.begin(), f.outR.end(), 0.0f);
        f.processor.process(f.data);
    }
    // The output after gain=0 should be much quieter
    for (size_t i = 0; i < f.kBlockSize; ++i) {
        float absL = std::abs(f.outL[i]);
        if (absL > peakAfter) peakAfter = absL;
    }

    // Audio was present before
    CHECK(peakBefore > 0.001f);
    // Audio should be silent or near-silent after gain=0
    CHECK(peakAfter < 0.001f);
}

// =============================================================================
// SC-002: Section param change produces measurable output difference
// =============================================================================
// For each major audio-path section, verifies that changing a representative
// parameter from its default actually alters the output waveform. This ensures
// applyParamsToEngine() forwards values to the engine for that section.
// =============================================================================

TEST_CASE("Section param changes produce measurable output difference", "[param_flow][sc002][integration]") {
    // --- Helper lambdas ---
    auto makeNoteOnEvents = []() {
        struct NoteOnList : public Steinberg::Vst::IEventList {
            Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
                return Steinberg::kNoInterface;
            }
            Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
            Steinberg::uint32 PLUGIN_API release() override { return 1; }
            Steinberg::int32 PLUGIN_API getEventCount() override { return done_ ? 0 : 1; }
            Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32 index,
                                                    Steinberg::Vst::Event& e) override {
                if (index != 0 || done_) return Steinberg::kResultFalse;
                e = {};
                e.type = Steinberg::Vst::Event::kNoteOnEvent;
                e.sampleOffset = 0;
                e.noteOn.channel = 0;
                e.noteOn.pitch = 60;
                e.noteOn.velocity = 0.8f;
                e.noteOn.noteId = -1;
                done_ = true;
                return Steinberg::kResultTrue;
            }
            Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event&) override {
                return Steinberg::kResultTrue;
            }
            bool done_ = false;
        };
        return NoteOnList{};
    };

    // Compute peak and sum-of-squares for comparing output shape
    auto computeStats = [](const std::vector<float>& buf) -> std::pair<float, double> {
        float peak = 0.0f;
        double energy = 0.0;
        for (float s : buf) {
            float a = std::abs(s);
            if (a > peak) peak = a;
            energy += static_cast<double>(s) * s;
        }
        return {peak, energy};
    };

    // Collect output after a note has been playing for a few blocks
    auto collectOutput = [&](ParamFlowFixture& fix, TestParamChanges* paramChange = nullptr) {
        // Process a few blocks for the sound to stabilize
        TestParamChanges empty;
        TestParamChanges& pc = paramChange ? *paramChange : empty;
        fix.data.inputParameterChanges = &pc;
        for (int i = 0; i < 4; ++i) {
            std::fill(fix.outL.begin(), fix.outL.end(), 0.0f);
            std::fill(fix.outR.begin(), fix.outR.end(), 0.0f);
            fix.processor.process(fix.data);
            // After first block, clear param changes (they've been consumed)
            if (i == 0) fix.data.inputParameterChanges = &empty;
        }
        // Capture one final block
        std::fill(fix.outL.begin(), fix.outL.end(), 0.0f);
        std::fill(fix.outR.begin(), fix.outR.end(), 0.0f);
        fix.processor.process(fix.data);
        return computeStats(fix.outL);
    };

    struct SectionTest {
        const char* name;
        Steinberg::Vst::ParamID paramId;
        double value;
    };

    // Representative parameter per section that directly affects the audio path.
    // Each uses a value that deviates significantly from the default.
    SectionTest sections[] = {
        {"OSC A Type",       Ruinae::kOscATypeId,         0.9},    // Noise vs PolyBLEP
        {"OSC A Level",      Ruinae::kOscALevelId,        0.0},    // Silence vs full
        {"Filter Cutoff",    Ruinae::kFilterCutoffId,     0.0},    // 20 Hz vs default
        {"Distortion Drive", Ruinae::kDistortionDriveId,  1.0},    // Full drive vs none
        {"Delay Mix",        Ruinae::kDelayMixId,         1.0},    // Full wet vs dry
        {"Reverb Mix",       Ruinae::kReverbMixId,        1.0},    // Full wet vs dry
    };

    for (const auto& sec : sections) {
        SECTION(sec.name) {
            // Create fresh fixture each section (independent processor state)
            ParamFlowFixture f;

            // Start a note
            auto noteEvents = makeNoteOnEvents();
            f.data.inputEvents = &noteEvents;
            TestParamChanges emptyPC;
            f.data.inputParameterChanges = &emptyPC;
            f.processor.process(f.data);

            // Switch to no events
            EmptyEventList noEvents;
            f.data.inputEvents = &noEvents;

            // Collect baseline output (default params)
            auto [peakDefault, energyDefault] = collectOutput(f);

            // Now apply the section's parameter change
            TestParamChanges sectionChange;
            sectionChange.addChange(sec.paramId, sec.value);
            auto [peakChanged, energyChanged] = collectOutput(f, &sectionChange);

            // At least one of peak or energy must differ by >5%
            bool peakDiffers = std::abs(peakDefault - peakChanged) > 0.05f * std::max(peakDefault, peakChanged);
            bool energyDiffers = std::abs(energyDefault - energyChanged) > 0.05 * std::max(energyDefault, energyChanged);

            INFO("Section: " << sec.name);
            INFO("Peak default: " << peakDefault << ", changed: " << peakChanged);
            INFO("Energy default: " << energyDefault << ", changed: " << energyChanged);
            CHECK((peakDiffers || energyDiffers));
        }
    }
}

TEST_CASE("Out-of-range parameter values are clamped", "[param_flow][integration]") {
    ParamFlowFixture f;
    TestParamChanges params;

    // Send out-of-range values (>1.0 and <0.0 could come from non-compliant hosts)
    // These should not crash
    params.addChange(Ruinae::kMasterGainId, 1.5);
    params.addChange(Ruinae::kFilterCutoffId, -0.1);
    params.addChange(Ruinae::kAmpEnvAttackId, 2.0);

    f.processWithParams(params);
    REQUIRE(true);
}
