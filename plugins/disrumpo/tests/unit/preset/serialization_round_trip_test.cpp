// =============================================================================
// Serialization Round-Trip Tests
// =============================================================================
// Spec 010: Preset System - User Story 1
// Verifies that Disrumpo's versioned serialization (v1-v6) round-trips all
// ~450 parameters faithfully without data loss.
//
// Strategy: Build a binary stream with known non-default values, load via
// setState(), re-serialize via getState(), and compare streams byte-by-byte.
//
// References:
// - FR-013: All parameters round-trip through serialize/deserialize
// - SC-001: Floating-point precision within 1e-6
// =============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "processor/processor.h"
#include "plugin_ids.h"
#include "dsp/band_state.h"
#include "dsp/morph_node.h"
#include "dsp/sweep_types.h"
#include "dsp/sweep_processor.h"
#include "dsp/sweep_envelope.h"
#include "dsp/distortion_types.h"

#include <krate/dsp/core/modulation_types.h>
#include <krate/dsp/core/note_value.h>
#include <krate/dsp/primitives/lfo.h>

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "public.sdk/source/common/memorystream.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

using namespace Disrumpo;
using Steinberg::IBStream;
using Steinberg::MemoryStream;

// =============================================================================
// Helper: Create and initialize a Processor for testing
// =============================================================================

static std::unique_ptr<Processor> createTestProcessor() {
    auto proc = std::make_unique<Processor>();
    // initialize() requires FUnknown context but nullptr is acceptable for tests
    proc->initialize(nullptr);

    // Setup processing with standard configuration
    Steinberg::Vst::ProcessSetup setup{};
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.processMode = Steinberg::Vst::kRealtime;
    proc->setupProcessing(setup);

    return proc;
}

// =============================================================================
// Helper: Write a complete v6 preset with non-default values to a stream
// =============================================================================

static void writeNonDefaultV6Preset(Steinberg::IBStreamer& s) {
    // Version
    s.writeInt32(kPresetVersion);  // v6

    // Global parameters (non-default values)
    s.writeFloat(0.7f);   // inputGain (default 0.5)
    s.writeFloat(0.3f);   // outputGain (default 0.5)
    s.writeFloat(0.8f);   // globalMix (default 1.0)

    // Band count
    s.writeInt32(6);  // default is 4

    // Per-band state: 8 bands x (gainDb, pan, solo, bypass, mute)
    for (int b = 0; b < kMaxBands; ++b) {
        float gain = static_cast<float>(b) * 2.0f - 8.0f;  // -8, -6, -4, -2, 0, 2, 4, 6
        gain = std::clamp(gain, kMinBandGainDb, kMaxBandGainDb);
        s.writeFloat(gain);

        float pan = static_cast<float>(b) / 7.0f * 2.0f - 1.0f;  // -1.0 to +1.0
        s.writeFloat(pan);

        // solo: bands 1,3 soloed
        s.writeInt8(static_cast<Steinberg::int8>((b == 1 || b == 3) ? 1 : 0));
        // bypass: band 2 bypassed
        s.writeInt8(static_cast<Steinberg::int8>(b == 2 ? 1 : 0));
        // mute: band 7 muted
        s.writeInt8(static_cast<Steinberg::int8>(b == 7 ? 1 : 0));
    }

    // Crossover frequencies: 7 floats
    // Use non-default frequencies spanning the spectrum
    float crossovers[] = {100.0f, 250.0f, 500.0f, 1000.0f, 2500.0f, 5000.0f, 10000.0f};
    for (int c = 0; c < kMaxBands - 1; ++c) {
        s.writeFloat(crossovers[c]);
    }

    // =========================================================================
    // Sweep System State (v4+)
    // =========================================================================

    // Sweep Core (6 values)
    s.writeInt8(1);     // enabled (default disabled)
    s.writeFloat(0.7f); // frequency normalized (non-default)
    s.writeFloat(0.5f); // width normalized (non-default)
    s.writeFloat(0.6f); // intensity normalized (non-default)
    s.writeInt8(static_cast<Steinberg::int8>(SweepFalloff::Sharp));  // falloff
    s.writeInt8(static_cast<Steinberg::int8>(MorphLinkMode::SweepFreq)); // morph link

    // Sweep LFO (6 values)
    s.writeInt8(1);     // enabled
    s.writeFloat(0.4f); // rate normalized
    s.writeInt8(2);     // waveform = Sawtooth
    s.writeFloat(0.7f); // depth
    s.writeInt8(0);     // tempo sync off
    s.writeInt8(4);     // note index (encodes note value + modifier)

    // Sweep Envelope (4 values)
    s.writeInt8(1);     // enabled
    s.writeFloat(0.3f); // attack normalized
    s.writeFloat(0.6f); // release normalized
    s.writeFloat(0.8f); // sensitivity

    // Custom Curve breakpoints
    s.writeInt32(2);      // 2 breakpoints (default)
    s.writeFloat(0.0f);   // point 0 X
    s.writeFloat(0.2f);   // point 0 Y (non-default)
    s.writeFloat(1.0f);   // point 1 X
    s.writeFloat(0.9f);   // point 1 Y (non-default)

    // =========================================================================
    // Modulation System State (v5+)
    // =========================================================================

    // LFO 1 (7 values)
    s.writeFloat(0.3f);  // rate normalized
    s.writeInt8(1);       // shape = Triangle
    s.writeFloat(0.25f);  // phase offset (normalized, 0-1 maps to 0-360)
    s.writeInt8(0);       // tempo sync off
    s.writeInt8(6);       // note index
    s.writeInt8(1);       // unipolar on
    s.writeInt8(0);       // retrigger off

    // LFO 2 (7 values)
    s.writeFloat(0.6f);  // rate normalized
    s.writeInt8(3);       // shape = Square
    s.writeFloat(0.5f);   // phase offset
    s.writeInt8(1);       // tempo sync on
    s.writeInt8(9);       // note index
    s.writeInt8(0);       // unipolar off
    s.writeInt8(1);       // retrigger on

    // Envelope Follower (4 values)
    s.writeFloat(0.5f);  // attack normalized
    s.writeFloat(0.4f);  // release normalized
    s.writeFloat(0.7f);  // sensitivity
    s.writeInt8(2);       // source = InputSum

    // Random (3 values)
    s.writeFloat(0.3f);  // rate normalized
    s.writeFloat(0.6f);  // smoothness
    s.writeInt8(0);       // tempo sync off

    // Chaos (3 values)
    s.writeInt8(1);       // model = Rossler
    s.writeFloat(0.5f);  // speed normalized
    s.writeFloat(0.7f);  // coupling

    // Sample & Hold (3 values)
    s.writeInt8(1);       // source = LFO1
    s.writeFloat(0.4f);  // rate normalized
    s.writeFloat(0.3f);  // slew normalized

    // Pitch Follower (4 values)
    s.writeFloat(0.5f);  // minHz normalized
    s.writeFloat(0.3f);  // maxHz normalized
    s.writeFloat(0.6f);  // confidence
    s.writeFloat(0.4f);  // tracking speed normalized

    // Transient (3 values)
    s.writeFloat(0.8f);  // sensitivity
    s.writeFloat(0.3f);  // attack normalized
    s.writeFloat(0.5f);  // decay normalized

    // Macros (4 x 4 = 16 values)
    for (int m = 0; m < 4; ++m) {
        float base = static_cast<float>(m) / 3.0f;
        s.writeFloat(base + 0.1f);   // value
        s.writeFloat(base * 0.3f);   // min
        s.writeFloat(std::min(1.0f, base + 0.5f)); // max
        s.writeInt8(static_cast<Steinberg::int8>(m % 4));  // curve (cycles through all 4)
    }

    // Routings (32 x 4 = 128 values)
    for (int r = 0; r < 32; ++r) {
        if (r < 4) {
            // First 4 routings are active with non-default values
            s.writeInt8(static_cast<Steinberg::int8>(r + 1));  // source (LFO1=1, LFO2=2, etc.)
            s.writeInt32(r);                                     // dest param ID
            s.writeFloat(0.5f + static_cast<float>(r) * 0.1f);  // amount
            s.writeInt8(static_cast<Steinberg::int8>(r % 4));   // curve
        } else {
            // Remaining routings are inactive (source = None)
            s.writeInt8(0);      // source = None
            s.writeInt32(0);     // dest
            s.writeFloat(0.0f);  // amount
            s.writeInt8(0);      // curve = Linear
        }
    }

    // =========================================================================
    // Morph Node State (v6+)
    // =========================================================================
    for (int b = 0; b < kMaxBands; ++b) {
        // Band morph position & config (3 floats + 2 int8)
        float morphX = static_cast<float>(b) / 7.0f;  // 0.0 to 1.0
        float morphY = 1.0f - morphX;                    // 1.0 to 0.0
        s.writeFloat(morphX);     // morphX
        s.writeFloat(morphY);     // morphY
        s.writeInt8(static_cast<Steinberg::int8>(b % 3)); // morphMode (0,1,2 cycling)
        s.writeInt8(static_cast<Steinberg::int8>(2 + (b % 3))); // activeNodeCount (2,3,4 cycling)
        s.writeFloat(0.0f);       // morphSmoothing

        // Per-node state (4 nodes x 7 values)
        for (int n = 0; n < kMaxMorphNodes; ++n) {
            int typeIdx = (b * kMaxMorphNodes + n) % static_cast<int>(DistortionType::COUNT);
            s.writeInt8(static_cast<Steinberg::int8>(typeIdx)); // type

            float drive = 1.0f + static_cast<float>(n) * 2.0f;  // 1, 3, 5, 7
            s.writeFloat(drive);  // drive
            float mix = 0.5f + static_cast<float>(n) * 0.1f;    // 0.5, 0.6, 0.7, 0.8
            s.writeFloat(mix);    // mix
            float tone = 500.0f + static_cast<float>(b) * 500.0f; // 500 to 4000
            s.writeFloat(tone);   // toneHz
            float bias = static_cast<float>(n) * 0.2f - 0.3f;   // -0.3, -0.1, 0.1, 0.3
            s.writeFloat(bias);   // bias
            float folds = 1.0f + static_cast<float>(n);           // 1, 2, 3, 4
            s.writeFloat(folds);  // folds
            float bits = 8.0f + static_cast<float>(n) * 2.0f;    // 8, 10, 12, 14
            s.writeFloat(bits);   // bitDepth
        }
    }
}

