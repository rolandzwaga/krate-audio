// ==============================================================================
// Layer 2: Processor Tests - Profundum Phase 4 (specs/profundum-phase4-drop-note-behaviour)
// ==============================================================================
// Reference: specs/profundum-phase4-drop-note-behaviour/plan.md (S6, S11.1)
//            specs/profundum-phase4-drop-note-behaviour/tasks.md (T013)
//
// Holds: SC-009(a) -- the NoteMemory resolver table (FR-030 to FR-036, D-7, D-12).
// The expected Actions are built here, independently of the header, from the
// FR-034 matrix (spec.md "FR-034 The matrix") and FR-033's derivation.
// ==============================================================================

#include <krate/dsp/processors/note_memory.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

using Krate::DSP::NoteMemory;

namespace {

using Mode = NoteMemory::Mode;
using Retrigger = NoteMemory::Retrigger;
using Transition = NoteMemory::Transition;
using PhasePolicy = NoteMemory::PhasePolicy;
using Actions = NoteMemory::Actions;

constexpr int kNumModes = 4;
constexpr int kNumRetriggers = 2;
constexpr int kNumTransitions = 5;

// FR-034, transcribed independently of the header.
// Rows: AlwaysRetrigger, Legato, EnvelopeRetriggerOnly, OscillatorContinuous.
// Columns: FromSilence, Overlap, Tail, ReturnToHeld, VoiceReuse.
struct ExpectedCell {
    bool osc;
    bool env;
};
constexpr std::array<std::array<ExpectedCell, kNumTransitions>, kNumModes> kExpectedMatrix{{
    {{{true, true}, {true, true}, {true, true}, {true, true}, {true, true}}},
    {{{true, true}, {false, false}, {true, true}, {false, false}, {true, true}}},
    {{{true, true}, {false, true}, {false, true}, {false, true}, {false, true}}},
    {{{false, true}, {false, false}, {false, true}, {false, false}, {false, true}}},
}};

// FR-033 derivation.
constexpr Actions expectedActions(int mode, int retrigger, int transition, bool audible) noexcept
{
    const ExpectedCell cell =
        kExpectedMatrix[static_cast<std::size_t>(mode)][static_cast<std::size_t>(transition)];
    const bool phaseReset = cell.osc && (retrigger == 0);
    const bool declick = phaseReset && audible;
    Actions a{};
    a.oscillatorRestart = cell.osc;
    a.phase = phaseReset ? PhasePolicy::Reset : PhasePolicy::FreeRunning;
    a.envelopeRestart = cell.env;
    a.dropTrigger = cell.env;
    a.dropSnap = !audible || declick;
    a.anchorRearm = cell.env;
    a.declick = declick;
    return a;
}

constexpr bool actionsEqual(const Actions& x, const Actions& y) noexcept
{
    return x.oscillatorRestart == y.oscillatorRestart && x.phase == y.phase &&
           x.envelopeRestart == y.envelopeRestart && x.dropTrigger == y.dropTrigger &&
           x.dropSnap == y.dropSnap && x.anchorRearm == y.anchorRearm && x.declick == y.declick;
}

// Every field a consumer acts on (phase, envelope, Drop, Anchor re-arm, declick).
// oscillatorRestart is the FR-034 column itself, which D-12 says "has no effect"
// under Continue; it differs between the collapsed pairs by construction of the
// FR-034 table (e.g. Overlap: AlwaysRetrigger (1, 1) vs EnvelopeRetriggerOnly (0, 1)).
constexpr bool actedOnFieldsEqual(const Actions& x, const Actions& y) noexcept
{
    return x.phase == y.phase && x.envelopeRestart == y.envelopeRestart &&
           x.dropTrigger == y.dropTrigger && x.dropSnap == y.dropSnap &&
           x.anchorRearm == y.anchorRearm && x.declick == y.declick;
}

constexpr Actions resolveStatic(int mode, int retrigger, int transition, bool audible) noexcept
{
    return NoteMemory::resolve(static_cast<Mode>(mode), static_cast<Retrigger>(retrigger),
                               static_cast<Transition>(transition), audible);
}

constexpr bool allCellsMatch() noexcept
{
    for (int m = 0; m < kNumModes; ++m)
        for (int r = 0; r < kNumRetriggers; ++r)
            for (int t = 0; t < kNumTransitions; ++t)
                for (int a = 0; a < 2; ++a)
                    if (!actionsEqual(resolveStatic(m, r, t, a != 0), expectedActions(m, r, t, a != 0)))
                        return false;
    return true;
}

constexpr bool continuePairCollapses(int modeA, int modeB) noexcept
{
    for (int t = 0; t < kNumTransitions; ++t)
        for (int a = 0; a < 2; ++a)
            if (!actedOnFieldsEqual(resolveStatic(modeA, 1, t, a != 0),
                                    resolveStatic(modeB, 1, t, a != 0)))
                return false;
    return true;
}

constexpr bool modesDifferUnder(int retrigger, int modeA, int modeB) noexcept
{
    for (int t = 0; t < kNumTransitions; ++t)
        for (int a = 0; a < 2; ++a)
            if (!actionsEqual(resolveStatic(modeA, retrigger, t, a != 0),
                              resolveStatic(modeB, retrigger, t, a != 0)))
                return true;
    return false;
}

constexpr bool everyResetPairDiffers() noexcept
{
    for (int x = 0; x < kNumModes; ++x)
        for (int y = x + 1; y < kNumModes; ++y)
            if (!modesDifferUnder(0, x, y))
                return false;
    return true;
}

// Enum underlying values (FR-031, FR-032, FR-033; plan S6).
static_assert(std::is_same_v<std::underlying_type_t<Mode>, std::uint8_t>);
static_assert(static_cast<int>(Mode::AlwaysRetrigger) == 0);
static_assert(static_cast<int>(Mode::Legato) == 1);
static_assert(static_cast<int>(Mode::EnvelopeRetriggerOnly) == 2);
static_assert(static_cast<int>(Mode::OscillatorContinuous) == 3);
static_assert(std::is_same_v<std::underlying_type_t<Retrigger>, std::uint8_t>);
static_assert(static_cast<int>(Retrigger::Reset) == 0);
static_assert(static_cast<int>(Retrigger::Continue) == 1);
static_assert(std::is_same_v<std::underlying_type_t<Transition>, std::uint8_t>);
static_assert(static_cast<int>(Transition::FromSilence) == 0);
static_assert(static_cast<int>(Transition::Overlap) == 1);
static_assert(static_cast<int>(Transition::Tail) == 2);
static_assert(static_cast<int>(Transition::ReturnToHeld) == 3);
static_assert(static_cast<int>(Transition::VoiceReuse) == 4);
static_assert(std::is_same_v<std::underlying_type_t<PhasePolicy>, std::uint8_t>);
static_assert(static_cast<int>(PhasePolicy::Reset) == 0);
static_assert(static_cast<int>(PhasePolicy::FreeRunning) == 1);

// SC-009(a) at compile time: the 80 cells, the Continue collapse, Reset distinctness.
static_assert(allCellsMatch(), "SC-009(a): resolver differs from the FR-033 / FR-034 tables");
static_assert(continuePairCollapses(0, 2),
              "FR-036: AlwaysRetrigger / EnvelopeRetriggerOnly must collapse under Continue");
static_assert(continuePairCollapses(1, 3),
              "FR-036: Legato / OscillatorContinuous must collapse under Continue");
static_assert(everyResetPairDiffers(), "D-12: every mode pair must differ under Reset");

} // namespace

