// =============================================================================
// TapPatternEditor Unit Tests
// =============================================================================
// Tests for Custom Tap Pattern Editor logic - Spec 046, redesigned by specs/iterum-tap-editor/design.md
// Following humble object pattern: tests pure logic functions without VSTGUI
//
// Geometry of the 852 x 140 view (design 5.1): plot x 42..844 (802 px), 0 dB at y 14, -48 dB at y 106, silence band
// y 106..114. Every grid, snap and placement position comes from one function, gridLines (design 2.2).
// =============================================================================

#include <cmath>
#include <array>
#include <string>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "ui/tap_pattern_editor_logic.h"
#include "parameters/custom_pattern_defaults.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/note_value.h>

using namespace Iterum;
using Catch::Approx;

namespace {

/// The ratios of the lines of @p kind that gridLines returns, in its order.
template <typename Lines>
std::vector<float> ratiosOfKind(const Lines& lines, decltype(GridLine{}.kind) kind) {
    std::vector<float> out;
    for (const auto& line : lines) {
        if (line.kind == kind) out.push_back(line.ratio);
    }
    return out;
}

void requireLines(const std::vector<float>& actual, const std::vector<float>& expected) {
    INFO("line count " << actual.size() << " expected " << expected.size());
    REQUIRE(actual.size() == expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        INFO("line " << i);
        REQUIRE(actual[i] == Approx(expected[i]).margin(1e-5));
    }
}

/// The fresh instance's 16 stored ratios (design 3.1, ruling 3).
std::array<float, kMaxPatternTaps> freshRatios() {
    std::array<float, kMaxPatternTaps> r{};
    for (size_t i = 0; i < kMaxPatternTaps; ++i) r[i] = kDefaultCustomTimeRatios[i];
    return r;
}

const std::string kTimes = "\xC3\x97";   // U+00D7 MULTIPLICATION SIGN
const std::string kDot = " \xC2\xB7 ";   // " U+00B7 MIDDLE DOT "

} // namespace

// =============================================================================
// Constants
// =============================================================================

TEST_CASE("TapPatternEditor constants are valid", "[tap_pattern_editor][construction]") {

    SECTION("max taps matches DSP layer") {
        REQUIRE(kMaxPatternTaps == 16);
    }

    SECTION("default tap level is full") {
        REQUIRE(kDefaultTapLevel == 1.0f);
    }

    SECTION("the level axis ends at -48 dB and two taps closer than 8 px of 802 collide") {
        REQUIRE(kLevelFloorDb == -48.0f);
        REQUIRE(kCollisionRatio == Approx(8.0f / 802.0f));
    }
}

// =============================================================================
// Axis mapping (design 5.1, 5.2)
// =============================================================================

TEST_CASE("timeRatioToX maps ratio 0..1 onto the inset plot 42..844", "[tap_pattern_editor][coordinates]") {
    REQUIRE(timeRatioToX(0.0f) == 42.0f);
    REQUIRE(timeRatioToX(1.0f) == 844.0f);
    REQUIRE(timeRatioToX(0.5f) == Approx(443.0f));
    REQUIRE(timeRatioToX(0.25f) == Approx(242.5f));

    SECTION("out-of-range ratios land on the plot edges by branch") {
        REQUIRE(timeRatioToX(-0.1f) == 42.0f);
        REQUIRE(timeRatioToX(1.5f) == 844.0f);
    }

    SECTION("a 12 px bar at ratio 0 or 1 lies fully inside the 852 px view") {
        REQUIRE(timeRatioToX(0.0f) - 6.0f >= 0.0f);
        REQUIRE(timeRatioToX(1.0f) + 6.0f <= 852.0f);
    }
}

TEST_CASE("xToTimeRatio inverts timeRatioToX and clamps outside the plot", "[tap_pattern_editor][coordinates]") {
    REQUIRE(xToTimeRatio(42.0f) == 0.0f);
    REQUIRE(xToTimeRatio(844.0f) == 1.0f);
    REQUIRE(xToTimeRatio(443.0f) == Approx(0.5f));
    REQUIRE(xToTimeRatio(0.0f) == 0.0f);
    REQUIRE(xToTimeRatio(852.0f) == 1.0f);

    for (float ratio = 0.0f; ratio <= 1.0f; ratio += 0.05f) {
        REQUIRE(xToTimeRatio(timeRatioToX(ratio)) == Approx(ratio).margin(1e-5));
    }
}

