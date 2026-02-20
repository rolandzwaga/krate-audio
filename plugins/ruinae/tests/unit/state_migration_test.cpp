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