TEST_CASE("NoteMemory_ResolverTable", "[processors][profundum][note-memory]")
{
    SECTION("all 80 cells, member and static forms, equal the FR-033 / FR-034 tables")
    {
        int checked = 0;
        for (int m = 0; m < kNumModes; ++m) {
            for (int r = 0; r < kNumRetriggers; ++r) {
                NoteMemory nm;
                nm.setMode(static_cast<Mode>(m));
                nm.setRetrigger(static_cast<Retrigger>(r));
                REQUIRE(static_cast<int>(nm.mode()) == m);
                REQUIRE(static_cast<int>(nm.retrigger()) == r);
                for (int t = 0; t < kNumTransitions; ++t) {
                    for (int a = 0; a < 2; ++a) {
                        const bool audible = a != 0;
                        INFO("mode " << m << " retrigger " << r << " transition " << t << " audible "
                                     << a);
                        const Actions expected = expectedActions(m, r, t, audible);
                        const Actions member = nm.resolve(static_cast<Transition>(t), audible);
                        const Actions stat = resolveStatic(m, r, t, audible);
                        CHECK(actionsEqual(member, expected));
                        CHECK(actionsEqual(stat, expected));
                        // FR-035 determinism: a repeated call returns the same Actions.
                        CHECK(actionsEqual(nm.resolve(static_cast<Transition>(t), audible), member));
                        ++checked;
                    }
                }
            }
        }
        REQUIRE(checked == 80);
    }

    SECTION("Continue collapse (FR-036, D-12) and Reset distinctness")
    {
        for (int t = 0; t < kNumTransitions; ++t) {
            for (int a = 0; a < 2; ++a) {
                INFO("transition " << t << " audible " << a);
                CHECK(actedOnFieldsEqual(resolveStatic(0, 1, t, a != 0), resolveStatic(2, 1, t, a != 0)));
                CHECK(actedOnFieldsEqual(resolveStatic(1, 1, t, a != 0), resolveStatic(3, 1, t, a != 0)));
                // Under Continue, phase is FreeRunning and declick false everywhere;
                // Drop and re-arm stay on the envelope column (FR-036).
                for (int m = 0; m < kNumModes; ++m) {
                    const Actions act = resolveStatic(m, 1, t, a != 0);
                    CHECK(act.phase == PhasePolicy::FreeRunning);
                    CHECK_FALSE(act.declick);
                    CHECK(act.dropTrigger == act.envelopeRestart);
                    CHECK(act.anchorRearm == act.envelopeRestart);
                }
            }
        }
        for (int x = 0; x < kNumModes; ++x) {
            for (int y = x + 1; y < kNumModes; ++y) {
                INFO("modes " << x << " vs " << y);
                CHECK(modesDifferUnder(0, x, y));
            }
        }
    }

    SECTION("defaults and out-of-enum setters")
    {
        NoteMemory nm;
        CHECK(nm.mode() == Mode::AlwaysRetrigger);
        CHECK(nm.retrigger() == Retrigger::Reset);

        nm.setMode(Mode::Legato);
        nm.setRetrigger(Retrigger::Continue);
        nm.setMode(static_cast<Mode>(4));
        nm.setRetrigger(static_cast<Retrigger>(2));
        CHECK(nm.mode() == Mode::Legato);
        CHECK(nm.retrigger() == Retrigger::Continue);

        NoteMemory fresh;
        fresh.setMode(static_cast<Mode>(4));
        fresh.setRetrigger(static_cast<Retrigger>(2));
        CHECK(fresh.mode() == Mode::AlwaysRetrigger);
        CHECK(fresh.retrigger() == Retrigger::Reset);
    }
}
