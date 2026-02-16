// ==============================================================================
// Unit Test: State Migration and Forward Compatibility
// ==============================================================================
// Verifies that unknown future versions and truncated streams are handled
// safely with fail-closed defaults.
//
// Reference: specs/045-plugin-shell/spec.md FR-017
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"

#include "public.sdk/source/common/memorystream.h"
#include "base/source/fstreamer.h"

#include <cstring>
#include <vector>

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
// Tests
// =============================================================================

TEST_CASE("Unknown future version loads with defaults", "[state][migration]") {
    // Create a stream with version 999 followed by garbage
    Steinberg::MemoryStream stream;
    Steinberg::IBStreamer streamer(&stream, kLittleEndian);
    streamer.writeInt32(999); // Unknown future version
    streamer.writeFloat(42.0f); // Some data that should be ignored
    streamer.writeFloat(99.0f);

    auto proc = makeProcessor();
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);

    // Should return kResultTrue (fail closed with safe defaults)
    auto result = proc->setState(&stream);
    REQUIRE(result == Steinberg::kResultTrue);

    // Verify the processor still works (save state and check it's valid)
    Steinberg::MemoryStream outStream;
    auto saveResult = proc->getState(&outStream);
    REQUIRE(saveResult == Steinberg::kResultTrue);

    // The saved state should have the current version
    outStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    Steinberg::IBStreamer outStreamer(&outStream, kLittleEndian);
    Steinberg::int32 savedVersion = 0;
    REQUIRE(outStreamer.readInt32(savedVersion));
    REQUIRE(savedVersion == Ruinae::kCurrentStateVersion);

    proc->terminate();
}

TEST_CASE("Empty stream loads with defaults", "[state][migration]") {
    Steinberg::MemoryStream emptyStream;

    auto proc = makeProcessor();

    // Should return kResultTrue (empty stream, keep defaults)
    auto result = proc->setState(&emptyStream);
    REQUIRE(result == Steinberg::kResultTrue);

    // Processor should still be functional
    Steinberg::MemoryStream outStream;
    proc->getState(&outStream);

    Steinberg::int64 size = 0;
    outStream.seek(0, Steinberg::IBStream::kIBSeekEnd, &size);
    REQUIRE(size > 4); // Should have version + defaults

    proc->terminate();
}

TEST_CASE("Truncated v1 stream loads partial defaults", "[state][migration]") {
    // Create a v1 stream that's truncated after just the global params
    Steinberg::MemoryStream stream;
    Steinberg::IBStreamer streamer(&stream, kLittleEndian);
    streamer.writeInt32(1); // version
    // Write only global params (4 values) then stop
    streamer.writeFloat(1.5f);  // masterGain
    streamer.writeInt32(0);     // voiceMode (Poly)
    streamer.writeInt32(4);     // polyphony
    streamer.writeInt32(1);     // softLimit (true)
    // Stream ends here -- rest of packs are missing

    auto proc = makeProcessor();
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);

    // Should return kResultTrue (truncated but handled gracefully)
    auto result = proc->setState(&stream);
    REQUIRE(result == Steinberg::kResultTrue);

    proc->terminate();
}

TEST_CASE("setState does not crash on any stream content", "[state][migration]") {
    auto proc = makeProcessor();

    // Random garbage data
    Steinberg::MemoryStream garbageStream;
    Steinberg::IBStreamer streamer(&garbageStream, kLittleEndian);
    streamer.writeInt32(1); // valid version
    // Write some random floats (less than full state)
    for (int i = 0; i < 5; ++i) {
        streamer.writeFloat(static_cast<float>(i) * 0.1f);
    }

    garbageStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    auto result = proc->setState(&garbageStream);
    REQUIRE(result == Steinberg::kResultTrue);

    proc->terminate();
}

// =============================================================================
// Helper: Expose processParameterChanges for setting params during test
// =============================================================================

class MigrationTestProcessor : public Ruinae::Processor {
public:
    using Ruinae::Processor::processParameterChanges;
};

static std::unique_ptr<MigrationTestProcessor> makeMigrationTestProcessor() {
    auto p = std::make_unique<MigrationTestProcessor>();
    p->initialize(nullptr);

    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    p->setupProcessing(setup);

    return p;
}

