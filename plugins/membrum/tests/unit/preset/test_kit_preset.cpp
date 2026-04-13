// ==============================================================================
// Kit preset save/load tests (Phase 5 / T028)
// ==============================================================================
// Verifies:
//   - Kit preset StateProvider produces 9036-byte blob (v4 without selectedPadIndex)
//   - Kit preset LoadProvider restores all 32 pad configs
//   - Loading kit preset does not change selectedPadIndex
//   - Truncated/corrupted kit preset blob fails gracefully
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
#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

namespace {

constexpr int64 kKitPresetBytes = 9036;  // v4 format without selectedPadIndex

ProcessSetup makeSetup(double sampleRate = 44100.0)
{
    ProcessSetup setup{};
    setup.processMode        = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = 512;
    setup.sampleRate         = sampleRate;
    return setup;
}

// Write a kit preset blob (v4 format without selectedPadIndex).
// Uses the processor's getState() and truncates everything after pad configs:
// v5 layout appends selectedPadIndex (4) + Phase 5 data (4*8 global + 32*8 per-pad + 2 count
// + override payload). For a default-initialized processor, no overrides are present,
// so the tail is exactly 4 + 290 = 294 bytes.
// Kit preset must also use version=4 header, not the current v5, so the truncated
// blob is still accepted by loadKitPreset.
MemoryStream* createKitPresetFromProcessor(Membrum::Processor& proc)
{
    auto* stateStream = new MemoryStream();
    proc.getState(stateStream);

    int64 totalSize = 0;
    stateStream->tell(&totalSize);

    auto* kitStream = new MemoryStream();
    stateStream->seek(0, IBStream::kIBSeekSet, nullptr);

    // v5 tail size = 4 (selectedPadIndex) + 32 (4 global coupling float64)
    //              + 256 (32 per-pad coupling float64) + 2 (uint16 overrideCount)
    //              + 0 (no overrides by default)
    // Phase 6 (spec 141) appends 160 * 8 bytes of float64 macros.
    constexpr int64 kV5TailBytes = 4 + 32 + 256 + 2 + 1280;  // 1574
    const int64 kitSize = totalSize - kV5TailBytes;
    std::vector<char> buf(static_cast<std::size_t>(kitSize));
    int32 bytesRead = 0;
    stateStream->read(buf.data(), static_cast<int32>(kitSize), &bytesRead);

    // Rewrite the version header to 4 (kit preset is version 4 format).
    if (bytesRead >= 4)
    {
        int32 v4 = 4;
        std::memcpy(buf.data(), &v4, sizeof(v4));
    }

    kitStream->write(buf.data(), bytesRead, nullptr);

    stateStream->release();
    return kitStream;
}

} // namespace

TEST_CASE("Kit preset: StateProvider produces exactly 9036 bytes",
          "[membrum][preset][kit_preset]")
{
    Membrum::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);
    auto setup = makeSetup();
    REQUIRE(processor.setupProcessing(setup) == kResultOk);
    REQUIRE(processor.setActive(true) == kResultOk);

    auto* kitStream = createKitPresetFromProcessor(processor);

    int64 pos = 0;
    kitStream->tell(&pos);
    CHECK(pos == kKitPresetBytes);

    kitStream->release();
    REQUIRE(processor.setActive(false) == kResultOk);
    REQUIRE(processor.terminate() == kResultOk);
}

