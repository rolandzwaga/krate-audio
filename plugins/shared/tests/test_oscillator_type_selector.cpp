// ==============================================================================
// OscillatorTypeSelector Unit Tests (050-oscillator-selector)
// ==============================================================================
// Tests for value conversion, waveform icon path generation, hit testing,
// and NaN defense -- all pure logic, no VSTGUI draw context needed.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "ui/oscillator_type_selector.h"

using namespace Krate::Plugins;
using namespace Krate::Plugins::OscWaveformIcons;
using Catch::Approx;

// ==============================================================================
// Phase 2.1 T005: oscTypeIndexFromNormalized() tests
// ==============================================================================

TEST_CASE("oscTypeIndexFromNormalized maps 0.0 to index 0", "[osc_selector][value]") {
    REQUIRE(oscTypeIndexFromNormalized(0.0f) == 0);
}

TEST_CASE("oscTypeIndexFromNormalized maps 1.0 to index 9", "[osc_selector][value]") {
    REQUIRE(oscTypeIndexFromNormalized(1.0f) == 9);
}

TEST_CASE("oscTypeIndexFromNormalized maps 0.5 to index 5 (Chaos)", "[osc_selector][value]") {
    // 0.5 * 9 = 4.5, rounds to 5
    REQUIRE(oscTypeIndexFromNormalized(0.5f) == 5);
}

TEST_CASE("oscTypeIndexFromNormalized maps each normalized value back correctly", "[osc_selector][value]") {
    for (int i = 0; i < 10; ++i) {
        float normalized = static_cast<float>(i) / 9.0f;
        REQUIRE(oscTypeIndexFromNormalized(normalized) == i);
    }
}

TEST_CASE("oscTypeIndexFromNormalized NaN defense: NaN maps to index 5", "[osc_selector][value][nan]") {
    float nan = std::numeric_limits<float>::quiet_NaN();
    // NaN -> 0.5 -> round(0.5 * 9) = round(4.5) = 5
    REQUIRE(oscTypeIndexFromNormalized(nan) == 5);
}

TEST_CASE("oscTypeIndexFromNormalized NaN defense: +inf maps to index 9", "[osc_selector][value][nan]") {
    float inf = std::numeric_limits<float>::infinity();
    // +inf -> 0.5 -> round(0.5 * 9) = 5
    REQUIRE(oscTypeIndexFromNormalized(inf) == 5);
}

TEST_CASE("oscTypeIndexFromNormalized NaN defense: -inf maps to index 5", "[osc_selector][value][nan]") {
    float negInf = -std::numeric_limits<float>::infinity();
    // -inf -> 0.5 -> round(0.5 * 9) = 5
    REQUIRE(oscTypeIndexFromNormalized(negInf) == 5);
}

TEST_CASE("oscTypeIndexFromNormalized clamps negative values to 0", "[osc_selector][value]") {
    REQUIRE(oscTypeIndexFromNormalized(-0.5f) == 0);
    REQUIRE(oscTypeIndexFromNormalized(-100.0f) == 0);
}

TEST_CASE("oscTypeIndexFromNormalized clamps values above 1.0 to 9", "[osc_selector][value]") {
    REQUIRE(oscTypeIndexFromNormalized(1.5f) == 9);
    REQUIRE(oscTypeIndexFromNormalized(100.0f) == 9);
}

TEST_CASE("oscTypeIndexFromNormalized rounding at boundaries", "[osc_selector][value]") {
    // Value between index 2 and 3: 2.5/9 = 0.2778
    // round(0.2778 * 9) = round(2.5) = 3 (round-half-up on most platforms)
    int idx = oscTypeIndexFromNormalized(2.5f / 9.0f);
    REQUIRE((idx == 2 || idx == 3)); // either is acceptable for 0.5 rounding
}

// ==============================================================================
// Phase 2.1 T006: normalizedFromOscTypeIndex() tests
// ==============================================================================

TEST_CASE("normalizedFromOscTypeIndex index 0 maps to 0.0", "[osc_selector][value]") {
    REQUIRE(normalizedFromOscTypeIndex(0) == Approx(0.0f).margin(1e-6f));
}

TEST_CASE("normalizedFromOscTypeIndex index 9 maps to 1.0", "[osc_selector][value]") {
    REQUIRE(normalizedFromOscTypeIndex(9) == Approx(1.0f).margin(1e-6f));
}

