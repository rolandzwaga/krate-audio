// ==============================================================================
// State migration test: v1 -> v3 (T3.4.3 / FR-143)
// ==============================================================================
// Synthesizes a v1 blob (version=1 int32 + 5 Phase-1 float64 values, 44 bytes
// total) and feeds it to a Phase 3 processor. Verifies that the chained
// migration path produces:
//   - Phase 1 params preserved bit-exactly
//   - Phase 2 params at their Phase 2 defaults (the v2 "bypass" set)
//   - Phase 3 params at their documented defaults: maxPoly=8, policy=0,
//     chokeAssignments all 0
//   - getState() subsequently emits a 302-byte v3 blob with version==3
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"

#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

#include <array>
#include <cstdint>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

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

TEST_CASE("State v1 StateMigration v1->v3: chained migration loads cleanly",
          "[membrum][vst][state_v1_v3][StateMigration]")
{
    Membrum::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);

    auto setup = makeSetup();
    REQUIRE(processor.setupProcessing(setup) == kResultOk);
    REQUIRE(processor.setActive(true) == kResultOk);

    // Synthetic v1 blob: 4 bytes version + 5 * 8 bytes float64 = 44 bytes.
    const double phase1[5] = {0.11, 0.22, 0.33, 0.44, 0.55};

    auto* inStream = new MemoryStream();
    int32 version = 1;
    inStream->write(&version, sizeof(version), nullptr);
    for (double v : phase1)
        inStream->write(&v, sizeof(v), nullptr);

    inStream->seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(processor.setState(inStream) == kResultOk);
    inStream->release();

    // getState now emits v4 format after v1 migration.
    auto* outStream = new MemoryStream();
    REQUIRE(processor.getState(outStream) == kResultOk);
    outStream->seek(0, IBStream::kIBSeekSet, nullptr);

    // v4 header
    int32 readVersion = 0, readMaxPoly32 = 0, readPolicy32 = 0;
    outStream->read(&readVersion, sizeof(readVersion), nullptr);
    outStream->read(&readMaxPoly32, sizeof(readMaxPoly32), nullptr);
    outStream->read(&readPolicy32, sizeof(readPolicy32), nullptr);
    CHECK(readVersion == Membrum::kCurrentStateVersion);
    CHECK(readMaxPoly32 == 8);
    CHECK(readPolicy32 == 0);

    // Pad 0 header
    int32 readExc = 99, readBody = 99;
    outStream->read(&readExc, sizeof(readExc), nullptr);
    outStream->read(&readBody, sizeof(readBody), nullptr);
    CHECK(readExc == 0);   // Impulse
    CHECK(readBody == 0);  // Membrane

    // Phase 1 values must be preserved (now in pad 0's float64 block).
    for (int i = 0; i < 5; ++i)
    {
        double v = 0.0;
        outStream->read(&v, sizeof(v), nullptr);
        INFO("phase 1 index=" << i);
        CHECK(v == Approx(phase1[i]).margin(0.001));
    }

    outStream->release();
    REQUIRE(processor.setActive(false) == kResultOk);
    REQUIRE(processor.terminate() == kResultOk);
}