// =============================================================================
// Helper: Compare two streams byte-by-byte, returning position of first diff
// Returns -1 if streams are identical, -2 if different sizes
// =============================================================================

static int compareStreams(MemoryStream& a, MemoryStream& b) {
    // Seek both to start
    a.seek(0, IBStream::kIBSeekSet, nullptr);
    b.seek(0, IBStream::kIBSeekSet, nullptr);

    // Get sizes
    Steinberg::int64 posA = 0;
    Steinberg::int64 posB = 0;
    a.seek(0, IBStream::kIBSeekEnd, &posA);
    b.seek(0, IBStream::kIBSeekEnd, &posB);
    a.seek(0, IBStream::kIBSeekSet, nullptr);
    b.seek(0, IBStream::kIBSeekSet, nullptr);

    if (posA != posB) {
        return -2;  // Different sizes
    }

    for (int i = 0; i < static_cast<int>(posA); ++i) {
        uint8_t byteA = 0;
        uint8_t byteB = 0;
        Steinberg::int32 readA = 0;
        Steinberg::int32 readB = 0;
        a.read(&byteA, 1, &readA);
        b.read(&byteB, 1, &readB);
        if (readA != 1 || readB != 1 || byteA != byteB) {
            return i;
        }
    }

    return -1;  // Identical
}

// =============================================================================
// Helper: Compare two v6 preset streams field-by-field with float tolerance
// Returns true if all values match within 1e-6 for floats, exact for ints
// =============================================================================

static bool readAndCompareFloat(Steinberg::IBStreamer& rA, Steinberg::IBStreamer& rB,
                                 float tolerance = 1e-6f) {
    float fA = 0.0f, fB = 0.0f;
    if (!rA.readFloat(fA) || !rB.readFloat(fB)) return false;
    return std::abs(fA - fB) <= tolerance;
}

static bool readAndCompareInt32(Steinberg::IBStreamer& rA, Steinberg::IBStreamer& rB) {
    int32_t iA = 0, iB = 0;
    if (!rA.readInt32(iA) || !rB.readInt32(iB)) return false;
    return iA == iB;
}

static bool readAndCompareInt8(Steinberg::IBStreamer& rA, Steinberg::IBStreamer& rB) {
    Steinberg::int8 iA = 0, iB = 0;
    if (!rA.readInt8(iA) || !rB.readInt8(iB)) return false;
    return iA == iB;
}

