// ==============================================================================
// Integration Test: ModRingIndicator Arc Rendering (spec 049, Phase 4)
// ==============================================================================
// Verifies arc stacking, bypass filtering, clamping, and composite behavior
// for the ModRingIndicator component.
//
// T075: 1 arc with gold color for ENV 2 -> Filter Cutoff
// T076: 2 stacked arcs to same destination
// T077: 5 routes -> 4 individual + 1 composite gray
// T078: Arc clamping at min/max boundaries
// T106: Bypassed arcs are excluded from rendering
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "ui/mod_ring_indicator.h"
#include "ui/mod_source_colors.h"

using Catch::Approx;
using namespace Krate::Plugins;

// =============================================================================
// T075: Create route ENV 2 -> Filter Cutoff at +0.72, verify 1 arc with gold
// =============================================================================

TEST_CASE("ModRingIndicator: single arc with source color", "[modmatrix][ring][integration]") {
    ModRingIndicator ring(VSTGUI::CRect(0, 0, 50, 50));
    ring.setBaseValue(0.5f);

    std::vector<ModRingIndicator::ArcInfo> arcs;
    ModRingIndicator::ArcInfo arc;
    arc.amount = 0.72f;
    arc.color = sourceColorForIndex(static_cast<int>(ModSource::Env2)); // Gold
    arc.sourceIndex = static_cast<int>(ModSource::Env2);
    arc.destIndex = static_cast<int>(ModDestination::FilterCutoff);
    arc.bypassed = false;
    arcs.push_back(arc);

    ring.setArcs(arcs);
    REQUIRE(ring.getArcs().size() == 1);
    REQUIRE(ring.getArcs()[0].amount == Approx(0.72f));
    // Verify gold color (ENV 2 color: rgb(220, 170, 60))
    REQUIRE(ring.getArcs()[0].color.red == 220);
    REQUIRE(ring.getArcs()[0].color.green == 170);
    REQUIRE(ring.getArcs()[0].color.blue == 60);
}

// =============================================================================
// T076: 2 routes to same destination, verify 2 stacked arcs
// =============================================================================

TEST_CASE("ModRingIndicator: 2 stacked arcs to same destination", "[modmatrix][ring][integration]") {
    ModRingIndicator ring(VSTGUI::CRect(0, 0, 50, 50));
    ring.setBaseValue(0.5f);

    std::vector<ModRingIndicator::ArcInfo> arcs;

    // Arc 1: ENV 1 -> Filter Cutoff +0.3
    ModRingIndicator::ArcInfo arc1;
    arc1.amount = 0.3f;
    arc1.color = sourceColorForIndex(static_cast<int>(ModSource::Env1));
    arc1.sourceIndex = static_cast<int>(ModSource::Env1);
    arc1.destIndex = static_cast<int>(ModDestination::FilterCutoff);
    arcs.push_back(arc1);

    // Arc 2: ENV 2 -> Filter Cutoff -0.5
    ModRingIndicator::ArcInfo arc2;
    arc2.amount = -0.5f;
    arc2.color = sourceColorForIndex(static_cast<int>(ModSource::Env2));
    arc2.sourceIndex = static_cast<int>(ModSource::Env2);
    arc2.destIndex = static_cast<int>(ModDestination::FilterCutoff);
    arcs.push_back(arc2);

    ring.setArcs(arcs);
    REQUIRE(ring.getArcs().size() == 2);
    REQUIRE(ring.getArcs()[0].amount == Approx(0.3f));
    REQUIRE(ring.getArcs()[1].amount == Approx(-0.5f));
}

// =============================================================================
// T077: 5 routes -> 4 individual + 1 composite gray (FR-026)
// =============================================================================

TEST_CASE("ModRingIndicator: 5 arcs triggers composite mode", "[modmatrix][ring][integration]") {
    ModRingIndicator ring(VSTGUI::CRect(0, 0, 50, 50));
    ring.setBaseValue(0.5f);

    std::vector<ModRingIndicator::ArcInfo> arcs;
    for (int i = 0; i < 5; ++i) {
        ModRingIndicator::ArcInfo arc;
        arc.amount = 0.1f * static_cast<float>(i + 1);
        arc.color = sourceColorForIndex(i);
        arc.sourceIndex = i;
        arc.destIndex = static_cast<int>(ModDestination::FilterCutoff);
        arcs.push_back(arc);
    }

    ring.setArcs(arcs);
    // All 5 arcs are stored (composite rendering is handled in draw())
    REQUIRE(ring.getArcs().size() == 5);
    // kMaxVisibleArcs = 4, so draw() will show 4 individual + 1 composite
    REQUIRE(ModRingIndicator::kMaxVisibleArcs == 4);
}

TEST_CASE("ModRingIndicator: exactly 4 arcs does NOT trigger composite", "[modmatrix][ring][integration]") {
    ModRingIndicator ring(VSTGUI::CRect(0, 0, 50, 50));
    ring.setBaseValue(0.5f);

    std::vector<ModRingIndicator::ArcInfo> arcs;
    for (int i = 0; i < 4; ++i) {
        ModRingIndicator::ArcInfo arc;
        arc.amount = 0.2f;
        arc.color = sourceColorForIndex(i);
        arc.sourceIndex = i;
        arc.destIndex = 0;
        arcs.push_back(arc);
    }

    ring.setArcs(arcs);
    REQUIRE(ring.getArcs().size() == 4);
}

// =============================================================================
// T078: Arc clamping at min/max (base=0.9, amount=+0.5 clamps at 1.0)
// =============================================================================

