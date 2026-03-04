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
#include "drain_preset_transfer.h"

#include "public.sdk/source/common/memorystream.h"
#include "base/source/fstreamer.h"

#include <cstring>
#include <cstdint>
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
    drainPresetTransfer(proc.get());

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
    drainPresetTransfer(proc.get());

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
    drainPresetTransfer(proc.get());

    proc->terminate();
}

TEST_CASE("Version 1 state loads without midiOut field", "[state][migration][version]") {
    // Save a default-state v2 stream from a fresh processor
    auto proc = makeProcessor();
    Steinberg::MemoryStream v2Stream;
    REQUIRE(proc->getState(&v2Stream) == Steinberg::kResultTrue);

    // Read the full v2 data
    Steinberg::int64 v2Size = 0;
    v2Stream.seek(0, Steinberg::IBStream::kIBSeekEnd, &v2Size);
    std::vector<uint8_t> v2Data(static_cast<size_t>(v2Size));
    v2Stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    Steinberg::int32 bytesRead = 0;
    v2Stream.read(v2Data.data(), static_cast<Steinberg::int32>(v2Size), &bytesRead);

    // Patch version from 2 to 1 (first 4 bytes, little-endian int32)
    Steinberg::int32 v1 = 1;
    std::memcpy(v2Data.data(), &v1, sizeof(v1));

    // Truncate last 4 bytes (the midiOut int32 that v1 wouldn't have)
    auto v1Size = v2Data.size() - 4;
    Steinberg::MemoryStream v1Stream;
    Steinberg::int32 bytesWritten = 0;
    v1Stream.write(v2Data.data(), static_cast<Steinberg::int32>(v1Size), &bytesWritten);
    v1Stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);

    // Load the v1 stream into a fresh processor
    auto proc2 = makeProcessor();
    auto result = proc2->setState(&v1Stream);
    REQUIRE(result == Steinberg::kResultTrue);
    drainPresetTransfer(proc2.get());

    // Verify the processor still works by saving and checking version
    Steinberg::MemoryStream outStream;
    REQUIRE(proc2->getState(&outStream) == Steinberg::kResultTrue);

    outStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    Steinberg::IBStreamer outStreamer(&outStream, kLittleEndian);
    Steinberg::int32 savedVersion = 0;
    REQUIRE(outStreamer.readInt32(savedVersion));
    REQUIRE(savedVersion == Ruinae::kCurrentStateVersion);

    proc->terminate();
    proc2->terminate();
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
    drainPresetTransfer(proc.get());

    proc->terminate();
}
