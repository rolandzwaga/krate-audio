// ==============================================================================
// IconSegmentButton: segment text font name
// ==============================================================================
// The segment names were drawn in a hard-coded "Helvetica", which Windows replaces with a different face, so a mode
// strip never matched the editor's other text (Iterum UI redesign, visual review 2026-10-28). The font name is now an
// attribute; its default stays "Helvetica", so editors that do not set it (Ruinae, Seraphis) draw exactly as before.

#include <catch2/catch_test_macros.hpp>

#include "ui/icon_segment_button.h"

#include "vstgui/uidescription/uiattributes.h"

#include <string>

using Krate::Plugins::IconSegmentButton;
using Krate::Plugins::IconSegmentButtonCreator;

TEST_CASE("IconSegmentButton font name defaults to Helvetica", "[icon_segment_button][font]") {
    IconSegmentButton btn(VSTGUI::CRect(0, 0, 100, 20), nullptr, -1);
    CHECK(btn.getTextFontName() == "Helvetica");
}

TEST_CASE("IconSegmentButton creator applies and reports font-name", "[icon_segment_button][font]") {
    IconSegmentButtonCreator creator;
    IconSegmentButton btn(VSTGUI::CRect(0, 0, 100, 20), nullptr, -1);

    VSTGUI::UIAttributes attributes;
    attributes.setAttribute("font-name", "Arial");
    REQUIRE(creator.apply(&btn, attributes, nullptr));
    CHECK(btn.getTextFontName() == "Arial");

    std::string value;
    REQUIRE(creator.getAttributeValue(&btn, "font-name", value, nullptr));
    CHECK(value == "Arial");
    CHECK(creator.getAttributeType("font-name") == VSTGUI::IViewCreator::kStringType);

    // A copy keeps it.
    IconSegmentButton copy(btn);
    CHECK(copy.getTextFontName() == "Arial");
}
