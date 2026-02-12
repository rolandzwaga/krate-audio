// ==============================================================================
// Unit Test: State Round-Trip Persistence
// ==============================================================================
// Verifies that getState() followed by setState() on a new Processor
// preserves all parameter values within acceptable precision.
//
// Reference: specs/045-plugin-shell/spec.md FR-015, FR-016
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"

#include "public.sdk/source/common/memorystream.h"
#include "base/source/fstreamer.h"

#include <vector>

using Catch::Approx;

// =============================================================================
// Helper: create and initialize a Processor
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

// =============================================================================
// Helpers for parameter injection
// =============================================================================

// Expose processParameterChanges for testing
class TestableProcessor : public Ruinae::Processor {
public:
    using Ruinae::Processor::processParameterChanges;
};

static std::unique_ptr<TestableProcessor> makeTestableProcessor() {
    auto p = std::make_unique<TestableProcessor>();
    p->initialize(nullptr);

    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    p->setupProcessing(setup);

    return p;
}

class SingleParamQueue : public Steinberg::Vst::IParamValueQueue {
public:
    SingleParamQueue(Steinberg::Vst::ParamID id, double value)
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

class ParamChangeBatch : public Steinberg::Vst::IParameterChanges {
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
    std::vector<SingleParamQueue> queues_;
};

// =============================================================================
// Tests
// =============================================================================

TEST_CASE("State round-trip preserves default values", "[state][roundtrip]") {
    auto proc1 = makeProcessor();

    // Save state from default processor
    Steinberg::MemoryStream stream;
    auto saveResult = proc1->getState(&stream);
    REQUIRE(saveResult == Steinberg::kResultTrue);

    // Load into a fresh processor
    auto proc2 = makeProcessor();
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    auto loadResult = proc2->setState(&stream);
    REQUIRE(loadResult == Steinberg::kResultTrue);

    // Save again from proc2
    Steinberg::MemoryStream stream2;
    proc2->getState(&stream2);

    // Both streams should be identical (byte-for-byte)
    Steinberg::int64 size1 = 0;
    Steinberg::int64 size2 = 0;
    stream.seek(0, Steinberg::IBStream::kIBSeekEnd, &size1);
    stream2.seek(0, Steinberg::IBStream::kIBSeekEnd, &size2);
    REQUIRE(size1 == size2);
    REQUIRE(size1 > 4); // At least version + some data

    proc1->terminate();
    proc2->terminate();
}

TEST_CASE("State version is written first", "[state][roundtrip]") {
    auto proc = makeProcessor();

    Steinberg::MemoryStream stream;
    proc->getState(&stream);

    // Read the first int32 -- should be current state version
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    Steinberg::IBStreamer streamer(&stream, kLittleEndian);
    Steinberg::int32 version = 0;
    REQUIRE(streamer.readInt32(version));
    REQUIRE(version == Ruinae::kCurrentStateVersion);

    proc->terminate();
}

TEST_CASE("State round-trip byte equivalence", "[state][roundtrip]") {
    // Verify that save -> load -> save produces identical bytes
    auto proc1 = makeProcessor();

    // First save
    Steinberg::MemoryStream stream1;
    proc1->getState(&stream1);

    // Load into proc2
    auto proc2 = makeProcessor();
    stream1.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    proc2->setState(&stream1);

    // Second save from proc2
    Steinberg::MemoryStream stream2;
    proc2->getState(&stream2);

    // Compare byte-by-byte
    Steinberg::int64 size1 = 0;
    Steinberg::int64 size2 = 0;
    stream1.seek(0, Steinberg::IBStream::kIBSeekEnd, &size1);
    stream2.seek(0, Steinberg::IBStream::kIBSeekEnd, &size2);
    REQUIRE(size1 == size2);

    // Read all bytes
    stream1.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    stream2.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);

    std::vector<char> data1(static_cast<size_t>(size1));
    std::vector<char> data2(static_cast<size_t>(size2));

    Steinberg::int32 bytesRead1 = 0;
    Steinberg::int32 bytesRead2 = 0;
    stream1.read(data1.data(), static_cast<Steinberg::int32>(size1), &bytesRead1);
    stream2.read(data2.data(), static_cast<Steinberg::int32>(size2), &bytesRead2);

    REQUIRE(bytesRead1 == bytesRead2);
    REQUIRE(data1 == data2);

    proc1->terminate();
    proc2->terminate();
}

TEST_CASE("State round-trip preserves non-default values", "[state][roundtrip]") {
    // This test catches the class of bug where getState writes version N
    // but setState has no handler for it -- all params silently revert to
    // defaults. With non-default values, the re-saved stream differs.
    auto proc1 = makeTestableProcessor();

    // Set non-default values across multiple parameter packs.
    // Normalized values at the VST boundary (0.0 to 1.0).
    ParamChangeBatch changes;
    // Global: master gain
    changes.add(Ruinae::kMasterGainId, 0.75);
    // OSC A: type = Noise (index 9, normalized = 9/9 = 1.0)
    changes.add(Ruinae::kOscATypeId, 1.0);
    // OSC A: level
    changes.add(Ruinae::kOscALevelId, 0.6);
    // OSC B: type = Additive (index 3, normalized = 3/9)
    changes.add(Ruinae::kOscBTypeId, 3.0 / 9.0);
    // Mixer: position full B
    changes.add(Ruinae::kMixerPositionId, 0.9);
    // Mixer: shift
    changes.add(Ruinae::kMixerShiftId, 0.7);
    // Filter: cutoff
    changes.add(Ruinae::kFilterCutoffId, 0.8);
    // Amp envelope: attack
    changes.add(Ruinae::kAmpEnvAttackId, 0.4);
    proc1->processParameterChanges(&changes);

    // Save state
    Steinberg::MemoryStream stream1;
    REQUIRE(proc1->getState(&stream1) == Steinberg::kResultTrue);

    // Load into a fresh processor
    auto proc2 = makeTestableProcessor();
    stream1.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(proc2->setState(&stream1) == Steinberg::kResultTrue);

    // Save again from proc2
    Steinberg::MemoryStream stream2;
    REQUIRE(proc2->getState(&stream2) == Steinberg::kResultTrue);

    // Byte-for-byte equivalence: if setState didn't restore the non-default
    // values, proc2 would save defaults and the streams would differ.
    Steinberg::int64 size1 = 0;
    Steinberg::int64 size2 = 0;
    stream1.seek(0, Steinberg::IBStream::kIBSeekEnd, &size1);
    stream2.seek(0, Steinberg::IBStream::kIBSeekEnd, &size2);
    REQUIRE(size1 == size2);

    stream1.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    stream2.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);

    std::vector<char> data1(static_cast<size_t>(size1));
    std::vector<char> data2(static_cast<size_t>(size2));

    Steinberg::int32 bytesRead1 = 0;
    Steinberg::int32 bytesRead2 = 0;
    stream1.read(data1.data(), static_cast<Steinberg::int32>(size1), &bytesRead1);
    stream2.read(data2.data(), static_cast<Steinberg::int32>(size2), &bytesRead2);

    REQUIRE(bytesRead1 == bytesRead2);
    REQUIRE(data1 == data2);

    proc1->terminate();
    proc2->terminate();
}
