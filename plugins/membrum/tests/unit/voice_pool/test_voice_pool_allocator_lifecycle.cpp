// ==============================================================================
// T1.9 -- VoicePool allocator lifecycle (MB-049, MB-050, MB-105, MB-118, MB-069)
// ==============================================================================
// (a) Quietest never steals a voice triggered in the same block (it has no
//     measured level yet).
// (b) Quietest does not pre-steal for a same-note retrigger, which the
//     allocator serves on the note's own slot.
// (c) A voice that finishes before its MIDI note-off frees its allocator
//     slot, so the late note-off leaks nothing and polyphony stays intact.
// (d) The multi-bus renderer releases the coupling resonators of a voice that
//     finishes naturally (one renderer for both overloads).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "voice_pool/voice_pool.h"
#include "voice_pool_test_helpers.h"
#include "dsp/pad_config.h"

#include <krate/dsp/systems/sympathetic_resonance.h>

#include <array>
#include <cmath>
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

// Four long-ringing voices (49, 51, 57, 59) in a Quietest pool of 4.
void fillQuietestPoolWithRingingVoices(PoolFixture& fix)
{
    fix.pool.setMaxPolyphony(4);
    fix.pool.setVoiceStealingPolicy(Membrum::VoiceStealingPolicy::Quietest);
    Membrum::TestHelpers::setAllPadsVoiceParams(fix.pool, 0.5f, 0.5f, 1.0f, 0.3f, 0.8f);

    for (const std::uint8_t note : {std::uint8_t{49}, std::uint8_t{51},
                                    std::uint8_t{57}, std::uint8_t{59}})
    {
        fix.pool.noteOn(note, 1.0f);
        fix.block();
    }
    for (int b = 0; b < 20; ++b)
        fix.block();
}

bool allMetaFree(const Membrum::VoicePool& pool, int maxPoly) noexcept
{
    for (int i = 0; i < maxPoly; ++i)
        if (pool.voiceMeta(i).state != Membrum::VoiceSlotState::Free)
            return false;
    return true;
}

void configureCouplingPads(Membrum::VoicePool& pool)
{
    Membrum::TestHelpers::setAllPadsVoiceParams(pool, 0.5f, 0.5f, 0.1f, 0.3f, 0.8f);
    Membrum::TestHelpers::setAllPadsExciterType(pool, Membrum::ExciterType::Impulse);
    Membrum::TestHelpers::setAllPadsBodyModel(pool, Membrum::BodyModelType::Membrane);
    for (int p = 0; p < 2; ++p)
        pool.setPadConfigField(p, Membrum::kPadCouplingAmount, 1.0f);
}

} // namespace

TEST_CASE("VoicePool Quietest: three note-ons in one block all sound",
          "[voice_pool][stealing]")
{
    PoolFixture fix;
    fillQuietestPoolWithRingingVoices(fix);
    REQUIRE(fix.pool.getActiveVoiceCount() == 4);

    // Kick, snare and hat in the same block, no render in between.
    fix.pool.noteOn(36, 1.0f);
    fix.pool.noteOn(38, 1.0f);
    fix.pool.noteOn(42, 1.0f);
    fix.block();

    CHECK(findActiveSlotByNote(fix.pool, 4, 36) >= 0);
    CHECK(findActiveSlotByNote(fix.pool, 4, 38) >= 0);
    CHECK(findActiveSlotByNote(fix.pool, 4, 42) >= 0);
    CHECK(fix.pool.getActiveVoiceCount() == 4);
}

TEST_CASE("VoicePool Quietest: retriggering a ringing pad in a full pool kills no other voice",
          "[voice_pool][stealing]")
{
    PoolFixture fix;
    fillQuietestPoolWithRingingVoices(fix);
    REQUIRE(fix.pool.getActiveVoiceCount() == 4);

    fix.pool.noteOn(51, 1.0f);
    fix.block();

    CHECK(fix.pool.getActiveVoiceCount() == 4);
    CHECK(findActiveSlotByNote(fix.pool, 4, 49) >= 0);
    CHECK(findActiveSlotByNote(fix.pool, 4, 57) >= 0);
    CHECK(findActiveSlotByNote(fix.pool, 4, 59) >= 0);

    const int ownSlot = findActiveSlotByNote(fix.pool, 4, 51);
    REQUIRE(ownSlot >= 0);
    for (int i = 0; i < Membrum::kMaxVoices; ++i)
    {
        if (i == ownSlot)
            continue;
        INFO("slot " << i << " is fast-releasing (note "
             << static_cast<int>(fix.pool.releasingMeta(i).originatingNote) << ")");
        CHECK(fix.pool.releasingMeta(i).state != Membrum::VoiceSlotState::FastReleasing);
    }
}

