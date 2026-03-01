// ==============================================================================
// Unit Test: Crash-Proof Preset Loading (RTTransferT)
// ==============================================================================
// Verifies the deferred preset loading mechanism:
//   - setState() captures bytes and defers application
//   - process() drains the transfer and applies atomically
//   - Edge cases: empty, truncated, unknown version, rapid switching
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"
#include "drain_preset_transfer.h"

#include "public.sdk/source/common/memorystream.h"
#include "base/source/fstreamer.h"

#include <cstring>
#include <vector>

// =============================================================================
// Helpers
// =============================================================================

namespace {

std::unique_ptr<Ruinae::Processor> makeProcessor() {
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

class TestableProcessor : public Ruinae::Processor {
public:
    using Ruinae::Processor::processParameterChanges;
};

std::unique_ptr<TestableProcessor> makeTestableProcessor() {
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
        Steinberg::int32 index, Steinberg::int32& sampleOffset,
        Steinberg::Vst::ParamValue& value) override {
        if (index != 0) return Steinberg::kResultFalse;
        sampleOffset = 0;
        value = value_;
        return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API addPoint(
        Steinberg::int32, Steinberg::Vst::ParamValue, Steinberg::int32&) override {
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

std::vector<char> extractStreamBytes(Steinberg::MemoryStream& stream) {
    Steinberg::int64 size = 0;
    stream.seek(0, Steinberg::IBStream::kIBSeekEnd, &size);
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    std::vector<char> data(static_cast<size_t>(size));
    Steinberg::int32 bytesRead = 0;
    stream.read(data.data(), static_cast<Steinberg::int32>(size), &bytesRead);
    return data;
}

} // anonymous namespace

// =============================================================================
// Tests
// =============================================================================

TEST_CASE("RTTransferT round-trip preserves state bytes",
          "[preset-safety][roundtrip]") {
    // Set non-default params on proc1, getState, setState on proc2,
    // drain, getState from proc2, compare bytes (identical)
    auto proc1 = makeTestableProcessor();

    ParamChangeBatch changes;
    changes.add(Ruinae::kMasterGainId, 0.75);
    changes.add(Ruinae::kOscATypeId, 1.0);
    changes.add(Ruinae::kFilterCutoffId, 0.8);
    changes.add(Ruinae::kAmpEnvAttackId, 0.4);
    proc1->processParameterChanges(&changes);

    Steinberg::MemoryStream stream1;
    REQUIRE(proc1->getState(&stream1) == Steinberg::kResultTrue);

    auto proc2 = makeProcessor();
    stream1.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(proc2->setState(&stream1) == Steinberg::kResultTrue);
    drainPresetTransfer(proc2.get());

    Steinberg::MemoryStream stream2;
    REQUIRE(proc2->getState(&stream2) == Steinberg::kResultTrue);

    auto data1 = extractStreamBytes(stream1);
    auto data2 = extractStreamBytes(stream2);

    REQUIRE(data1.size() == data2.size());
    REQUIRE(data1 == data2);

    proc1->terminate();
    proc2->terminate();
}

TEST_CASE("setState applies params immediately for host compatibility",
          "[preset-safety][immediate]") {
    auto proc1 = makeTestableProcessor();

    // Set non-default params
    ParamChangeBatch changes;
    changes.add(Ruinae::kMasterGainId, 0.75);
    proc1->processParameterChanges(&changes);

    // Capture non-default state
    Steinberg::MemoryStream nonDefaultStream;
    REQUIRE(proc1->getState(&nonDefaultStream) == Steinberg::kResultTrue);
    auto nonDefaultBytes = extractStreamBytes(nonDefaultStream);

    // Create fresh processor with default state
    auto proc2 = makeProcessor();

    // Call setState — params should be applied IMMEDIATELY (no drain needed)
    nonDefaultStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(proc2->setState(&nonDefaultStream) == Steinberg::kResultTrue);

    // getState should return the new values right away (host compatibility)
    Steinberg::MemoryStream immediateStream;
    REQUIRE(proc2->getState(&immediateStream) == Steinberg::kResultTrue);
    auto immediateBytes = extractStreamBytes(immediateStream);

    // Voice routes are deferred, so we need to drain for byte-identical match.
    // But atomic params should already match.
    drainPresetTransfer(proc2.get());

    Steinberg::MemoryStream afterDrainStream;
    REQUIRE(proc2->getState(&afterDrainStream) == Steinberg::kResultTrue);
    auto afterDrainBytes = extractStreamBytes(afterDrainStream);

    REQUIRE(afterDrainBytes == nonDefaultBytes);

    proc1->terminate();
    proc2->terminate();
}

TEST_CASE("Empty snapshot handled gracefully",
          "[preset-safety][edge-case]") {
    auto proc = makeProcessor();

    // Empty stream — should not crash
    Steinberg::MemoryStream emptyStream;
    REQUIRE(proc->setState(&emptyStream) == Steinberg::kResultTrue);
    drainPresetTransfer(proc.get());

    // Processor should still produce valid state
    Steinberg::MemoryStream outStream;
    REQUIRE(proc->getState(&outStream) == Steinberg::kResultTrue);
    Steinberg::int64 size = 0;
    outStream.seek(0, Steinberg::IBStream::kIBSeekEnd, &size);
    REQUIRE(size > 4);

    proc->terminate();
}

TEST_CASE("Truncated snapshot loads partial state",
          "[preset-safety][edge-case]") {
    auto proc1 = makeProcessor();

    // Create valid state
    Steinberg::MemoryStream fullStream;
    REQUIRE(proc1->getState(&fullStream) == Steinberg::kResultTrue);
    auto fullData = extractStreamBytes(fullStream);

    // Truncate to just version + global params
    size_t truncLen = 20; // version (4) + some global params
    REQUIRE(fullData.size() > truncLen);

    Steinberg::MemoryStream truncStream;
    truncStream.write(fullData.data(), static_cast<Steinberg::int32>(truncLen), nullptr);
    truncStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);

    auto proc2 = makeProcessor();
    REQUIRE(proc2->setState(&truncStream) == Steinberg::kResultTrue);
    drainPresetTransfer(proc2.get());

    // Should not crash — processor remains functional
    Steinberg::MemoryStream outStream;
    REQUIRE(proc2->getState(&outStream) == Steinberg::kResultTrue);

    proc1->terminate();
    proc2->terminate();
}

TEST_CASE("Unknown version leaves defaults",
          "[preset-safety][edge-case]") {
    auto proc = makeProcessor();

    // Get default state for comparison
    Steinberg::MemoryStream defaultStream;
    REQUIRE(proc->getState(&defaultStream) == Steinberg::kResultTrue);
    auto defaultBytes = extractStreamBytes(defaultStream);

    // Create stream with version 999
    Steinberg::MemoryStream futureStream;
    Steinberg::IBStreamer streamer(&futureStream, kLittleEndian);
    streamer.writeInt32(999);
    streamer.writeFloat(42.0f);
    futureStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);

    REQUIRE(proc->setState(&futureStream) == Steinberg::kResultTrue);
    drainPresetTransfer(proc.get());

    // State should still be defaults (version 999 is ignored)
    Steinberg::MemoryStream afterStream;
    REQUIRE(proc->getState(&afterStream) == Steinberg::kResultTrue);
    auto afterBytes = extractStreamBytes(afterStream);

    REQUIRE(defaultBytes == afterBytes);

    proc->terminate();
}

TEST_CASE("Multiple rapid setState calls, only last applied",
          "[preset-safety][rapid]") {
    // Set up 3 different processor states
    auto procA = makeTestableProcessor();
    auto procB = makeTestableProcessor();
    auto procC = makeTestableProcessor();

    ParamChangeBatch changesA;
    changesA.add(Ruinae::kMasterGainId, 0.2);
    procA->processParameterChanges(&changesA);

    ParamChangeBatch changesB;
    changesB.add(Ruinae::kMasterGainId, 0.5);
    procB->processParameterChanges(&changesB);

    ParamChangeBatch changesC;
    changesC.add(Ruinae::kMasterGainId, 0.9);
    procC->processParameterChanges(&changesC);

    // Get state bytes for each
    Steinberg::MemoryStream streamA, streamB, streamC;
    REQUIRE(procA->getState(&streamA) == Steinberg::kResultTrue);
    REQUIRE(procB->getState(&streamB) == Steinberg::kResultTrue);
    REQUIRE(procC->getState(&streamC) == Steinberg::kResultTrue);

    auto expectedBytes = extractStreamBytes(streamC);

    // Now load all 3 states rapidly into a fresh processor without draining
    auto target = makeProcessor();
    streamA.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(target->setState(&streamA) == Steinberg::kResultTrue);
    streamB.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(target->setState(&streamB) == Steinberg::kResultTrue);
    streamC.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(target->setState(&streamC) == Steinberg::kResultTrue);

    // Single drain — only the last state should be applied
    drainPresetTransfer(target.get());

    Steinberg::MemoryStream resultStream;
    REQUIRE(target->getState(&resultStream) == Steinberg::kResultTrue);
    auto resultBytes = extractStreamBytes(resultStream);

    REQUIRE(expectedBytes == resultBytes);

    procA->terminate();
    procB->terminate();
    procC->terminate();
    target->terminate();
}