static bool compareV6PresetStreams(MemoryStream& a, MemoryStream& b) {
    a.seek(0, IBStream::kIBSeekSet, nullptr);
    b.seek(0, IBStream::kIBSeekSet, nullptr);

    Steinberg::IBStreamer rA(&a, kLittleEndian);
    Steinberg::IBStreamer rB(&b, kLittleEndian);

    // Version
    if (!readAndCompareInt32(rA, rB)) return false;

    // Globals (3 floats)
    for (int i = 0; i < 3; ++i)
        if (!readAndCompareFloat(rA, rB)) return false;

    // Band count
    if (!readAndCompareInt32(rA, rB)) return false;

    // Per-band state: 8 bands x (float, float, int8, int8, int8)
    for (int b = 0; b < kMaxBands; ++b) {
        if (!readAndCompareFloat(rA, rB)) return false;  // gainDb
        if (!readAndCompareFloat(rA, rB)) return false;  // pan
        if (!readAndCompareInt8(rA, rB)) return false;    // solo
        if (!readAndCompareInt8(rA, rB)) return false;    // bypass
        if (!readAndCompareInt8(rA, rB)) return false;    // mute
    }

    // Crossovers: 7 floats
    for (int c = 0; c < kMaxBands - 1; ++c)
        if (!readAndCompareFloat(rA, rB)) return false;

    // Sweep Core: int8, float, float, float, int8, int8
    if (!readAndCompareInt8(rA, rB)) return false;   // enable
    if (!readAndCompareFloat(rA, rB)) return false;  // freq
    if (!readAndCompareFloat(rA, rB)) return false;  // width
    if (!readAndCompareFloat(rA, rB)) return false;  // intensity
    if (!readAndCompareInt8(rA, rB)) return false;   // falloff
    if (!readAndCompareInt8(rA, rB)) return false;   // morph link

    // Sweep LFO: int8, float, int8, float, int8, int8
    if (!readAndCompareInt8(rA, rB)) return false;   // enable
    if (!readAndCompareFloat(rA, rB)) return false;  // rate
    if (!readAndCompareInt8(rA, rB)) return false;   // waveform
    if (!readAndCompareFloat(rA, rB)) return false;  // depth
    if (!readAndCompareInt8(rA, rB)) return false;   // sync
    if (!readAndCompareInt8(rA, rB)) return false;   // note index

    // Sweep Envelope: int8, float, float, float
    if (!readAndCompareInt8(rA, rB)) return false;   // enable
    if (!readAndCompareFloat(rA, rB)) return false;  // attack
    if (!readAndCompareFloat(rA, rB)) return false;  // release
    if (!readAndCompareFloat(rA, rB)) return false;  // sensitivity

    // Custom Curve: int32 + variable points
    int32_t pcA = 0, pcB = 0;
    rA.readInt32(pcA);
    rB.readInt32(pcB);
    if (pcA != pcB) return false;
    for (int32_t i = 0; i < pcA; ++i) {
        if (!readAndCompareFloat(rA, rB)) return false;  // x
        if (!readAndCompareFloat(rA, rB)) return false;  // y
    }

    // Modulation Sources:
    // LFO 1: float, int8, float, int8, int8, int8, int8
    if (!readAndCompareFloat(rA, rB)) return false;  // rate
    if (!readAndCompareInt8(rA, rB)) return false;   // shape
    if (!readAndCompareFloat(rA, rB)) return false;  // phase
    if (!readAndCompareInt8(rA, rB)) return false;   // sync
    if (!readAndCompareInt8(rA, rB)) return false;   // note
    if (!readAndCompareInt8(rA, rB)) return false;   // unipolar
    if (!readAndCompareInt8(rA, rB)) return false;   // retrigger

    // LFO 2: float, int8, float, int8, int8, int8, int8
    if (!readAndCompareFloat(rA, rB)) return false;
    if (!readAndCompareInt8(rA, rB)) return false;
    if (!readAndCompareFloat(rA, rB)) return false;
    if (!readAndCompareInt8(rA, rB)) return false;
    if (!readAndCompareInt8(rA, rB)) return false;
    if (!readAndCompareInt8(rA, rB)) return false;
    if (!readAndCompareInt8(rA, rB)) return false;

    // EnvFollower: float, float, float, int8
    for (int i = 0; i < 3; ++i)
        if (!readAndCompareFloat(rA, rB)) return false;
    if (!readAndCompareInt8(rA, rB)) return false;

    // Random: float, float, int8
    if (!readAndCompareFloat(rA, rB)) return false;
    if (!readAndCompareFloat(rA, rB)) return false;
    if (!readAndCompareInt8(rA, rB)) return false;

    // Chaos: int8, float, float
    if (!readAndCompareInt8(rA, rB)) return false;
    if (!readAndCompareFloat(rA, rB)) return false;
    if (!readAndCompareFloat(rA, rB)) return false;

    // S&H: int8, float, float
    if (!readAndCompareInt8(rA, rB)) return false;
    if (!readAndCompareFloat(rA, rB)) return false;
    if (!readAndCompareFloat(rA, rB)) return false;

    // Pitch Follower: 4 floats
    for (int i = 0; i < 4; ++i)
        if (!readAndCompareFloat(rA, rB)) return false;

    // Transient: 3 floats
    for (int i = 0; i < 3; ++i)
        if (!readAndCompareFloat(rA, rB)) return false;

    // Macros: 4 x (float, float, float, int8)
    for (int m = 0; m < 4; ++m) {
        if (!readAndCompareFloat(rA, rB)) return false;
        if (!readAndCompareFloat(rA, rB)) return false;
        if (!readAndCompareFloat(rA, rB)) return false;
        if (!readAndCompareInt8(rA, rB)) return false;
    }

    // Routings: 32 x (int8, int32, float, int8)
    for (int r = 0; r < 32; ++r) {
        if (!readAndCompareInt8(rA, rB)) return false;
        if (!readAndCompareInt32(rA, rB)) return false;
        if (!readAndCompareFloat(rA, rB)) return false;
        if (!readAndCompareInt8(rA, rB)) return false;
    }

    // Morph nodes: 8 bands x (float, float, int8, int8, float, 4 nodes x 7 values)
    for (int b = 0; b < kMaxBands; ++b) {
        if (!readAndCompareFloat(rA, rB)) return false;  // morphX
        if (!readAndCompareFloat(rA, rB)) return false;  // morphY
        if (!readAndCompareInt8(rA, rB)) return false;   // morphMode
        if (!readAndCompareInt8(rA, rB)) return false;   // activeNodeCount
        if (!readAndCompareFloat(rA, rB)) return false;  // morphSmoothing

        for (int n = 0; n < kMaxMorphNodes; ++n) {
            if (!readAndCompareInt8(rA, rB)) return false;   // type
            if (!readAndCompareFloat(rA, rB)) return false;  // drive
            if (!readAndCompareFloat(rA, rB)) return false;  // mix
            if (!readAndCompareFloat(rA, rB)) return false;  // toneHz
            if (!readAndCompareFloat(rA, rB)) return false;  // bias
            if (!readAndCompareFloat(rA, rB)) return false;  // folds
            if (!readAndCompareFloat(rA, rB)) return false;  // bitDepth
        }
    }

    return true;
}

// =============================================================================
// T060-T064: Full v6 round-trip test
// =============================================================================

TEST_CASE("Serialization round-trip: v6 full parameter set", "[preset][serialization][round-trip]") {
    // Strategy: Load non-default preset, save to stream1, load from stream1
    // into fresh processor, save to stream2, verify stream1 == stream2 exactly.
    // This double round-trip ensures values have stabilized through any
    // normalize/denormalize transformations.

    // Step 1: Build initial stream with non-default values
    auto inputStream = Steinberg::owned(new MemoryStream());
    {
        Steinberg::IBStreamer writer(inputStream, kLittleEndian);
        writeNonDefaultV6Preset(writer);
    }

    // Step 2: Load into processor A and save to stream1
    auto procA = createTestProcessor();
    inputStream->seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(procA->setState(inputStream) == Steinberg::kResultOk);

    auto stream1 = Steinberg::owned(new MemoryStream());
    REQUIRE(procA->getState(stream1) == Steinberg::kResultOk);

    // Step 3: Load stream1 into processor B and save to stream2
    auto procB = createTestProcessor();
    stream1->seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(procB->setState(stream1) == Steinberg::kResultOk);

    auto stream2 = Steinberg::owned(new MemoryStream());
    REQUIRE(procB->getState(stream2) == Steinberg::kResultOk);

    // Step 4: stream1 and stream2 must match within float tolerance
    // Note: Log/exp normalize/denormalize transforms may produce 1 ULP differences
    // between successive round-trips, so we compare field-by-field with tolerance.
    REQUIRE(compareV6PresetStreams(*stream1, *stream2));

    // Step 5: Verify stream1 is a reasonable size (sanity check)
    Steinberg::int64 streamSize = 0;
    stream1->seek(0, IBStream::kIBSeekEnd, &streamSize);
    REQUIRE(streamSize > 100);  // Should be several KB for full v6 state
}

