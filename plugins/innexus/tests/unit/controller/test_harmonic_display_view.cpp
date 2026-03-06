// ==============================================================================
// HarmonicDisplayView Unit Tests
// ==============================================================================
// T020: Construction, updateData, amplitudeToBarHeight, active/attenuated state
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "controller/views/harmonic_display_view.h"
#include "controller/display_data.h"

using Catch::Approx;
using Innexus::HarmonicDisplayView;
using Innexus::DisplayData;

TEST_CASE("HarmonicDisplayView construction does not crash", "[innexus][ui][harmonic-display]")
{
    VSTGUI::CRect rect(0, 0, 500, 140);
    HarmonicDisplayView view(rect);
    REQUIRE(view.hasData() == false);
}

TEST_CASE("HarmonicDisplayView updateData with zero amplitudes sets hasData", "[innexus][ui][harmonic-display]")
{
    VSTGUI::CRect rect(0, 0, 500, 140);
    HarmonicDisplayView view(rect);

    DisplayData data{};
    // All amplitudes are zero by default
    view.updateData(data);

    REQUIRE(view.hasData() == true);
    for (int i = 0; i < 48; ++i)
        REQUIRE(view.getAmplitude(i) == Approx(0.0f));
}

TEST_CASE("HarmonicDisplayView amplitudeToBarHeight floor at -60 dB", "[innexus][ui][harmonic-display]")
{
    // 0.001 amplitude = 20*log10(0.001) = -60 dB => should be at floor (0)
    float height = HarmonicDisplayView::amplitudeToBarHeight(0.001f, 140.0f);
    REQUIRE(height == Approx(0.0f).margin(0.1f));
}

TEST_CASE("HarmonicDisplayView amplitudeToBarHeight full height at 0 dB", "[innexus][ui][harmonic-display]")
{
    // 1.0 amplitude = 0 dB => full height
    float height = HarmonicDisplayView::amplitudeToBarHeight(1.0f, 140.0f);
    REQUIRE(height == Approx(140.0f));
}

TEST_CASE("HarmonicDisplayView amplitudeToBarHeight below floor returns 0", "[innexus][ui][harmonic-display]")
{
    // Very small amplitude, below -60 dB
    float height = HarmonicDisplayView::amplitudeToBarHeight(0.0001f, 140.0f);
    REQUIRE(height == Approx(0.0f));
}

TEST_CASE("HarmonicDisplayView amplitudeToBarHeight midpoint", "[innexus][ui][harmonic-display]")
{
    // -30 dB is halfway in the dB range [-60, 0]
    // amplitude for -30 dB: 10^(-30/20) = 10^(-1.5) ~ 0.03162
    float amp = 0.03162f;
    float height = HarmonicDisplayView::amplitudeToBarHeight(amp, 140.0f);
    // Should be about half of 140 = 70
    REQUIRE(height == Approx(70.0f).margin(1.0f));
}

TEST_CASE("HarmonicDisplayView active vs attenuated state stored correctly", "[innexus][ui][harmonic-display]")
{
    VSTGUI::CRect rect(0, 0, 500, 140);
    HarmonicDisplayView view(rect);

    DisplayData data{};
    data.partialAmplitudes[0] = 0.5f;
    data.partialAmplitudes[5] = 0.3f;
    data.partialActive[0] = 1;   // active
    data.partialActive[5] = 0;   // attenuated
    data.partialActive[10] = 1;  // active

    view.updateData(data);

    REQUIRE(view.isActive(0) == true);
    REQUIRE(view.isActive(5) == false);
    REQUIRE(view.isActive(10) == true);
    REQUIRE(view.isActive(47) == false);  // default is inactive
    REQUIRE(view.getAmplitude(0) == Approx(0.5f));
    REQUIRE(view.getAmplitude(5) == Approx(0.3f));
}

// ==============================================================================
// T035a: FR-051 — Freeze display behavior
// ==============================================================================
// The processor sends the frozen frame when freeze is active.
// The view faithfully displays whatever data it receives.
// These tests verify that:
// 1. updateData() with advancing frameCounter correctly stores frozen frame data
// 2. Duplicate frameCounter does not overwrite state (dedup in timer callback)

TEST_CASE("HarmonicDisplayView stores frozen frame data when frameCounter advances",
          "[innexus][ui][harmonic-display][freeze]")
{
    VSTGUI::CRect rect(0, 0, 500, 140);
    HarmonicDisplayView view(rect);

    // Simulate a frozen frame sent by the processor
    // (the processor sends the same captured snapshot each process() call)
    DisplayData frozenFrame{};
    frozenFrame.frameCounter = 10;
    frozenFrame.partialAmplitudes[0] = 0.8f;
    frozenFrame.partialAmplitudes[1] = 0.6f;
    frozenFrame.partialAmplitudes[2] = 0.4f;
    frozenFrame.partialActive[0] = 1;
    frozenFrame.partialActive[1] = 1;
    frozenFrame.partialActive[2] = 0; // filtered partial

    view.updateData(frozenFrame);

    REQUIRE(view.hasData() == true);
    REQUIRE(view.getAmplitude(0) == Approx(0.8f));
    REQUIRE(view.getAmplitude(1) == Approx(0.6f));
    REQUIRE(view.getAmplitude(2) == Approx(0.4f));
    REQUIRE(view.isActive(0) == true);
    REQUIRE(view.isActive(1) == true);
    REQUIRE(view.isActive(2) == false);
}

TEST_CASE("HarmonicDisplayView duplicate frameCounter dedup prevents overwrite",
          "[innexus][ui][harmonic-display][freeze]")
{
    // This tests the timer-callback dedup pattern: the timer callback
    // (Controller::onDisplayTimerFired) only calls updateData when the
    // frameCounter has changed. If the same counter arrives twice,
    // the view is not called again, so state is preserved.

    VSTGUI::CRect rect(0, 0, 500, 140);
    HarmonicDisplayView view(rect);

    // First update: frozen frame
    DisplayData frozenFrame{};
    frozenFrame.frameCounter = 10;
    frozenFrame.partialAmplitudes[0] = 0.8f;
    frozenFrame.partialActive[0] = 1;

    // Simulate timer callback dedup: track lastProcessedFrameCounter
    uint32_t lastProcessedFrameCounter = 0;

    // First tick: counter changed => update view
    if (frozenFrame.frameCounter != lastProcessedFrameCounter)
    {
        lastProcessedFrameCounter = frozenFrame.frameCounter;
        view.updateData(frozenFrame);
    }

    REQUIRE(view.getAmplitude(0) == Approx(0.8f));

    // Simulate a hypothetical second DisplayData arriving with same frameCounter
    // but different data (this shouldn't happen in practice, but tests the guard)
    DisplayData staleData{};
    staleData.frameCounter = 10;  // same counter
    staleData.partialAmplitudes[0] = 0.1f;  // different amplitude
    staleData.partialActive[0] = 0;

    // Second tick: same counter => skip (view NOT updated)
    if (staleData.frameCounter != lastProcessedFrameCounter)
    {
        lastProcessedFrameCounter = staleData.frameCounter;
        view.updateData(staleData);
    }

    // View still has the original data, not overwritten
    REQUIRE(view.getAmplitude(0) == Approx(0.8f));
    REQUIRE(view.isActive(0) == true);
}
