// =============================================================================
// MarkovMatrixEditor Logic Unit Tests
// =============================================================================
// Spec 133 (Gradus v1.7) — 7x7 matrix editor grid.
//
// Humble-object pattern: all testable logic lives in markov_matrix_editor_logic.h
// with no VSTGUI dependency. The CControl subclass is a thin wrapper that is
// NOT instantiated in unit tests.
// =============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "ui/markov_matrix_editor_logic.h"

using Catch::Approx;
namespace Logic = Gradus::MarkovMatrixEditorLogic;

// -----------------------------------------------------------------------------
// computeLayout / rectForCell geometry
// -----------------------------------------------------------------------------

TEST_CASE("Gradus markov editor computes layout with 12px label margin",
          "[gradus][markov][ui]")
{
    const auto l = Logic::computeLayout(152.0f, 152.0f);
    CHECK(l.left == Approx(12.0f));
    CHECK(l.top == Approx(12.0f));
    // Grid is 140x140, divided into 7 → 20 per cell
    CHECK(l.cellW == Approx(20.0f));
    CHECK(l.cellH == Approx(20.0f));
}

TEST_CASE("Gradus markov editor rectForCell returns non-overlapping cells",
          "[gradus][markov][ui]")
{
    // 152x152 widget → 140x140 grid → 20x20 cells
    auto cell00 = Logic::rectForCell(0, 0, 152.0f, 152.0f);
    CHECK(cell00.left == Approx(12.0f));
    CHECK(cell00.top == Approx(12.0f));
    CHECK(cell00.right == Approx(32.0f));
    CHECK(cell00.bottom == Approx(32.0f));

    auto cell66 = Logic::rectForCell(6, 6, 152.0f, 152.0f);
    CHECK(cell66.left == Approx(12.0f + 6 * 20.0f));
    CHECK(cell66.top == Approx(12.0f + 6 * 20.0f));
    CHECK(cell66.right == Approx(152.0f));
    CHECK(cell66.bottom == Approx(152.0f));

    // Adjacent cells share an edge
    auto cell01 = Logic::rectForCell(0, 1, 152.0f, 152.0f);
    CHECK(cell01.left == Approx(cell00.right));
}

// -----------------------------------------------------------------------------
// cellAtPoint hit-test
// -----------------------------------------------------------------------------

TEST_CASE("Gradus markov editor cellAtPoint returns correct cell",
          "[gradus][markov][ui]")
{
    // 152x152 widget, cells are 20x20 starting at (12, 12)
    // Click center of cell (0,0) at (22, 22)
    auto hit = Logic::cellAtPoint(22.0f, 22.0f, 152.0f, 152.0f);
    REQUIRE(hit.valid());
    CHECK(hit.row == 0);
    CHECK(hit.col == 0);

    // Center of cell (3, 4)
    hit = Logic::cellAtPoint(12.0f + 4 * 20.0f + 10.0f,
                             12.0f + 3 * 20.0f + 10.0f,
                             152.0f, 152.0f);
    REQUIRE(hit.valid());
    CHECK(hit.row == 3);
    CHECK(hit.col == 4);

    // Last cell (6, 6)
    hit = Logic::cellAtPoint(12.0f + 6 * 20.0f + 10.0f,
                             12.0f + 6 * 20.0f + 10.0f,
                             152.0f, 152.0f);
    REQUIRE(hit.valid());
    CHECK(hit.row == 6);
    CHECK(hit.col == 6);
}

TEST_CASE("Gradus markov editor cellAtPoint rejects points in the label margins",
          "[gradus][markov][ui]")
{
    // Point in the top-left label margin corner
    auto hit = Logic::cellAtPoint(5.0f, 5.0f, 152.0f, 152.0f);
    CHECK_FALSE(hit.valid());

    // Point in row-label strip (x < 12)
    hit = Logic::cellAtPoint(6.0f, 50.0f, 152.0f, 152.0f);
    CHECK_FALSE(hit.valid());

    // Point in column-label strip (y < 12)
    hit = Logic::cellAtPoint(50.0f, 6.0f, 152.0f, 152.0f);
    CHECK_FALSE(hit.valid());
}

TEST_CASE("Gradus markov editor cellAtPoint rejects out-of-bounds points",
          "[gradus][markov][ui]")
{
    CHECK_FALSE(Logic::cellAtPoint(-10.0f, 50.0f, 152.0f, 152.0f).valid());
    CHECK_FALSE(Logic::cellAtPoint(50.0f, -10.0f, 152.0f, 152.0f).valid());
    CHECK_FALSE(Logic::cellAtPoint(200.0f, 50.0f, 152.0f, 152.0f).valid());
    CHECK_FALSE(Logic::cellAtPoint(50.0f, 200.0f, 152.0f, 152.0f).valid());
}

TEST_CASE("Gradus markov editor cellAtPoint handles zero/negative dimensions",
          "[gradus][markov][ui]")
{
    CHECK_FALSE(Logic::cellAtPoint(50.0f, 50.0f, 0.0f, 152.0f).valid());
    CHECK_FALSE(Logic::cellAtPoint(50.0f, 50.0f, 152.0f, 0.0f).valid());
    CHECK_FALSE(Logic::cellAtPoint(50.0f, 50.0f, 10.0f, 10.0f).valid());  // too small
}

// -----------------------------------------------------------------------------
// CellIndex flatIndex
// -----------------------------------------------------------------------------

TEST_CASE("Gradus markov editor CellIndex::flatIndex is row-major",
          "[gradus][markov][ui]")
{
    CHECK(Logic::CellIndex{0, 0}.flatIndex() == 0);
    CHECK(Logic::CellIndex{0, 6}.flatIndex() == 6);
    CHECK(Logic::CellIndex{1, 0}.flatIndex() == 7);
    CHECK(Logic::CellIndex{6, 6}.flatIndex() == 48);
}

// -----------------------------------------------------------------------------
// valueFromDragY
// -----------------------------------------------------------------------------

TEST_CASE("Gradus markov editor valueFromDragY maps drag to [0,1]",
          "[gradus][markov][ui]")
{
    // Cell spans y=[100, 120] (20px tall)
    // Top of cell → value 1.0
    CHECK(Logic::valueFromDragY(100.0f, 100.0f, 120.0f) == Approx(1.0f));
    // Middle → value 0.5
    CHECK(Logic::valueFromDragY(110.0f, 100.0f, 120.0f) == Approx(0.5f));
    // Bottom → value 0.0 (technically localY == cellBottom is outside, but
    // valueFromDragY clamps it to 0).
    CHECK(Logic::valueFromDragY(120.0f, 100.0f, 120.0f) == Approx(0.0f));
}

TEST_CASE("Gradus markov editor valueFromDragY clamps out-of-range drags",
          "[gradus][markov][ui]")
{
    // Drag above cell top
    CHECK(Logic::valueFromDragY(50.0f, 100.0f, 120.0f) == Approx(1.0f));
    // Drag below cell bottom
    CHECK(Logic::valueFromDragY(200.0f, 100.0f, 120.0f) == Approx(0.0f));
}

TEST_CASE("Gradus markov editor valueFromDragY handles degenerate cell",
          "[gradus][markov][ui]")
{
    // Zero-height cell
    CHECK(Logic::valueFromDragY(100.0f, 100.0f, 100.0f) == Approx(0.0f));
    // Inverted cell (shouldn't happen but guard against it)
    CHECK(Logic::valueFromDragY(100.0f, 120.0f, 100.0f) == Approx(0.0f));
}
