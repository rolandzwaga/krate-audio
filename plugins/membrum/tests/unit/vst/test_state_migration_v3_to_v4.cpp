// ==============================================================================
// State v3-to-v4 migration tests (Phase 5 / T027)
// ==============================================================================
// Verifies:
//   - Loading v3 blob succeeds
//   - Phase 3 shared config lands on pad 0
//   - Pads 1-31 receive GM defaults
//   - All output buses default to 0 (main)
//   - selectedPadIndex defaults to 0
//   - Loading v1/v2 blob chains through existing migration and succeeds
//   - Loading unknown future version rejects gracefully
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"
#include "dsp/pad_config.h"
#include "dsp/default_kit.h"
#include "dsp/exciter_type.h"
#include "dsp/body_model_type.h"
#include "unit/vst/v4_state_reader.h"

#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

#include <array>
#include <cstdint>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

namespace {

constexpr int kPhase2FloatCount = 27;
constexpr int kChokeAssignCount = 32;

ProcessSetup makeSetup(double sampleRate = 44100.0)
{
    ProcessSetup setup{};
    setup.processMode        = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = 512;
    setup.sampleRate         = sampleRate;
    return setup;
}

// Build a v3 state blob.
void writeV3State(MemoryStream* stream,
                  const double phase1[5],
                  int32 exciterTypeI32,
                  int32 bodyModelI32,
                  const double phase2Vals[kPhase2FloatCount],
                  std::uint8_t maxPolyphony,
                  std::uint8_t voiceStealingPolicy,
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
    stream->write(&maxPolyphony, sizeof(maxPolyphony), nullptr);
    stream->write(&voiceStealingPolicy, sizeof(voiceStealingPolicy), nullptr);
    for (int i = 0; i < kChokeAssignCount; ++i)
    {
        std::uint8_t b = chokes[static_cast<std::size_t>(i)];
        stream->write(&b, sizeof(b), nullptr);
    }
}

// Build a minimal v1 state blob (version=1, 5 Phase 1 float64 params).
void writeV1State(MemoryStream* stream, const double phase1[5])
{
    int32 version = 1;
    stream->write(&version, sizeof(version), nullptr);
    for (int i = 0; i < 5; ++i)
    {
        double v = phase1[i];
        stream->write(&v, sizeof(v), nullptr);
    }
}

} // namespace

TEST_CASE("Migration v3->v4: Phase 3 shared config lands on pad 0",
          "[membrum][vst][migration_v3_v4]")
{
    Membrum::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);
    auto setup = makeSetup();
    REQUIRE(processor.setupProcessing(setup) == kResultOk);
    REQUIRE(processor.setActive(true) == kResultOk);

    // Write a v3 blob with known Phase 1/2/3 params
    const double phase1[5] = {0.21, 0.72, 0.13, 0.84, 0.55};
    const int32 excI32 = 3;   // Friction
    const int32 bodyI32 = 2;  // Shell

    double phase2[kPhase2FloatCount];
    for (int i = 0; i < kPhase2FloatCount; ++i)
        phase2[i] = 0.10 + i * 0.03;

    const std::uint8_t maxPoly = 12;
    const std::uint8_t policy = 1;
    std::array<std::uint8_t, kChokeAssignCount> chokes{};
    chokes[0] = 3;  // pad 0 choke group

    auto* inStream = new MemoryStream();
    writeV3State(inStream, phase1, excI32, bodyI32, phase2, maxPoly, policy, chokes);
    inStream->seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(processor.setState(inStream) == kResultOk);
    inStream->release();

    const auto& pad0 = processor.voicePoolForTest().padConfig(0);
    CHECK(static_cast<int>(pad0.exciterType) == excI32);
    CHECK(static_cast<int>(pad0.bodyModel) == bodyI32);
    CHECK(pad0.material == Approx(0.21f).margin(0.001f));
    CHECK(pad0.size == Approx(0.72f).margin(0.001f));
    CHECK(pad0.decay == Approx(0.13f).margin(0.001f));
    CHECK(pad0.strikePosition == Approx(0.84f).margin(0.001f));
    CHECK(pad0.level == Approx(0.55f).margin(0.001f));

    // Phase 2 params: first one is FM Ratio at phase2[0]=0.10
    CHECK(pad0.fmRatio == Approx(0.10f).margin(0.001f));

    REQUIRE(processor.setActive(false) == kResultOk);
    REQUIRE(processor.terminate() == kResultOk);
}