TEST_CASE("levelToY: 1.0 -> 14, 0.5 -> 25.5, dbToGain(-48) -> 106, 0.003 -> silence band; yToLevel inverts within 1e-4 and the band gives 0",
          "[tap_pattern_editor][coordinates]") {
    using Krate::DSP::dbToGain;

    REQUIRE(levelToY(1.0f) == Approx(14.0f).margin(1e-3));
    REQUIRE(levelToY(0.5f) == Approx(14.0f + 6.0206f / 48.0f * 92.0f).margin(0.05));
    // A hair above the floor, so the float round trip of dbToGain(-48) cannot fall into the band.
    REQUIRE(levelToY(dbToGain(-48.0f) * 1.0001f) == Approx(106.0f).margin(0.05));
    const float silentY = levelToY(0.003f);
    REQUIRE(silentY >= 106.0f);
    REQUIRE(silentY <= 114.0f);
    REQUIRE(levelToY(0.0f) >= 106.0f);
    REQUIRE(levelToY(0.0f) <= 114.0f);

    SECTION("levels above 0 dB draw at the plot top") {
        REQUIRE(levelToY(1.5f) == Approx(14.0f).margin(1e-3));
    }

    SECTION("yToLevel inverts levelToY") {
        for (float level : {1.0f, 0.7f, 0.5f, 0.25f, 0.1f, 0.05f, 0.01f, 0.005f}) {
            INFO("level " << level);
            REQUIRE(yToLevel(levelToY(level)) == Approx(level).margin(1e-4));
        }
    }

    SECTION("above the plot is 1.0, the silence band and below are exactly 0") {
        REQUIRE(yToLevel(5.0f) == 1.0f);
        REQUIRE(yToLevel(110.0f) == 0.0f);
        REQUIRE(yToLevel(113.0f) == 0.0f);
        REQUIRE(yToLevel(130.0f) == 0.0f);
    }
}

TEST_CASE("levelDragDb starts a silent tap at -48 dB", "[tap_pattern_editor][drag]") {
    // A level of 0 is -144 dB (gainToDb's floor); the drag starts from the display floor instead (design 4.2).
    REQUIRE(levelDragDb(0.0f, -1.0f, false) == Approx(-48.0f + 48.0f / 92.0f).margin(0.01));
    REQUIRE(levelDragDb(0.0f, -10.0f, false) == Approx(-48.0f + 10.0f * 48.0f / 92.0f).margin(0.01));

    SECTION("a full-level tap dragged 23 px down reads -12 dB; with Shift 10:1") {
        REQUIRE(levelDragDb(1.0f, 23.0f, false) == Approx(-12.0f).margin(0.01));
        REQUIRE(levelDragDb(1.0f, 23.0f, true) == Approx(-1.2f).margin(0.01));
    }
}

// =============================================================================
// Grid lines: the snap table against true note lengths (design 2.2, 2.3)
// =============================================================================

TEST_CASE("gridLines: 1/1 with Snap 1/8D has snap lines at 0.1875, 0.375, 0.5625, 0.75, 0.9375 and 1.0 and beat lines at quarters",
          "[tap_pattern_editor][grid]") {
    const auto lines = gridLines(CustomLength::N1, SnapDivision::N8D, 120.0);
    requireLines(ratiosOfKind(lines, GridLine::Snap), {0.1875f, 0.375f, 0.5625f, 0.75f, 0.9375f, 1.0f});

    // Beats 1..3 inside the plot; the bar lines carry the whole notes (ratio 0 and 1 for a 1/1 length).
    const auto beats = ratiosOfKind(lines, GridLine::Beat);
    for (float q : {0.25f, 0.5f, 0.75f}) {
        bool found = false;
        for (float b : beats) found = found || std::abs(b - q) < 1e-5f;
        INFO("beat line at " << q);
        REQUIRE(found);
    }
    for (float b : beats) {
        INFO("beat line " << b);
        REQUIRE(std::abs(b * 4.0f - std::round(b * 4.0f)) < 1e-5f);
    }
}

TEST_CASE("gridLines: Length 1/8 with Snap 1/4 falls back to the 1/16 lines 0.5 and 1.0", "[tap_pattern_editor][grid]") {
    const auto lines = gridLines(CustomLength::N8, SnapDivision::N4, 120.0);
    requireLines(ratiosOfKind(lines, GridLine::Snap), {0.5f, 1.0f});
    REQUIRE(effectiveSnapUnit(CustomLength::N8, SnapDivision::N4, 120.0) == SnapDivision::N16);
}

TEST_CASE("gridLines: Snap 1/2 over Length 1/4 and Snap 1/1 over Length 1/2 fall back to 1/16", "[tap_pattern_editor][grid]") {
    requireLines(ratiosOfKind(gridLines(CustomLength::N4, SnapDivision::N2, 120.0), GridLine::Snap),
                 {0.25f, 0.5f, 0.75f, 1.0f});
    requireLines(ratiosOfKind(gridLines(CustomLength::N2, SnapDivision::N1, 120.0), GridLine::Snap),
                 {0.125f, 0.25f, 0.375f, 0.5f, 0.625f, 0.75f, 0.875f, 1.0f});
    REQUIRE(effectiveSnapUnit(CustomLength::N4, SnapDivision::N2, 120.0) == SnapDivision::N16);
    REQUIRE(effectiveSnapUnit(CustomLength::N2, SnapDivision::N1, 120.0) == SnapDivision::N16);
    // A unit shorter than the length is its own effective unit.
    REQUIRE(effectiveSnapUnit(CustomLength::N1, SnapDivision::N8, 120.0) == SnapDivision::N8);
}

