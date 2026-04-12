// ==============================================================================
// State v4 round-trip tests (Phase 5 / T026)
// ==============================================================================
// Verifies:
//   - getState() writes version=4 header + globals + 32 pad configs + selectedPadIndex
//   - setState() round-trips all 32 pad configs bit-exactly
//   - Total blob size = 9040 bytes (12 header + 32*282 + 4)
//   - selectedPadIndex round-trips
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"
#include "dsp/pad_config.h"
#include "dsp/default_kit.h"
#include "unit/vst/v4_state_reader.h"

#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

#include <array>
#include <cstdint>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

namespace {

// Phase 5: state bumped to v5 = v4 (9040) + 4*8 (global coupling) + 32*8 (per-pad) + 2 (overrideCount)
constexpr int64 kV4BlobBytes = 9040 + 32 + 256 + 2;  // 9330 bytes for v5

ProcessSetup makeSetup(double sampleRate = 44100.0)
{
    ProcessSetup setup{};
    setup.processMode        = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = 512;
    setup.sampleRate         = sampleRate;
    return setup;
}

} // namespace

TEST_CASE("State v4: getState emits exactly 9040 bytes",
          "[membrum][vst][state_v4]")
{
    Membrum::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);

    auto setup = makeSetup();
    REQUIRE(processor.setupProcessing(setup) == kResultOk);
    REQUIRE(processor.setActive(true) == kResultOk);

    auto* stream = new MemoryStream();
    REQUIRE(processor.getState(stream) == kResultOk);

    // Check total size
    int64 pos = 0;
    stream->tell(&pos);
    CHECK(pos == kV4BlobBytes);

    stream->release();
    REQUIRE(processor.setActive(false) == kResultOk);
    REQUIRE(processor.terminate() == kResultOk);
}

TEST_CASE("State v4: getState writes version=4 header and global settings",
          "[membrum][vst][state_v4]")
{
    Membrum::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);

    auto setup = makeSetup();
    REQUIRE(processor.setupProcessing(setup) == kResultOk);
    REQUIRE(processor.setActive(true) == kResultOk);

    auto* stream = new MemoryStream();
    REQUIRE(processor.getState(stream) == kResultOk);

    Membrum::TestHelpers::V4StateHeader hdr;
    REQUIRE(Membrum::TestHelpers::readV4Header(stream, hdr));

    // Phase 5: state version bumped to 5 (v5 = v4 layout + Phase 5 appended data)
    CHECK(hdr.version == 5);
    CHECK(hdr.maxPolyphony == 8);   // default
    CHECK(hdr.stealPolicy == 0);    // default (Oldest)

    stream->release();
    REQUIRE(processor.setActive(false) == kResultOk);
    REQUIRE(processor.terminate() == kResultOk);
}

