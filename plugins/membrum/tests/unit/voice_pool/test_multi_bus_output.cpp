// ==============================================================================
// Phase 4 / Phase 7: Multi-bus output routing tests
// ==============================================================================
// Tests that VoicePool::processBlock correctly routes pad audio to main and
// auxiliary output buses based on pad outputBus assignment and bus active state.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "voice_pool/voice_pool.h"
#include "voice_pool_test_helpers.h"
#include "dsp/pad_config.h"

#include <array>
#include <cmath>
#include <cstring>
#include <numeric>

namespace {

using namespace Membrum;

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 256;

// Compute RMS of a buffer
float computeRMS(const float* buf, int n)
{
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
        sum += static_cast<double>(buf[i]) * static_cast<double>(buf[i]);
    return static_cast<float>(std::sqrt(sum / static_cast<double>(n)));
}

// Check if buffer is silent (all zeros)
bool isSilent(const float* buf, int n)
{
    for (int i = 0; i < n; ++i)
    {
        if (buf[i] != 0.0f)
            return false;
    }
    return true;
}

// Helper struct to manage multi-bus buffers for testing
struct MultiBusBuffers
{
    static constexpr int kMaxBuses = 16;

    // Main output
    std::array<float, kBlockSize> mainL{};
    std::array<float, kBlockSize> mainR{};

    // Aux outputs (buses 1-15)
    std::array<std::array<float, kBlockSize>, kMaxBuses> auxLBufs{};
    std::array<std::array<float, kBlockSize>, kMaxBuses> auxRBufs{};

    // Pointer arrays for VoicePool
    std::array<float*, kMaxBuses> auxLPtrs{};
    std::array<float*, kMaxBuses> auxRPtrs{};

    // Bus active flags
    std::array<bool, kMaxBuses> busActive{};

    MultiBusBuffers()
    {
        clear();
        busActive[0] = true; // main always active
        for (int i = 0; i < kMaxBuses; ++i)
        {
            auxLPtrs[i] = auxLBufs[i].data();
            auxRPtrs[i] = auxRBufs[i].data();
        }
    }

    void clear()
    {
        mainL.fill(0.0f);
        mainR.fill(0.0f);
        for (auto& b : auxLBufs) b.fill(0.0f);
        for (auto& b : auxRBufs) b.fill(0.0f);
        busActive.fill(false);
        busActive[0] = true;
    }
};

// Prepare a VoicePool with standard settings for multi-bus testing
void preparePool(VoicePool& pool)
{
    pool.prepare(kSampleRate, kBlockSize);
    // Use default kit configs -- we just need voices to produce non-zero output
    TestHelpers::setAllPadsVoiceParams(pool, 0.5f, 0.5f, 0.5f, 0.3f, 0.8f);
    TestHelpers::setAllPadsExciterType(pool, ExciterType::Impulse);
    TestHelpers::setAllPadsBodyModel(pool, BodyModelType::Membrane);
}

} // namespace

TEST_CASE("Multi-bus: pad on bus 0 writes to main only", "[multi-bus]")
{
    using namespace Membrum;

    VoicePool pool;
    preparePool(pool);

    // Pad 0 (MIDI 36) assigned to bus 0 (default)
    pool.setPadConfigField(0, kPadOutputBus, 0.0f);

    MultiBusBuffers bufs;
    bufs.busActive[1] = true; // activate bus 1 (should remain silent)
    bufs.busActive[2] = true; // activate bus 2 (should remain silent)

    // Trigger pad 0 (MIDI 36)
    pool.noteOn(36, 0.9f);

    // Process
    pool.processBlock(bufs.mainL.data(), bufs.mainR.data(),
                      bufs.auxLPtrs.data(), bufs.auxRPtrs.data(),
                      bufs.busActive.data(), MultiBusBuffers::kMaxBuses, kBlockSize);

    // Main should have audio
    REQUIRE(computeRMS(bufs.mainL.data(), kBlockSize) > 0.0001f);

    // Aux buses 1 and 2 should be silent
    CHECK(isSilent(bufs.auxLBufs[1].data(), kBlockSize));
    CHECK(isSilent(bufs.auxRBufs[1].data(), kBlockSize));
    CHECK(isSilent(bufs.auxLBufs[2].data(), kBlockSize));
    CHECK(isSilent(bufs.auxRBufs[2].data(), kBlockSize));
}