TEST_CASE("gridLines: ratio 1 is a snap line for 1/8D and 1/4T over 1/2", "[tap_pattern_editor][grid]") {
    requireLines(ratiosOfKind(gridLines(CustomLength::N2, SnapDivision::N8D, 120.0), GridLine::Snap),
                 {0.375f, 0.75f, 1.0f});
    requireLines(ratiosOfKind(gridLines(CustomLength::N2, SnapDivision::N4T, 120.0), GridLine::Snap),
                 {1.0f / 3.0f, 2.0f / 3.0f, 1.0f});
}

TEST_CASE("gridLines: no snap line at ratio 0 for any length and snap", "[tap_pattern_editor][grid]") {
    const CustomLength lengths[] = {CustomLength::Free, CustomLength::N8, CustomLength::N4,
                                    CustomLength::N2, CustomLength::N1, CustomLength::N2x1};
    for (const CustomLength length : lengths) {
        for (int s = 0; s <= 21; ++s) {
            for (double bpm : {20.0, 120.0, 300.0}) {
                INFO("length " << static_cast<int>(length) << " snap " << s << " bpm " << bpm);
                const auto snaps = ratiosOfKind(gridLines(length, static_cast<SnapDivision>(s), bpm), GridLine::Snap);
                if (s == 0) {
                    REQUIRE(snaps.empty());  // Snap Off draws and quantises nothing
                    continue;
                }
                REQUIRE(snaps.size() >= 2u);
                for (float r : snaps) {
                    REQUIRE(r > 1e-6f);
                    REQUIRE(r <= 1.0f + 1e-6f);
                }
                bool hasEnd = false;
                for (float r : snaps) hasEnd = hasEnd || std::abs(r - 1.0f) < 1e-6f;
                REQUIRE(hasEnd);
            }
        }
    }
}

TEST_CASE("gridLines: Free at 120 BPM has beat lines every 0.1 and bar lines every 0.4", "[tap_pattern_editor][grid]") {
    // Free is 5000 ms wide; a beat at 120 BPM is 500 ms, a bar 2000 ms.
    const auto lines = gridLines(CustomLength::Free, SnapDivision::Off, 120.0);
    const auto beats = ratiosOfKind(lines, GridLine::Beat);
    const auto bars = ratiosOfKind(lines, GridLine::Bar);

    for (int k = 1; k <= 9; ++k) {
        const float r = 0.1f * static_cast<float>(k);
        bool found = false;
        for (float b : beats) found = found || std::abs(b - r) < 1e-5f;
        for (float b : bars) found = found || std::abs(b - r) < 1e-5f;
        INFO("beat " << k);
        REQUIRE(found);
    }
    for (float b : beats) {
        INFO("beat line " << b);
        REQUIRE(std::abs(b * 10.0f - std::round(b * 10.0f)) < 1e-4f);
    }
    bool bar04 = false;
    bool bar08 = false;
    for (float b : bars) {
        INFO("bar line " << b);
        REQUIRE(std::abs(b / 0.4f - std::round(b / 0.4f)) < 1e-4f);
        bar04 = bar04 || std::abs(b - 0.4f) < 1e-5f;
        bar08 = bar08 || std::abs(b - 0.8f) < 1e-5f;
    }
    REQUIRE(bar04);
    REQUIRE(bar08);
}

TEST_CASE("gridLines: 1/4T over 1/1 has 6 snap lines, exact", "[tap_pattern_editor][grid]") {
    requireLines(ratiosOfKind(gridLines(CustomLength::N1, SnapDivision::N4T, 120.0), GridLine::Snap),
                 {1.0f / 6.0f, 2.0f / 6.0f, 3.0f / 6.0f, 4.0f / 6.0f, 5.0f / 6.0f, 1.0f});
}

TEST_CASE("gridLines: every Snap entry is its true note length over 2/1", "[tap_pattern_editor][grid]") {
    // Snap index s is note-value dropdown index s - 1 (design 2.2). Over 2/1 (8 beats) no unit falls back.
    for (int s = 1; s <= 21; ++s) {
        const auto mapping = Krate::DSP::kNoteValueDropdownMapping[s - 1];
        const float unitBeats = Krate::DSP::getBeatsForNote(mapping.note, mapping.modifier);
        INFO("snap " << s << " unit " << unitBeats << " beats");
        const auto snaps = ratiosOfKind(gridLines(CustomLength::N2x1, static_cast<SnapDivision>(s), 120.0),
                                        GridLine::Snap);
        REQUIRE(snaps.size() >= 2u);
        REQUIRE(snaps.front() == Approx(unitBeats / 8.0f).margin(1e-5));
        if (snaps.size() >= 3u) {
            REQUIRE(snaps[1] - snaps[0] == Approx(unitBeats / 8.0f).margin(1e-5));
        }
    }
}

