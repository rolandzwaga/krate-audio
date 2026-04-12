// ==============================================================================
// State round-trip tests for Phase 3 (T3.4.1 / FR-184 / SC-026)
// ==============================================================================
// Verifies that getState() writes a 302-byte v3 blob and that setState() on a
// fresh processor round-trips every Phase 3 field bit-exactly.
//
// v3 layout (per data-model.md §7): version(4) + v2 body(264) + v3 tail(34)
//   = 4 + 40 + 4 + 4 + 216 + 1 + 1 + 32 = 302 bytes
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"

#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

#include <array>
#include <cstdint>
#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

namespace {

constexpr int kPhase2FloatCount = 27;
constexpr int kChokeAssignCount = 32;
constexpr int64 kV3BlobBytes    = 302;
constexpr int64 kV2BodyBytes    = 268;

ProcessSetup makeSetup(double sampleRate = 44100.0)
{
    ProcessSetup setup{};
    setup.processMode         = kRealtime;
    setup.symbolicSampleSize  = kSample32;
    setup.maxSamplesPerBlock  = 512;
    setup.sampleRate          = sampleRate;
    return setup;
}

// Build a v3 stream with the full v3 layout. All 27 Phase 2 floats are filled
// from `phase2Vals`. The v3 tail is filled from the Phase 3 args.
void writeV3State(MemoryStream* stream,
                  const double  phase1[5],
                  int32         exciterTypeI32,
                  int32         bodyModelI32,
                  const double  phase2Vals[kPhase2FloatCount],
                  std::uint8_t  maxPolyphony,
                  std::uint8_t  voiceStealingPolicy,
                  const std::array<std::uint8_t, kChokeAssignCount>& chokes)
{
    int32 version = 3;
    stream->write(&version, sizeof(version), nullptr);
    for (int i = 0; i < 5; ++i)
    {
        double v = phase1[i];
        stream->write(&v, sizeof(v), nullptr);
    }
    stream->write(&exciterTypeI32, sizeof(exciterTypeI32), nullptr);
    stream->write(&bodyModelI32, sizeof(bodyModelI32), nullptr);
    for (int i = 0; i < kPhase2FloatCount; ++i)
    {
        double v = phase2Vals[i];
        stream->write(&v, sizeof(v), nullptr);
    }
    // Phase 3 tail
    stream->write(&maxPolyphony, sizeof(maxPolyphony), nullptr);
    stream->write(&voiceStealingPolicy, sizeof(voiceStealingPolicy), nullptr);
    for (int i = 0; i < kChokeAssignCount; ++i)
    {
        std::uint8_t b = chokes[static_cast<std::size_t>(i)];
        stream->write(&b, sizeof(b), nullptr);
    }
}

} // namespace