TEST_CASE("State v4: round-trip all 32 pad configs bit-exactly",
          "[membrum][vst][state_v4]")
{
    // Set up processor with distinct per-pad values
    Membrum::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);
    auto setup = makeSetup();
    REQUIRE(processor.setupProcessing(setup) == kResultOk);
    REQUIRE(processor.setActive(true) == kResultOk);

    // Configure pads with distinct values via VoicePool
    // (We set pad 0 and pad 31 with known values and check they round-trip)
    auto& pads = processor.voicePoolForTest().padConfigsArray();
    for (int p = 0; p < Membrum::kNumPads; ++p)
    {
        auto& cfg = pads[static_cast<std::size_t>(p)];
        cfg.exciterType = static_cast<Membrum::ExciterType>(p % 6);
        cfg.bodyModel = static_cast<Membrum::BodyModelType>((p + 1) % 6);
        cfg.material = 0.01f * static_cast<float>(p);
        cfg.size = 0.02f * static_cast<float>(p);
        cfg.decay = 0.03f * static_cast<float>(p);
        cfg.strikePosition = 0.01f * static_cast<float>(p + 5);
        cfg.level = 0.8f;
        cfg.tsFilterCutoff = 0.5f + 0.01f * static_cast<float>(p);
        cfg.fmRatio = 0.1f * static_cast<float>(p % 10);
        cfg.feedbackAmount = 0.05f * static_cast<float>(p % 20);
        cfg.noiseBurstDuration = 0.1f + 0.01f * static_cast<float>(p);
        cfg.frictionPressure = 0.2f + 0.01f * static_cast<float>(p);
        cfg.chokeGroup = static_cast<std::uint8_t>(p % 9);
        cfg.outputBus = static_cast<std::uint8_t>(p % 16);
    }

    // Save state
    auto* outStream = new MemoryStream();
    REQUIRE(processor.getState(outStream) == kResultOk);

    // Load state into a fresh processor
    Membrum::Processor processor2;
    REQUIRE(processor2.initialize(nullptr) == kResultOk);
    auto setup2 = makeSetup();
    REQUIRE(processor2.setupProcessing(setup2) == kResultOk);
    REQUIRE(processor2.setActive(true) == kResultOk);

    outStream->seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(processor2.setState(outStream) == kResultOk);

    // Verify all 32 pads match
    for (int p = 0; p < Membrum::kNumPads; ++p)
    {
        const auto& orig = pads[static_cast<std::size_t>(p)];
        const auto& loaded = processor2.voicePoolForTest().padConfig(p);

        INFO("Pad " << p);
        CHECK(static_cast<int>(loaded.exciterType) == static_cast<int>(orig.exciterType));
        CHECK(static_cast<int>(loaded.bodyModel) == static_cast<int>(orig.bodyModel));
        CHECK(loaded.material == Approx(orig.material).margin(1e-6));
        CHECK(loaded.size == Approx(orig.size).margin(1e-6));
        CHECK(loaded.decay == Approx(orig.decay).margin(1e-6));
        CHECK(loaded.strikePosition == Approx(orig.strikePosition).margin(1e-6));
        CHECK(loaded.level == Approx(orig.level).margin(1e-6));
        CHECK(loaded.tsFilterCutoff == Approx(orig.tsFilterCutoff).margin(1e-6));
        CHECK(loaded.fmRatio == Approx(orig.fmRatio).margin(1e-6));
        CHECK(loaded.feedbackAmount == Approx(orig.feedbackAmount).margin(1e-6));
        CHECK(loaded.noiseBurstDuration == Approx(orig.noiseBurstDuration).margin(1e-6));
        CHECK(loaded.frictionPressure == Approx(orig.frictionPressure).margin(1e-6));
        CHECK(loaded.chokeGroup == orig.chokeGroup);
        CHECK(loaded.outputBus == orig.outputBus);
    }

    outStream->release();
    REQUIRE(processor2.setActive(false) == kResultOk);
    REQUIRE(processor2.terminate() == kResultOk);
    REQUIRE(processor.setActive(false) == kResultOk);
    REQUIRE(processor.terminate() == kResultOk);
}

TEST_CASE("State v4: selectedPadIndex round-trips",
          "[membrum][vst][state_v4]")
{
    Membrum::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);
    auto setup = makeSetup();
    REQUIRE(processor.setupProcessing(setup) == kResultOk);
    REQUIRE(processor.setActive(true) == kResultOk);

    // Set selectedPadIndex to 17 via the state blob
    processor.setSelectedPadIndexForTest(17);

    auto* outStream = new MemoryStream();
    REQUIRE(processor.getState(outStream) == kResultOk);

    // Read back the selectedPadIndex from the stream
    outStream->seek(0, IBStream::kIBSeekSet, nullptr);
    Membrum::TestHelpers::V4StateHeader hdr;
    REQUIRE(Membrum::TestHelpers::readV4Header(outStream, hdr));

    // Skip 32 pad data blocks
    for (int p = 0; p < Membrum::kNumPads; ++p)
    {
        Membrum::TestHelpers::V4PadData padData;
        REQUIRE(Membrum::TestHelpers::readV4Pad(outStream, padData));
    }

    // Read selectedPadIndex
    int32 selPad = 0;
    REQUIRE(outStream->read(&selPad, sizeof(selPad), nullptr) == kResultOk);
    CHECK(selPad == 17);

    // Round-trip: load into fresh processor
    Membrum::Processor processor2;
    REQUIRE(processor2.initialize(nullptr) == kResultOk);
    auto setup2 = makeSetup();
    REQUIRE(processor2.setupProcessing(setup2) == kResultOk);
    REQUIRE(processor2.setActive(true) == kResultOk);

    outStream->seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(processor2.setState(outStream) == kResultOk);

    CHECK(processor2.selectedPadIndexForTest() == 17);

    outStream->release();
    REQUIRE(processor2.setActive(false) == kResultOk);
    REQUIRE(processor2.terminate() == kResultOk);
    REQUIRE(processor.setActive(false) == kResultOk);
    REQUIRE(processor.terminate() == kResultOk);
}
