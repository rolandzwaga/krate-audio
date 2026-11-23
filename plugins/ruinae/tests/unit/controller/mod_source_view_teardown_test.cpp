// ==============================================================================
// Mod-Source View Pointer Teardown Tests
// ==============================================================================
// The mod-source views (LFO1/LFO2 waveform, chaos, rungler, sample & hold,
// random, the sidechain indicator labels, and the rate / note-value groups) are
// owned by the editor frame, but the controller caches raw pointers to them.
// Three paths destroy those views:
//
//   1. willClose()                -- editor teardown            (F001)
//   2. MainTab switch away from MOD                              (F003)
//   3. ModSourceViewMode switch   -- UIViewSwitchContainer      (F002)
//
// Every deref site guards with `if (ptr)`, which a dangling non-null pointer
// passes, so the invariant is: a cached pointer is non-null exactly while its
// view is in the live frame tree. The switches are driven through their bound
// controls in the real order (view_lifecycle_rig.h), where the incoming template
// is built inside the base setParamNormalized call: a test that changes the
// parameter first and switches the container afterwards hides both the dangling
// outgoing pointers and the nulled incoming ones (R-194).

#include "view_lifecycle_rig.h"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>

using namespace RuinaeViewLifecycleTest;

namespace {

constexpr int kModSourceRunglerIndex = 4;

const std::set<std::string> kModSourcePointers{
    "lfo1RateGroup_",        "lfo2RateGroup_",       "lfo1NoteValueGroup_",
    "lfo2NoteValueGroup_",   "lfo1WaveformDisplay_", "lfo2WaveformDisplay_",
    "chaosModDisplay_",      "runglerDisplay_",      "sampleHoldDisplay_",
    "randomModDisplay_",     "sidechainIndicatorEnvFollower_",
    "sidechainIndicatorPitchFollower_",               "sidechainIndicatorTransient_",
    "chaosRateGroup_",       "chaosNoteValueGroup_", "shRateGroup_",
    "shNoteValueGroup_",     "randomRateGroup_",     "randomNoteValueGroup_"};

int countNonNull(const ViewLifecycleRig& rig, const std::set<std::string>& names) {
    int n = 0;
    for (const auto& name : names)
        if (rig.pointer(name)) ++n;
    return n;
}

}  // namespace

TEST_CASE("willClose nulls every cached mod-source view pointer",
          "[ruinae][controller][ui][lifecycle]")
{
    ViewLifecycleRig rig({{Ruinae::kMainTabTag, kTabMod}});

    // Guard against a vacuous test: if the open path never captured any of these
    // pointers there is nothing for teardown to clear and a green result is a lie.
    REQUIRE(countNonNull(rig, kModSourcePointers) > 0);

    rig.closeEditor();  // -> close() -> willClose()

    CHECK(rig.nonNullPointers().empty());
}

TEST_CASE("ModSourceViewMode switch nulls the destroyed mod-source pointers",
          "[ruinae][controller][ui][lifecycle]")
{
    ViewLifecycleRig rig({{Ruinae::kMainTabTag, kTabMod}, {Ruinae::kModSourceViewModeTag, modSourceNorm(0)}});
    REQUIRE(countNonNull(rig, kModSourcePointers) > 0);

    // Each switch destroys the previous template's views and builds the next one's.
    // Whatever pointed into the outgoing template must be null, and the incoming
    // template's views must be cached exactly as a fresh open on it caches them.
    for (int i = 1; i <= kModSourceCount; ++i) {
        const int index = i % kModSourceCount;  // ends back on LFO1
        INFO("ModSourceViewMode -> " << index);
        rig.clickControl(Ruinae::kModSourceViewModeTag, modSourceNorm(index));
        checkMatchesFreshOpen(rig, {{Ruinae::kMainTabTag, kTabMod},
                                    {Ruinae::kModSourceViewModeTag, modSourceNorm(index)}});
    }
}

TEST_CASE("MainTab switch away from MOD nulls the mod-source pointers",
          "[ruinae][controller][ui][lifecycle]")
{
    // Rungler: a template whose display is not among the LFO / chaos views.
    ViewLifecycleRig rig({{Ruinae::kMainTabTag, kTabMod},
                          {Ruinae::kModSourceViewModeTag, modSourceNorm(kModSourceRunglerIndex)}});
    REQUIRE(rig.isLive(rig.pointer("runglerDisplay_")));

    rig.clickControl(Ruinae::kMainTabTag, kTabSound);

    CHECK(countNonNull(rig, kModSourcePointers) == 0);
    checkMatchesFreshOpen(rig, {{Ruinae::kMainTabTag, kTabSound},
                                {Ruinae::kModSourceViewModeTag, modSourceNorm(kModSourceRunglerIndex)}});
}