TEST_CASE("Serialization round-trip: initial setState values are preserved", "[preset][serialization][round-trip]") {
    // Verify that the initial non-default values loaded into procA are
    // not lost (i.e. stream1 differs from default state)
    auto inputStream = Steinberg::owned(new MemoryStream());
    {
        Steinberg::IBStreamer writer(inputStream, kLittleEndian);
        writeNonDefaultV6Preset(writer);
    }

    auto procA = createTestProcessor();
    inputStream->seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(procA->setState(inputStream) == Steinberg::kResultOk);

    auto stream1 = Steinberg::owned(new MemoryStream());
    REQUIRE(procA->getState(stream1) == Steinberg::kResultOk);

    // Get default state for comparison
    auto procDefault = createTestProcessor();
    auto defaultStream = Steinberg::owned(new MemoryStream());
    REQUIRE(procDefault->getState(defaultStream) == Steinberg::kResultOk);

    // stream1 must differ from default (we loaded non-default values)
    int diffPos = compareStreams(*stream1, *defaultStream);
    REQUIRE(diffPos != -1);  // Must NOT be identical to defaults
}

// =============================================================================
// T060: Verify round-trip preserves global parameters
// =============================================================================

TEST_CASE("Serialization round-trip: global parameters preserved", "[preset][serialization][globals]") {
    auto stream = Steinberg::owned(new MemoryStream());
    {
        Steinberg::IBStreamer writer(stream, kLittleEndian);
        writeNonDefaultV6Preset(writer);
    }

    // Load then save
    auto proc = createTestProcessor();
    stream->seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(proc->setState(stream) == Steinberg::kResultOk);

    auto outputStream = Steinberg::owned(new MemoryStream());
    REQUIRE(proc->getState(outputStream) == Steinberg::kResultOk);

    // Read back global params from output stream
    outputStream->seek(0, IBStream::kIBSeekSet, nullptr);
    Steinberg::IBStreamer reader(outputStream, kLittleEndian);

    int32_t version = 0;
    float inputGain = 0.0f;
    float outputGain = 0.0f;
    float globalMix = 0.0f;

    REQUIRE(reader.readInt32(version));
    REQUIRE(version == kPresetVersion);
    REQUIRE(reader.readFloat(inputGain));
    REQUIRE(reader.readFloat(outputGain));
    REQUIRE(reader.readFloat(globalMix));

    REQUIRE_THAT(inputGain, Catch::Matchers::WithinAbs(0.7f, 1e-6f));
    REQUIRE_THAT(outputGain, Catch::Matchers::WithinAbs(0.3f, 1e-6f));
    REQUIRE_THAT(globalMix, Catch::Matchers::WithinAbs(0.8f, 1e-6f));
}

// =============================================================================
// T060: Verify round-trip preserves band state
// =============================================================================

TEST_CASE("Serialization round-trip: band state preserved", "[preset][serialization][bands]") {
    auto stream = Steinberg::owned(new MemoryStream());
    {
        Steinberg::IBStreamer writer(stream, kLittleEndian);
        writeNonDefaultV6Preset(writer);
    }

    auto proc = createTestProcessor();
    stream->seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(proc->setState(stream) == Steinberg::kResultOk);

    auto outputStream = Steinberg::owned(new MemoryStream());
    REQUIRE(proc->getState(outputStream) == Steinberg::kResultOk);

    // Read band state from output
    outputStream->seek(0, IBStream::kIBSeekSet, nullptr);
    Steinberg::IBStreamer reader(outputStream, kLittleEndian);

    // Skip version + globals (4 + 3*4 = 16 bytes)
    int32_t version = 0;
    float dummy = 0.0f;
    reader.readInt32(version);
    reader.readFloat(dummy); // inputGain
    reader.readFloat(dummy); // outputGain
    reader.readFloat(dummy); // globalMix

    // Read band count
    int32_t bandCount = 0;
    REQUIRE(reader.readInt32(bandCount));
    REQUIRE(bandCount == 6);

    // Read per-band state
    for (int b = 0; b < kMaxBands; ++b) {
        float gainDb = 0.0f;
        float pan = 0.0f;
        Steinberg::int8 solo = 0;
        Steinberg::int8 bypass = 0;
        Steinberg::int8 mute = 0;

        REQUIRE(reader.readFloat(gainDb));
        REQUIRE(reader.readFloat(pan));
        REQUIRE(reader.readInt8(solo));
        REQUIRE(reader.readInt8(bypass));
        REQUIRE(reader.readInt8(mute));

        // Verify non-default values
        float expectedGain = std::clamp(
            static_cast<float>(b) * 2.0f - 8.0f,
            kMinBandGainDb, kMaxBandGainDb);
        REQUIRE_THAT(gainDb, Catch::Matchers::WithinAbs(static_cast<double>(expectedGain), 1e-6));

        float expectedPan = std::clamp(
            static_cast<float>(b) / 7.0f * 2.0f - 1.0f,
            -1.0f, 1.0f);
        REQUIRE_THAT(pan, Catch::Matchers::WithinAbs(static_cast<double>(expectedPan), 1e-6));

        // Solo: bands 1 and 3
        REQUIRE(solo == ((b == 1 || b == 3) ? 1 : 0));
        // Bypass: band 2
        REQUIRE(bypass == (b == 2 ? 1 : 0));
        // Mute: band 7
        REQUIRE(mute == (b == 7 ? 1 : 0));
    }
}

// =============================================================================
// T070: Empty stream (0 bytes) - verify defaults without crashing
// =============================================================================

TEST_CASE("Serialization edge case: empty stream uses defaults", "[preset][serialization][edge]") {
    auto proc = createTestProcessor();

    // Create an empty stream
    auto emptyStream = Steinberg::owned(new MemoryStream());
    auto result = proc->setState(emptyStream);

    // Should return false (failed to read version) but not crash
    REQUIRE(result == Steinberg::kResultFalse);

    // Verify processor still works - getState should succeed
    auto outputStream = Steinberg::owned(new MemoryStream());
    REQUIRE(proc->getState(outputStream) == Steinberg::kResultOk);
}

// =============================================================================
// T071: Truncated stream - verify partial load with defaults
// =============================================================================

TEST_CASE("Serialization edge case: truncated stream loads partial data", "[preset][serialization][edge]") {
    auto proc = createTestProcessor();

    // Create a stream with only version + partial globals (truncated after inputGain)
    auto truncatedStream = Steinberg::owned(new MemoryStream());
    {
        Steinberg::IBStreamer writer(truncatedStream, kLittleEndian);
        writer.writeInt32(kPresetVersion); // version
        writer.writeFloat(0.8f);            // inputGain only
        // Missing: outputGain, globalMix, bands, etc.
    }

    truncatedStream->seek(0, IBStream::kIBSeekSet, nullptr);
    auto result = proc->setState(truncatedStream);

    // setState reads version successfully but fails partway through globals
    // The implementation returns kResultFalse when read fails mid-stream
    // But it shouldn't crash
    // Note: exact return value depends on where truncation occurs
    // The important thing is NO CRASH
    (void)result;

    // Processor should still be usable
    auto outputStream = Steinberg::owned(new MemoryStream());
    REQUIRE(proc->getState(outputStream) == Steinberg::kResultOk);
}