TEST_CASE("Multi-bus: pad on bus 2 writes to both main and bus 2", "[multi-bus]")
{
    using namespace Membrum;

    VoicePool pool;
    preparePool(pool);

    // Pad 0 (MIDI 36) assigned to bus 2
    // outputBus normalized: 2/15 = 0.1333...
    pool.setPadConfigField(0, kPadOutputBus, 2.0f / 15.0f);

    MultiBusBuffers bufs;
    bufs.busActive[2] = true;

    pool.noteOn(36, 0.9f);

    pool.processBlock(bufs.mainL.data(), bufs.mainR.data(),
                      bufs.auxLPtrs.data(), bufs.auxRPtrs.data(),
                      bufs.busActive.data(), MultiBusBuffers::kMaxBuses, kBlockSize);

    // Main should have audio
    float mainRMS = computeRMS(bufs.mainL.data(), kBlockSize);
    REQUIRE(mainRMS > 0.0001f);

    // Bus 2 should also have audio
    float bus2RMS = computeRMS(bufs.auxLBufs[2].data(), kBlockSize);
    REQUIRE(bus2RMS > 0.0001f);

    // Bus 1 should be silent
    CHECK(isSilent(bufs.auxLBufs[1].data(), kBlockSize));
}

TEST_CASE("Multi-bus: inactive bus receives silence", "[multi-bus]")
{
    using namespace Membrum;

    VoicePool pool;
    preparePool(pool);

    // Pad 0 assigned to bus 3, but bus 3 is NOT active
    pool.setPadConfigField(0, kPadOutputBus, 3.0f / 15.0f);

    MultiBusBuffers bufs;
    // busActive[3] = false (default)

    pool.noteOn(36, 0.9f);

    pool.processBlock(bufs.mainL.data(), bufs.mainR.data(),
                      bufs.auxLPtrs.data(), bufs.auxRPtrs.data(),
                      bufs.busActive.data(), MultiBusBuffers::kMaxBuses, kBlockSize);

    // Main should have audio (pad always goes to main)
    REQUIRE(computeRMS(bufs.mainL.data(), kBlockSize) > 0.0001f);

    // Bus 3 should be silent (inactive)
    CHECK(isSilent(bufs.auxLBufs[3].data(), kBlockSize));
    CHECK(isSilent(bufs.auxRBufs[3].data(), kBlockSize));
}

TEST_CASE("Multi-bus: multiple pads on different buses", "[multi-bus]")
{
    using namespace Membrum;

    VoicePool pool;
    preparePool(pool);

    // Pad 0 (MIDI 36) -> bus 1
    pool.setPadConfigField(0, kPadOutputBus, 1.0f / 15.0f);
    // Pad 2 (MIDI 38) -> bus 2
    pool.setPadConfigField(2, kPadOutputBus, 2.0f / 15.0f);
    // Pad 6 (MIDI 42) -> bus 0 (main only)
    pool.setPadConfigField(6, kPadOutputBus, 0.0f);

    MultiBusBuffers bufs;
    bufs.busActive[1] = true;
    bufs.busActive[2] = true;

    pool.noteOn(36, 0.9f);
    pool.noteOn(38, 0.9f);
    pool.noteOn(42, 0.9f);

    pool.processBlock(bufs.mainL.data(), bufs.mainR.data(),
                      bufs.auxLPtrs.data(), bufs.auxRPtrs.data(),
                      bufs.busActive.data(), MultiBusBuffers::kMaxBuses, kBlockSize);

    // Main should have audio from all three
    REQUIRE(computeRMS(bufs.mainL.data(), kBlockSize) > 0.0001f);

    // Bus 1 should have audio (pad 0)
    REQUIRE(computeRMS(bufs.auxLBufs[1].data(), kBlockSize) > 0.0001f);

    // Bus 2 should have audio (pad 2)
    REQUIRE(computeRMS(bufs.auxLBufs[2].data(), kBlockSize) > 0.0001f);

    // Buses 3-15 should be silent
    for (int b = 3; b < MultiBusBuffers::kMaxBuses; ++b)
    {
        CHECK(isSilent(bufs.auxLBufs[b].data(), kBlockSize));
    }
}