// Minimal IParamValueQueue for injecting a single parameter change
class MigrationSingleParamQueue : public Steinberg::Vst::IParamValueQueue {
public:
    MigrationSingleParamQueue(Steinberg::Vst::ParamID id, double value)
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

class MigrationParamBatch : public Steinberg::Vst::IParameterChanges {
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
    std::vector<MigrationSingleParamQueue> queues_;
};

// =============================================================================
// ModSource enum migration test (FR-009a)
// =============================================================================

TEST_CASE("ModSource enum migration from v12 to v13", "[state][migration]") {
    // Strategy:
    // 1. Save default v13 state to find byte offset of mod matrix slot 0 source
    // 2. Save state with slot 0 source set to a known value, find the offset
    // 3. Build a simulated v12 stream with old SampleHold (10) at that offset
    // 4. Load and verify migration changed it to 11

    // Step 1: Save default state (all sources = 0)
    auto proc1 = makeMigrationTestProcessor();
    Steinberg::MemoryStream defaultStream;
    REQUIRE(proc1->getState(&defaultStream) == Steinberg::kResultTrue);

    Steinberg::int64 defaultSize = 0;
    defaultStream.seek(0, Steinberg::IBStream::kIBSeekEnd, &defaultSize);

    // Step 2: Set slot 0 source to value 5 (Macro1 -- a distinctive value)
    // kModSourceCount = 14, so normalized = 5.0 / 13.0
    MigrationParamBatch changes;
    changes.add(Ruinae::kModMatrixSlot0SourceId, 5.0 / 13.0);
    proc1->processParameterChanges(&changes);

    Steinberg::MemoryStream modifiedStream;
    REQUIRE(proc1->getState(&modifiedStream) == Steinberg::kResultTrue);

    // Read both streams into vectors
    auto readStream = [](Steinberg::MemoryStream& s) -> std::vector<char> {
        Steinberg::int64 sz = 0;
        s.seek(0, Steinberg::IBStream::kIBSeekEnd, &sz);
        s.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
        std::vector<char> data(static_cast<size_t>(sz));
        Steinberg::int32 bytesRead = 0;
        s.read(data.data(), static_cast<Steinberg::int32>(sz), &bytesRead);
        return data;
    };

    auto defaultData = readStream(defaultStream);
    auto modifiedData = readStream(modifiedStream);

    REQUIRE(defaultData.size() == modifiedData.size());

    // Find the byte offset where they differ -- that's the source field
    size_t sourceOffset = 0;
    bool found = false;
    for (size_t i = 4; i + 3 < defaultData.size(); ++i) { // skip version
        if (defaultData[i] != modifiedData[i]) {
            sourceOffset = i;
            found = true;
            break;
        }
    }
    REQUIRE(found);

    // Verify the modified stream has value 5 at that offset
    Steinberg::int32 sourceVal = 0;
    std::memcpy(&sourceVal, &modifiedData[sourceOffset], sizeof(Steinberg::int32));
    REQUIRE(sourceVal == 5);

    // Step 3: Build a v12 stream with old SampleHold (10) at the source offset
    // Copy the default state (all sources = 0), patch version to 12,
    // set source to 10 (old SampleHold), truncate v13 tail (40 bytes)
    auto v12Data = defaultData;

    // Patch version from 13 to 12
    Steinberg::int32 v12 = 12;
    std::memcpy(v12Data.data(), &v12, sizeof(Steinberg::int32));

    // Patch slot 0 source from 0 to 10 (old SampleHold)
    Steinberg::int32 oldSampleHold = 10;
    std::memcpy(&v12Data[sourceOffset], &oldSampleHold, sizeof(Steinberg::int32));

    // Truncate v13 tail: saveMacroParams (4 floats = 16 bytes) +
    //                     saveRunglerParams (4 floats + 2 int32 = 24 bytes) = 40 bytes
    constexpr size_t kV13TailBytes = 40;
    REQUIRE(v12Data.size() > kV13TailBytes);
    v12Data.resize(v12Data.size() - kV13TailBytes);

    // Write the patched data to a MemoryStream
    Steinberg::MemoryStream v12Stream;
    Steinberg::int32 bytesWritten = 0;
    v12Stream.write(v12Data.data(), static_cast<Steinberg::int32>(v12Data.size()),
                    &bytesWritten);
    v12Stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);