TEST_CASE("VoicePool: a voice that finishes before its note-off frees its allocator slot",
          "[voice_pool][stealing]")
{
    auto policy = Membrum::VoiceStealingPolicy::Quietest;
    SECTION("Quietest") { policy = Membrum::VoiceStealingPolicy::Quietest; }
    SECTION("Oldest")   { policy = Membrum::VoiceStealingPolicy::Oldest; }
    SECTION("Priority") { policy = Membrum::VoiceStealingPolicy::Priority; }

    PoolFixture fix;
    fix.pool.setMaxPolyphony(4);
    fix.pool.setVoiceStealingPolicy(policy);
    Membrum::TestHelpers::setAllPadsVoiceParams(fix.pool, 0.5f, 0.5f, 0.0f, 0.3f, 0.8f);

    // Four held notes, no note-off.
    for (std::uint8_t note = 36; note <= 39; ++note)
    {
        fix.pool.noteOn(note, 1.0f);
        fix.block();
    }

    // Let every voice ring out on its own, with the notes still held.
    const int maxBlocks = static_cast<int>(kSampleRate * 10.0) / kBlockSize;
    bool allFree = false;
    for (int b = 0; b < maxBlocks && !allFree; ++b)
    {
        fix.block();
        allFree = allMetaFree(fix.pool, 4);
    }
    REQUIRE(allFree);

    // The late MIDI note-offs.
    for (std::uint8_t note = 36; note <= 39; ++note)
        fix.pool.noteOff(note);

    for (std::uint8_t note = 40; note <= 42; ++note)
    {
        fix.pool.noteOn(note, 1.0f);
        fix.block();
    }

    if (policy == Membrum::VoiceStealingPolicy::Quietest)
        CHECK(fix.pool.getActiveVoiceCount() == 3);

    for (int i = 0; i < Membrum::kMaxVoices; ++i)
    {
        INFO("slot " << i << " is fast-releasing");
        CHECK(fix.pool.releasingMeta(i).state != Membrum::VoiceSlotState::FastReleasing);
    }
}

TEST_CASE("VoicePool multi-bus: a naturally finished coupled voice releases its coupling resonators",
          "[voice_pool][multi-bus][coupling]")
{
    constexpr int kBuses = 2;
    std::array<float, kBlockSize> mainL{};
    std::array<float, kBlockSize> mainR{};
    std::array<std::array<float, kBlockSize>, kBuses> auxLBufs{};
    std::array<std::array<float, kBlockSize>, kBuses> auxRBufs{};
    std::array<float*, kBuses> auxL{auxLBufs[0].data(), auxLBufs[1].data()};
    std::array<float*, kBuses> auxR{auxRBufs[0].data(), auxRBufs[1].data()};
    const std::array<bool, kBuses> busActive{true, true};

    Krate::DSP::SympatheticResonance engine;
    engine.prepare(kSampleRate);

    Membrum::VoicePool pool;
    pool.prepare(kSampleRate, kBlockSize);
    configureCouplingPads(pool);
    pool.setCouplingEngine(&engine);

    const int maxPoly = pool.maxPolyphony();
    auto renderMultiBus = [&]() {
        pool.processBlock(mainL.data(), mainR.data(), auxL.data(), auxR.data(),
                          busActive.data(), kBuses, kBlockSize);
    };

    // Step 1: hit 36 and read its partial 0.
    pool.noteOn(36, 0.8f);
    const int slotA = findActiveSlotByNote(pool, maxPoly, 36);
    REQUIRE(slotA >= 0);
    const float fA = pool.mainVoiceRef(slotA).getPartialInfo().frequencies[0];
    REQUIRE(fA > 0.0f);

    // Step 2: render (engine never processed) until 36 has finished.
    const int maxBlocks = static_cast<int>(kSampleRate * 10.0) / kBlockSize;
    bool finished = false;
    for (int b = 0; b < maxBlocks && !finished; ++b)
    {
        renderMultiBus();
        finished = pool.voiceMeta(slotA).state == Membrum::VoiceSlotState::Free;
    }
    REQUIRE(finished);

    // Step 3: find a size for pad 1 whose partial 0 lies 0.05..0.25 Hz from
    // fA (inside the engine's 0.3 Hz merge threshold). Search on a separate
    // probe pool with the same pad configuration.
    Membrum::VoicePool probe;
    probe.prepare(kSampleRate, kBlockSize);
    configureCouplingPads(probe);
    auto partial0ForSize = [&](float size) {
        probe.setPadConfigField(1, Membrum::kPadSize, size);
        probe.noteOn(37, 0.8f);
        const int s = findActiveSlotByNote(probe, probe.maxPolyphony(), 37);
        return s >= 0 ? probe.mainVoiceRef(s).getPartialInfo().frequencies[0] : 0.0f;
    };
    constexpr float kTargetDistHz = 0.15f;
    const float s0 = 0.5f;
    float lo = s0;
    float hi = s0 + 0.05f;
    REQUIRE(std::fabs(partial0ForSize(hi) - fA) > kTargetDistHz);
    for (int it = 0; it < 60; ++it)
    {
        const float mid = 0.5f * (lo + hi);
        if (std::fabs(partial0ForSize(mid) - fA) < kTargetDistHz)
            lo = mid;
        else
            hi = mid;
    }
    pool.setPadConfigField(1, Membrum::kPadSize, lo);

    // Step 4: hit 37 on a different slot.
    pool.noteOn(37, 0.8f);
    const int slotB = findActiveSlotByNote(pool, maxPoly, 37);
    REQUIRE(slotB >= 0);
    REQUIRE(slotB != slotA);
    const float fB = pool.mainVoiceRef(slotB).getPartialInfo().frequencies[0];
    CAPTURE(fA, fB, lo);
    const float dist = std::fabs(fB - fA);
    REQUIRE(dist >= 0.05f);
    REQUIRE(dist <= 0.25f);

    // Step 5: the merged resonator near fA.
    int merged = -1;
    for (int i = 0; i < Krate::DSP::kMaxSympatheticResonators; ++i)
    {
        const float f = engine.getResonatorFrequency(i);
        if (f > 0.0f && std::fabs(f - fA) < Krate::DSP::kMergeThresholdHz)
        {
            merged = i;
            break;
        }
    }
    REQUIRE(merged >= 0);
    const float fMerged = engine.getResonatorFrequency(merged);
    CAPTURE(fMerged);
    // With the dead owner released, the refCount is 0 and the merge takes fB
    // unweighted; a leaked owner would give (fA + fB) / 2.
    CHECK(std::fabs(fMerged - fB) <= 1.0e-3f);
}
