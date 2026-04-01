// ==============================================================================
// ConfidenceIndicatorView Unit Tests
// ==============================================================================
// T021: Color zones, freqToNoteName, updateData
// Polyphonic extension: modeLabel, poly voice data propagation
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

// =========================================================================
// modeLabel tests
// =========================================================================

TEST_CASE("ConfidenceIndicatorView modeLabel Mono", "[innexus][ui][confidence]")
{
    REQUIRE(ConfidenceIndicatorView::modeLabel(0, false) == "MONO");
    REQUIRE(ConfidenceIndicatorView::modeLabel(0, true) == "MONO");
}

TEST_CASE("ConfidenceIndicatorView modeLabel Poly", "[innexus][ui][confidence]")
{
    REQUIRE(ConfidenceIndicatorView::modeLabel(1, false) == "POLY");
    REQUIRE(ConfidenceIndicatorView::modeLabel(1, true) == "POLY");
}

TEST_CASE("ConfidenceIndicatorView modeLabel Auto mono", "[innexus][ui][confidence]")
{
    REQUIRE(ConfidenceIndicatorView::modeLabel(2, false) == "AUTO>MONOPHONIC");
}

TEST_CASE("ConfidenceIndicatorView modeLabel Auto poly", "[innexus][ui][confidence]")
{
    REQUIRE(ConfidenceIndicatorView::modeLabel(2, true) == "AUTO>POLYPHONIC");
}

// =========================================================================
// Polyphonic voice data propagation tests
// =========================================================================

TEST_CASE("ConfidenceIndicatorView updateData propagates poly voice data", "[innexus][ui][confidence]")
{
    VSTGUI::CRect rect(0, 0, 255, 130);
    ConfidenceIndicatorView view(rect);

    DisplayData data{};
    data.numVoices = 3;
    data.isPolyphonic = 1;
    data.analysisMode = 2; // Auto

    data.voices[0].f0 = 440.0f;
    data.voices[0].confidence = 0.95f;
    data.voices[0].amplitude = 0.8f;

    data.voices[1].f0 = 659.26f;
    data.voices[1].confidence = 0.80f;
    data.voices[1].amplitude = 0.5f;

    data.voices[2].f0 = 277.18f;
    data.voices[2].confidence = 0.60f;
    data.voices[2].amplitude = 0.3f;

    view.updateData(data);

    REQUIRE(view.getNumVoices() == 3);
    REQUIRE(view.getIsPolyphonic() == true);
    REQUIRE(view.getAnalysisMode() == 2);

    // Verify actual voice data propagated
    REQUIRE(view.getVoice(0).f0 == Approx(440.0f));
    REQUIRE(view.getVoice(0).confidence == Approx(0.95f));
    REQUIRE(view.getVoice(0).amplitude == Approx(0.8f));

    REQUIRE(view.getVoice(1).f0 == Approx(659.26f));
    REQUIRE(view.getVoice(1).confidence == Approx(0.80f));

    REQUIRE(view.getVoice(2).f0 == Approx(277.18f));
    REQUIRE(view.getVoice(2).amplitude == Approx(0.3f));
}

TEST_CASE("ConfidenceIndicatorView mono fallback with zero voices", "[innexus][ui][confidence]")
{
    VSTGUI::CRect rect(0, 0, 255, 130);
    ConfidenceIndicatorView view(rect);

    DisplayData data{};
    data.numVoices = 0;
    data.isPolyphonic = 0;
    data.analysisMode = 0; // Mono
    data.f0Confidence = 0.9f;
    data.f0 = 440.0f;

    view.updateData(data);

    // Falls back to mono display using f0/f0Confidence
    REQUIRE(view.getNumVoices() == 0);
    REQUIRE(view.getIsPolyphonic() == false);
    REQUIRE(view.getConfidence() == Approx(0.9f));
    REQUIRE(view.getF0() == Approx(440.0f));
}

TEST_CASE("ConfidenceIndicatorView single voice non-poly uses mono layout", "[innexus][ui][confidence]")
{
    VSTGUI::CRect rect(0, 0, 255, 130);
    ConfidenceIndicatorView view(rect);

    DisplayData data{};
    data.numVoices = 1;
    data.isPolyphonic = 0;
    data.analysisMode = 2; // Auto, but decided mono
    data.f0Confidence = 0.85f;
    data.f0 = 440.0f;

    view.updateData(data);

    REQUIRE(view.getNumVoices() == 1);
    REQUIRE(view.getIsPolyphonic() == false);
    REQUIRE(view.getAnalysisMode() == 2);
}