TEST_CASE("snapToLines rounds to the nearest line, ties later, never 0", "[tap_pattern_editor][grid]") {
    const auto lines = gridLines(CustomLength::N1, SnapDivision::N4, 120.0);  // 0.25, 0.5, 0.75, 1.0
    REQUIRE(snapToLines(0.3f, lines) == Approx(0.25f));
    REQUIRE(snapToLines(0.6f, lines) == Approx(0.5f));
    REQUIRE(snapToLines(0.375f, lines) == Approx(0.5f));   // tie between 0.25 and 0.5: the later line
    REQUIRE(snapToLines(0.875f, lines) == Approx(1.0f));   // tie between 0.75 and 1.0
    REQUIRE(snapToLines(0.1f, lines) == Approx(0.25f));    // ratio 0 is never a line
    REQUIRE(snapToLines(0.0f, lines) == Approx(0.25f));
    REQUIRE(snapToLines(1.2f, lines) == Approx(1.0f));
}

// =============================================================================
// The one default rule (design 2.1, 3.1)
// =============================================================================

TEST_CASE("defaultTapTime is (i+1)/n x c", "[tap_pattern_editor][default]") {
    REQUIRE(spanMs(CustomLength::Free, 120.0) == Approx(5000.0f));
    REQUIRE(spanMs(CustomLength::N1, 120.0) == Approx(2000.0f));
    REQUIRE(spanMs(CustomLength::N2x1, 60.0) == Approx(8000.0f));
    REQUIRE(playableFraction(CustomLength::N1, 120.0) == 1.0f);
    REQUIRE(playableFraction(CustomLength::Free, 30.0) == 1.0f);

    const float c = playableFraction(CustomLength::N2x1, 60.0);
    REQUIRE(c == Approx(0.625f));

    REQUIRE(defaultTapTime(0, 4, 1.0f) == Approx(0.25f));
    REQUIRE(defaultTapTime(3, 4, 1.0f) == Approx(1.0f));
    REQUIRE(defaultTapTime(0, 8, c) == Approx(0.078125f));
    REQUIRE(defaultTapTime(7, 8, c) == Approx(0.625f));
    for (size_t n = 2; n <= 16; ++n) {
        for (size_t i = 0; i < n; ++i) {
            REQUIRE(defaultTapTime(i, n, c) == Approx(static_cast<float>(i + 1) / static_cast<float>(n) * c));
        }
    }
}

// =============================================================================
// Placement: a revealed tap never lands on an occupied grid line (design 3.3)
// =============================================================================

TEST_CASE("placeTap keeps a free stored value", "[tap_pattern_editor][placement]") {
    const auto ratios = freshRatios();
    const auto grids = placementGrids(CustomLength::N1, SnapDivision::N4, 120.0);
    // 4 -> 5: tap 5's stored 0.125 is free.
    REQUIRE(placeTap(ratios, 5, 4, defaultTapTime(4, 5, 1.0f), grids, 1.0f) == Approx(0.125f));
}

TEST_CASE("placeTap moves a colliding tap to the nearest free snap line, later first", "[tap_pattern_editor][placement]") {
    // The collision example of design 3.3: Snap 1/8, the user drags tap 2 from 0.5 to 0.625, then raises 4 -> 8.
    auto ratios = freshRatios();
    ratios[1] = 0.625f;
    const auto grids = placementGrids(CustomLength::N1, SnapDivision::N8, 120.0);
    for (size_t i = 4; i < 8; ++i) {
        ratios[i] = placeTap(ratios, 8, i, defaultTapTime(i, 8, 1.0f), grids, 1.0f);
    }
    REQUIRE(ratios[4] == Approx(0.125f));
    REQUIRE(ratios[5] == Approx(0.375f));
    REQUIRE(ratios[6] == Approx(0.5f));
    REQUIRE(ratios[7] == Approx(0.875f));
}

TEST_CASE("the registered defaults reveal without a move", "[tap_pattern_editor][placement]") {
    const auto grids = placementGrids(CustomLength::N1, SnapDivision::N4, 120.0);
    const float c = playableFraction(CustomLength::N1, 120.0);

    SECTION("4 -> 8") {
        const auto ratios = freshRatios();
        for (size_t i = 4; i < 8; ++i) {
            INFO("tap " << i + 1);
            REQUIRE(placeTap(ratios, 8, i, defaultTapTime(i, 8, c), grids, c) == kDefaultCustomTimeRatios[i]);
        }
    }
    SECTION("8 -> 16") {
        const auto ratios = freshRatios();
        for (size_t i = 8; i < 16; ++i) {
            INFO("tap " << i + 1);
            REQUIRE(placeTap(ratios, 16, i, defaultTapTime(i, 16, c), grids, c) == kDefaultCustomTimeRatios[i]);
        }
    }
}

