// ==============================================================================
// LfoShapeSelector Unit Tests
// ==============================================================================
// Tests for value conversion, waveform icon path generation, hit testing,
// and NaN defense -- all pure logic, no VSTGUI draw context needed.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "ui/lfo_shape_selector.h"

using namespace Krate::Plugins;
using namespace Krate::Plugins::LfoWaveformIcons;
using Catch::Approx;

// ==============================================================================
// Value Conversion: lfoShapeIndexFromNormalized()
// ==============================================================================

TEST_CASE("lfoShapeIndexFromNormalized maps 0.0 to index 0", "[lfo-shape-selector][value]") {
    REQUIRE(lfoShapeIndexFromNormalized(0.0f) == 0);
}

TEST_CASE("lfoShapeIndexFromNormalized maps 1.0 to index 5", "[lfo-shape-selector][value]") {
    REQUIRE(lfoShapeIndexFromNormalized(1.0f) == 5);
}

TEST_CASE("lfoShapeIndexFromNormalized maps 0.5 to index 3 (halfway)", "[lfo-shape-selector][value]") {
    // 0.5 * 5 = 2.5, rounds to 3
    REQUIRE(lfoShapeIndexFromNormalized(0.5f) == 3);
}

TEST_CASE("lfoShapeIndexFromNormalized round-trips all indices", "[lfo-shape-selector][value]") {
    for (int i = 0; i < 6; ++i) {
        float normalized = static_cast<float>(i) / 5.0f;
        REQUIRE(lfoShapeIndexFromNormalized(normalized) == i);
    }
}

TEST_CASE("lfoShapeIndexFromNormalized NaN defense: NaN maps to index 0", "[lfo-shape-selector][value][nan]") {
    float nan = std::numeric_limits<float>::quiet_NaN();
    // NaN -> 0.0 -> round(0.0 * 5) = 0
    REQUIRE(lfoShapeIndexFromNormalized(nan) == 0);
}

TEST_CASE("lfoShapeIndexFromNormalized NaN defense: +inf maps to index 0", "[lfo-shape-selector][value][nan]") {
    float inf = std::numeric_limits<float>::infinity();
    // +inf -> 0.0 -> round(0.0 * 5) = 0
    REQUIRE(lfoShapeIndexFromNormalized(inf) == 0);
}

TEST_CASE("lfoShapeIndexFromNormalized clamps negative values to 0", "[lfo-shape-selector][value]") {
    REQUIRE(lfoShapeIndexFromNormalized(-0.5f) == 0);
}

TEST_CASE("lfoShapeIndexFromNormalized clamps values > 1.0 to 5", "[lfo-shape-selector][value]") {
    REQUIRE(lfoShapeIndexFromNormalized(1.5f) == 5);
}

// ==============================================================================
// Value Conversion: normalizedFromLfoShapeIndex()
// ==============================================================================

TEST_CASE("normalizedFromLfoShapeIndex maps 0 to 0.0", "[lfo-shape-selector][value]") {
    REQUIRE(normalizedFromLfoShapeIndex(0) == Approx(0.0f));
}

TEST_CASE("normalizedFromLfoShapeIndex maps 5 to 1.0", "[lfo-shape-selector][value]") {
    REQUIRE(normalizedFromLfoShapeIndex(5) == Approx(1.0f));
}

TEST_CASE("normalizedFromLfoShapeIndex maps 3 to 0.6", "[lfo-shape-selector][value]") {
    REQUIRE(normalizedFromLfoShapeIndex(3) == Approx(0.6f));
}

TEST_CASE("normalizedFromLfoShapeIndex clamps negative index to 0.0", "[lfo-shape-selector][value]") {
    REQUIRE(normalizedFromLfoShapeIndex(-1) == Approx(0.0f));
}

TEST_CASE("normalizedFromLfoShapeIndex clamps index > 5 to 1.0", "[lfo-shape-selector][value]") {
    REQUIRE(normalizedFromLfoShapeIndex(10) == Approx(1.0f));
}

TEST_CASE("normalizedFromLfoShapeIndex round-trips with lfoShapeIndexFromNormalized", "[lfo-shape-selector][value]") {
    for (int i = 0; i < 6; ++i) {
        float norm = normalizedFromLfoShapeIndex(i);
        REQUIRE(lfoShapeIndexFromNormalized(norm) == i);
    }
}

// ==============================================================================
// Display Name Tables
// ==============================================================================

TEST_CASE("lfoShapeDisplayName returns correct names for all indices", "[lfo-shape-selector][names]") {
    REQUIRE(std::string(lfoShapeDisplayName(0)) == "Sine");
    REQUIRE(std::string(lfoShapeDisplayName(1)) == "Triangle");
    REQUIRE(std::string(lfoShapeDisplayName(2)) == "Sawtooth");
    REQUIRE(std::string(lfoShapeDisplayName(3)) == "Square");
    REQUIRE(std::string(lfoShapeDisplayName(4)) == "Sample & Hold");
    REQUIRE(std::string(lfoShapeDisplayName(5)) == "Smooth Random");
}