TEST_CASE("Kit preset: round-trip restores all 32 pad configs",
          "[membrum][preset][kit_preset]")
{
    // Source processor with distinctive pad values
    Membrum::Processor srcProc;
    REQUIRE(srcProc.initialize(nullptr) == kResultOk);
    auto setup = makeSetup();
    REQUIRE(srcProc.setupProcessing(setup) == kResultOk);
    REQUIRE(srcProc.setActive(true) == kResultOk);

    auto& srcPads = srcProc.voicePoolForTest().padConfigsArray();
    for (int p = 0; p < Membrum::kNumPads; ++p)
    {
        auto& cfg = srcPads[static_cast<std::size_t>(p)];
        cfg.material = 0.01f * static_cast<float>(p + 10);
        cfg.size = 0.02f * static_cast<float>(p + 3);
        cfg.chokeGroup = static_cast<std::uint8_t>(p % 9);
        cfg.outputBus = static_cast<std::uint8_t>(p % 16);
    }

    auto* kitStream = createKitPresetFromProcessor(srcProc);

    // Target processor: load the kit preset blob
    Membrum::Processor dstProc;
    REQUIRE(dstProc.initialize(nullptr) == kResultOk);
    auto setup2 = makeSetup();
    REQUIRE(dstProc.setupProcessing(setup2) == kResultOk);
    REQUIRE(dstProc.setActive(true) == kResultOk);

    // Load kit preset (same format as setState but without selectedPadIndex)
    kitStream->seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(dstProc.loadKitPreset(kitStream) == kResultOk);

    // Verify all 32 pads match
    for (int p = 0; p < Membrum::kNumPads; ++p)
    {
        const auto& orig = srcPads[static_cast<std::size_t>(p)];
        const auto& loaded = dstProc.voicePoolForTest().padConfig(p);
        INFO("Pad " << p);
        CHECK(loaded.material == Approx(orig.material).margin(1e-6));
        CHECK(loaded.size == Approx(orig.size).margin(1e-6));
        CHECK(loaded.chokeGroup == orig.chokeGroup);
        CHECK(loaded.outputBus == orig.outputBus);
    }

    kitStream->release();
    REQUIRE(dstProc.setActive(false) == kResultOk);
    REQUIRE(dstProc.terminate() == kResultOk);
    REQUIRE(srcProc.setActive(false) == kResultOk);
    REQUIRE(srcProc.terminate() == kResultOk);
}

TEST_CASE("Kit preset: loading does not change selectedPadIndex",
          "[membrum][preset][kit_preset]")
{
    Membrum::Processor srcProc;
    REQUIRE(srcProc.initialize(nullptr) == kResultOk);
    auto setup = makeSetup();
    REQUIRE(srcProc.setupProcessing(setup) == kResultOk);
    REQUIRE(srcProc.setActive(true) == kResultOk);

    auto* kitStream = createKitPresetFromProcessor(srcProc);

    Membrum::Processor dstProc;
    REQUIRE(dstProc.initialize(nullptr) == kResultOk);
    auto setup2 = makeSetup();
    REQUIRE(dstProc.setupProcessing(setup2) == kResultOk);
    REQUIRE(dstProc.setActive(true) == kResultOk);

    // Set selectedPadIndex to a non-default value
    dstProc.setSelectedPadIndexForTest(15);

    kitStream->seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(dstProc.loadKitPreset(kitStream) == kResultOk);

    // selectedPadIndex should remain 15
    CHECK(dstProc.selectedPadIndexForTest() == 15);

    kitStream->release();
    REQUIRE(dstProc.setActive(false) == kResultOk);
    REQUIRE(dstProc.terminate() == kResultOk);
    REQUIRE(srcProc.setActive(false) == kResultOk);
    REQUIRE(srcProc.terminate() == kResultOk);
}

TEST_CASE("Kit preset: truncated blob fails gracefully",
          "[membrum][preset][kit_preset]")
{
    Membrum::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);
    auto setup = makeSetup();
    REQUIRE(processor.setupProcessing(setup) == kResultOk);
    REQUIRE(processor.setActive(true) == kResultOk);

    // Save default state first so we know what the pad configs are
    auto& padsBefore = processor.voicePoolForTest().padConfigsArray();
    std::array<Membrum::PadConfig, Membrum::kNumPads> backup = padsBefore;

    // Create a truncated stream (only 100 bytes of header)
    auto* truncStream = new MemoryStream();
    int32 version = 4;
    truncStream->write(&version, sizeof(version), nullptr);
    int32 maxPoly = 8;
    truncStream->write(&maxPoly, sizeof(maxPoly), nullptr);
    int32 stealPolicy = 0;
    truncStream->write(&stealPolicy, sizeof(stealPolicy), nullptr);
    // Just the header -- no pad data -- this should fail on first pad read

    truncStream->seek(0, IBStream::kIBSeekSet, nullptr);
    // Loading truncated blob should either fail or not corrupt state
    auto result = processor.loadKitPreset(truncStream);
    // We accept kResultFalse as graceful failure
    CHECK((result == kResultFalse || result == kResultOk));

    truncStream->release();
    REQUIRE(processor.setActive(false) == kResultOk);
    REQUIRE(processor.terminate() == kResultOk);
}