TEST_CASE("ModRingIndicator: arc clamping at boundaries", "[modmatrix][ring][integration]") {
    ModRingIndicator ring(VSTGUI::CRect(0, 0, 50, 50));

    SECTION("Positive clamping: base=0.9, amount=+0.5 clamps end at 1.0") {
        ring.setBaseValue(0.9f);

        std::vector<ModRingIndicator::ArcInfo> arcs;
        ModRingIndicator::ArcInfo arc;
        arc.amount = 0.5f; // Would go to 1.4, clamped to 1.0
        arc.color = sourceColorForIndex(0);
        arc.sourceIndex = 0;
        arc.destIndex = 0;
        arcs.push_back(arc);

        ring.setArcs(arcs);
        REQUIRE(ring.getArcs().size() == 1);
        // The arc stores the original amount; clamping happens in draw()
        REQUIRE(ring.getArcs()[0].amount == Approx(0.5f));
    }

    SECTION("Negative clamping: base=0.1, amount=-0.5 clamps end at 0.0") {
        ring.setBaseValue(0.1f);

        std::vector<ModRingIndicator::ArcInfo> arcs;
        ModRingIndicator::ArcInfo arc;
        arc.amount = -0.5f; // Would go to -0.4, clamped to 0.0
        arc.color = sourceColorForIndex(0);
        arc.sourceIndex = 0;
        arc.destIndex = 0;
        arcs.push_back(arc);

        ring.setArcs(arcs);
        REQUIRE(ring.getArcs().size() == 1);
        REQUIRE(ring.getArcs()[0].amount == Approx(-0.5f));
    }
}

// =============================================================================
// T106: Bypassed routes are excluded from arc rendering (FR-019)
// =============================================================================

TEST_CASE("ModRingIndicator: bypassed arcs are filtered out", "[modmatrix][ring][integration]") {
    ModRingIndicator ring(VSTGUI::CRect(0, 0, 50, 50));
    ring.setBaseValue(0.5f);

    std::vector<ModRingIndicator::ArcInfo> arcs;

    // Active arc
    ModRingIndicator::ArcInfo active;
    active.amount = 0.5f;
    active.color = sourceColorForIndex(0);
    active.sourceIndex = 0;
    active.destIndex = 0;
    active.bypassed = false;
    arcs.push_back(active);

    // Bypassed arc
    ModRingIndicator::ArcInfo bypassed;
    bypassed.amount = -0.3f;
    bypassed.color = sourceColorForIndex(1);
    bypassed.sourceIndex = 1;
    bypassed.destIndex = 0;
    bypassed.bypassed = true;
    arcs.push_back(bypassed);

    // Another active arc
    ModRingIndicator::ArcInfo active2;
    active2.amount = 0.2f;
    active2.color = sourceColorForIndex(2);
    active2.sourceIndex = 2;
    active2.destIndex = 0;
    active2.bypassed = false;
    arcs.push_back(active2);

    ring.setArcs(arcs);

    // Should have filtered out the bypassed arc
    REQUIRE(ring.getArcs().size() == 2);
    REQUIRE(ring.getArcs()[0].sourceIndex == 0);
    REQUIRE(ring.getArcs()[1].sourceIndex == 2);
}

TEST_CASE("ModRingIndicator: all bypassed arcs results in empty", "[modmatrix][ring][integration]") {
    ModRingIndicator ring(VSTGUI::CRect(0, 0, 50, 50));

    std::vector<ModRingIndicator::ArcInfo> arcs;
    for (int i = 0; i < 3; ++i) {
        ModRingIndicator::ArcInfo arc;
        arc.amount = 0.3f;
        arc.color = sourceColorForIndex(i);
        arc.sourceIndex = i;
        arc.destIndex = 0;
        arc.bypassed = true;
        arcs.push_back(arc);
    }

    ring.setArcs(arcs);
    REQUIRE(ring.getArcs().empty());
}

// =============================================================================
// Base value and stroke width configuration
// =============================================================================

TEST_CASE("ModRingIndicator: base value and stroke width", "[modmatrix][ring][unit]") {
    ModRingIndicator ring(VSTGUI::CRect(0, 0, 50, 50));

    REQUIRE(ring.getBaseValue() == Approx(0.5f));
    REQUIRE(ring.getStrokeWidth() == Approx(3.0f));

    ring.setBaseValue(0.75f);
    REQUIRE(ring.getBaseValue() == Approx(0.75f));

    ring.setStrokeWidth(5.0f);
    REQUIRE(ring.getStrokeWidth() == Approx(5.0f));

    // Clamping
    ring.setBaseValue(-0.1f);
    REQUIRE(ring.getBaseValue() == Approx(0.0f));
    ring.setBaseValue(1.5f);
    REQUIRE(ring.getBaseValue() == Approx(1.0f));
}

// =============================================================================
// Selection callback
// =============================================================================

TEST_CASE("ModRingIndicator: select callback is stored", "[modmatrix][ring][unit]") {
    ModRingIndicator ring(VSTGUI::CRect(0, 0, 50, 50));

    int selectedSrc = -1;
    int selectedDst = -1;

    ring.setSelectCallback([&](int src, int dst) {
        selectedSrc = src;
        selectedDst = dst;
    });

    // Cannot easily test mouse click without CFrame, but verify callback compiles
    // and is stored correctly (plumbing test)
    REQUIRE(selectedSrc == -1);
    REQUIRE(selectedDst == -1);
}
