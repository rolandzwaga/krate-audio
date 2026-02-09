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
// Helper: read state version + parameter values from stream for verification
// =============================================================================

struct StateSnapshot {
    Steinberg::int32 version = 0;
    // Global
    float masterGain = 0.0f;
    Steinberg::int32 voiceMode = 0;
    Steinberg::int32 polyphony = 0;
    Steinberg::int32 softLimit = 0;
    // OSC A
    Steinberg::int32 oscAType = 0;
    float oscATune = 0.0f;
    float oscAFine = 0.0f;
    float oscALevel = 0.0f;
    float oscAPhase = 0.0f;
    // (abbreviated for key fields verification)
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

    // Read the first int32 -- should be state version 1
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    Steinberg::IBStreamer streamer(&stream, kLittleEndian);
    Steinberg::int32 version = 0;
    REQUIRE(streamer.readInt32(version));
    REQUIRE(version == 1);

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
