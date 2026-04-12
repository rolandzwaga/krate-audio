// ==============================================================================
// Phase 3.1 -- Voice stealing policies
// ==============================================================================
// T3.1.2 -- satisfies FR-121, FR-122, FR-123, FR-128, FR-180.
//
// Parameterized over {4, 8, 16} max polyphony x {Oldest, Quietest, Priority}.
// Stealing victim determination is verified by checking which `meta_` slot
// transitions from Active to Free after the steal is performed.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "voice_pool/voice_pool.h"

#include <cstdint>
#include <vector>

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int    kBlockSize  = 128;

struct PoolFixture
{
    Membrum::VoicePool pool;
    std::vector<float> outL;
    std::vector<float> outR;

    explicit PoolFixture()
        : outL(static_cast<size_t>(kBlockSize), 0.0f)
        , outR(static_cast<size_t>(kBlockSize), 0.0f)
    {
        pool.prepare(kSampleRate, kBlockSize);
    }

    void block() noexcept
    {
        pool.processBlock(outL.data(), outR.data(), kBlockSize);
    }
};

// Find the index of the slot whose note matches `note` and state==Active.
int findActiveSlotByNote(const Membrum::VoicePool& pool,
                         int maxPoly,
                         std::uint8_t note) noexcept
{
    for (int i = 0; i < maxPoly; ++i)
    {
        const auto& m = pool.voiceMeta(i);
        if (m.state == Membrum::VoiceSlotState::Active &&
            m.originatingNote == note)
            return i;
    }
    return -1;
}

} // namespace

TEST_CASE("VoicePool stealing: Oldest steals the earliest noteOnSampleCount",
          "[membrum][voice_pool][phase3_1][stealing]")
{
    for (int maxPoly : {4, 8, 16})
    {
        PoolFixture fix;
        fix.pool.setMaxPolyphony(maxPoly);
        fix.pool.setVoiceStealingPolicy(Membrum::VoiceStealingPolicy::Oldest);

        // Fill the pool. Advance sampleCounter_ between notes by processing
        // a block so each voice has a distinct noteOnSampleCount.
        std::vector<std::uint8_t> notes;
        for (int i = 0; i < maxPoly; ++i)
        {
            const auto note = static_cast<std::uint8_t>(36 + i);
            fix.pool.noteOn(note, 0.7f);
            notes.push_back(note);
            fix.block();
        }
        REQUIRE(fix.pool.getActiveVoiceCount() == maxPoly);

        const auto firstNote = notes.front();

        // Trigger one more note -- the first note (oldest) must be stolen.
        const auto stealerNote = static_cast<std::uint8_t>(36 + maxPoly);
        fix.pool.noteOn(stealerNote, 0.7f);
        fix.block();

        INFO("maxPoly=" << maxPoly << " Oldest should have stolen note "
             << static_cast<int>(firstNote));
        CHECK(findActiveSlotByNote(fix.pool, maxPoly, firstNote) == -1);
        CHECK(findActiveSlotByNote(fix.pool, maxPoly, stealerNote) >= 0);
    }
}

TEST_CASE("VoicePool stealing: Priority steals the highest-pitched note",
          "[membrum][voice_pool][phase3_1][stealing]")
{
    for (int maxPoly : {4, 8, 16})
    {
        PoolFixture fix;
        fix.pool.setMaxPolyphony(maxPoly);
        fix.pool.setVoiceStealingPolicy(Membrum::VoiceStealingPolicy::Priority);

        std::vector<std::uint8_t> notes;
        for (int i = 0; i < maxPoly; ++i)
        {
            const auto note = static_cast<std::uint8_t>(36 + i);
            fix.pool.noteOn(note, 0.7f);
            notes.push_back(note);
            fix.block();
        }
        REQUIRE(fix.pool.getActiveVoiceCount() == maxPoly);

        // The highest note in the pool is the last one we inserted.
        const std::uint8_t highestNote = notes.back();

        // Trigger the highest pad so Priority (HighestNote) picks a victim.
        const auto stealerNote = static_cast<std::uint8_t>(67);
        fix.pool.noteOn(stealerNote, 0.7f);
        fix.block();

        if (stealerNote > highestNote)
        {
            INFO("maxPoly=" << maxPoly
                 << " Priority should have stolen highest note "
                 << static_cast<int>(highestNote));
            CHECK(findActiveSlotByNote(fix.pool, maxPoly, highestNote) == -1);
        }
        else
        {
            // Same-note retrigger: one Active copy remains.
            int count = 0;
            for (int i = 0; i < maxPoly; ++i)
                if (fix.pool.voiceMeta(i).originatingNote == highestNote &&
                    fix.pool.voiceMeta(i).state == Membrum::VoiceSlotState::Active)
                    ++count;
            CHECK(count == 1);
        }
        CHECK(findActiveSlotByNote(fix.pool, maxPoly, stealerNote) >= 0);
    }
}