TEST_CASE("placeTap never returns 0 and falls back to the 1/16 then 1/64 grid", "[tap_pattern_editor][placement]") {
    SECTION("every 1/4 line taken: the 1/16 retry") {
        std::array<float, kMaxPatternTaps> ratios = freshRatios();  // 0.25, 0.5, 0.75, 1.0 ...
        ratios[4] = 0.5f;                                           // tap 5 collides with tap 2
        const auto grids = placementGrids(CustomLength::N1, SnapDivision::N4, 120.0);
        REQUIRE(placeTap(ratios, 5, 4, defaultTapTime(4, 5, 1.0f), grids, 1.0f) == Approx(0.9375f));
    }

    SECTION("every snap and 1/16 line taken: the 1/64 retry") {
        // Length 1/8: Snap 1/4 falls back to 1/16, whose only lines are 0.5 and 1.0.
        std::array<float, kMaxPatternTaps> ratios{};
        ratios[0] = 0.5f;
        ratios[1] = 1.0f;
        ratios[2] = 0.5f;
        const auto grids = placementGrids(CustomLength::N8, SnapDivision::N4, 120.0);
        REQUIRE(placeTap(ratios, 3, 2, defaultTapTime(2, 3, 1.0f), grids, 1.0f) == Approx(0.875f));
    }

    SECTION("an ideal at 0 lands on the first line") {
        std::array<float, kMaxPatternTaps> ratios{};
        ratios[0] = 0.0f;
        ratios[1] = 1.0f;
        const auto grids = placementGrids(CustomLength::N1, SnapDivision::N4, 120.0);
        const float placed = placeTap(ratios, 2, 0, 0.0f, grids, 1.0f);
        REQUIRE(placed > 0.0f);
        REQUIRE(placed == Approx(0.25f));
    }
}

TEST_CASE("placeTap skips lines beyond the 5 s ceiling", "[tap_pattern_editor][placement]") {
    // 2/1 at 60 BPM is 8000 ms: only (0, 0.625] plays below 5 s.
    const float c = playableFraction(CustomLength::N2x1, 60.0);
    REQUIRE(c == Approx(0.625f));
    const auto grids = placementGrids(CustomLength::N2x1, SnapDivision::N4, 60.0);  // beats: every 0.125

    std::array<float, kMaxPatternTaps> ratios{};
    ratios[0] = 0.625f;
    ratios[1] = 0.5f;
    ratios[2] = 0.625f;  // collides with tap 1
    const float placed = placeTap(ratios, 3, 2, 0.625f, grids, c);
    REQUIRE(placed <= c + 1e-6f);
    REQUIRE(placed == Approx(0.375f));  // 0.625 taken, 0.75 beyond the ceiling, 0.5 taken, 0.375 free
}

TEST_CASE("placeTap never lands on an occupied line: sixteen stacked taps spread out", "[tap_pattern_editor][placement]") {
    std::array<float, kMaxPatternTaps> ratios{};
    ratios.fill(0.5f);
    const auto grids = placementGrids(CustomLength::N1, SnapDivision::N16, 120.0);
    for (size_t i = 1; i < 16; ++i) {
        ratios[i] = placeTap(ratios, 16, i, defaultTapTime(i, 16, 1.0f), grids, 1.0f);
    }
    for (size_t i = 0; i < 16; ++i) {
        INFO("tap " << i + 1 << " at " << ratios[i]);
        REQUIRE(ratios[i] > 0.0f);
        REQUIRE(ratios[i] <= 1.0f);
        for (size_t j = 0; j < i; ++j) {
            REQUIRE(std::abs(ratios[i] - ratios[j]) >= kCollisionRatio);
        }
    }
}

// =============================================================================
// Ruler, labels, readout (design 2.2, 4.3, 5.5)
// =============================================================================