TEST_CASE("Migration v3->v4: pads 1-31 receive GM defaults",
          "[membrum][vst][migration_v3_v4]")
{
    Membrum::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);
    auto setup = makeSetup();
    REQUIRE(processor.setupProcessing(setup) == kResultOk);
    REQUIRE(processor.setActive(true) == kResultOk);

    const double phase1[5] = {0.5, 0.5, 0.3, 0.3, 0.8};
    const int32 excI32 = 0;
    const int32 bodyI32 = 0;
    double phase2[kPhase2FloatCount];
    for (int i = 0; i < kPhase2FloatCount; ++i)
        phase2[i] = 0.5;

    std::array<std::uint8_t, kChokeAssignCount> chokes{};

    auto* inStream = new MemoryStream();
    writeV3State(inStream, phase1, excI32, bodyI32, phase2, 8, 0, chokes);
    inStream->seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(processor.setState(inStream) == kResultOk);
    inStream->release();

    // Verify pads 1-31 have GM defaults by checking a few known pads:
    // Pad 1 (MIDI 37) = Side Stick -> Perc template (Mallet, Plate)
    std::array<Membrum::PadConfig, Membrum::kNumPads> gmDefaults;
    Membrum::DefaultKit::apply(gmDefaults);

    for (int p = 1; p < Membrum::kNumPads; ++p)
    {
        const auto& loaded = processor.voicePoolForTest().padConfig(p);
        const auto& expected = gmDefaults[static_cast<std::size_t>(p)];
        INFO("Pad " << p);
        CHECK(static_cast<int>(loaded.exciterType) == static_cast<int>(expected.exciterType));
        CHECK(static_cast<int>(loaded.bodyModel) == static_cast<int>(expected.bodyModel));
        CHECK(loaded.material == Approx(expected.material).margin(1e-5f));
        CHECK(loaded.size == Approx(expected.size).margin(1e-5f));
        CHECK(loaded.decay == Approx(expected.decay).margin(1e-5f));
    }

    REQUIRE(processor.setActive(false) == kResultOk);
    REQUIRE(processor.terminate() == kResultOk);
}

TEST_CASE("Migration v3->v4: all output buses default to 0 and selectedPadIndex to 0",
          "[membrum][vst][migration_v3_v4]")
{
    Membrum::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);
    auto setup = makeSetup();
    REQUIRE(processor.setupProcessing(setup) == kResultOk);
    REQUIRE(processor.setActive(true) == kResultOk);

    const double phase1[5] = {0.5, 0.5, 0.3, 0.3, 0.8};
    double phase2[kPhase2FloatCount];
    for (int i = 0; i < kPhase2FloatCount; ++i)
        phase2[i] = 0.5;
    std::array<std::uint8_t, kChokeAssignCount> chokes{};

    auto* inStream = new MemoryStream();
    writeV3State(inStream, phase1, 0, 0, phase2, 8, 0, chokes);
    inStream->seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(processor.setState(inStream) == kResultOk);
    inStream->release();

    // All pads should have outputBus = 0
    for (int p = 0; p < Membrum::kNumPads; ++p)
    {
        const auto& cfg = processor.voicePoolForTest().padConfig(p);
        INFO("Pad " << p);
        CHECK(cfg.outputBus == 0);
    }

    // selectedPadIndex should be 0
    CHECK(processor.selectedPadIndexForTest() == 0);

    REQUIRE(processor.setActive(false) == kResultOk);
    REQUIRE(processor.terminate() == kResultOk);
}

TEST_CASE("Migration v1->v4: v1 blob loads successfully",
          "[membrum][vst][migration_v3_v4]")
{
    Membrum::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);
    auto setup = makeSetup();
    REQUIRE(processor.setupProcessing(setup) == kResultOk);
    REQUIRE(processor.setActive(true) == kResultOk);

    const double phase1[5] = {0.3, 0.7, 0.2, 0.6, 0.9};

    auto* inStream = new MemoryStream();
    writeV1State(inStream, phase1);
    inStream->seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(processor.setState(inStream) == kResultOk);
    inStream->release();

    // Phase 1 values should land on pad 0
    const auto& pad0 = processor.voicePoolForTest().padConfig(0);
    CHECK(pad0.material == Approx(0.3f).margin(0.001f));
    CHECK(pad0.size == Approx(0.7f).margin(0.001f));
    CHECK(pad0.decay == Approx(0.2f).margin(0.001f));
    CHECK(pad0.strikePosition == Approx(0.6f).margin(0.001f));
    CHECK(pad0.level == Approx(0.9f).margin(0.001f));

    REQUIRE(processor.setActive(false) == kResultOk);
    REQUIRE(processor.terminate() == kResultOk);
}

TEST_CASE("Migration: unknown future version rejected gracefully",
          "[membrum][vst][migration_v3_v4]")
{
    Membrum::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);
    auto setup = makeSetup();
    REQUIRE(processor.setupProcessing(setup) == kResultOk);
    REQUIRE(processor.setActive(true) == kResultOk);

    auto* inStream = new MemoryStream();
    int32 futureVersion = 99;
    inStream->write(&futureVersion, sizeof(futureVersion), nullptr);
    inStream->seek(0, IBStream::kIBSeekSet, nullptr);

    CHECK(processor.setState(inStream) == kResultFalse);
    inStream->release();

    REQUIRE(processor.setActive(false) == kResultOk);
    REQUIRE(processor.terminate() == kResultOk);
}
