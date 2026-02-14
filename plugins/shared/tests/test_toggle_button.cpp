// ==============================================================================
// ToggleButton Tests (052-expand-master-section)
// ==============================================================================
// Tests for gear icon style extension to the ToggleButton custom view.
// Tests cover enum/string conversion, ToggleButton construction with gear icon
// style, and edge-case parameter configurations.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "ui/toggle_button.h"

using namespace Krate::Plugins;

// ==============================================================================
// Gear Icon Style String Conversion Tests
// ==============================================================================

TEST_CASE("iconStyleFromString returns kGear for 'gear'", "[toggle_button][gear]") {
    IconStyle style = iconStyleFromString("gear");
    REQUIRE(style == IconStyle::kGear);
}

TEST_CASE("iconStyleToString returns 'gear' for kGear", "[toggle_button][gear]") {
    std::string name = iconStyleToString(IconStyle::kGear);
    REQUIRE(name == "gear");
}

TEST_CASE("iconStyleFromString returns kPower for unknown strings", "[toggle_button]") {
    REQUIRE(iconStyleFromString("unknown") == IconStyle::kPower);
    REQUIRE(iconStyleFromString("") == IconStyle::kPower);
}

TEST_CASE("iconStyleFromString round-trips all styles", "[toggle_button]") {
    REQUIRE(iconStyleFromString(iconStyleToString(IconStyle::kPower)) == IconStyle::kPower);
    REQUIRE(iconStyleFromString(iconStyleToString(IconStyle::kChevron)) == IconStyle::kChevron);
    REQUIRE(iconStyleFromString(iconStyleToString(IconStyle::kGear)) == IconStyle::kGear);
}

// ==============================================================================
// Gear Icon ToggleButton Construction Tests (T013a)
// ==============================================================================
// These tests verify that a ToggleButton can be constructed with gear icon style
// and configured for icon+title mode without errors. Actual rendering requires
// a CDrawContext (visual verification in Phase 3).

TEST_CASE("ToggleButton with gear icon style can be constructed", "[toggle_button][gear]") {
    VSTGUI::CRect rect(0, 0, 80, 18);
    auto* button = new ToggleButton(rect, nullptr, -1);
    button->setIconStyle(IconStyle::kGear);
    button->setTitle("Settings");
    button->setTitlePosition(TitlePosition::kRight);

    REQUIRE(button->getIconStyle() == IconStyle::kGear);
    REQUIRE(button->getTitle() == "Settings");
    REQUIRE(button->getTitlePosition() == TitlePosition::kRight);

    button->forget();
}

TEST_CASE("ToggleButton gear icon+title mode configures correctly", "[toggle_button][gear]") {
    VSTGUI::CRect rect(0, 0, 80, 18);
    auto* button = new ToggleButton(rect, nullptr, -1);
    button->setIconStyle(IconStyle::kGear);
    button->setTitlePosition(TitlePosition::kLeft);
    button->setTitle("Gear");
    button->setIconSize(0.65f);
    button->setStrokeWidth(1.5);

    REQUIRE(button->getIconStyle() == IconStyle::kGear);
    REQUIRE(button->getTitlePosition() == TitlePosition::kLeft);
    REQUIRE(button->getIconSize() == 0.65f);
    REQUIRE(button->getStrokeWidth() == 1.5);

    button->forget();
}

// ==============================================================================
// Edge-Case Parameter Tests (T013b)
// ==============================================================================
// Verify that gear icon with edge-case parameters does not crash during
// construction and configuration. Rendering verification is visual.

TEST_CASE("ToggleButton gear icon with iconSize=0 does not crash on construction",
          "[toggle_button][gear][edge]") {
    VSTGUI::CRect rect(0, 0, 18, 18);
    auto* button = new ToggleButton(rect, nullptr, -1);
    button->setIconStyle(IconStyle::kGear);
    button->setIconSize(0.0f);

    REQUIRE(button->getIconSize() == 0.0f);
    REQUIRE(button->getIconStyle() == IconStyle::kGear);

    button->forget();
}

TEST_CASE("ToggleButton gear icon with iconSize=1.0 does not crash on construction",
          "[toggle_button][gear][edge]") {
    VSTGUI::CRect rect(0, 0, 18, 18);
    auto* button = new ToggleButton(rect, nullptr, -1);
    button->setIconStyle(IconStyle::kGear);
    button->setIconSize(1.0f);

    REQUIRE(button->getIconSize() == 1.0f);
    REQUIRE(button->getIconStyle() == IconStyle::kGear);

    button->forget();
}

TEST_CASE("ToggleButton gear icon with strokeWidth=0 does not crash on construction",
          "[toggle_button][gear][edge]") {
    VSTGUI::CRect rect(0, 0, 18, 18);
    auto* button = new ToggleButton(rect, nullptr, -1);
    button->setIconStyle(IconStyle::kGear);
    button->setStrokeWidth(0.0);

    REQUIRE(button->getStrokeWidth() == 0.0);
    REQUIRE(button->getIconStyle() == IconStyle::kGear);

    button->forget();
}