TEST_CASE("rulerLabels: 1/1 gives 1..4 with the 4 right-aligned at or left of x 842; Free ends with a right-aligned 5.0 s; no label beyond x 844",
          "[tap_pattern_editor][ruler]") {
    SECTION("1/1") {
        const auto labels = rulerLabels(CustomLength::N1, 120.0);
        std::vector<std::string> texts;
        for (const auto& l : labels) texts.push_back(l.text);
        REQUIRE(texts == std::vector<std::string>{"1", "2", "3", "4"});
        size_t k = 1;
        for (const auto& l : labels) {
            INFO("label " << l.text);
            REQUIRE(l.right <= 844.0f);
            if (k < 4) {
                REQUIRE(l.left == Approx(timeRatioToX(static_cast<float>(k) / 4.0f) + 2.0f).margin(0.5));
            } else {
                REQUIRE(l.right <= 842.0f + 1e-3f);
            }
            ++k;
        }
    }

    SECTION("Free") {
        const auto labels = rulerLabels(CustomLength::Free, 120.0);
        REQUIRE_FALSE(labels.empty());
        std::vector<std::string> texts;
        for (const auto& l : labels) {
            INFO("label " << l.text);
            REQUIRE(l.right <= 844.0f);
            texts.push_back(l.text);
        }
        REQUIRE(texts.back() == "5.0 s");
        REQUIRE(labels.back().right <= 842.0f + 1e-3f);
        REQUIRE(texts.front() == "0.5");
    }
}

TEST_CASE("labelGroups joins taps within 8 px", "[tap_pattern_editor][overlap]") {
    // 4 px apart, alone, 7 px apart, 10 px from its neighbour.
    const float px = 1.0f / 802.0f;
    const float ratios[kMaxPatternTaps] = {0.25f, 0.25f + 4.0f * px, 0.5f, 0.75f, 0.75f + 7.0f * px, 0.9f, 0.9f + 10.0f * px};
    std::vector<std::vector<size_t>> groups;
    for (const auto& g : labelGroups(ratios, 7)) {
        std::vector<size_t> members;
        for (size_t idx : g) members.push_back(idx);
        groups.push_back(members);
    }
    const std::vector<std::vector<size_t>> expected{{0, 1}, {2}, {3, 4}, {5}, {6}};
    REQUIRE(groups == expected);
}

TEST_CASE("formatTapReadout: 750.0 ms / 1.25 s / -6.0 dB / -inf dB and the grid field 3 × 1/8, 5 × 1/8T, absent off-grid, with Snap Off or with Shift",
          "[tap_pattern_editor][readout]") {
    // Length 1/1 at 120 BPM: span 2000 ms, a beat is ratio 0.25.
    SECTION("on a 1/8 line") {
        REQUIRE(formatTapReadout(0.375f, 0.5f, CustomLength::N1, SnapDivision::N8, 120.0, false) ==
                "3 " + kTimes + " 1/8" + kDot + "750.0 ms" + kDot + "-6.0 dB");
        REQUIRE(formatTapReadout(0.625f, 1.0f, CustomLength::N1, SnapDivision::N8, 120.0, false) ==
                "5 " + kTimes + " 1/8" + kDot + "1.25 s" + kDot + "0.0 dB");
    }
    SECTION("on a 1/8T line") {
        REQUIRE(formatTapReadout(5.0f / 12.0f, 1.0f, CustomLength::N1, SnapDivision::N8T, 120.0, false) ==
                "5 " + kTimes + " 1/8T" + kDot + "833.3 ms" + kDot + "0.0 dB");
    }
    SECTION("the end line that is not a multiple of the unit") {
        // 1/2 is 2 beats; 1/8D lines are 0.75 and 1.5 beats, then the end.
        REQUIRE(formatTapReadout(1.0f, 1.0f, CustomLength::N2, SnapDivision::N8D, 120.0, false) ==
                "end" + kDot + "1.00 s" + kDot + "0.0 dB");
    }
    SECTION("off the grid, Snap Off and Shift drop the grid field") {
        REQUIRE(formatTapReadout(0.3f, 1.0f, CustomLength::N1, SnapDivision::N8, 120.0, false) ==
                "600.0 ms" + kDot + "0.0 dB");
        REQUIRE(formatTapReadout(0.375f, 0.5f, CustomLength::N1, SnapDivision::Off, 120.0, false) ==
                "750.0 ms" + kDot + "-6.0 dB");
        REQUIRE(formatTapReadout(0.375f, 0.5f, CustomLength::N1, SnapDivision::N8, 120.0, true) ==
                "750.0 ms" + kDot + "-6.0 dB");
    }
    SECTION("silence and the Free axis") {
        REQUIRE(formatTapReadout(0.474f, 0.0005f, CustomLength::Free, SnapDivision::Off, 120.0, false) ==
                "2.37 s" + kDot + "-inf dB");
    }
    SECTION("a synced time is capped at 5000 ms") {
        // 2/1 at 60 BPM: ratio 1.0 is 8000 ms of span, which plays at 5 s.
        REQUIRE(formatTapReadout(1.0f, 1.0f, CustomLength::N2x1, SnapDivision::Off, 60.0, false) ==
                "5.00 s" + kDot + "0.0 dB");
    }
}

// =============================================================================
// T018.5: Right-Click Ignored Tests (Edge Case)
// =============================================================================