TEST_CASE("lfoShapeDisplayName clamps out-of-range index", "[lfo-shape-selector][names]") {
    REQUIRE(std::string(lfoShapeDisplayName(-1)) == "Sine");
    REQUIRE(std::string(lfoShapeDisplayName(10)) == "Smooth Random");
}

TEST_CASE("lfoShapePopupLabel returns abbreviated names", "[lfo-shape-selector][names]") {
    REQUIRE(std::string(lfoShapePopupLabel(0)) == "Sine");
    REQUIRE(std::string(lfoShapePopupLabel(1)) == "Tri");
    REQUIRE(std::string(lfoShapePopupLabel(2)) == "Saw");
    REQUIRE(std::string(lfoShapePopupLabel(3)) == "Sq");
    REQUIRE(std::string(lfoShapePopupLabel(4)) == "S&H");
    REQUIRE(std::string(lfoShapePopupLabel(5)) == "SmRnd");
}

// ==============================================================================
// Waveform Icon Path Data
// ==============================================================================

TEST_CASE("All 6 LFO shape icons have at least 2 points", "[lfo-shape-selector][icons]") {
    for (int i = 0; i < 6; ++i) {
        auto shape = static_cast<Krate::DSP::Waveform>(i);
        auto icon = getIconPath(shape);
        INFO("Shape index: " << i);
        REQUIRE(icon.count >= 2);
    }
}

TEST_CASE("All LFO icon points are in [0,1] range", "[lfo-shape-selector][icons]") {
    for (int i = 0; i < 6; ++i) {
        auto shape = static_cast<Krate::DSP::Waveform>(i);
        auto icon = getIconPath(shape);
        for (int j = 0; j < icon.count; ++j) {
            INFO("Shape " << i << " point " << j);
            REQUIRE(icon.points[j].x >= 0.0f);
            REQUIRE(icon.points[j].x <= 1.0f);
            REQUIRE(icon.points[j].y >= 0.0f);
            REQUIRE(icon.points[j].y <= 1.0f);
        }
    }
}

TEST_CASE("Sine icon has enough points for smooth curve", "[lfo-shape-selector][icons]") {
    auto icon = getIconPath(Krate::DSP::Waveform::Sine);
    REQUIRE(icon.count >= 10);
}

TEST_CASE("Square icon has sharp transitions", "[lfo-shape-selector][icons]") {
    auto icon = getIconPath(Krate::DSP::Waveform::Square);
    // Square should have at least 4 points for the step shape
    REQUIRE(icon.count >= 4);
}

TEST_CASE("SampleHold icon has stepped segments", "[lfo-shape-selector][icons]") {
    auto icon = getIconPath(Krate::DSP::Waveform::SampleHold);
    // Stepped pattern needs many points for horizontal + vertical segments
    REQUIRE(icon.count >= 8);
}

// ==============================================================================
// Hit Testing
// ==============================================================================

TEST_CASE("hitTestLfoPopupCell returns correct cell for center of each cell", "[lfo-shape-selector][hit]") {
    constexpr double pad = 6.0;
    constexpr double cellW = 48.0;
    constexpr double cellH = 40.0;
    constexpr double gap = 2.0;

    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < 3; ++col) {
            int expected = row * 3 + col;
            double cx = pad + col * (cellW + gap) + cellW / 2.0;
            double cy = pad + row * (cellH + gap) + cellH / 2.0;
            INFO("Cell " << expected << " at (" << cx << ", " << cy << ")");
            REQUIRE(hitTestLfoPopupCell(cx, cy) == expected);
        }
    }
}

TEST_CASE("hitTestLfoPopupCell returns -1 for padding area", "[lfo-shape-selector][hit]") {
    REQUIRE(hitTestLfoPopupCell(2.0, 2.0) == -1);
}

TEST_CASE("hitTestLfoPopupCell returns -1 for gap between cells", "[lfo-shape-selector][hit]") {
    constexpr double pad = 6.0;
    constexpr double cellW = 48.0;
    constexpr double gap = 2.0;
    // In the horizontal gap between col 0 and col 1
    double gapX = pad + cellW + gap / 2.0;
    REQUIRE(hitTestLfoPopupCell(gapX, pad + 20.0) == -1);
}

TEST_CASE("hitTestLfoPopupCell returns -1 for out of bounds", "[lfo-shape-selector][hit]") {
    REQUIRE(hitTestLfoPopupCell(500.0, 500.0) == -1);
    REQUIRE(hitTestLfoPopupCell(-1.0, -1.0) == -1);
}
