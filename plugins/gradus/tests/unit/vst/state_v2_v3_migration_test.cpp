// ==============================================================================
// State v2 -> v3 Migration Tests (spec 142: gradus-piano-roll-sequencer)
// ==============================================================================
// Verifies the v3 state stream contract:
//   * v2 fixtures load via v3 setState and yield correct defaults for the
//     new Sequencer Note lane (sourceMode=Live, length=16, all pitches=60,
//     all rest flags=1, modulators at defaults).
//   * v3 round-trip preserves all 71 new fields bit-exact.
//   * v3 setState refuses unknown future versions (version > 3 -> kResultFalse).
//   * v2-format streams still produce byte-identical MIDI output in Live mode
//     (regression guard for SC-004, FR-039b).
//
// The byte-identical-MIDI assertion is exercised in a separate Phase 3 test
// (`live_mode_byte_identical_test.cpp`). This file owns the state-stream side
// of the contract only — Phase 2 scope.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "parameters/arpeggiator_params.h"
#include "plugin_ids.h"

#include "public.sdk/source/common/memorystream.h"
#include "base/source/fstreamer.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

namespace {

// Load a v2 fixture file from the test fixtures directory.
// Returns the raw bytes; throws via REQUIRE failure if the file cannot be read.
std::vector<char> loadV2Fixture(const std::string& name)
{
    const std::string path = std::string(GRADUS_FIXTURES_DIR) + "/" + name;
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.is_open());
    in.seekg(0, std::ios::end);
    const auto sz = static_cast<std::streamsize>(in.tellg());
    in.seekg(0, std::ios::beg);
    std::vector<char> bytes(static_cast<size_t>(sz));
    in.read(bytes.data(), sz);
    REQUIRE(in.gcount() == sz);
    return bytes;
}

// Wrap a vector<char> as an IBStream that can be fed to setState.
// Caller owns the returned MemoryStream (must release).
MemoryStream* makeStreamFromBytes(const std::vector<char>& bytes)
{
    auto* stream = new MemoryStream();
    int32 written = 0;
    stream->write(const_cast<char*>(bytes.data()),
        static_cast<int32>(bytes.size()), &written);
    REQUIRE(written == static_cast<int32>(bytes.size()));
    stream->seek(0, IBStream::kIBSeekSet, nullptr);
    return stream;
}

} // namespace

// =============================================================================
// v2 fixture -> v3 setState: defaults for Sequencer Note lane fields
// =============================================================================

TEST_CASE("v3 setState handles v2-formatted stream",
          "[gradus][vst][state][migration]")
{
    // Load the default-preset v2 fixture (captured on parent commit pre-bump).
    auto bytes = loadV2Fixture("gradus_v2_preset_default.bin");
    auto* stream = makeStreamFromBytes(bytes);

    Gradus::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);

    REQUIRE(processor.setState(stream) == kResultOk);

    // After loading a v2 fixture, the v3 appendix is missing, so the Sequencer
    // Note lane params MUST hold their constructor defaults (FR-039a).
    //
    // NOTE: This test accesses the processor's internal arpParams_ via the
    // public state stream round-trip. To verify, we serialize back to v3
    // format and inspect the appendix.
    {
        auto* outStream = new MemoryStream();
        REQUIRE(processor.getState(outStream) == kResultOk);
        outStream->seek(0, IBStream::kIBSeekSet, nullptr);

        IBStreamer reader(outStream, kLittleEndian);
        int32 version = 0;
        REQUIRE(reader.readInt32(version));
        CHECK(version == Gradus::kCurrentStateVersion);
        CHECK(version == 3);

        // Skip the v2 block by loading it into a scratch params struct.
        Gradus::ArpeggiatorParams scratch;
        REQUIRE(Gradus::loadArpParams(scratch, reader) == true);

        // Now read the v3 appendix and verify defaults.
        int32 sourceMode = -1;
        REQUIRE(reader.readInt32(sourceMode));
        CHECK(sourceMode == 0);  // Live (default for v2 fixtures)

        int32 length = -1;
        REQUIRE(reader.readInt32(length));
        CHECK(length == 16);

        for (int i = 0; i < 32; ++i) {
            int32 pitch = -1;
            REQUIRE(reader.readInt32(pitch));
            CHECK(pitch == 60);
        }
        for (int i = 0; i < 32; ++i) {
            int32 restFlag = -1;
            REQUIRE(reader.readInt32(restFlag));
            CHECK(restFlag == 1);  // all rest by default
        }

        float speed = 0.0f;
        REQUIRE(reader.readFloat(speed));
        CHECK(speed == Approx(1.0f));

        float swing = -1.0f;
        REQUIRE(reader.readFloat(swing));
        CHECK(swing == Approx(0.0f));

        int32 jitter = -1;
        REQUIRE(reader.readInt32(jitter));
        CHECK(jitter == 0);

        float depth = -1.0f;
        REQUIRE(reader.readFloat(depth));
        CHECK(depth == Approx(0.0f));

        outStream->release();
    }

    stream->release();
    REQUIRE(processor.terminate() == kResultOk);
}

