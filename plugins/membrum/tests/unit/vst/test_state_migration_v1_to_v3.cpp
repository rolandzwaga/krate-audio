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

    // getState must now emit a 302-byte v3 blob.
    auto* outStream = new MemoryStream();
    REQUIRE(processor.getState(outStream) == kResultOk);

    int64 endPos = 0;
    outStream->seek(0, IBStream::kIBSeekEnd, &endPos);
    CHECK(endPos == kV3BlobBytes);

    outStream->seek(0, IBStream::kIBSeekSet, nullptr);

    int32 readVersion = 0;
    outStream->read(&readVersion, sizeof(readVersion), nullptr);
    CHECK(readVersion == 3);

    // Phase 1 values must be preserved bit-exactly.
    for (int i = 0; i < 5; ++i)
    {
        double v = 0.0;
        outStream->read(&v, sizeof(v), nullptr);
        INFO("phase 1 index=" << i);
        CHECK(v == Approx(phase1[i]).margin(0.0));
    }

    // Phase 2 selectors default to 0 / 0 (Impulse / Membrane).
    int32 readExc = 99, readBody = 99;
    outStream->read(&readExc, sizeof(readExc), nullptr);
    outStream->read(&readBody, sizeof(readBody), nullptr);
    CHECK(readExc  == 0);
    CHECK(readBody == 0);

    // Phase 2 float defaults (see kPhase2FloatSlots in processor.cpp).
    const double kPhase2Defaults[kPhase2FloatCount] = {
        0.133333,  // exciterFMRatio
        0.0,       // exciterFeedbackAmount
        0.230769,  // exciterNoiseBurstDuration
        0.3,       // exciterFrictionPressure
        0.0,       // toneShaperFilterType
        1.0,       // toneShaperFilterCutoff
        0.0,       // toneShaperFilterResonance
        0.5,       // toneShaperFilterEnvAmount
        0.0,       // toneShaperDriveAmount
        0.0,       // toneShaperFoldAmount
        0.070721,  // toneShaperPitchEnvStart
        0.0,       // toneShaperPitchEnvEnd
        0.0,       // toneShaperPitchEnvTime
        0.0,       // toneShaperPitchEnvCurve
        0.0,       // toneShaperFilterEnvAttack
        0.1,       // toneShaperFilterEnvDecay
        0.0,       // toneShaperFilterEnvSustain
        0.1,       // toneShaperFilterEnvRelease
        0.333333,  // unnaturalModeStretch
        0.5,       // unnaturalDecaySkew
        0.0,       // unnaturalModeInjectAmount
        0.0,       // unnaturalNonlinearCoupling
        0.0,       // morphEnabled
        1.0,       // morphStart
        0.0,       // morphEnd
        0.095477,  // morphDurationMs
        0.0,       // morphCurve
    };
    for (int i = 0; i < kPhase2FloatCount; ++i)
    {
        double v = 0.0;
        outStream->read(&v, sizeof(v), nullptr);
        INFO("phase 2 default index=" << i);
        // The atomics hold float32; the 0.133333 / 0.230769 / 0.070721 /
        // 0.333333 / 0.095477 values round-trip to within ~1e-6.
        CHECK(v == Approx(kPhase2Defaults[i]).margin(1e-5));
    }

    // Phase 3 tail: defaults.
    std::uint8_t readMaxPoly = 0xFF;
    std::uint8_t readPolicy  = 0xFF;
    outStream->read(&readMaxPoly, sizeof(readMaxPoly), nullptr);
    outStream->read(&readPolicy,  sizeof(readPolicy),  nullptr);
    CHECK(static_cast<int>(readMaxPoly) == 8);
    CHECK(static_cast<int>(readPolicy)  == 0);

    for (int i = 0; i < 32; ++i)
    {
        std::uint8_t b = 0xFF;
        outStream->read(&b, sizeof(b), nullptr);
        INFO("choke default index=" << i);
        CHECK(static_cast<int>(b) == 0);
    }

    outStream->release();
    REQUIRE(processor.setActive(false) == kResultOk);
    REQUIRE(processor.terminate() == kResultOk);
}
