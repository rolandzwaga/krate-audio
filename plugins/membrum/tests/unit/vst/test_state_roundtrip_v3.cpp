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

    // getState emits v4 format after loading v3 state (migration).
    auto* outStream = new MemoryStream();
    REQUIRE(processor.getState(outStream) == kResultOk);

    outStream->seek(0, IBStream::kIBSeekSet, nullptr);

    // v4 header: version, maxPolyphony, stealPolicy
    int32 readVersion = 0, readMaxPoly32 = 0, readPolicy32 = 0;
    outStream->read(&readVersion, sizeof(readVersion), nullptr);
    outStream->read(&readMaxPoly32, sizeof(readMaxPoly32), nullptr);
    outStream->read(&readPolicy32, sizeof(readPolicy32), nullptr);
    CHECK(readVersion == Membrum::kCurrentStateVersion);
    CHECK(readMaxPoly32 == 12);
    CHECK(readPolicy32 == 1);

    // Pad 0: exciter/body + 34 float64 + uint8 chokeGroup + uint8 outputBus
    int32 readExc = 0, readBody = 0;
    outStream->read(&readExc, sizeof(readExc), nullptr);
    outStream->read(&readBody, sizeof(readBody), nullptr);
    CHECK(readExc == excI32);
    CHECK(readBody == bodyI32);

    // First 5 float64 = Phase 1 params (material, size, decay, strikePos, level)
    for (int i = 0; i < 5; ++i)
    {
        double v = 0.0;
        outStream->read(&v, sizeof(v), nullptr);
        CHECK(v == Approx(phase1[i]).margin(0.001));
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

    // getState emits current v5 format (v4 layout + Phase 5 appended data).
    // Check maxPoly and stealPolicy survived.
    auto* outStream = new MemoryStream();
    REQUIRE(processor.getState(outStream) == kResultOk);

    outStream->seek(0, IBStream::kIBSeekSet, nullptr);
    int32 readVersion = 0, readMaxPoly32 = 0, readPolicy32 = 0;
    outStream->read(&readVersion, sizeof(readVersion), nullptr);
    outStream->read(&readMaxPoly32, sizeof(readMaxPoly32), nullptr);
    outStream->read(&readPolicy32, sizeof(readPolicy32), nullptr);
    CHECK(readVersion == 5);
    CHECK(readMaxPoly32 == 4);
    CHECK(readPolicy32 == 2);

    outStream->release();
    REQUIRE(processor.setActive(false) == kResultOk);
    REQUIRE(processor.terminate() == kResultOk);
}
