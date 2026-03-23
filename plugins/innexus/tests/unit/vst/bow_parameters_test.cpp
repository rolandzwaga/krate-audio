// =============================================================================
// Bow Parameter ID Tests (Spec 130, Phase 7 T060)
// =============================================================================
// Verifies that bow parameter IDs are declared in plugin_ids.h with unique
// values that do not collide with existing IDs.

#include "plugin_ids.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Bow parameter IDs are correctly defined", "[vst][innexus][bow]")
{
    SECTION("kBowPressureId is defined and in 820-829 range")
    {
        REQUIRE(Innexus::kBowPressureId >= 820);
        REQUIRE(Innexus::kBowPressureId <= 829);
    }

    SECTION("kBowSpeedId is defined and in 820-829 range")
    {
        REQUIRE(Innexus::kBowSpeedId >= 820);
        REQUIRE(Innexus::kBowSpeedId <= 829);
    }

    SECTION("kBowPositionId is defined and in 820-829 range")
    {
        REQUIRE(Innexus::kBowPositionId >= 820);
        REQUIRE(Innexus::kBowPositionId <= 829);
    }

    SECTION("kBowOversamplingId is defined and in 820-829 range")
    {
        REQUIRE(Innexus::kBowOversamplingId >= 820);
        REQUIRE(Innexus::kBowOversamplingId <= 829);
    }

    SECTION("All bow IDs are unique from each other")
    {
        REQUIRE(Innexus::kBowPressureId != Innexus::kBowSpeedId);
        REQUIRE(Innexus::kBowPressureId != Innexus::kBowPositionId);
        REQUIRE(Innexus::kBowPressureId != Innexus::kBowOversamplingId);
        REQUIRE(Innexus::kBowSpeedId != Innexus::kBowPositionId);
        REQUIRE(Innexus::kBowSpeedId != Innexus::kBowOversamplingId);
        REQUIRE(Innexus::kBowPositionId != Innexus::kBowOversamplingId);
    }

    SECTION("Bow IDs do not collide with existing physical modelling IDs")
    {
        REQUIRE(Innexus::kBowPressureId != Innexus::kPhysModelMixId);
        REQUIRE(Innexus::kBowPressureId != Innexus::kResonanceDecayId);
        REQUIRE(Innexus::kBowPressureId != Innexus::kExciterTypeId);
        REQUIRE(Innexus::kBowPressureId != Innexus::kImpactHardnessId);
        REQUIRE(Innexus::kBowPressureId != Innexus::kImpactMassId);
        REQUIRE(Innexus::kBowPressureId != Innexus::kImpactBrightnessId);
        REQUIRE(Innexus::kBowPressureId != Innexus::kImpactPositionId);
        REQUIRE(Innexus::kBowPressureId != Innexus::kResonanceTypeId);
        REQUIRE(Innexus::kBowPressureId != Innexus::kWaveguideStiffnessId);
        REQUIRE(Innexus::kBowPressureId != Innexus::kWaveguidePickPositionId);
    }
}