TEST_CASE("right-click is ignored (no state change)", "[tap_pattern_editor][right_click][edge]") {

    SECTION("shouldIgnoreRightClick returns true for right button") {
        REQUIRE(shouldIgnoreRightClick(true) == true);
    }

    SECTION("shouldIgnoreRightClick returns false for left button") {
        REQUIRE(shouldIgnoreRightClick(false) == false);
    }
}

// =============================================================================
// T018.7: Pattern Change During Drag Tests (Edge Case)
// Note: This is a UI state change test - verified at integration level
// Pattern == Custom (index 19) enables the editor
// =============================================================================

TEST_CASE("pattern change conceptual test", "[tap_pattern_editor][pattern_change][edge]") {
    // This test documents the expected behavior when pattern changes
    // Actual implementation requires UI state tracking

    constexpr int kCustomPatternIndex = 19;

    SECTION("Custom pattern index is 19") {
        REQUIRE(kCustomPatternIndex == 19);
    }

    SECTION("editor should be visible only when pattern is Custom") {
        // Conceptual: isCustomPattern(patternIndex) -> bool
        auto isCustomPattern = [](int index) { return index == kCustomPatternIndex; };

        REQUIRE(isCustomPattern(0) == false);   // First preset pattern
        REQUIRE(isCustomPattern(18) == false);  // Last non-custom pattern
        REQUIRE(isCustomPattern(19) == true);   // Custom pattern
    }
}

// =============================================================================
// Editor Size Tests (T031.9)
// =============================================================================

TEST_CASE("editor enforces minimum width", "[tap_pattern_editor][size][edge]") {

    SECTION("width below minimum is clamped") {
        REQUIRE(getEffectiveEditorWidth(100.0f) == kMinEditorWidth);
    }

    SECTION("width at minimum is preserved") {
        REQUIRE(getEffectiveEditorWidth(kMinEditorWidth) == kMinEditorWidth);
    }

    SECTION("width above minimum is preserved") {
        REQUIRE(getEffectiveEditorWidth(500.0f) == 500.0f);
    }

    SECTION("negative width returns minimum") {
        REQUIRE(getEffectiveEditorWidth(-50.0f) == kMinEditorWidth);
    }
}

TEST_CASE("value clamping for out-of-bounds ratios", "[tap_pattern_editor][clamping][edge]") {
    REQUIRE(clampRatio(-0.5f) == 0.0f);
    REQUIRE(clampRatio(1.5f) == 1.0f);
    REQUIRE(clampRatio(0.5f) == 0.5f);
    REQUIRE(clampRatio(0.0f) == 0.0f);
    REQUIRE(clampRatio(1.0f) == 1.0f);
}

// =============================================================================
// Pattern Calculator: Copy to Custom mirrors the DSP in milliseconds (design 2.6)
// =============================================================================

#include "parameters/pattern_calculator.h"
#include <krate/dsp/core/block_context.h>
#include <krate/dsp/effects/multi_tap_delay.h>
#include <memory>

namespace {

constexpr double kMirrorSampleRate = 44100.0;
constexpr size_t kMirrorBlock = 512;

/// The note value of the 911 dropdown index the processor maps (Whole, Half, Quarter, 8th, ...).
Krate::DSP::NoteValue mirrorNote(int noteIdx) {
    using NV = Krate::DSP::NoteValue;
    static constexpr NV notes[] = {NV::Whole, NV::Half, NV::Quarter, NV::Eighth, NV::Sixteenth};
    return notes[noteIdx];
}

Krate::DSP::NoteModifier mirrorModifier(int modIdx) {
    using NM = Krate::DSP::NoteModifier;
    static constexpr NM mods[] = {NM::None, NM::Triplet, NM::Dotted};
    return mods[modIdx];
}

/// Plays the pattern the way the processor does: timing pattern loaded, then per block the spatial pattern and
/// process() at the tempo, transport playing.
void playPattern(Krate::DSP::MultiTapDelay& delay, int timing, int spatial, int noteIdx, int modIdx, size_t n,
                 double bpm) {
    delay.setTempo(static_cast<float>(bpm));
    delay.setNoteValue(mirrorNote(noteIdx), mirrorModifier(modIdx));
    delay.loadTimingPattern(static_cast<Krate::DSP::TimingPattern>(timing), n);
    const Krate::DSP::BlockContext ctx{.sampleRate = kMirrorSampleRate, .blockSize = kMirrorBlock, .tempoBPM = bpm,
                                       .isPlaying = true};
    std::array<float, kMirrorBlock> left{};
    std::array<float, kMirrorBlock> right{};
    for (int block = 0; block < 3; ++block) {
        delay.applySpatialPattern(static_cast<Krate::DSP::SpatialPattern>(spatial));
        left.fill(0.0f);
        right.fill(0.0f);
        delay.process(left.data(), right.data(), kMirrorBlock, ctx);
    }
}

std::unique_ptr<Krate::DSP::MultiTapDelay> makeMirrorDelay() {
    auto delay = std::make_unique<Krate::DSP::MultiTapDelay>();
    delay->prepare(kMirrorSampleRate, kMirrorBlock, 5000.0f);
    return delay;
}

} // namespace

