// ==============================================================================
// T2.6 (MB-053) -- No ghost voices
// ==============================================================================
// A "ghost" is a main voice that still renders (DrumVoice::isActive()) while
// its slot's bookkeeping says the slot is not Active: no auto-release, no
// note-off and no voice count reach it, and isAnyVoiceActive() reports the
// bus silent while it carries audio.
//
// Before T2.6 a finished fast release left the shadow DrumVoice active with
// frozen state; the next hand-over on that slot brought it back into the main
// role, and a choke, a Quietest pre-steal or a polyphony shrink (none of them
// followed by a NoteOn on that slot) left it rendering with meta Free. A
// non-forced hand-over into a busy shadow returned early, so a shrink left the
// sounding voice in a slot above the polyphony that resumed when the
// polyphony grew back (probes S6 and S7 of the Membrum audit).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "dsp/default_kit.h"
#include "voice_pool/voice_pool.h"
#include "voice_pool_test_helpers.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int    kBlockSize  = 128;

using Membrum::TestHelpers::countGhosts;

// Default kit in the pool, with the choke table filled from the pads' groups.
void applyDefaultKit(Membrum::VoicePool& pool)
{
    Membrum::DefaultKit::apply(pool.padConfigsArray());
    for (int p = 0; p < Membrum::kNumPads; ++p)
        pool.setPadChokeGroup(p, pool.padConfig(p).chokeGroup);
}

int blocksForMs(double ms)
{
    return static_cast<int>(std::ceil(ms * 1e-3 * kSampleRate / kBlockSize));
}

int findActiveSlotForNote(const Membrum::VoicePool& pool, std::uint8_t note)
{
    for (int s = 0; s < Membrum::kMaxVoices; ++s)
    {
        const auto& m = pool.voiceMeta(s);
        if (m.state == Membrum::VoiceSlotState::Active && m.originatingNote == note)
            return s;
    }
    return -1;
}

// Per-block invariants of the shrink / grow scenario.
struct BlockChecks
{
    int   ghostBlocks          = 0;  // blocks after which countGhosts > 0
    int   maxGhosts            = 0;
    int   silentFlagLoudBlocks = 0;  // peak > 1e-6 while isAnyVoiceActive() == false
    float silentFlagMaxPeak    = 0.0f;
    int   resurrectedBlocks    = 0;  // released slot's main voice active after the grow
};

float renderBlock(Membrum::VoicePool& pool, std::vector<float>& outL,
                  std::vector<float>& outR)
{
    pool.processBlock(outL.data(), outR.data(), kBlockSize);
    float peak = 0.0f;
    for (int i = 0; i < kBlockSize; ++i)
        peak = std::max({peak, std::fabs(outL[static_cast<std::size_t>(i)]),
                         std::fabs(outR[static_cast<std::size_t>(i)])});
    return peak;
}

void checkBlock(Membrum::VoicePool& pool, float peak, BlockChecks& c,
                const bool (&released)[Membrum::kMaxVoices], bool afterGrow)
{
    const int g = countGhosts(pool);
    if (g > 0)
        ++c.ghostBlocks;
    c.maxGhosts = std::max(c.maxGhosts, g);
    if (peak > 1e-6f && !pool.isAnyVoiceActive())
    {
        ++c.silentFlagLoudBlocks;
        c.silentFlagMaxPeak = std::max(c.silentFlagMaxPeak, peak);
    }
    if (afterGrow)
    {
        for (int s = 4; s < 8; ++s)
        {
            if (released[s] && pool.mainVoiceRef(s).isActive())
            {
                ++c.resurrectedBlocks;
                break;
            }
        }
    }
}

} // namespace

