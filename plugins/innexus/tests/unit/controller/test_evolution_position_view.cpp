// ==============================================================================
// EvolutionPositionView Tests (T050)
// ==============================================================================
// FR-036: Horizontal track with playhead and ghost marker
// FR-050: Ghost marker tracks manualMorphPosition
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "controller/views/evolution_position_view.h"
#include "controller/display_data.h"

using Catch::Approx;

TEST_CASE("EvolutionPositionView construction does not crash",
          "[innexus][ui][evolution]")
{
    VSTGUI::CRect rect(0, 0, 180, 20);
    Innexus::EvolutionPositionView view(rect);

    // Default state
    REQUIRE(view.getPosition() == Approx(0.0f));
    REQUIRE(view.getManualPosition() == Approx(0.0f));
    REQUIRE(view.getShowGhost() == false);
}

TEST_CASE("EvolutionPositionView updateData sets position from evolutionPosition",
          "[innexus][ui][evolution]")
{
    VSTGUI::CRect rect(0, 0, 180, 20);
    Innexus::EvolutionPositionView view(rect);

    Innexus::DisplayData data{};
    data.evolutionPosition = 0.5f;
    data.manualMorphPosition = 0.0f;

    view.updateData(data, true);

    REQUIRE(view.getPosition() == Approx(0.5f));
}

TEST_CASE("EvolutionPositionView updateData sets both position and manualPosition",
          "[innexus][ui][evolution]")
{
    VSTGUI::CRect rect(0, 0, 180, 20);
    Innexus::EvolutionPositionView view(rect);

    Innexus::DisplayData data{};
    data.evolutionPosition = 0.0f;
    data.manualMorphPosition = 0.3f;

    view.updateData(data, true);

    REQUIRE(view.getPosition() == Approx(0.0f));
    REQUIRE(view.getManualPosition() == Approx(0.3f));
}

TEST_CASE("EvolutionPositionView showGhost tracks evolutionActive parameter",
          "[innexus][ui][evolution]")
{
    VSTGUI::CRect rect(0, 0, 180, 20);
    Innexus::EvolutionPositionView view(rect);

    Innexus::DisplayData data{};
    data.evolutionPosition = 0.5f;
    data.manualMorphPosition = 0.3f;

    // When evolution is active, ghost should be shown
    view.updateData(data, true);
    REQUIRE(view.getShowGhost() == true);

    // When evolution is inactive, ghost should be hidden
    view.updateData(data, false);
    REQUIRE(view.getShowGhost() == false);
}