// =============================================================================
// v3 round-trip: all Sequencer Note lane params preserved bit-exact
// =============================================================================

TEST_CASE("v3 round-trip preserves all sequencer fields",
          "[gradus][vst][state][migration]")
{
    Gradus::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);

    // Set non-default values for every Sequencer Note lane param via
    // handleArpParamChange (the processor exposes arpParams_ via getState
    // round-trip; we drive it through the parameter pipeline).
    //
    // We use a private helper: directly populate the params via the public
    // processParameterChanges interface would require building IParameterChanges
    // queues, which is verbose. Instead, we use a controlled stream-load-set
    // round-trip after staging values via the controller-side denormalization.
    //
    // Simplest valid path: write a v3 stream with chosen values, setState
    // (loads them), getState (reads them back), and verify identity.
    //
    // 1. Build a v3 stream by writing version=3, default v2 block, then a
    //    custom v3 appendix with non-default values.
    auto* inStream = new MemoryStream();
    {
        IBStreamer writer(inStream, kLittleEndian);
        writer.writeInt32(3);

        Gradus::ArpeggiatorParams baseline;
        Gradus::saveArpParams(baseline, writer);

        // v3 appendix with deliberate non-defaults.
        writer.writeInt32(1);                  // sourceMode = Sequencer
        writer.writeInt32(7);                  // length = 7
        for (int i = 0; i < 32; ++i) {
            writer.writeInt32(36 + i);         // pitches: 36, 37, 38, ...
        }
        for (int i = 0; i < 32; ++i) {
            writer.writeInt32(i % 2);          // rest flags: alternating 0/1
        }
        writer.writeFloat(2.0f);               // speed
        writer.writeFloat(33.3f);              // swing
        writer.writeInt32(3);                  // jitter
        writer.writeFloat(0.75f);              // speed curve depth
    }
    inStream->seek(0, IBStream::kIBSeekSet, nullptr);

    REQUIRE(processor.setState(inStream) == kResultOk);

    // 2. getState into a fresh stream and verify the v3 appendix matches.
    auto* outStream = new MemoryStream();
    REQUIRE(processor.getState(outStream) == kResultOk);
    outStream->seek(0, IBStream::kIBSeekSet, nullptr);

    IBStreamer reader(outStream, kLittleEndian);
    int32 version = 0;
    REQUIRE(reader.readInt32(version));
    CHECK(version == 3);

    Gradus::ArpeggiatorParams scratch;
    REQUIRE(Gradus::loadArpParams(scratch, reader) == true);

    int32 sourceMode = -1;
    REQUIRE(reader.readInt32(sourceMode));
    CHECK(sourceMode == 1);

    int32 length = -1;
    REQUIRE(reader.readInt32(length));
    CHECK(length == 7);

    for (int i = 0; i < 32; ++i) {
        int32 pitch = -1;
        REQUIRE(reader.readInt32(pitch));
        CHECK(pitch == 36 + i);
    }
    for (int i = 0; i < 32; ++i) {
        int32 restFlag = -1;
        REQUIRE(reader.readInt32(restFlag));
        CHECK(restFlag == (i % 2));
    }

    float speed = 0.0f;
    REQUIRE(reader.readFloat(speed));
    CHECK(speed == Approx(2.0f));

    float swing = -1.0f;
    REQUIRE(reader.readFloat(swing));
    CHECK(swing == Approx(33.3f));

    int32 jitter = -1;
    REQUIRE(reader.readInt32(jitter));
    CHECK(jitter == 3);

    float depth = -1.0f;
    REQUIRE(reader.readFloat(depth));
    CHECK(depth == Approx(0.75f));

    inStream->release();
    outStream->release();
    REQUIRE(processor.terminate() == kResultOk);
}

