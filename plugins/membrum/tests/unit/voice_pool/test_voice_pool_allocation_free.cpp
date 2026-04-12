// ==============================================================================
// Phase 3.1 -- VoicePool allocation-free guarantee
// ==============================================================================
// T3.1.3 -- satisfies FR-116 / FR-163.
//
// Wraps every VoicePool audio-thread call in a `TestHelpers::AllocationScope`
// and asserts zero heap allocations. The operator new/delete overrides that
// back AllocationDetector live in test_allocation_matrix.cpp -- ODR-safe
// since they are the only overrides in the membrum_tests binary.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "voice_pool/voice_pool.h"
#include "voice_pool_test_helpers.h"

#include <allocation_detector.h>

#include <cstdint>
#include <random>
#include <vector>

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int    kBlockSize  = 128;

} // namespace

TEST_CASE("VoicePool: every audio-thread call is allocation-free",
          "[membrum][voice_pool][phase3_1][rt-safety]")
{
    Membrum::VoicePool pool;
    // prepare() is the only allocation point; do it OUTSIDE the scope.
    pool.prepare(kSampleRate, kBlockSize);
    pool.setMaxPolyphony(16);

    std::vector<float> outL(kBlockSize, 0.0f);
    std::vector<float> outR(kBlockSize, 0.0f);

    // Prime every main voice with one pass of each Phase 1 setter so any
    // first-touch lazy init is paid for OUTSIDE the tracking scope.
    Membrum::TestHelpers::setAllPadsVoiceParams(pool, 0.5f, 0.5f, 0.3f, 0.3f, 0.8f);
    Membrum::TestHelpers::setAllPadsExciterType(pool, Membrum::ExciterType::Impulse);
    Membrum::TestHelpers::setAllPadsBodyModel(pool, Membrum::BodyModelType::Membrane);
    for (int i = 0; i < 16; ++i)
        pool.noteOn(static_cast<std::uint8_t>(36 + (i % 32)), 0.7f);
    pool.processBlock(outL.data(), outR.data(), kBlockSize);

    auto& detector = TestHelpers::AllocationDetector::instance();
    detector.startTracking();

    std::mt19937 rng(0xABCD1234u);
    std::uniform_int_distribution<int> noteDist(36, 67);
    std::uniform_real_distribution<float> velDist(0.1f, 1.0f);

    // 1 second at 44.1 kHz / 128-sample blocks = ~345 blocks, with a new
    // note-on roughly every 5 ms (4 blocks).
    const int blocks = static_cast<int>(kSampleRate) / kBlockSize;
    int noteTimer = 0;
    for (int b = 0; b < blocks; ++b)
    {
        if (noteTimer == 0)
        {
            const auto note = static_cast<std::uint8_t>(noteDist(rng));
            pool.noteOn(note, velDist(rng));
            pool.noteOff(note);
        }
        noteTimer = (noteTimer + 1) % 4;

        // Exercise the parameter mutators periodically.
        if ((b & 0x1F) == 0)
        {
            pool.setMaxPolyphony(8 + (b & 1) * 4);
            pool.setVoiceStealingPolicy(
                static_cast<Membrum::VoiceStealingPolicy>((b >> 3) % 3));
            pool.setChokeGroup(static_cast<std::uint8_t>((b >> 4) & 7));
        }

        pool.processBlock(outL.data(), outR.data(), kBlockSize);
    }

    const size_t allocCount = detector.stopTracking();
    INFO("Allocations during VoicePool fuzz: " << allocCount);
    CHECK(allocCount == 0);
}
