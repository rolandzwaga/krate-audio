// ==============================================================================
// MultiTap Copy / Reset pattern buttons: dark-theme styling (design-system.md 5.6, 9.3)
// ==============================================================================
// CTextButton defaults to a light 220-to-180 grey gradient with white highlighted text (cbuttons.cpp), unreadable on
// the dark panel. Both buttons set the full CTextButton colour set, and each gradient is held exactly once: a bare
// CGradient::create handed to setGradient would leak one reference per panel instantiation.

#include <editor_lifecycle_harness.h>

#include "controller/custom_views.h"

#include <vstgui/lib/ccolor.h>
#include <vstgui/lib/cgradient.h>

#include <catch2/catch_test_macros.hpp>

namespace {

const VSTGUI::CColor kSliderFrame(58, 58, 70);
const VSTGUI::CColor kSliderTrack(39, 39, 48);
const VSTGUI::CColor kAccentDim(46, 90, 136);
const VSTGUI::CColor kText(230, 230, 236);
const VSTGUI::CColor kTextLight(255, 255, 255);

/// A gradient of exactly two stops at 0 and 1, both @p color.
void checkFlatGradient(const VSTGUI::CGradient* g, const VSTGUI::CColor& color) {
    REQUIRE(g != nullptr);
    const auto& stops = g->getColorStops();
    REQUIRE(stops.size() == 2u);
    CHECK(stops.begin()->first == 0.0);
    CHECK(stops.begin()->second == color);
    CHECK(stops.rbegin()->first == 1.0);
    CHECK(stops.rbegin()->second == color);
}

void checkStyle(const VSTGUI::CTextButton& b) {
    CHECK(b.getFrameColor() == kSliderFrame);
    CHECK(b.getFrameColorHighlighted() == kAccentDim);
    CHECK(b.getTextColor() == kText);
    CHECK(b.getTextColorHighlighted() == kTextLight);
    CHECK(b.getRoundRadius() == 3.0);
    checkFlatGradient(b.getGradient(), kSliderTrack);
    checkFlatGradient(b.getGradientHighlighted(), kAccentDim);
    // The button's SharedPointer is the only owner of each gradient.
    CHECK(b.getGradient()->getNbReference() == 1);
    CHECK(b.getGradientHighlighted()->getNbReference() == 1);
}

}  // namespace

TEST_CASE("MultiTap Copy and Reset pattern buttons use the dark-theme colours", "[multitap][ui]") {
    Krate::TestSupport::ensureVstguiInitialized();
    const VSTGUI::CRect rect(0, 0, 150, 24);

    SECTION("CopyPatternButton") {
        auto button = VSTGUI::owned(new CopyPatternButton(rect, nullptr));
        checkStyle(*button);
    }
    SECTION("ResetPatternButton") {
        auto button = VSTGUI::owned(new ResetPatternButton(rect, nullptr));
        checkStyle(*button);
    }
}