TEST_CASE("Multi-bus: all aux inactive matches Phase 3 behavior", "[multi-bus]")
{
    using namespace Membrum;

    VoicePool pool;
    preparePool(pool);

    // Pad 0 assigned to bus 2, but no aux buses active
    pool.setPadConfigField(0, kPadOutputBus, 2.0f / 15.0f);

    // First: render with multi-bus (all aux inactive)
    MultiBusBuffers bufs;
    pool.noteOn(36, 0.9f);

    pool.processBlock(bufs.mainL.data(), bufs.mainR.data(),
                      bufs.auxLPtrs.data(), bufs.auxRPtrs.data(),
                      bufs.busActive.data(), MultiBusBuffers::kMaxBuses, kBlockSize);

    float multiBusMainRMS = computeRMS(bufs.mainL.data(), kBlockSize);
    REQUIRE(multiBusMainRMS > 0.0001f);

    // All aux buses should be silent
    for (int b = 1; b < MultiBusBuffers::kMaxBuses; ++b)
    {
        CHECK(isSilent(bufs.auxLBufs[b].data(), kBlockSize));
    }
}

TEST_CASE("Multi-bus: RMS of unassigned aux bus is zero", "[multi-bus]")
{
    using namespace Membrum;

    VoicePool pool;
    preparePool(pool);

    // Pad 0 -> bus 1 (active), pad 2 -> bus 0 (main)
    pool.setPadConfigField(0, kPadOutputBus, 1.0f / 15.0f);
    pool.setPadConfigField(2, kPadOutputBus, 0.0f);

    MultiBusBuffers bufs;
    bufs.busActive[1] = true;
    bufs.busActive[2] = true;  // active but no pad assigned

    pool.noteOn(36, 0.9f);
    pool.noteOn(38, 0.9f);

    pool.processBlock(bufs.mainL.data(), bufs.mainR.data(),
                      bufs.auxLPtrs.data(), bufs.auxRPtrs.data(),
                      bufs.busActive.data(), MultiBusBuffers::kMaxBuses, kBlockSize);

    // Bus 2 is active but no pad is assigned to it -> zero RMS
    CHECK(computeRMS(bufs.auxLBufs[2].data(), kBlockSize) == 0.0f);
    CHECK(computeRMS(bufs.auxRBufs[2].data(), kBlockSize) == 0.0f);
}

TEST_CASE("Multi-bus: bus deactivation mid-session routes to main only", "[multi-bus]")
{
    using namespace Membrum;

    VoicePool pool;
    preparePool(pool);

    // Pad 0 -> bus 2
    pool.setPadConfigField(0, kPadOutputBus, 2.0f / 15.0f);

    MultiBusBuffers bufs;
    bufs.busActive[2] = true;

    pool.noteOn(36, 0.9f);

    // First block: bus 2 active, should get audio
    pool.processBlock(bufs.mainL.data(), bufs.mainR.data(),
                      bufs.auxLPtrs.data(), bufs.auxRPtrs.data(),
                      bufs.busActive.data(), MultiBusBuffers::kMaxBuses, kBlockSize);

    REQUIRE(computeRMS(bufs.auxLBufs[2].data(), kBlockSize) > 0.0001f);

    // Now deactivate bus 2
    bufs.busActive[2] = false;
    bufs.clear();
    bufs.busActive[2] = false; // re-set after clear resets to default

    // Second block: bus 2 deactivated, should be silent; main still gets audio
    pool.processBlock(bufs.mainL.data(), bufs.mainR.data(),
                      bufs.auxLPtrs.data(), bufs.auxRPtrs.data(),
                      bufs.busActive.data(), MultiBusBuffers::kMaxBuses, kBlockSize);

    REQUIRE(computeRMS(bufs.mainL.data(), kBlockSize) > 0.0001f);
    CHECK(isSilent(bufs.auxLBufs[2].data(), kBlockSize));
    CHECK(isSilent(bufs.auxRBufs[2].data(), kBlockSize));
}
