// ==============================================================================
// State corruption clamp test (T3.4.4 / FR-144)
// ==============================================================================
// Feeds a v3 blob containing corrupt Phase 3 tail values into a Phase 3
// processor and verifies:
//   (a) setState returns kResultOk (no rejection)
//   (b) maxPolyphony is clamped to 16 (from 99)
//   (c) voiceStealingPolicy is clamped to 0 (from 200)
//   (d) chokeGroupAssignments[3] is clamped to 0 (from 250)
//   (e) no crash
//   (f) a subsequent getState() produces a valid 302-byte blob containing the
//       clamped values.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"

#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

#include <array>
#include <cstdint>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

constexpr int kPhase2FloatCount = 27;
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

} // namespace

TEST_CASE("State v3 StateMigration corruption clamp: out-of-range values are clamped",
          "[membrum][vst][state_v3][StateMigration]")
{
    Membrum::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);

    auto setup = makeSetup();
    REQUIRE(processor.setupProcessing(setup) == kResultOk);
    REQUIRE(processor.setActive(true) == kResultOk);

    auto* inStream = new MemoryStream();

    // Version 3
    int32 version = 3;
    inStream->write(&version, sizeof(version), nullptr);

    // Phase 1 -- 5 float64 defaults (values chosen arbitrarily but valid).
    const double phase1[5] = {0.5, 0.5, 0.3, 0.3, 0.8};
    for (double v : phase1)
        inStream->write(&v, sizeof(v), nullptr);

    // Selectors (0 / 0).
    int32 excI32 = 0, bodyI32 = 0;
    inStream->write(&excI32, sizeof(excI32), nullptr);
    inStream->write(&bodyI32, sizeof(bodyI32), nullptr);

    // Phase 2 floats (arbitrary valid values).
    for (int i = 0; i < kPhase2FloatCount; ++i)
    {
        double v = 0.25;
        inStream->write(&v, sizeof(v), nullptr);
    }

    // Phase 3 tail -- CORRUPT
    std::uint8_t maxPolyCorrupt = 99;
    std::uint8_t policyCorrupt  = 200;
    inStream->write(&maxPolyCorrupt, sizeof(maxPolyCorrupt), nullptr);
    inStream->write(&policyCorrupt,  sizeof(policyCorrupt),  nullptr);

    // Choke tail -- index 3 corrupt at 250, others valid (0..5 mix).
    std::array<std::uint8_t, 32> chokes{};
    for (int i = 0; i < 32; ++i)
        chokes[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i % 4);
    chokes[3] = 250;
    for (int i = 0; i < 32; ++i)
    {
        std::uint8_t b = chokes[static_cast<std::size_t>(i)];
        inStream->write(&b, sizeof(b), nullptr);
    }

    inStream->seek(0, IBStream::kIBSeekSet, nullptr);

    // (a) setState must succeed.
    REQUIRE(processor.setState(inStream) == kResultOk);
    inStream->release();

    // (f) getState must emit a 302-byte v3 blob.
    auto* outStream = new MemoryStream();
    REQUIRE(processor.getState(outStream) == kResultOk);

    int64 endPos = 0;
    outStream->seek(0, IBStream::kIBSeekEnd, &endPos);
    CHECK(endPos == kV3BlobBytes);

    // Skip straight to the v3 tail.
    outStream->seek(kV2BodyBytes, IBStream::kIBSeekSet, nullptr);

    std::uint8_t readMaxPoly = 0xFF;
    std::uint8_t readPolicy  = 0xFF;
    outStream->read(&readMaxPoly, sizeof(readMaxPoly), nullptr);
    outStream->read(&readPolicy,  sizeof(readPolicy),  nullptr);

    // (b) maxPoly 99 -> 16
    CHECK(static_cast<int>(readMaxPoly) == 16);
    // (c) policy 200 -> 0
    CHECK(static_cast<int>(readPolicy)  == 0);

    // (d) chokes[3] -> 0; others preserved at (i%4)
    for (int i = 0; i < 32; ++i)
    {
        std::uint8_t b = 0xFF;
        outStream->read(&b, sizeof(b), nullptr);
        const int expected = (i == 3) ? 0 : (i % 4);
        INFO("choke index=" << i);
        CHECK(static_cast<int>(b) == expected);
    }

    outStream->release();
    REQUIRE(processor.setActive(false) == kResultOk);
    REQUIRE(processor.terminate() == kResultOk);
}