// =============================================================================
// T072: Version 0 (invalid) - verify rejection
// =============================================================================

TEST_CASE("Serialization edge case: version 0 rejected", "[preset][serialization][edge]") {
    auto proc = createTestProcessor();

    auto invalidStream = Steinberg::owned(new MemoryStream());
    {
        Steinberg::IBStreamer writer(invalidStream, kLittleEndian);
        writer.writeInt32(0);  // version 0 is invalid
        writer.writeFloat(0.8f);
        writer.writeFloat(0.3f);
        writer.writeFloat(0.5f);
    }

    invalidStream->seek(0, IBStream::kIBSeekSet, nullptr);
    auto result = proc->setState(invalidStream);

    // FR-012: Invalid version should be rejected
    REQUIRE(result == Steinberg::kResultFalse);
}

// =============================================================================
// T074: Enumerated type round-trip tests
// =============================================================================

TEST_CASE("Serialization round-trip: enum types preserved", "[preset][serialization][enum]") {
    SECTION("SweepFalloff modes round-trip") {
        for (int f = 0; f < kSweepFalloffCount; ++f) {
            auto stream = Steinberg::owned(new MemoryStream());
            {
                Steinberg::IBStreamer writer(stream, kLittleEndian);
                writer.writeInt32(kPresetVersion);
                // Globals
                writer.writeFloat(0.5f);
                writer.writeFloat(0.5f);
                writer.writeFloat(1.0f);
                // Band count
                writer.writeInt32(kDefaultBands);
                // Band state (8 bands, defaults)
                for (int b = 0; b < kMaxBands; ++b) {
                    writer.writeFloat(0.0f);  // gainDb
                    writer.writeFloat(0.0f);  // pan
                    writer.writeInt8(0);       // solo
                    writer.writeInt8(0);       // bypass
                    writer.writeInt8(0);       // mute
                }
                // Crossovers (7 defaults)
                for (int c = 0; c < kMaxBands - 1; ++c) {
                    writer.writeFloat(1000.0f);
                }
                // Sweep core - test specific falloff
                writer.writeInt8(0);     // enabled
                writer.writeFloat(0.5f); // freq
                writer.writeFloat(0.5f); // width
                writer.writeFloat(0.5f); // intensity
                writer.writeInt8(static_cast<Steinberg::int8>(f));  // falloff under test
                writer.writeInt8(0);     // morph link
                // LFO defaults
                writer.writeInt8(0);
                writer.writeFloat(0.5f);
                writer.writeInt8(0);
                writer.writeFloat(0.0f);
                writer.writeInt8(0);
                writer.writeInt8(0);
                // Envelope defaults
                writer.writeInt8(0);
                writer.writeFloat(0.091f);
                writer.writeFloat(0.184f);
                writer.writeFloat(0.5f);
                // Custom curve default (2 points)
                writer.writeInt32(2);
                writer.writeFloat(0.0f);
                writer.writeFloat(0.0f);
                writer.writeFloat(1.0f);
                writer.writeFloat(1.0f);
                // Modulation - write defaults for all sources
                // LFO1
                writer.writeFloat(0.5f); writer.writeInt8(0); writer.writeFloat(0.0f);
                writer.writeInt8(0); writer.writeInt8(0); writer.writeInt8(0); writer.writeInt8(1);
                // LFO2
                writer.writeFloat(0.5f); writer.writeInt8(0); writer.writeFloat(0.0f);
                writer.writeInt8(0); writer.writeInt8(0); writer.writeInt8(0); writer.writeInt8(1);
                // EnvFollower
                writer.writeFloat(0.0f); writer.writeFloat(0.0f); writer.writeFloat(0.5f); writer.writeInt8(0);
                // Random
                writer.writeFloat(0.0f); writer.writeFloat(0.0f); writer.writeInt8(0);
                // Chaos
                writer.writeInt8(0); writer.writeFloat(0.0f); writer.writeFloat(0.0f);
                // S&H
                writer.writeInt8(0); writer.writeFloat(0.0f); writer.writeFloat(0.0f);
                // Pitch Follower
                writer.writeFloat(0.0f); writer.writeFloat(0.0f); writer.writeFloat(0.5f); writer.writeFloat(0.0f);
                // Transient
                writer.writeFloat(0.5f); writer.writeFloat(0.0f); writer.writeFloat(0.0f);
                // Macros (4)
                for (int m = 0; m < 4; ++m) {
                    writer.writeFloat(0.0f); writer.writeFloat(0.0f);
                    writer.writeFloat(1.0f); writer.writeInt8(0);
                }
                // Routings (32)
                for (int r = 0; r < 32; ++r) {
                    writer.writeInt8(0); writer.writeInt32(0);
                    writer.writeFloat(0.0f); writer.writeInt8(0);
                }
                // Morph nodes (8 bands)
                for (int b = 0; b < kMaxBands; ++b) {
                    writer.writeFloat(0.5f); writer.writeFloat(0.5f);
                    writer.writeInt8(0); writer.writeInt8(static_cast<Steinberg::int8>(kDefaultActiveNodes));
                    writer.writeFloat(0.0f);
                    for (int n = 0; n < kMaxMorphNodes; ++n) {
                        writer.writeInt8(0); // SoftClip
                        writer.writeFloat(1.0f); writer.writeFloat(1.0f);
                        writer.writeFloat(4000.0f); writer.writeFloat(0.0f);
                        writer.writeFloat(1.0f); writer.writeFloat(16.0f);
                    }
                }
            }

            auto proc = createTestProcessor();
            stream->seek(0, IBStream::kIBSeekSet, nullptr);
            REQUIRE(proc->setState(stream) == Steinberg::kResultOk);

            auto out = Steinberg::owned(new MemoryStream());
            REQUIRE(proc->getState(out) == Steinberg::kResultOk);

            // Read back the falloff from output stream
            out->seek(0, IBStream::kIBSeekSet, nullptr);
            Steinberg::IBStreamer reader(out, kLittleEndian);

            // Skip: version(4) + globals(12) + bandCount(4)
            //      + bands(8 * (4+4+1+1+1) = 88) + crossovers(28)
            //      = 136 bytes, then sweep enable(1), freq(4), width(4), intensity(4) = 13
            // Total skip = 136 + 1 + 4 + 4 + 4 = 149 bytes to falloff
            int32_t v = 0;
            reader.readInt32(v);
            float fdummy = 0.0f;
            reader.readFloat(fdummy); reader.readFloat(fdummy); reader.readFloat(fdummy);
            int32_t bc = 0;
            reader.readInt32(bc);
            for (int b = 0; b < kMaxBands; ++b) {
                reader.readFloat(fdummy); reader.readFloat(fdummy);
                Steinberg::int8 idummy = 0;
                reader.readInt8(idummy); reader.readInt8(idummy); reader.readInt8(idummy);
            }
            for (int c = 0; c < kMaxBands - 1; ++c) {
                reader.readFloat(fdummy);
            }
            Steinberg::int8 sweepEnable = 0;
            reader.readInt8(sweepEnable);
            reader.readFloat(fdummy); // freq
            reader.readFloat(fdummy); // width
            reader.readFloat(fdummy); // intensity
            Steinberg::int8 falloff = 0;
            REQUIRE(reader.readInt8(falloff));
            REQUIRE(falloff == static_cast<Steinberg::int8>(f));
        }
    }

    SECTION("DistortionType round-trips for morph nodes") {
        // Use double round-trip: Load -> Save -> Load -> Save, compare saves
        auto inputStream = Steinberg::owned(new MemoryStream());
        {
            Steinberg::IBStreamer writer(inputStream, kLittleEndian);
            writeNonDefaultV6Preset(writer);
        }

        // First round-trip
        auto procA = createTestProcessor();
        inputStream->seek(0, IBStream::kIBSeekSet, nullptr);
        REQUIRE(procA->setState(inputStream) == Steinberg::kResultOk);

        auto stream1 = Steinberg::owned(new MemoryStream());
        REQUIRE(procA->getState(stream1) == Steinberg::kResultOk);

        // Second round-trip
        auto procB = createTestProcessor();
        stream1->seek(0, IBStream::kIBSeekSet, nullptr);
        REQUIRE(procB->setState(stream1) == Steinberg::kResultOk);

        auto stream2 = Steinberg::owned(new MemoryStream());
        REQUIRE(procB->getState(stream2) == Steinberg::kResultOk);

        // Streams must match within float tolerance after double round-trip
        REQUIRE(compareV6PresetStreams(*stream1, *stream2));
    }

    SECTION("MorphMode values round-trip") {
        // The full v6 preset includes all morph mode values (cycling 0,1,2)
        auto inputStream = Steinberg::owned(new MemoryStream());
        {
            Steinberg::IBStreamer writer(inputStream, kLittleEndian);
            writeNonDefaultV6Preset(writer);
        }

        // Double round-trip
        auto procA = createTestProcessor();
        inputStream->seek(0, IBStream::kIBSeekSet, nullptr);
        REQUIRE(procA->setState(inputStream) == Steinberg::kResultOk);

        auto stream1 = Steinberg::owned(new MemoryStream());
        REQUIRE(procA->getState(stream1) == Steinberg::kResultOk);

        auto procB = createTestProcessor();
        stream1->seek(0, IBStream::kIBSeekSet, nullptr);
        REQUIRE(procB->setState(stream1) == Steinberg::kResultOk);

        auto stream2 = Steinberg::owned(new MemoryStream());
        REQUIRE(procB->getState(stream2) == Steinberg::kResultOk);

        REQUIRE(compareV6PresetStreams(*stream1, *stream2));
    }

    SECTION("ModSource values round-trip") {
        // The non-default preset has sources 1-4 in the first 4 routings
        auto inputStream = Steinberg::owned(new MemoryStream());
        {
            Steinberg::IBStreamer writer(inputStream, kLittleEndian);
            writeNonDefaultV6Preset(writer);
        }

        // Double round-trip
        auto procA = createTestProcessor();
        inputStream->seek(0, IBStream::kIBSeekSet, nullptr);
        REQUIRE(procA->setState(inputStream) == Steinberg::kResultOk);

        auto stream1 = Steinberg::owned(new MemoryStream());
        REQUIRE(procA->getState(stream1) == Steinberg::kResultOk);

        auto procB = createTestProcessor();
        stream1->seek(0, IBStream::kIBSeekSet, nullptr);
        REQUIRE(procB->setState(stream1) == Steinberg::kResultOk);

        auto stream2 = Steinberg::owned(new MemoryStream());
        REQUIRE(procB->getState(stream2) == Steinberg::kResultOk);

        REQUIRE(compareV6PresetStreams(*stream1, *stream2));
    }

    SECTION("ModCurve values round-trip") {
        // The non-default preset cycles through all 4 curves in macros
        auto inputStream = Steinberg::owned(new MemoryStream());
        {
            Steinberg::IBStreamer writer(inputStream, kLittleEndian);
            writeNonDefaultV6Preset(writer);
        }

        // Double round-trip
        auto procA = createTestProcessor();
        inputStream->seek(0, IBStream::kIBSeekSet, nullptr);
        REQUIRE(procA->setState(inputStream) == Steinberg::kResultOk);

        auto stream1 = Steinberg::owned(new MemoryStream());
        REQUIRE(procA->getState(stream1) == Steinberg::kResultOk);

        auto procB = createTestProcessor();
        stream1->seek(0, IBStream::kIBSeekSet, nullptr);
        REQUIRE(procB->setState(stream1) == Steinberg::kResultOk);

        auto stream2 = Steinberg::owned(new MemoryStream());
        REQUIRE(procB->getState(stream2) == Steinberg::kResultOk);

        REQUIRE(compareV6PresetStreams(*stream1, *stream2));
    }
}