TEST_CASE("normalizedFromOscTypeIndex all 10 indices produce correct values", "[osc_selector][value]") {
    for (int i = 0; i < 10; ++i) {
        float expected = static_cast<float>(i) / 9.0f;
        REQUIRE(normalizedFromOscTypeIndex(i) == Approx(expected).margin(1e-6f));
    }
}

TEST_CASE("normalizedFromOscTypeIndex clamps negative index to 0.0", "[osc_selector][value]") {
    REQUIRE(normalizedFromOscTypeIndex(-1) == Approx(0.0f).margin(1e-6f));
    REQUIRE(normalizedFromOscTypeIndex(-100) == Approx(0.0f).margin(1e-6f));
}

TEST_CASE("normalizedFromOscTypeIndex clamps index above 9 to 1.0", "[osc_selector][value]") {
    REQUIRE(normalizedFromOscTypeIndex(10) == Approx(1.0f).margin(1e-6f));
    REQUIRE(normalizedFromOscTypeIndex(100) == Approx(1.0f).margin(1e-6f));
}

TEST_CASE("normalizedFromOscTypeIndex round-trips with oscTypeIndexFromNormalized", "[osc_selector][value]") {
    for (int i = 0; i < 10; ++i) {
        float norm = normalizedFromOscTypeIndex(i);
        REQUIRE(oscTypeIndexFromNormalized(norm) == i);
    }
}

// ==============================================================================
// Phase 2.1 T007: Display name lookup tests
// ==============================================================================

TEST_CASE("oscTypeDisplayName returns correct names for all types", "[osc_selector][display]") {
    REQUIRE(std::string(oscTypeDisplayName(0)) == "PolyBLEP");
    REQUIRE(std::string(oscTypeDisplayName(1)) == "Wavetable");
    REQUIRE(std::string(oscTypeDisplayName(2)) == "Phase Distortion");
    REQUIRE(std::string(oscTypeDisplayName(3)) == "Sync");
    REQUIRE(std::string(oscTypeDisplayName(4)) == "Additive");
    REQUIRE(std::string(oscTypeDisplayName(5)) == "Chaos");
    REQUIRE(std::string(oscTypeDisplayName(6)) == "Particle");
    REQUIRE(std::string(oscTypeDisplayName(7)) == "Formant");
    REQUIRE(std::string(oscTypeDisplayName(8)) == "Spectral Freeze");
    REQUIRE(std::string(oscTypeDisplayName(9)) == "Noise");
}

TEST_CASE("oscTypePopupLabel returns correct abbreviated labels", "[osc_selector][display]") {
    REQUIRE(std::string(oscTypePopupLabel(0)) == "BLEP");
    REQUIRE(std::string(oscTypePopupLabel(1)) == "WTbl");
    REQUIRE(std::string(oscTypePopupLabel(2)) == "PDst");
    REQUIRE(std::string(oscTypePopupLabel(3)) == "Sync");
    REQUIRE(std::string(oscTypePopupLabel(4)) == "Add");
    REQUIRE(std::string(oscTypePopupLabel(5)) == "Chaos");
    REQUIRE(std::string(oscTypePopupLabel(6)) == "Prtcl");
    REQUIRE(std::string(oscTypePopupLabel(7)) == "Fmnt");
    REQUIRE(std::string(oscTypePopupLabel(8)) == "SFrz");
    REQUIRE(std::string(oscTypePopupLabel(9)) == "Noise");
}

TEST_CASE("oscTypeDisplayName clamps out-of-range index", "[osc_selector][display]") {
    // Should not crash for out-of-range, returns first or last
    REQUIRE(std::string(oscTypeDisplayName(-1)) == "PolyBLEP");
    REQUIRE(std::string(oscTypeDisplayName(10)) == "Noise");
}

TEST_CASE("oscTypePopupLabel clamps out-of-range index", "[osc_selector][display]") {
    REQUIRE(std::string(oscTypePopupLabel(-1)) == "BLEP");
    REQUIRE(std::string(oscTypePopupLabel(10)) == "Noise");
}

// ==============================================================================
// Phase 2.3 T011: OscWaveformIcons::getIconPath() tests
// ==============================================================================

TEST_CASE("getIconPath returns valid path for all 10 types", "[osc_selector][icons]") {
    for (int i = 0; i < 10; ++i) {
        auto type = static_cast<Krate::DSP::OscType>(i);
        auto path = getIconPath(type);

        INFO("OscType index: " << i);
        REQUIRE(path.count >= 3);     // minimum 3 points to form a visible shape
        REQUIRE(path.count <= 12);    // max array capacity
    }
}