TEST_CASE("VoicePool: a finished fast release never returns a sounding voice to a main slot",
          "[voice_pool][fast_release][ghost]")
{
    Membrum::VoicePool pool;
    applyDefaultKit(pool);
    pool.prepare(kSampleRate, kBlockSize);
    pool.setMaxPolyphony(8);
    pool.setVoiceStealingPolicy(Membrum::VoiceStealingPolicy::Oldest);

    std::vector<float> outL(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> outR(static_cast<std::size_t>(kBlockSize), 0.0f);

    constexpr int kHits          = 48;
    constexpr int kBlocksPerHit  = 48;  // 48 x 128 samples = 128 ms at 48 kHz

    int hitsFollowedByGhost = 0;
    int maxConcurrentGhosts = 0;
    for (int h = 0; h < kHits; ++h)
    {
        // Open hat (46) and closed hat (42) alternate; both are in choke
        // group 1, so every hit chokes the previous one.
        pool.noteOn((h % 2 == 0) ? std::uint8_t{46} : std::uint8_t{42}, 0.8f);

        bool ghostAfterThisHit = false;
        for (int b = 0; b < kBlocksPerHit; ++b)
        {
            pool.processBlock(outL.data(), outR.data(), kBlockSize);
            const int g = countGhosts(pool);
            maxConcurrentGhosts = std::max(maxConcurrentGhosts, g);
            if (g > 0)
                ghostAfterThisHit = true;
        }
        if (ghostAfterThisHit)
            ++hitsFollowedByGhost;
    }

    CAPTURE(hitsFollowedByGhost, maxConcurrentGhosts);
    CHECK(hitsFollowedByGhost == 0);
    CHECK(maxConcurrentGhosts == 0);
}

TEST_CASE("VoicePool: shrinking polyphony then growing it back resurrects nothing",
          "[voice_pool][poly][ghost]")
{
    Membrum::VoicePool pool;
    applyDefaultKit(pool);
    pool.prepare(kSampleRate, kBlockSize);
    pool.setMaxPolyphony(8);
    pool.setVoiceStealingPolicy(Membrum::VoiceStealingPolicy::Oldest);

    std::vector<float> outL(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> outR(static_cast<std::size_t>(kBlockSize), 0.0f);

    // Eight voices on eight different pads: notes 36..43 take slots 0..7.
    for (int n = 36; n <= 43; ++n)
        pool.noteOn(static_cast<std::uint8_t>(n), 0.8f);
    for (int s = 0; s < 8; ++s)
        REQUIRE(pool.voiceMeta(s).state == Membrum::VoiceSlotState::Active);

    BlockChecks c;
    bool released[Membrum::kMaxVoices] = {};

    const int blocks50ms = blocksForMs(50.0);
    for (int b = 0; b < blocks50ms; ++b)
        checkBlock(pool, renderBlock(pool, outL, outR), c, released, false);

    const char* section = "";
    SECTION("shadow free")
    {
        section = "shadow free";
        for (int s = 4; s < 8; ++s)
            released[s] = pool.voiceMeta(s).state == Membrum::VoiceSlotState::Active;
        pool.setMaxPolyphony(4);
    }

    SECTION("shadow busy")
    {
        section = "shadow busy";
        // A same-note retrigger steals the note's own slot; note 40 sits in
        // slot 4, which the shrink to 4 releases. Retrigger and shrink in the
        // same block, so the shrink meets a shadow that is still fading.
        const int slot = findActiveSlotForNote(pool, 40);
        REQUIRE(slot >= 4);
        REQUIRE(slot < 8);
        pool.noteOn(40, 0.9f);
        REQUIRE(pool.releasingMeta(slot).state == Membrum::VoiceSlotState::FastReleasing);
        REQUIRE(pool.voiceMeta(slot).state == Membrum::VoiceSlotState::Active);

        for (int s = 4; s < 8; ++s)
            released[s] = pool.voiceMeta(s).state == Membrum::VoiceSlotState::Active;
        pool.setMaxPolyphony(4);
    }

    for (int b = 0; b < blocksForMs(1000.0); ++b)
        checkBlock(pool, renderBlock(pool, outL, outR), c, released, false);

    pool.setMaxPolyphony(8);
    for (int b = 0; b < blocksForMs(3000.0); ++b)
        checkBlock(pool, renderBlock(pool, outL, outR), c, released, true);

    CAPTURE(section, c.ghostBlocks, c.maxGhosts, c.silentFlagLoudBlocks,
            c.silentFlagMaxPeak, c.resurrectedBlocks);
    CHECK(c.ghostBlocks == 0);
    CHECK(c.silentFlagLoudBlocks == 0);
    CHECK(c.resurrectedBlocks == 0);
}