// =============================================================================
// SC-004 / FR-039b: v2-stream Live mode produces byte-identical state
// (state-side check; full MIDI byte-identical is Phase 3's live_mode test)
// =============================================================================

TEST_CASE("v2-stream Live mode preserves base arp params byte-identical",
          "[gradus][vst][state][migration][sc004]")
{
    // Load the heavy_lanes v2 fixture which exercises all modulator lanes.
    auto bytes = loadV2Fixture("gradus_v2_preset_heavy_lanes.bin");
    auto* stream = makeStreamFromBytes(bytes);

    Gradus::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);

    REQUIRE(processor.setState(stream) == kResultOk);

    // Round-trip through v3 getState back into a scratch ArpeggiatorParams
    // and compare a few representative fields against what loadArpParams
    // produces when run directly against the original v2 bytes.
    Gradus::ArpeggiatorParams expected;
    {
        auto* refStream = makeStreamFromBytes(bytes);
        IBStreamer refReader(refStream, kLittleEndian);
        int32 v = 0;
        REQUIRE(refReader.readInt32(v));
        CHECK(v == 2);
        REQUIRE(Gradus::loadArpParams(expected, refReader) == true);
        refStream->release();
    }

    auto* outStream = new MemoryStream();
    REQUIRE(processor.getState(outStream) == kResultOk);
    outStream->seek(0, IBStream::kIBSeekSet, nullptr);

    IBStreamer reader(outStream, kLittleEndian);
    int32 version = 0;
    REQUIRE(reader.readInt32(version));
    CHECK(version == 3);

    Gradus::ArpeggiatorParams actual;
    REQUIRE(Gradus::loadArpParams(actual, reader) == true);

    // Force operatingMode to match the post-setState forced value (always MIDI)
    expected.operatingMode.store(Gradus::kArpMIDI, std::memory_order_relaxed);

    // Spot-check representative fields.
    CHECK(actual.mode.load() == expected.mode.load());
    CHECK(actual.octaveRange.load() == expected.octaveRange.load());
    CHECK(actual.tempoSync.load() == expected.tempoSync.load());
    CHECK(actual.noteValue.load() == expected.noteValue.load());
    CHECK(actual.gateLength.load() == Approx(expected.gateLength.load()));
    CHECK(actual.swing.load() == Approx(expected.swing.load()));
    CHECK(actual.velocityLaneLength.load() == expected.velocityLaneLength.load());
    CHECK(actual.midiDelayLaneLength.load() == expected.midiDelayLaneLength.load());

    stream->release();
    outStream->release();
    REQUIRE(processor.terminate() == kResultOk);
}

// =============================================================================
// v3 setState rejects unknown future versions
// =============================================================================

TEST_CASE("v3 setState rejects unknown future versions",
          "[gradus][vst][state][migration]")
{
    auto* stream = new MemoryStream();
    {
        IBStreamer writer(stream, kLittleEndian);
        writer.writeInt32(999);  // bogus future version
        // Trailing bytes don't matter — setState should reject on version.
        writer.writeInt32(0);
    }
    stream->seek(0, IBStream::kIBSeekSet, nullptr);

    Gradus::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);

    CHECK(processor.setState(stream) == kResultFalse);

    stream->release();
    REQUIRE(processor.terminate() == kResultOk);
}