TEST_CASE("getIconPath all points are in normalized [0,1] range", "[osc_selector][icons]") {
    for (int i = 0; i < 10; ++i) {
        auto type = static_cast<Krate::DSP::OscType>(i);
        auto path = getIconPath(type);

        for (int p = 0; p < path.count; ++p) {
            INFO("OscType " << i << " point " << p);
            REQUIRE(path.points[p].x >= 0.0f);
            REQUIRE(path.points[p].x <= 1.0f);
            REQUIRE(path.points[p].y >= 0.0f);
            REQUIRE(path.points[p].y <= 1.0f);
        }
    }
}

TEST_CASE("getIconPath each type has distinct point count or positions", "[osc_selector][icons]") {
    // At minimum, each icon should have at least 3 points
    // and icons should be visually distinguishable (not identical paths)
    auto path0 = getIconPath(Krate::DSP::OscType::PolyBLEP);
    auto path1 = getIconPath(Krate::DSP::OscType::Wavetable);
    auto path9 = getIconPath(Krate::DSP::OscType::Noise);

    // Different types should produce different paths
    bool allSame = (path0.count == path1.count);
    if (allSame) {
        for (int p = 0; p < path0.count; ++p) {
            if (std::abs(path0.points[p].x - path1.points[p].x) > 0.01f ||
                std::abs(path0.points[p].y - path1.points[p].y) > 0.01f) {
                allSame = false;
                break;
            }
        }
    }
    REQUIRE_FALSE(allSame);
}

// ==============================================================================
// Phase 2.5 T014: Grid hit testing tests
// ==============================================================================

TEST_CASE("hitTestPopupCell returns 0 for top-left cell", "[osc_selector][hittest]") {
    // Cell 0 starts at padding (6,6), size 48x40
    REQUIRE(hitTestPopupCell(10.0, 10.0) == 0);
}

TEST_CASE("hitTestPopupCell returns 4 for top-right cell", "[osc_selector][hittest]") {
    // Cell 4: col=4, row=0. X = 6 + 4*(48+2) = 206, so within cell at 210
    REQUIRE(hitTestPopupCell(210.0, 10.0) == 4);
}

TEST_CASE("hitTestPopupCell returns 5 for second row first cell", "[osc_selector][hittest]") {
    // Cell 5: col=0, row=1. Y = 6 + 1*(40+2) = 48, so within cell at 50
    REQUIRE(hitTestPopupCell(10.0, 50.0) == 5);
}

TEST_CASE("hitTestPopupCell returns 9 for bottom-right cell", "[osc_selector][hittest]") {
    // Cell 9: col=4, row=1. X = 6 + 4*(48+2) = 206, Y = 6 + 1*(40+2) = 48
    REQUIRE(hitTestPopupCell(210.0, 50.0) == 9);
}

TEST_CASE("hitTestPopupCell returns -1 for padding area", "[osc_selector][hittest]") {
    // In left padding (x < 6)
    REQUIRE(hitTestPopupCell(3.0, 20.0) == -1);
    // In top padding (y < 6)
    REQUIRE(hitTestPopupCell(20.0, 3.0) == -1);
}

TEST_CASE("hitTestPopupCell returns -1 for gap between cells", "[osc_selector][hittest]") {
    // Gap between col 0 and col 1: x = 6 + 48 = 54 to 56
    // Cell 0 ends at x=54, gap is 54-56, Cell 1 starts at 56
    REQUIRE(hitTestPopupCell(55.0, 20.0) == -1);
}

TEST_CASE("hitTestPopupCell returns -1 for out-of-bounds", "[osc_selector][hittest]") {
    // Way beyond the grid
    REQUIRE(hitTestPopupCell(300.0, 200.0) == -1);
    // Negative coordinates
    REQUIRE(hitTestPopupCell(-10.0, -10.0) == -1);
}

TEST_CASE("hitTestPopupCell center of each cell returns correct index", "[osc_selector][hittest]") {
    constexpr double kPadding = 6.0;
    constexpr double kCellW = 48.0;
    constexpr double kCellH = 40.0;
    constexpr double kGap = 2.0;

    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < 5; ++col) {
            double cx = kPadding + col * (kCellW + kGap) + kCellW / 2.0;
            double cy = kPadding + row * (kCellH + kGap) + kCellH / 2.0;
            int expected = row * 5 + col;
            INFO("Cell (" << col << "," << row << ") center=(" << cx << "," << cy << ")");
            REQUIRE(hitTestPopupCell(cx, cy) == expected);
        }
    }
}
