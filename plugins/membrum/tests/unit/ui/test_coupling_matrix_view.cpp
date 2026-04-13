// ==============================================================================
// CouplingMatrixView -- Unit tests (Phase 6, Spec 141, T066)
// FRs covered: FR-050, FR-051, FR-052, FR-053, FR-054.
// ==============================================================================

#include "ui/coupling_matrix_view.h"
#include "dsp/coupling_matrix.h"
#include "dsp/matrix_activity_publisher.h"

#include "vstgui/lib/crect.h"
#include "vstgui/lib/cpoint.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;
using namespace Membrum;
using namespace Membrum::UI;

namespace {

constexpr VSTGUI::CRect kDefaultRect{0.0, 0.0, 320.0, 320.0}; // 10 px per cell.

VSTGUI::CPoint pointInCell(int src, int dst,
                           const VSTGUI::CRect& rect = kDefaultRect) noexcept
{
    const double cellW = rect.getWidth()  / 32.0;
    const double cellH = rect.getHeight() / 32.0;
    return VSTGUI::CPoint{rect.left + (src + 0.5) * cellW,
                          rect.top  + (dst + 0.5) * cellH};
}

} // anonymous namespace

TEST_CASE("CouplingMatrixView construction accepts matrix and publisher",
          "[coupling_matrix_view][phase6]")
{
    CouplingMatrix         matrix;
    MatrixActivityPublisher pub;
    CouplingMatrixView view(kDefaultRect, &matrix, &pub);
    REQUIRE(view.hasSolo() == false);
}

TEST_CASE("CouplingMatrixView::cellRect maps (src, dst) to the correct screen rect",
          "[coupling_matrix_view][phase6]")
{
    CouplingMatrix         matrix;
    MatrixActivityPublisher pub;
    CouplingMatrixView view(kDefaultRect, &matrix, &pub);

    // 32x32 cells over 320x320 -> 10 px per cell.
    const auto r13 = view.cellRect(1, 3);
    REQUIRE_THAT(r13.left,   WithinAbs(10.0,  1e-6));
    REQUIRE_THAT(r13.top,    WithinAbs(30.0,  1e-6));
    REQUIRE_THAT(r13.right,  WithinAbs(20.0,  1e-6));
    REQUIRE_THAT(r13.bottom, WithinAbs(40.0,  1e-6));

    // Out-of-range -> empty rect.
    const auto bad = view.cellRect(-1, 5);
    REQUIRE(bad.getWidth()  == 0.0);
    REQUIRE(bad.getHeight() == 0.0);
}

TEST_CASE("CouplingMatrixView left click on cell (1,3) sets override",
          "[coupling_matrix_view][phase6]")
{
    CouplingMatrix         matrix;
    MatrixActivityPublisher pub;
    CouplingMatrixView view(kDefaultRect, &matrix, &pub);

    REQUIRE_FALSE(matrix.hasOverrideAt(1, 3));

    const auto hit = view.handleMouseDown(pointInCell(1, 3),
                                          /*isShift*/ false,
                                          /*isRight*/ false);
    REQUIRE(hit.src == 1);
    REQUIRE(hit.dst == 3);
    REQUIRE(matrix.hasOverrideAt(1, 3));
    REQUIRE_THAT(matrix.getOverrideGain(1, 3), WithinAbs(0.010f, 1e-6f));

    // Second click advances to the next step.
    view.handleMouseDown(pointInCell(1, 3), false, false);
    REQUIRE_THAT(matrix.getOverrideGain(1, 3), WithinAbs(0.025f, 1e-6f));

    // Third click: 0.05.
    view.handleMouseDown(pointInCell(1, 3), false, false);
    REQUIRE_THAT(matrix.getOverrideGain(1, 3), WithinAbs(0.050f, 1e-6f));

    // Fourth click: Reset -- override cleared.
    view.handleMouseDown(pointInCell(1, 3), false, false);
    REQUIRE_FALSE(matrix.hasOverrideAt(1, 3));
}