TEST_CASE("VoicePool stealing: Quietest steals the lowest currentLevel slot",
          "[membrum][voice_pool][phase3_1][stealing]")
{
    for (int maxPoly : {4, 8, 16})
    {
        PoolFixture fix;
        fix.pool.setMaxPolyphony(maxPoly);
        fix.pool.setVoiceStealingPolicy(Membrum::VoiceStealingPolicy::Quietest);

        std::vector<std::uint8_t> notes;
        for (int i = 0; i < maxPoly; ++i)
        {
            const auto note = static_cast<std::uint8_t>(36 + i);
            fix.pool.noteOn(note, 0.7f);
            notes.push_back(note);
            fix.block();
        }
        REQUIRE(fix.pool.getActiveVoiceCount() == maxPoly);

        // Let the envelopes diverge so each voice has a distinct level.
        for (int b = 0; b < 16; ++b)
            fix.block();

        // Expected victim = slot with lowest currentLevel, tiebreak oldest.
        int expectedVictim = -1;
        float bestLevel = 0.0f;
        std::uint64_t bestAge = 0;
        for (int i = 0; i < maxPoly; ++i)
        {
            const auto& m = fix.pool.voiceMeta(i);
            if (m.state != Membrum::VoiceSlotState::Active) continue;
            if (expectedVictim < 0 ||
                m.currentLevel < bestLevel ||
                (m.currentLevel == bestLevel && m.noteOnSampleCount < bestAge))
            {
                expectedVictim = i;
                bestLevel = m.currentLevel;
                bestAge = m.noteOnSampleCount;
            }
        }
        REQUIRE(expectedVictim >= 0);
        const std::uint8_t victimNote =
            fix.pool.voiceMeta(expectedVictim).originatingNote;

        const auto stealerNote = static_cast<std::uint8_t>(67);
        fix.pool.noteOn(stealerNote, 0.7f);
        fix.block();

        INFO("maxPoly=" << maxPoly << " Quietest should have stolen note "
             << static_cast<int>(victimNote) << " at slot " << expectedVictim);
        CHECK(findActiveSlotByNote(fix.pool, maxPoly, victimNote) == -1);
        CHECK(findActiveSlotByNote(fix.pool, maxPoly, stealerNote) >= 0);
    }
}

TEST_CASE("VoicePool stealing: silent pool tiebreaks on slot-index ascending",
          "[membrum][voice_pool][phase3_1][stealing][tiebreak]")
{
    // FR-128 edge case: all slots silent => every currentLevel ties at 0.
    // Before any processBlock, every noteOnSampleCount is also 0 (same
    // block), so the selector's preference for the earliest slot picks
    // slot 0.
    PoolFixture fix;
    fix.pool.setMaxPolyphony(4);
    fix.pool.setVoiceStealingPolicy(Membrum::VoiceStealingPolicy::Quietest);

    for (int i = 0; i < 4; ++i)
        fix.pool.noteOn(static_cast<std::uint8_t>(36 + i), 0.7f);

    const std::uint8_t victimNote = fix.pool.voiceMeta(0).originatingNote;

    fix.pool.noteOn(67, 0.7f);
    fix.block();

    CHECK(findActiveSlotByNote(fix.pool, 4, victimNote) == -1);
}