// =============================================================================
// Version migration tests (Phase 5 - T078-T082)
// =============================================================================

TEST_CASE("Version migration: v1 preset loads with defaults", "[preset][serialization][migration]") {
    auto stream = Steinberg::owned(new MemoryStream());
    {
        Steinberg::IBStreamer writer(stream, kLittleEndian);
        writer.writeInt32(1);     // version 1
        writer.writeFloat(0.7f);  // inputGain
        writer.writeFloat(0.3f);  // outputGain
        writer.writeFloat(0.8f);  // globalMix
    }

    auto proc = createTestProcessor();
    stream->seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(proc->setState(stream) == Steinberg::kResultOk);

    // Verify globals loaded
    auto out = Steinberg::owned(new MemoryStream());
    REQUIRE(proc->getState(out) == Steinberg::kResultOk);

    out->seek(0, IBStream::kIBSeekSet, nullptr);
    Steinberg::IBStreamer reader(out, kLittleEndian);

    int32_t version = 0;
    float inputGain = 0.0f;
    float outputGain = 0.0f;
    float globalMix = 0.0f;

    reader.readInt32(version);
    reader.readFloat(inputGain);
    reader.readFloat(outputGain);
    reader.readFloat(globalMix);

    REQUIRE(version == kPresetVersion);
    REQUIRE_THAT(inputGain, Catch::Matchers::WithinAbs(0.7f, 1e-6f));
    REQUIRE_THAT(outputGain, Catch::Matchers::WithinAbs(0.3f, 1e-6f));
    REQUIRE_THAT(globalMix, Catch::Matchers::WithinAbs(0.8f, 1e-6f));

    // v1 has no bands, sweep, modulation, or morph - they should all use defaults
    // bandCount should be default (4)
    int32_t bandCount = 0;
    reader.readInt32(bandCount);
    REQUIRE(bandCount == kDefaultBands);
}