TEST_CASE("CouplingMatrixView right click clears override (Reset)",
          "[coupling_matrix_view][phase6]")
{
    CouplingMatrix         matrix;
    MatrixActivityPublisher pub;
    CouplingMatrixView view(kDefaultRect, &matrix, &pub);

    matrix.setOverride(1, 3, 0.04f);
    REQUIRE(matrix.hasOverrideAt(1, 3));

    view.handleMouseDown(pointInCell(1, 3), /*isShift*/ false, /*isRight*/ true);
    REQUIRE_FALSE(matrix.hasOverrideAt(1, 3));
}

TEST_CASE("CouplingMatrixView diagonal cells are not editable",
          "[coupling_matrix_view][phase6]")
{
    CouplingMatrix         matrix;
    MatrixActivityPublisher pub;
    CouplingMatrixView view(kDefaultRect, &matrix, &pub);

    const auto hit = view.handleMouseDown(pointInCell(5, 5), false, false);
    REQUIRE(hit.src == -1);
    REQUIRE(hit.dst == -1);
    REQUIRE_FALSE(matrix.hasOverrideAt(5, 5));
}

TEST_CASE("CouplingMatrixView Solo engagement zeros non-solo pairs (FR-053)",
          "[coupling_matrix_view][phase6]")
{
    CouplingMatrix         matrix;
    MatrixActivityPublisher pub;
    CouplingMatrixView view(kDefaultRect, &matrix, &pub);

    // Populate a handful of pair overrides.
    matrix.setOverride(0, 1, 0.050f);
    matrix.setOverride(2, 3, 0.025f);
    matrix.setOverride(4, 5, 0.010f);

    // Shift-click engages Solo on (2, 3).
    view.handleMouseDown(pointInCell(2, 3), /*isShift*/ true, /*isRight*/ false);
    REQUIRE(view.hasSolo());
    REQUIRE(matrix.soloSrc() == 2);
    REQUIRE(matrix.soloDst() == 3);

    // Only the soloed pair reports non-zero effective gain.
    REQUIRE_THAT(matrix.getEffectiveGain(2, 3), WithinAbs(0.025f, 1e-6f));
    REQUIRE(matrix.getEffectiveGain(0, 1) == 0.0f);
    REQUIRE(matrix.getEffectiveGain(4, 5) == 0.0f);

    // Shift-clicking the soloed pair again disengages Solo.
    view.handleMouseDown(pointInCell(2, 3), /*isShift*/ true, /*isRight*/ false);
    REQUIRE_FALSE(view.hasSolo());
    REQUIRE_THAT(matrix.getEffectiveGain(0, 1), WithinAbs(0.050f, 1e-6f));
    REQUIRE_THAT(matrix.getEffectiveGain(4, 5), WithinAbs(0.010f, 1e-6f));
}

TEST_CASE("CouplingMatrixView clearSolo() restores all pairs",
          "[coupling_matrix_view][phase6]")
{
    CouplingMatrix         matrix;
    MatrixActivityPublisher pub;
    CouplingMatrixView view(kDefaultRect, &matrix, &pub);

    matrix.setOverride(7, 9, 0.030f);
    view.setSoloPath(1, 2);
    REQUIRE(view.hasSolo());
    REQUIRE(matrix.getEffectiveGain(7, 9) == 0.0f);

    view.clearSolo();
    REQUIRE_FALSE(view.hasSolo());
    REQUIRE_THAT(matrix.getEffectiveGain(7, 9), WithinAbs(0.030f, 1e-6f));
}

TEST_CASE("CouplingMatrixView destructor disengages Solo (FR-053 edge case)",
          "[coupling_matrix_view][phase6]")
{
    CouplingMatrix         matrix;
    MatrixActivityPublisher pub;

    {
        CouplingMatrixView view(kDefaultRect, &matrix, &pub);
        view.setSoloPath(3, 4);
        REQUIRE(matrix.hasSolo());
    }
    // View went out of scope -> Solo must be cleared.
    REQUIRE_FALSE(matrix.hasSolo());
}

