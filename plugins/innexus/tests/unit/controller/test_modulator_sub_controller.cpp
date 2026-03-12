// ==============================================================================
// ModulatorSubController Tests (T052)
// ==============================================================================
// FR-046: Reusable modulator template with sub-controller for tag remapping
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "controller/modulator_sub_controller.h"
#include "plugin_ids.h"

TEST_CASE("ModulatorSubController index 0 resolves Mod.Enable to kMod1EnableId",
          "[innexus][ui][sub-controller]")
{
    Innexus::ModulatorSubController sc(0, nullptr);
    int32_t tag = sc.getTagForName("Mod.Enable", -1);
    REQUIRE(tag == static_cast<int32_t>(Innexus::kMod1EnableId));
    REQUIRE(tag == 610);
}

TEST_CASE("ModulatorSubController index 1 resolves Mod.Enable to kMod2EnableId",
          "[innexus][ui][sub-controller]")
{
    Innexus::ModulatorSubController sc(1, nullptr);
    int32_t tag = sc.getTagForName("Mod.Enable", -1);
    REQUIRE(tag == static_cast<int32_t>(Innexus::kMod2EnableId));
    REQUIRE(tag == 620);
}

TEST_CASE("ModulatorSubController index 0 resolves Mod.Rate to kMod1RateId",
          "[innexus][ui][sub-controller]")
{
    Innexus::ModulatorSubController sc(0, nullptr);
    int32_t tag = sc.getTagForName("Mod.Rate", -1);
    REQUIRE(tag == static_cast<int32_t>(Innexus::kMod1RateId));
    REQUIRE(tag == 612);
}

TEST_CASE("ModulatorSubController index 1 resolves Mod.Rate to kMod2RateId",
          "[innexus][ui][sub-controller]")
{
    Innexus::ModulatorSubController sc(1, nullptr);
    int32_t tag = sc.getTagForName("Mod.Rate", -1);
    REQUIRE(tag == static_cast<int32_t>(Innexus::kMod2RateId));
    REQUIRE(tag == 622);
}

TEST_CASE("ModulatorSubController index 0 resolves Mod.Target to kMod1TargetId",
          "[innexus][ui][sub-controller]")
{
    Innexus::ModulatorSubController sc(0, nullptr);
    int32_t tag = sc.getTagForName("Mod.Target", -1);
    REQUIRE(tag == static_cast<int32_t>(Innexus::kMod1TargetId));
    REQUIRE(tag == 616);
}

TEST_CASE("ModulatorSubController resolves all Mod tags for index 0",
          "[innexus][ui][sub-controller]")
{
    Innexus::ModulatorSubController sc(0, nullptr);

    REQUIRE(sc.getTagForName("Mod.Enable", -1) == 610);
    REQUIRE(sc.getTagForName("Mod.Waveform", -1) == 611);
    REQUIRE(sc.getTagForName("Mod.Rate", -1) == 612);
    REQUIRE(sc.getTagForName("Mod.Depth", -1) == 613);
    REQUIRE(sc.getTagForName("Mod.RangeStart", -1) == 614);
    REQUIRE(sc.getTagForName("Mod.RangeEnd", -1) == 615);
    REQUIRE(sc.getTagForName("Mod.Target", -1) == 616);
    REQUIRE(sc.getTagForName("Mod.RateSync", -1) == 617);
    REQUIRE(sc.getTagForName("Mod.NoteValue", -1) == 618);
}

TEST_CASE("ModulatorSubController resolves all Mod tags for index 1",
          "[innexus][ui][sub-controller]")
{
    Innexus::ModulatorSubController sc(1, nullptr);

    REQUIRE(sc.getTagForName("Mod.Enable", -1) == 620);
    REQUIRE(sc.getTagForName("Mod.Waveform", -1) == 621);
    REQUIRE(sc.getTagForName("Mod.Rate", -1) == 622);
    REQUIRE(sc.getTagForName("Mod.Depth", -1) == 623);
    REQUIRE(sc.getTagForName("Mod.RangeStart", -1) == 624);
    REQUIRE(sc.getTagForName("Mod.RangeEnd", -1) == 625);
    REQUIRE(sc.getTagForName("Mod.Target", -1) == 626);
    REQUIRE(sc.getTagForName("Mod.RateSync", -1) == 627);
    REQUIRE(sc.getTagForName("Mod.NoteValue", -1) == 628);
}

TEST_CASE("ModulatorSubController index 0 resolves Mod.RateSync to kMod1RateSyncId (617)",
          "[innexus][ui][sub-controller]")
{
    Innexus::ModulatorSubController sc(0, nullptr);
    int32_t tag = sc.getTagForName("Mod.RateSync", -1);
    REQUIRE(tag == static_cast<int32_t>(Innexus::kMod1RateSyncId));
    REQUIRE(tag == 617);
}

TEST_CASE("ModulatorSubController index 1 resolves Mod.RateSync to kMod2RateSyncId (627)",
          "[innexus][ui][sub-controller]")
{
    Innexus::ModulatorSubController sc(1, nullptr);
    int32_t tag = sc.getTagForName("Mod.RateSync", -1);
    REQUIRE(tag == static_cast<int32_t>(Innexus::kMod2RateSyncId));
    REQUIRE(tag == 627);
}

TEST_CASE("ModulatorSubController index 0 resolves Mod.NoteValue to kMod1NoteValueId (618)",
          "[innexus][ui][sub-controller]")
{
    Innexus::ModulatorSubController sc(0, nullptr);
    int32_t tag = sc.getTagForName("Mod.NoteValue", -1);
    REQUIRE(tag == static_cast<int32_t>(Innexus::kMod1NoteValueId));
    REQUIRE(tag == 618);
}

TEST_CASE("ModulatorSubController index 1 resolves Mod.NoteValue to kMod2NoteValueId (628)",
          "[innexus][ui][sub-controller]")
{
    Innexus::ModulatorSubController sc(1, nullptr);
    int32_t tag = sc.getTagForName("Mod.NoteValue", -1);
    REQUIRE(tag == static_cast<int32_t>(Innexus::kMod2NoteValueId));
    REQUIRE(tag == 628);
}

TEST_CASE("ModulatorSubController returns registeredTag for unrecognized name",
          "[innexus][ui][sub-controller]")
{
    Innexus::ModulatorSubController sc(0, nullptr);

    // Unrecognized tag should return the registeredTag unchanged
    int32_t tag = sc.getTagForName("UnknownTag", 42);
    REQUIRE(tag == 42);
}