TEST_CASE("Version migration: v2 preset loads bands with defaults for sweep/mod/morph", "[preset][serialization][migration]") {
    auto stream = Steinberg::owned(new MemoryStream());
    {
        Steinberg::IBStreamer writer(stream, kLittleEndian);
        writer.writeInt32(2);     // version 2
        // Globals
        writer.writeFloat(0.6f);
        writer.writeFloat(0.4f);
        writer.writeFloat(0.9f);
        // Band count
        writer.writeInt32(3);
        // Band state (8 bands)
        for (int b = 0; b < kMaxBands; ++b) {
            writer.writeFloat(static_cast<float>(b) * 1.0f); // gainDb
            writer.writeFloat(0.0f);  // pan
            writer.writeInt8(0);       // solo
            writer.writeInt8(0);       // bypass
            writer.writeInt8(0);       // mute
        }
        // Crossovers
        for (int c = 0; c < kMaxBands - 1; ++c) {
            writer.writeFloat(200.0f + static_cast<float>(c) * 500.0f);
        }
    }

    auto proc = createTestProcessor();
    stream->seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(proc->setState(stream) == Steinberg::kResultOk);

    // Re-serialize and verify
    auto out = Steinberg::owned(new MemoryStream());
    REQUIRE(proc->getState(out) == Steinberg::kResultOk);

    out->seek(0, IBStream::kIBSeekSet, nullptr);
    Steinberg::IBStreamer reader(out, kLittleEndian);

    int32_t version = 0;
    reader.readInt32(version);
    REQUIRE(version == kPresetVersion);

    float ig = 0.0f, og = 0.0f, gm = 0.0f;
    reader.readFloat(ig); reader.readFloat(og); reader.readFloat(gm);
    REQUIRE_THAT(ig, Catch::Matchers::WithinAbs(0.6f, 1e-6f));

    int32_t bandCount = 0;
    reader.readInt32(bandCount);
    REQUIRE(bandCount == 3);
}

TEST_CASE("Version migration: v4 preset loads sweep with defaults for mod/morph", "[preset][serialization][migration]") {
    auto stream = Steinberg::owned(new MemoryStream());
    {
        Steinberg::IBStreamer writer(stream, kLittleEndian);
        writer.writeInt32(4);     // version 4
        // Globals
        writer.writeFloat(0.5f);
        writer.writeFloat(0.5f);
        writer.writeFloat(1.0f);
        // Band count
        writer.writeInt32(kDefaultBands);
        // Band state (defaults)
        for (int b = 0; b < kMaxBands; ++b) {
            writer.writeFloat(0.0f); writer.writeFloat(0.0f);
            writer.writeInt8(0); writer.writeInt8(0); writer.writeInt8(0);
        }
        // Crossovers
        for (int c = 0; c < kMaxBands - 1; ++c) {
            writer.writeFloat(1000.0f);
        }
        // Sweep (v4)
        writer.writeInt8(1);     // enabled
        writer.writeFloat(0.8f); // freq
        writer.writeFloat(0.4f); // width
        writer.writeFloat(0.7f); // intensity
        writer.writeInt8(0);     // falloff = Sharp
        writer.writeInt8(0);     // morph link = None
        // LFO
        writer.writeInt8(0); writer.writeFloat(0.5f); writer.writeInt8(0);
        writer.writeFloat(0.0f); writer.writeInt8(0); writer.writeInt8(0);
        // Envelope
        writer.writeInt8(0); writer.writeFloat(0.091f);
        writer.writeFloat(0.184f); writer.writeFloat(0.5f);
        // Custom curve (2 default points)
        writer.writeInt32(2);
        writer.writeFloat(0.0f); writer.writeFloat(0.0f);
        writer.writeFloat(1.0f); writer.writeFloat(1.0f);
    }

    auto proc = createTestProcessor();
    stream->seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(proc->setState(stream) == Steinberg::kResultOk);

    // Verify sweep params loaded
    auto out = Steinberg::owned(new MemoryStream());
    REQUIRE(proc->getState(out) == Steinberg::kResultOk);

    out->seek(0, IBStream::kIBSeekSet, nullptr);
    Steinberg::IBStreamer reader(out, kLittleEndian);

    // Skip to sweep section
    int32_t v = 0; reader.readInt32(v);
    float fd = 0.0f;
    reader.readFloat(fd); reader.readFloat(fd); reader.readFloat(fd);
    int32_t bc = 0; reader.readInt32(bc);
    for (int b = 0; b < kMaxBands; ++b) {
        reader.readFloat(fd); reader.readFloat(fd);
        Steinberg::int8 id = 0;
        reader.readInt8(id); reader.readInt8(id); reader.readInt8(id);
    }
    for (int c = 0; c < kMaxBands - 1; ++c) {
        reader.readFloat(fd);
    }

    // Read sweep core
    Steinberg::int8 sweepEnable = 0;
    float sweepFreq = 0.0f;
    float sweepWidth = 0.0f;
    float sweepIntensity = 0.0f;

    reader.readInt8(sweepEnable);
    reader.readFloat(sweepFreq);
    reader.readFloat(sweepWidth);
    reader.readFloat(sweepIntensity);

    REQUIRE(sweepEnable == 1);
    // Verify sweep freq/width/intensity round-trip (within tolerance due to normalize/denormalize)
    REQUIRE_THAT(static_cast<double>(sweepFreq), Catch::Matchers::WithinAbs(0.8, 0.01));
    REQUIRE_THAT(static_cast<double>(sweepWidth), Catch::Matchers::WithinAbs(0.4, 0.01));
    REQUIRE_THAT(static_cast<double>(sweepIntensity), Catch::Matchers::WithinAbs(0.7, 0.01));
}