TEST_CASE("CouplingMatrixView activity bitmask snapshot from publisher",
          "[coupling_matrix_view][phase6]")
{
    CouplingMatrix         matrix;
    MatrixActivityPublisher pub;
    CouplingMatrixView view(kDefaultRect, &matrix, &pub);

    // Source pad 5 activates destinations 1, 7, and 13.
    const std::uint32_t mask5 = (1u << 1) | (1u << 7) | (1u << 13);
    pub.publishSourceActivity(5, mask5);
    view.pollActivity();

    REQUIRE(view.activityMaskForSrc(5) == mask5);
    REQUIRE(view.activityMaskForSrc(6) == 0u);
}

TEST_CASE("CouplingMatrixView::nextOverrideStep cycles through 0.01, 0.025, 0.05, cleared",
          "[coupling_matrix_view][phase6]")
{
    auto s0 = CouplingMatrixView::nextOverrideStep(false, 0.0f);
    REQUIRE(s0.hasOverride);
    REQUIRE_THAT(s0.value, WithinAbs(0.010f, 1e-6f));

    auto s1 = CouplingMatrixView::nextOverrideStep(true, 0.010f);
    REQUIRE(s1.hasOverride);
    REQUIRE_THAT(s1.value, WithinAbs(0.025f, 1e-6f));

    auto s2 = CouplingMatrixView::nextOverrideStep(true, 0.025f);
    REQUIRE(s2.hasOverride);
    REQUIRE_THAT(s2.value, WithinAbs(0.050f, 1e-6f));

    auto s3 = CouplingMatrixView::nextOverrideStep(true, 0.050f);
    REQUIRE_FALSE(s3.hasOverride);
}

TEST_CASE("CouplingMatrixView click outside grid is a no-op",
          "[coupling_matrix_view][phase6]")
{
    CouplingMatrix         matrix;
    MatrixActivityPublisher pub;
    CouplingMatrixView view(kDefaultRect, &matrix, &pub);

    const auto outside = view.handleMouseDown(VSTGUI::CPoint{-10.0, -10.0},
                                              false, false);
    REQUIRE(outside.src == -1);
    REQUIRE(outside.dst == -1);

    const auto beyond = view.handleMouseDown(VSTGUI::CPoint{500.0, 500.0},
                                             false, false);
    REQUIRE(beyond.src == -1);
    REQUIRE(beyond.dst == -1);
}

// ==============================================================================
// T069: verify coupling matrix override round-trip (Phase 5 override block
// still serialises cleanly in state v6).
// ==============================================================================
TEST_CASE("CouplingMatrix override round-trip via forEachOverride (T069)",
          "[coupling_matrix_view][phase6][roundtrip]")
{
    CouplingMatrix src;
    src.setOverride(1, 3, 0.04f);
    src.setOverride(2, 2, 0.01f); // diagonal gets clamped but still stored
    src.setOverride(10, 20, 0.025f);

    // Serialise -> {count, (src, dst, coeff)...}.
    struct Entry { int s; int d; float v; };
    std::vector<Entry> wire;
    src.forEachOverride([&](int s, int d, float v) {
        wire.push_back({s, d, v});
    });
    REQUIRE(src.getOverrideCount() == static_cast<int>(wire.size()));

    // Deserialise into a fresh matrix.
    CouplingMatrix dst;
    for (const auto& e : wire)
        dst.setOverride(e.s, e.d, e.v);

    REQUIRE(dst.getOverrideCount() == src.getOverrideCount());
    REQUIRE_THAT(dst.getOverrideGain(1, 3),  WithinAbs(0.04f,  1e-6f));
    REQUIRE_THAT(dst.getOverrideGain(10, 20), WithinAbs(0.025f, 1e-6f));
}
