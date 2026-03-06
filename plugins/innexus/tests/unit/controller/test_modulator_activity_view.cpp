// ==============================================================================
// ModulatorActivityView Tests (T051)
// ==============================================================================
// FR-038, FR-040: Animated modulation indicator
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "controller/views/modulator_activity_view.h"

using Catch::Approx;

TEST_CASE("ModulatorActivityView construction does not crash",
          "[innexus][ui][modulator]")
{
    VSTGUI::CRect rect(0, 0, 25, 20);
    Innexus::ModulatorActivityView view(rect);

    // Default state
    REQUIRE(view.getModIndex() == 0);
    REQUIRE(view.getPhase() == Approx(0.0f));
    REQUIRE(view.isActive() == false);
}

TEST_CASE("ModulatorActivityView setModIndex sets modIndex",
          "[innexus][ui][modulator]")
{
    VSTGUI::CRect rect(0, 0, 25, 20);
    Innexus::ModulatorActivityView view(rect);

    view.setModIndex(0);
    REQUIRE(view.getModIndex() == 0);

    view.setModIndex(1);
    REQUIRE(view.getModIndex() == 1);
}

TEST_CASE("ModulatorActivityView updateData sets phase and active",
          "[innexus][ui][modulator]")
{
    VSTGUI::CRect rect(0, 0, 25, 20);
    Innexus::ModulatorActivityView view(rect);

    view.updateData(0.5f, true);
    REQUIRE(view.getPhase() == Approx(0.5f));
    REQUIRE(view.isActive() == true);
}

TEST_CASE("ModulatorActivityView updateData with inactive state",
          "[innexus][ui][modulator]")
{
    VSTGUI::CRect rect(0, 0, 25, 20);
    Innexus::ModulatorActivityView view(rect);

    view.updateData(0.0f, false);
    REQUIRE(view.isActive() == false);
}
