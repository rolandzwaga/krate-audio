// ==============================================================================
// Krate::Plugins::SustainLatch - shared semantics (Profundum Phase 11 FR-061, T002)
// ==============================================================================
// Port of the seven SECTIONs of "Vorago_SustainLatchUnit"
// (plugins/vorago/tests/integration/sustain_test.cpp) against the promoted
// shared latch, with the same assertions and numbers.
//
// ALLOCATION READING FORM: the count is read from the LIVE detector singleton
// INSIDE the scope and REQUIREd after it closes.
//
// This TU must NOT include <allocation_operator_overrides.h>; the global
// allocation-operator replacements for shared_tests are owned by
// test_allocation_detector.cpp.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "midi/sustain_latch.h"

#include <allocation_detector.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <random>

namespace {

/// Fixed-capacity release collector (no allocation).
struct ReleaseLog {
    std::array<std::uint8_t, 128> notes{};
    std::size_t count = 0;

    void clear() noexcept { count = 0; }
    void push(std::uint8_t n) noexcept {
        if (count < notes.size()) notes[count] = n;
        ++count;  // overflow still counted so an over-release is visible
    }
};

}  // namespace

TEST_CASE("SustainLatch_SharedSemantics", "[profundum][shared][midi]")
{
    Krate::Plugins::SustainLatch latch;
    ReleaseLog rel;
    auto collect = [&rel](std::uint8_t n) noexcept { rel.push(n); };

    SECTION("PedalUpNoteOffIsImmediate")
    {
        REQUIRE_FALSE(latch.isDown());
        latch.noteOn(60);
        REQUIRE(latch.noteOff(60));
    }

    SECTION("PedalDownLatchesThenReleasesOnPedalUp")
    {
        latch.setPedal(true, collect);
        REQUIRE(latch.isDown());
        REQUIRE(rel.count == 0u);
        latch.noteOn(60);
        REQUIRE_FALSE(latch.noteOff(60));
        latch.setPedal(false, collect);
        REQUIRE_FALSE(latch.isDown());
        REQUIRE(rel.count == 1u);
        REQUIRE(rel.notes[0] == 60);
    }

    SECTION("ReStrikeOfLatchedNoteIsHeld")
    {
        latch.setPedal(true, collect);
        latch.noteOn(60);
        REQUIRE_FALSE(latch.noteOff(60));  // latched
        latch.noteOn(60);                  // re-strike: held again, latch mark cleared
        latch.setPedal(false, collect);
        REQUIRE(rel.count == 0u);
        REQUIRE(latch.noteOff(60));  // pedal is up: immediate
        REQUIRE(rel.count == 0u);
    }

    SECTION("ReleaseAllReleasesEveryLatchedNote")
    {
        latch.setPedal(true, collect);
        latch.noteOn(60);
        latch.noteOn(64);
        REQUIRE_FALSE(latch.noteOff(60));
        REQUIRE_FALSE(latch.noteOff(64));
        latch.releaseAll(collect);
        REQUIRE_FALSE(latch.isDown());
        REQUIRE(rel.count == 2u);
        REQUIRE(rel.notes[0] == 60);
        REQUIRE(rel.notes[1] == 64);

        // Nothing is left latched: a fresh pedal cycle releases nothing.
        rel.clear();
        latch.setPedal(true, collect);
        latch.setPedal(false, collect);
        REQUIRE(rel.count == 0u);
    }

    SECTION("ClearWithoutReleaseForgetsEverything")
    {
        latch.setPedal(true, collect);
        latch.noteOn(60);
        latch.noteOn(64);
        REQUIRE_FALSE(latch.noteOff(60));
        REQUIRE_FALSE(latch.noteOff(64));
        latch.clearWithoutRelease();
        REQUIRE(rel.count == 0u);
        REQUIRE_FALSE(latch.isDown());

        // A later pedal cycle releases nothing (the latched set was cleared).
        latch.setPedal(true, collect);
        latch.setPedal(false, collect);
        REQUIRE(rel.count == 0u);
    }

    SECTION("NoteNumbersAbove127AreIgnored")
    {
        latch.setPedal(true, collect);
        latch.noteOn(200);
        REQUIRE(latch.noteOff(200));  // never latched
        latch.setPedal(false, collect);
        REQUIRE(rel.count == 0u);
    }

    SECTION("RandomOperationsAreAllocationFree")
    {
        std::mt19937 rng(12012u);
        std::uniform_int_distribution<int> opDist(0, 5);
        std::uniform_int_distribution<int> noteDist(0, 127);

        std::size_t allocs = 0;
        std::size_t immediateOffs = 0;
        std::size_t latchedOffs = 0;
        std::size_t maxReleasesPerPass = 0;
        {
            TestHelpers::AllocationScope scope;
            for (int i = 0; i < 10000; ++i) {
                const auto note = static_cast<std::uint8_t>(noteDist(rng));
                switch (opDist(rng)) {
                    case 0:
                    case 1:
                        latch.noteOn(note);
                        break;
                    case 2:
                    case 3:
                        if (latch.noteOff(note)) {
                            ++immediateOffs;
                        } else {
                            ++latchedOffs;
                        }
                        break;
                    case 4:
                        rel.clear();
                        latch.setPedal(!latch.isDown(), collect);
                        maxReleasesPerPass = std::max(rel.count, maxReleasesPerPass);
                        break;
                    default:
                        rel.clear();
                        if ((i % 64) == 0) {
                            latch.releaseAll(collect);
                        } else if ((i % 128) == 1) {
                            latch.clearWithoutRelease();
                        }
                        maxReleasesPerPass = std::max(rel.count, maxReleasesPerPass);
                        break;
                }
            }
            allocs = TestHelpers::AllocationDetector::instance().getAllocationCount();
        }
        REQUIRE(allocs == 0u);
        REQUIRE(immediateOffs > 0u);  // the run exercised pedal-up note-offs
        REQUIRE(latchedOffs > 0u);    // ... and pedal-down (latched) note-offs
        REQUIRE(maxReleasesPerPass <= 128u);  // no pass released a key twice
    }
}
