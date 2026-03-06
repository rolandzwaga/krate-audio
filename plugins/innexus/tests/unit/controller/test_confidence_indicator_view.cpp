// ==============================================================================
// ConfidenceIndicatorView Unit Tests
// ==============================================================================
// T021: Color zones, freqToNoteName, updateData
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "controller/views/confidence_indicator_view.h"
#include "controller/display_data.h"

#include <string>

using Catch::Approx;
using Innexus::ConfidenceIndicatorView;
using Innexus::DisplayData;

TEST_CASE("ConfidenceIndicatorView getConfidenceColor red for low confidence", "[innexus][ui][confidence]")
{
    auto color = ConfidenceIndicatorView::getConfidenceColor(0.1f);
    // Red-dominant: red channel should be highest
    REQUIRE(color.red > color.green);
    REQUIRE(color.red > color.blue);
}

TEST_CASE("ConfidenceIndicatorView getConfidenceColor yellow for moderate confidence", "[innexus][ui][confidence]")
{
    auto color = ConfidenceIndicatorView::getConfidenceColor(0.5f);
    // Yellow-dominant: red and green high, blue low
    REQUIRE(color.red > 100);
    REQUIRE(color.green > 100);
    REQUIRE(color.blue < 100);
}

TEST_CASE("ConfidenceIndicatorView getConfidenceColor green for high confidence", "[innexus][ui][confidence]")
{
    auto color = ConfidenceIndicatorView::getConfidenceColor(0.9f);
    // Green-dominant: green channel should be highest
    REQUIRE(color.green > color.red);
    REQUIRE(color.green > color.blue);
}

TEST_CASE("ConfidenceIndicatorView freqToNoteName 440 Hz returns A4", "[innexus][ui][confidence]")
{
    auto name = ConfidenceIndicatorView::freqToNoteName(440.0f);
    REQUIRE(name.find("A4") != std::string::npos);
}

TEST_CASE("ConfidenceIndicatorView freqToNoteName 261.63 Hz returns C4", "[innexus][ui][confidence]")
{
    auto name = ConfidenceIndicatorView::freqToNoteName(261.63f);
    REQUIRE(name.find("C4") != std::string::npos);
}

TEST_CASE("ConfidenceIndicatorView freqToNoteName 0 Hz returns placeholder", "[innexus][ui][confidence]")
{
    auto name = ConfidenceIndicatorView::freqToNoteName(0.0f);
    REQUIRE(name == "--");
}

TEST_CASE("ConfidenceIndicatorView freqToNoteName negative returns placeholder", "[innexus][ui][confidence]")
{
    auto name = ConfidenceIndicatorView::freqToNoteName(-100.0f);
    REQUIRE(name == "--");
}

TEST_CASE("ConfidenceIndicatorView updateData sets confidence correctly", "[innexus][ui][confidence]")
{
    VSTGUI::CRect rect(0, 0, 150, 140);
    ConfidenceIndicatorView view(rect);

    DisplayData data{};
    data.f0Confidence = 0.0f;
    data.f0 = 0.0f;
    view.updateData(data);
    REQUIRE(view.getConfidence() == Approx(0.0f));
    REQUIRE(view.getF0() == Approx(0.0f));
}

TEST_CASE("ConfidenceIndicatorView updateData stores frequency", "[innexus][ui][confidence]")
{
    VSTGUI::CRect rect(0, 0, 150, 140);
    ConfidenceIndicatorView view(rect);

    DisplayData data{};
    data.f0Confidence = 0.85f;
    data.f0 = 440.0f;
    view.updateData(data);
    REQUIRE(view.getConfidence() == Approx(0.85f));
    REQUIRE(view.getF0() == Approx(440.0f));
}