TEST_CASE("patternTap and playedLevelDb match MultiTapDelay for every timing and spatial pattern",
          "[tap_pattern_editor][copy]") {
    auto delay = makeMirrorDelay();
    for (int timing = 0; timing <= 18; ++timing) {
        for (int spatial = 0; spatial <= 6; ++spatial) {
            for (int noteIdx : {2, 3}) {  // Quarter, Eighth
                for (int modIdx = 0; modIdx <= 2; ++modIdx) {
                    for (size_t n : {size_t{4}, size_t{16}}) {
                        for (double bpm : {90.0, 140.0}) {
                            playPattern(*delay, timing, spatial, noteIdx, modIdx, n, bpm);
                            for (size_t i = 0; i < n; ++i) {
                                const PatternTap tap = patternTap(timing, noteIdx, modIdx, i, n, bpm);
                                INFO("timing " << timing << " spatial " << spatial << " note " << noteIdx << " mod "
                                               << modIdx << " taps " << n << " bpm " << bpm << " tap " << i);
                                REQUIRE(tap.ms == Approx(delay->getTapTimeMs(i)).margin(0.5));
                                REQUIRE(playedLevelDb(tap.levelDb, spatial, i) ==
                                        Approx(delay->getTapLevelDb(i)).margin(0.1));
                            }
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE("patternTap: Linear with 16 taps at 90 BPM follows the clamped spread", "[tap_pattern_editor][copy]") {
    constexpr int kLinear = 18;
    constexpr int kQuarterIdx = 2;
    auto delay = makeMirrorDelay();
    playPattern(*delay, kLinear, 2, kQuarterIdx, 0, 16, 90.0);

    REQUIRE(patternTap(kLinear, kQuarterIdx, 0, 1, 16, 90.0).ms == Approx(955.6f).margin(0.5));
    for (size_t i = 0; i < 16; ++i) {
        INFO("tap " << i);
        REQUIRE(patternTap(kLinear, kQuarterIdx, 0, i, 16, 90.0).ms == Approx(delay->getTapTimeMs(i)).margin(0.5));
    }
    REQUIRE(patternClampEngaged(kLinear, kQuarterIdx, 0, 16, 90.0));
}

TEST_CASE("patternTap: Note Value Whole at 30 BPM applies the base clamp", "[tap_pattern_editor][copy]") {
    constexpr int kWholeIdx = 0;
    auto delay = makeMirrorDelay();
    for (int timing : {15, 17, 18}) {  // Fibonacci, Primes, Linear
        for (size_t n : {size_t{4}, size_t{16}}) {
            playPattern(*delay, timing, 2, kWholeIdx, 0, n, 30.0);
            for (size_t i = 0; i < n; ++i) {
                INFO("timing " << timing << " taps " << n << " tap " << i);
                REQUIRE(patternTap(timing, kWholeIdx, 0, i, n, 30.0).ms ==
                        Approx(delay->getTapTimeMs(i)).margin(0.5));
            }
            REQUIRE(patternClampEngaged(timing, kWholeIdx, 0, n, 30.0));
        }
    }
    REQUIRE(patternTap(17, kWholeIdx, 0, 0, 4, 30.0).ms == Approx(2500.0f).margin(0.5));
    REQUIRE(patternTap(15, kWholeIdx, 0, 0, 4, 30.0).ms == Approx(1250.0f).margin(0.5));
    REQUIRE(patternTap(15, kWholeIdx, 0, 1, 4, 30.0).ms == Approx(1250.0f).margin(0.5));
    for (size_t i = 0; i < 16; ++i) {
        REQUIRE(patternTap(18, kWholeIdx, 0, i, 16, 30.0).ms == Approx(5000.0f).margin(0.5));
    }
}

TEST_CASE("patternClampEngaged is false for Quarter with 6 taps at 120 BPM and true for Whole with 4 taps at 120 BPM",
          "[tap_pattern_editor][copy]") {
    REQUIRE_FALSE(patternClampEngaged(2, 2, 0, 6, 120.0));
    REQUIRE(patternClampEngaged(0, 2, 0, 4, 120.0));

    // The beats of a rhythmic tap are its unclamped musical value.
    REQUIRE(patternTap(2, 2, 0, 5, 6, 120.0).beats == Approx(6.0f));
    REQUIRE(patternTap(0, 2, 0, 3, 4, 120.0).beats == Approx(16.0f));
    REQUIRE(patternTap(0, 2, 0, 3, 4, 120.0).ms == Approx(5000.0f));
    REQUIRE(patternTap(2, 2, 0, 3, 6, 120.0).levelDb == Approx(-9.0f));
}