TEST_CASE("State v3 StateRoundTripV3: getState emits exactly 302 bytes with v3 tail",
          "[membrum][vst][state_v3][StateRoundTripV3]")
{
    Membrum::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);

    auto setup = makeSetup();
    REQUIRE(processor.setupProcessing(setup) == kResultOk);
    REQUIRE(processor.setActive(true) == kResultOk);

    // Prepare a distinct set of Phase 1 / Phase 2 values.
    const double phase1[5]   = {0.21, 0.72, 0.13, 0.84, 0.55};
    const int32  excI32      = 3;  // Friction
    const int32  bodyI32     = 2;  // Shell
    double       phase2[kPhase2FloatCount];
    for (int i = 0; i < kPhase2FloatCount; ++i)
        phase2[i] = 0.10 + i * 0.03;

    // Phase 3 non-default fields.
    const std::uint8_t maxPoly   = 12;
    const std::uint8_t policy    = 1;  // Quietest
    std::array<std::uint8_t, kChokeAssignCount> chokes{};
    chokes.fill(5);

    auto* inStream = new MemoryStream();
    writeV3State(inStream, phase1, excI32, bodyI32, phase2, maxPoly, policy, chokes);
    inStream->seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(processor.setState(inStream) == kResultOk);
    inStream->release();

    // getState must emit a 302-byte v3 blob.
    auto* outStream = new MemoryStream();
    REQUIRE(processor.getState(outStream) == kResultOk);

    int64 endPos = 0;
    outStream->seek(0, IBStream::kIBSeekEnd, &endPos);
    CHECK(endPos == kV3BlobBytes);

    outStream->seek(0, IBStream::kIBSeekSet, nullptr);

    // Version
    int32 readVersion = 0;
    outStream->read(&readVersion, sizeof(readVersion), nullptr);
    CHECK(readVersion == 3);
    CHECK(readVersion == Membrum::kCurrentStateVersion);

    // Phase 1
    for (int i = 0; i < 5; ++i)
    {
        double v = 0.0;
        outStream->read(&v, sizeof(v), nullptr);
        CHECK(v == Approx(phase1[i]).margin(0.0));
    }

    // Selectors
    int32 readExc = 0, readBody = 0;
    outStream->read(&readExc, sizeof(readExc), nullptr);
    outStream->read(&readBody, sizeof(readBody), nullptr);
    CHECK(readExc == excI32);
    CHECK(readBody == bodyI32);

    // Phase 2 floats
    for (int i = 0; i < kPhase2FloatCount; ++i)
    {
        double v = 0.0;
        outStream->read(&v, sizeof(v), nullptr);
        INFO("Phase 2 index=" << i);
        CHECK(v == Approx(phase2[i]).margin(0.0));
    }

    // Phase 3 tail — maxPoly, policy, chokes
    std::uint8_t readMaxPoly = 0;
    std::uint8_t readPolicy  = 0;
    outStream->read(&readMaxPoly, sizeof(readMaxPoly), nullptr);
    outStream->read(&readPolicy, sizeof(readPolicy), nullptr);
    CHECK(static_cast<int>(readMaxPoly) == 12);
    CHECK(static_cast<int>(readPolicy)  == 1);

    for (int i = 0; i < kChokeAssignCount; ++i)
    {
        std::uint8_t b = 0;
        outStream->read(&b, sizeof(b), nullptr);
        INFO("choke index=" << i);
        CHECK(static_cast<int>(b) == 5);
    }

    outStream->release();
    REQUIRE(processor.setActive(false) == kResultOk);
    REQUIRE(processor.terminate() == kResultOk);
}

TEST_CASE("State v3 StateRoundTripV3: extreme / boundary values round-trip",
          "[membrum][vst][state_v3][StateRoundTripV3]")
{
    Membrum::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);

    auto setup = makeSetup();
    REQUIRE(processor.setupProcessing(setup) == kResultOk);
    REQUIRE(processor.setActive(true) == kResultOk);

    const double phase1[5] = {0.5, 0.5, 0.3, 0.3, 0.8};
    const int32  excI32    = 0;
    const int32  bodyI32   = 0;
    double       phase2[kPhase2FloatCount];
    for (int i = 0; i < kPhase2FloatCount; ++i)
        phase2[i] = 0.5;

    const std::uint8_t maxPoly = 4;    // lower bound
    const std::uint8_t policy  = 2;    // Priority
    std::array<std::uint8_t, kChokeAssignCount> chokes{};
    for (int i = 0; i < kChokeAssignCount; ++i)
        chokes[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((i % 2 == 0) ? 0 : 8);

    auto* inStream = new MemoryStream();
    writeV3State(inStream, phase1, excI32, bodyI32, phase2, maxPoly, policy, chokes);
    inStream->seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(processor.setState(inStream) == kResultOk);
    inStream->release();

    auto* outStream = new MemoryStream();
    REQUIRE(processor.getState(outStream) == kResultOk);

    int64 endPos = 0;
    outStream->seek(0, IBStream::kIBSeekEnd, &endPos);
    CHECK(endPos == kV3BlobBytes);

    // Seek to the v3 tail (offset 268) and verify.
    outStream->seek(kV2BodyBytes, IBStream::kIBSeekSet, nullptr);

    std::uint8_t readMaxPoly = 0;
    std::uint8_t readPolicy  = 0;
    outStream->read(&readMaxPoly, sizeof(readMaxPoly), nullptr);
    outStream->read(&readPolicy, sizeof(readPolicy), nullptr);
    CHECK(static_cast<int>(readMaxPoly) == 4);
    CHECK(static_cast<int>(readPolicy)  == 2);

    for (int i = 0; i < kChokeAssignCount; ++i)
    {
        std::uint8_t b = 0;
        outStream->read(&b, sizeof(b), nullptr);
        const int expected = (i % 2 == 0) ? 0 : 8;
        INFO("choke index=" << i);
        CHECK(static_cast<int>(b) == expected);
    }

    outStream->release();
    REQUIRE(processor.setActive(false) == kResultOk);
    REQUIRE(processor.terminate() == kResultOk);
}