    // Step 4: Load the v12 stream into a fresh processor
    auto proc2 = makeMigrationTestProcessor();
    REQUIRE(proc2->setState(&v12Stream) == Steinberg::kResultTrue);

    // Step 5: Save from proc2 and read back the source value
    Steinberg::MemoryStream migratedStream;
    REQUIRE(proc2->getState(&migratedStream) == Steinberg::kResultTrue);

    auto migratedData = readStream(migratedStream);

    // The source offset in v13 is the same as before (same stream format
    // before the v13 tail). Read the source value at that offset.
    Steinberg::int32 migratedSource = 0;
    std::memcpy(&migratedSource, &migratedData[sourceOffset],
                sizeof(Steinberg::int32));

    // Old SampleHold (10) should have been migrated to new SampleHold (11)
    REQUIRE(migratedSource == 11);

    proc1->terminate();
    proc2->terminate();
}

TEST_CASE("ModSource migration preserves values below threshold",
          "[state][migration]") {
    // Values 0-9 (None through Chaos) should NOT be modified by migration
    auto proc1 = makeMigrationTestProcessor();

    // Set slot 0 source to Chaos (value 9) -- should not be migrated
    MigrationParamBatch changes;
    changes.add(Ruinae::kModMatrixSlot0SourceId, 9.0 / 13.0);
    proc1->processParameterChanges(&changes);

    // Save v13 state
    Steinberg::MemoryStream v13Stream;
    REQUIRE(proc1->getState(&v13Stream) == Steinberg::kResultTrue);

    // Also save default to find source offset
    auto procDefault = makeMigrationTestProcessor();
    Steinberg::MemoryStream defaultStream;
    REQUIRE(procDefault->getState(&defaultStream) == Steinberg::kResultTrue);

    auto readStream = [](Steinberg::MemoryStream& s) -> std::vector<char> {
        Steinberg::int64 sz = 0;
        s.seek(0, Steinberg::IBStream::kIBSeekEnd, &sz);
        s.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
        std::vector<char> data(static_cast<size_t>(sz));
        Steinberg::int32 bytesRead = 0;
        s.read(data.data(), static_cast<Steinberg::int32>(sz), &bytesRead);
        return data;
    };

    auto defaultData = readStream(defaultStream);
    auto v13Data = readStream(v13Stream);

    // Find source offset by diffing
    size_t sourceOffset = 0;
    for (size_t i = 4; i + 3 < defaultData.size(); ++i) {
        if (defaultData[i] != v13Data[i]) {
            sourceOffset = i;
            break;
        }
    }
    REQUIRE(sourceOffset > 0);

    // Build v12 stream with source = 9 (Chaos, should not migrate)
    auto v12Data = defaultData;
    Steinberg::int32 v12 = 12;
    std::memcpy(v12Data.data(), &v12, sizeof(Steinberg::int32));

    Steinberg::int32 chaosVal = 9;
    std::memcpy(&v12Data[sourceOffset], &chaosVal, sizeof(Steinberg::int32));

    constexpr size_t kV13TailBytes = 40;
    v12Data.resize(v12Data.size() - kV13TailBytes);

    Steinberg::MemoryStream v12Stream;
    Steinberg::int32 bw = 0;
    v12Stream.write(v12Data.data(), static_cast<Steinberg::int32>(v12Data.size()), &bw);
    v12Stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);

    auto proc2 = makeMigrationTestProcessor();
    REQUIRE(proc2->setState(&v12Stream) == Steinberg::kResultTrue);

    Steinberg::MemoryStream migratedStream;
    REQUIRE(proc2->getState(&migratedStream) == Steinberg::kResultTrue);

    auto migratedData = readStream(migratedStream);

    Steinberg::int32 migratedSource = 0;
    std::memcpy(&migratedSource, &migratedData[sourceOffset],
                sizeof(Steinberg::int32));

    // Chaos (9) should remain unchanged -- no migration needed
    REQUIRE(migratedSource == 9);

    proc1->terminate();
    procDefault->terminate();
    proc2->terminate();
}
