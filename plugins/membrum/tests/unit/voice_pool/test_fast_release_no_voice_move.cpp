// ==============================================================================
// Audit T2.5 / MB-005 -- fast release moves no DrumVoice
// ==============================================================================
// VoicePool::beginFastRelease used to snapshot the live voice into the shadow
// slot with std::swap of two whole DrumVoice objects (~270 KB each): a stack
// temporary of one voice on the audio thread and three whole-voice moves per
// steal or choke. The pool now swaps the indices of the two voices, so the
// voice that was sounding in the main role is the very object that fades out
// in the shadow role. Its address must therefore survive the hand-over.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "voice_pool/voice_pool.h"

#include <cstdint>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int    kBlockSize  = 128;

int findActiveSlotForNote(const Membrum::VoicePool& pool, std::uint8_t note) noexcept
{
    for (int slot = 0; slot < Membrum::kMaxVoices; ++slot)
    {
        const auto& m = pool.voiceMeta(slot);
        if (m.state == Membrum::VoiceSlotState::Active && m.originatingNote == note)
            return slot;
    }
    return -1;
}

} // namespace

TEST_CASE("Fast release moves no DrumVoice: a stolen voice keeps its address",
          "[voice_pool][rt_safety]")
{
    Membrum::VoicePool pool;
    pool.prepare(kSampleRate, kBlockSize);
    pool.setMaxPolyphony(4);
    pool.setVoiceStealingPolicy(Membrum::VoiceStealingPolicy::Oldest);

    std::vector<float> outL(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> outR(static_cast<std::size_t>(kBlockSize), 0.0f);

    SECTION("steal")
    {
        for (int i = 0; i < 4; ++i)
        {
            pool.noteOn(static_cast<std::uint8_t>(36 + i), 0.9f);
            pool.processBlock(outL.data(), outR.data(), kBlockSize);
        }

        // Oldest policy: the slot of the first note (36) is the victim.
        const int victimSlot = findActiveSlotForNote(pool, 36);
        REQUIRE(victimSlot >= 0);
        const Membrum::DrumVoice* const before = &pool.mainVoiceRef(victimSlot);

        pool.noteOn(40, 0.9f);  // fifth note -> steal

        REQUIRE(pool.releasingMeta(victimSlot).state ==
                Membrum::VoiceSlotState::FastReleasing);
        REQUIRE(pool.releasingMeta(victimSlot).originatingNote == 36);
        CHECK(&pool.releasingVoiceRef(victimSlot) == before);
        // The main role of that slot now holds the other voice of the pair.
        CHECK(&pool.mainVoiceRef(victimSlot) != before);
    }

    SECTION("choke")
    {
        pool.setPadChokeGroup(6, 1);   // note 42, closed hat
        pool.setPadChokeGroup(10, 1);  // note 46, open hat

        pool.noteOn(46, 0.9f);
        pool.processBlock(outL.data(), outR.data(), kBlockSize);

        const int chokedSlot = findActiveSlotForNote(pool, 46);
        REQUIRE(chokedSlot >= 0);
        const Membrum::DrumVoice* const before = &pool.mainVoiceRef(chokedSlot);

        pool.noteOn(42, 0.9f);  // chokes the open hat

        REQUIRE(pool.releasingMeta(chokedSlot).state ==
                Membrum::VoiceSlotState::FastReleasing);
        REQUIRE(pool.releasingMeta(chokedSlot).originatingNote == 46);
        CHECK(&pool.releasingVoiceRef(chokedSlot) == before);
    }
}