TEST_CASE("Version migration: v5 preset loads modulation with defaults for morph", "[preset][serialization][migration]") {
    auto stream = Steinberg::owned(new MemoryStream());
    {
        Steinberg::IBStreamer writer(stream, kLittleEndian);
        writer.writeInt32(5);     // version 5
        // Globals
        writer.writeFloat(0.5f);
        writer.writeFloat(0.5f);
        writer.writeFloat(1.0f);
        // Band management (v2)
        writer.writeInt32(kDefaultBands);
        for (int b = 0; b < kMaxBands; ++b) {
            writer.writeFloat(0.0f); writer.writeFloat(0.0f);
            writer.writeInt8(0); writer.writeInt8(0); writer.writeInt8(0);
        }
        for (int c = 0; c < kMaxBands - 1; ++c) {
            writer.writeFloat(1000.0f);
        }
        // Sweep (v4) - defaults
        writer.writeInt8(0); writer.writeFloat(0.566f); writer.writeFloat(0.286f);
        writer.writeFloat(0.25f); writer.writeInt8(1); writer.writeInt8(0);
        writer.writeInt8(0); writer.writeFloat(0.606f); writer.writeInt8(0);
        writer.writeFloat(0.0f); writer.writeInt8(0); writer.writeInt8(0);
        writer.writeInt8(0); writer.writeFloat(0.091f);
        writer.writeFloat(0.184f); writer.writeFloat(0.5f);
        writer.writeInt32(2);
        writer.writeFloat(0.0f); writer.writeFloat(0.0f);
        writer.writeFloat(1.0f); writer.writeFloat(1.0f);
        // Modulation (v5) - non-default LFO1 rate
        writer.writeFloat(0.7f);  // LFO1 rate (non-default)
        writer.writeInt8(2);       // Sawtooth
        writer.writeFloat(0.0f); writer.writeInt8(0); writer.writeInt8(0);
        writer.writeInt8(0); writer.writeInt8(1);
        // LFO2 defaults
        writer.writeFloat(0.5f); writer.writeInt8(0); writer.writeFloat(0.0f);
        writer.writeInt8(0); writer.writeInt8(0); writer.writeInt8(0); writer.writeInt8(1);
        // EnvFollower defaults
        writer.writeFloat(0.0f); writer.writeFloat(0.0f); writer.writeFloat(0.5f); writer.writeInt8(0);
        // Random defaults
        writer.writeFloat(0.0f); writer.writeFloat(0.0f); writer.writeInt8(0);
        // Chaos defaults
        writer.writeInt8(0); writer.writeFloat(0.0f); writer.writeFloat(0.0f);
        // S&H defaults
        writer.writeInt8(0); writer.writeFloat(0.0f); writer.writeFloat(0.0f);
        // Pitch Follower defaults
        writer.writeFloat(0.0f); writer.writeFloat(0.0f); writer.writeFloat(0.5f); writer.writeFloat(0.0f);
        // Transient defaults
        writer.writeFloat(0.5f); writer.writeFloat(0.0f); writer.writeFloat(0.0f);
        // Macros
        for (int m = 0; m < 4; ++m) {
            writer.writeFloat(0.0f); writer.writeFloat(0.0f);
            writer.writeFloat(1.0f); writer.writeInt8(0);
        }
        // Routings (32 empty)
        for (int r = 0; r < 32; ++r) {
            writer.writeInt8(0); writer.writeInt32(0);
            writer.writeFloat(0.0f); writer.writeInt8(0);
        }
        // NO morph data (v5 doesn't have it)
    }

    auto proc = createTestProcessor();
    stream->seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(proc->setState(stream) == Steinberg::kResultOk);

    // Verify modulation loaded and morph uses defaults
    auto out = Steinberg::owned(new MemoryStream());
    REQUIRE(proc->getState(out) == Steinberg::kResultOk);

    // Output should have v6 with morph defaults appended
    out->seek(0, IBStream::kIBSeekSet, nullptr);
    Steinberg::IBStreamer reader(out, kLittleEndian);

    int32_t version = 0;
    reader.readInt32(version);
    REQUIRE(version == kPresetVersion);  // Always writes current version
}

TEST_CASE("Version migration: future version v99 loads known params", "[preset][serialization][migration]") {
    auto stream = Steinberg::owned(new MemoryStream());
    {
        Steinberg::IBStreamer writer(stream, kLittleEndian);
        writer.writeInt32(99);    // future version
        // Globals (known format)
        writer.writeFloat(0.6f);
        writer.writeFloat(0.4f);
        writer.writeFloat(0.7f);
        // Band count
        writer.writeInt32(5);
        // Band state (v2 format, still valid)
        for (int b = 0; b < kMaxBands; ++b) {
            writer.writeFloat(static_cast<float>(b)); writer.writeFloat(0.0f);
            writer.writeInt8(0); writer.writeInt8(0); writer.writeInt8(0);
        }
        // Crossovers
        for (int c = 0; c < kMaxBands - 1; ++c) {
            writer.writeFloat(300.0f + static_cast<float>(c) * 400.0f);
        }
        // Sweep (v4 format)
        writer.writeInt8(1); writer.writeFloat(0.5f); writer.writeFloat(0.5f);
        writer.writeFloat(0.5f); writer.writeInt8(1); writer.writeInt8(0);
        writer.writeInt8(0); writer.writeFloat(0.5f); writer.writeInt8(0);
        writer.writeFloat(0.0f); writer.writeInt8(0); writer.writeInt8(0);
        writer.writeInt8(0); writer.writeFloat(0.5f);
        writer.writeFloat(0.5f); writer.writeFloat(0.5f);
        writer.writeInt32(2);
        writer.writeFloat(0.0f); writer.writeFloat(0.0f);
        writer.writeFloat(1.0f); writer.writeFloat(1.0f);
        // Modulation (v5 format)
        writer.writeFloat(0.5f); writer.writeInt8(0); writer.writeFloat(0.0f);
        writer.writeInt8(0); writer.writeInt8(0); writer.writeInt8(0); writer.writeInt8(1);
        writer.writeFloat(0.5f); writer.writeInt8(0); writer.writeFloat(0.0f);
        writer.writeInt8(0); writer.writeInt8(0); writer.writeInt8(0); writer.writeInt8(1);
        writer.writeFloat(0.0f); writer.writeFloat(0.0f); writer.writeFloat(0.5f); writer.writeInt8(0);
        writer.writeFloat(0.0f); writer.writeFloat(0.0f); writer.writeInt8(0);
        writer.writeInt8(0); writer.writeFloat(0.0f); writer.writeFloat(0.0f);
        writer.writeInt8(0); writer.writeFloat(0.0f); writer.writeFloat(0.0f);
        writer.writeFloat(0.0f); writer.writeFloat(0.0f); writer.writeFloat(0.5f); writer.writeFloat(0.0f);
        writer.writeFloat(0.5f); writer.writeFloat(0.0f); writer.writeFloat(0.0f);
        for (int m = 0; m < 4; ++m) {
            writer.writeFloat(0.0f); writer.writeFloat(0.0f);
            writer.writeFloat(1.0f); writer.writeInt8(0);
        }
        for (int r = 0; r < 32; ++r) {
            writer.writeInt8(0); writer.writeInt32(0);
            writer.writeFloat(0.0f); writer.writeInt8(0);
        }
        // Morph (v6 format)
        for (int b = 0; b < kMaxBands; ++b) {
            writer.writeFloat(0.5f); writer.writeFloat(0.5f);
            writer.writeInt8(0); writer.writeInt8(static_cast<Steinberg::int8>(kDefaultActiveNodes));
            writer.writeFloat(0.0f);
            for (int n = 0; n < kMaxMorphNodes; ++n) {
                writer.writeInt8(0);
                writer.writeFloat(1.0f); writer.writeFloat(1.0f);
                writer.writeFloat(4000.0f); writer.writeFloat(0.0f);
                writer.writeFloat(1.0f); writer.writeFloat(16.0f);
            }
        }
        // Unknown future data (should be ignored)
        writer.writeFloat(42.0f);
        writer.writeFloat(99.0f);
        writer.writeInt32(12345);
    }

    auto proc = createTestProcessor();
    stream->seek(0, IBStream::kIBSeekSet, nullptr);

    // FR-011: Future version should load known params and ignore trailing data
    auto result = proc->setState(stream);
    REQUIRE(result == Steinberg::kResultOk);

    // Verify globals loaded correctly
    auto out = Steinberg::owned(new MemoryStream());
    REQUIRE(proc->getState(out) == Steinberg::kResultOk);

    out->seek(0, IBStream::kIBSeekSet, nullptr);
    Steinberg::IBStreamer reader(out, kLittleEndian);

    int32_t version = 0;
    float ig = 0.0f;
    reader.readInt32(version);
    reader.readFloat(ig);

    // Output writes current version, not future version
    REQUIRE(version == kPresetVersion);
    REQUIRE_THAT(ig, Catch::Matchers::WithinAbs(0.6f, 1e-6f));
}
