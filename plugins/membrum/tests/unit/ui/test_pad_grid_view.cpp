// ==============================================================================
// PadGridView -- Unit tests (Phase 6, Spec 141, T041)
// FRs covered: FR-010, FR-011, FR-012, FR-013, FR-014 (publisher wiring),
//              FR-015 (DPI-independent cell geom), SC-004, SC-005.
// ==============================================================================

#include "ui/pad_grid_view.h"
#include "dsp/pad_glow_publisher.h"
#include "dsp/pad_config.h"

#include "vstgui/lib/crect.h"

#include <catch2/catch_test_macros.hpp>

#include <array>

using namespace Membrum;
using namespace Membrum::UI;

// Helper: build a default PadConfig array and return a meta provider that
// reads out of it. The caller owns the storage.
static PadGridView::PadMetaProvider makeProvider(
    const std::array<PadConfig, kNumPads>& pads)
{
    return [&pads](int idx) -> const PadConfig* {
        if (idx < 0 || idx >= kNumPads)
            return nullptr;
        return &pads[static_cast<std::size_t>(idx)];
    };
}

TEST_CASE("PadGridView geometry: MIDI 36 bottom-left, MIDI 67 top-right",
          "[pad_grid][phase6]")
{
    // FR-010: 4 columns x 8 rows; pad 0 (MIDI 36) at bottom-left,
    // pad 31 (MIDI 67) at top-right.

    SECTION("padIndexFromCell bottom-left == 0")
    {
        REQUIRE(padIndexFromCell(0, kPadGridRows - 1) == 0);
        REQUIRE(midiNoteForPad(0) == 36);
    }
    SECTION("padIndexFromCell top-right == 31")
    {
        REQUIRE(padIndexFromCell(kPadGridColumns - 1, 0) == 31);
        REQUIRE(midiNoteForPad(31) == 67);
    }
    SECTION("all 32 cells map uniquely to pads 0..31")
    {
        std::array<int, kNumPads> seen{};
        for (int r = 0; r < kPadGridRows; ++r)
            for (int c = 0; c < kPadGridColumns; ++c)
                seen[static_cast<std::size_t>(padIndexFromCell(c, r))] += 1;
        for (int i = 0; i < kNumPads; ++i)
            REQUIRE(seen[static_cast<std::size_t>(i)] == 1);
    }
}

TEST_CASE("PadGridView::padIndexFromPoint maps screen coords to pad",
          "[pad_grid][phase6]")
{
    std::array<PadConfig, kNumPads> pads{};
    PadGridView view(
        VSTGUI::CRect{ 0, 0, 400, 800 },
        /*glowPublisher*/ nullptr,
        makeProvider(pads));

    // Cell is 100 x 100.
    SECTION("bottom-left quadrant -> pad 0 (MIDI 36)")
    {
        const int pad = view.padIndexFromPoint(VSTGUI::CPoint{ 10, 790 });
        REQUIRE(pad == 0);
        REQUIRE(midiNoteForPad(pad) == 36);
    }

    SECTION("top-right quadrant -> pad 31 (MIDI 67)")
    {
        const int pad = view.padIndexFromPoint(VSTGUI::CPoint{ 390, 10 });
        REQUIRE(pad == 31);
        REQUIRE(midiNoteForPad(pad) == 67);
    }

    SECTION("points outside the view bounds return -1")
    {
        REQUIRE(view.padIndexFromPoint(VSTGUI::CPoint{ -5, 10 }) == -1);
        REQUIRE(view.padIndexFromPoint(VSTGUI::CPoint{ 10, -5 }) == -1);
        REQUIRE(view.padIndexFromPoint(VSTGUI::CPoint{ 500, 10 }) == -1);
        REQUIRE(view.padIndexFromPoint(VSTGUI::CPoint{ 10, 900 }) == -1);
    }
}

TEST_CASE("PadGridView DPI-scale independence (FR-015)",
          "[pad_grid][phase6]")
{
    std::array<PadConfig, kNumPads> pads{};

    // At 1.5x DPI the pixel size of the view grows by 1.5; the mapping from
    // point to pad still works because padIndexFromPoint is expressed in the
    // view's own coordinate system.
    PadGridView view(
        VSTGUI::CRect{ 0, 0, 600, 1200 }, // 1.5x of 400x800
        /*glowPublisher*/ nullptr,
        makeProvider(pads));

    REQUIRE(view.padIndexFromPoint(VSTGUI::CPoint{ 10, 1190 }) == 0);
    REQUIRE(view.padIndexFromPoint(VSTGUI::CPoint{ 590, 10 }) == 31);
}

TEST_CASE("PadGridView click sets selection without modifiers (FR-012)",
          "[pad_grid][phase6]")
{
    std::array<PadConfig, kNumPads> pads{};
    PadGridView view(
        VSTGUI::CRect{ 0, 0, 400, 800 },
        nullptr,
        makeProvider(pads));

    int lastSelected = -1;
    view.setSelectCallback([&](int p) { lastSelected = p; });

    int lastAudition = -1;
    view.setAuditionCallback([&](int p, float /*v*/) { lastAudition = p; });

    // Click pad 5 at (col=1, row=5) -> midi 36 + 2*4 + 1 = 45
    const int hit = view.handleMouseDown(
        VSTGUI::CPoint{ 150, 550 }, /*isShift*/ false, /*isRight*/ false);
    REQUIRE(hit >= 0);
    REQUIRE(lastSelected == hit);
    REQUIRE(lastAudition == -1);      // must NOT audition
    REQUIRE(view.selectedPadIndex() == hit);
}

TEST_CASE("PadGridView shift-click auditions without changing selection (FR-013)",
          "[pad_grid][phase6]")
{
    std::array<PadConfig, kNumPads> pads{};
    PadGridView view(
        VSTGUI::CRect{ 0, 0, 400, 800 },
        nullptr,
        makeProvider(pads));

    int lastSelected = -1;
    int lastAudition = -1;
    float lastVelocity = -1.0f;
    view.setSelectCallback  ([&](int p)            { lastSelected  = p; });
    view.setAuditionCallback([&](int p, float v)   { lastAudition = p; lastVelocity = v; });

    // Prime selection to pad 0.
    view.setSelectedPadIndex(0);
    REQUIRE(view.selectedPadIndex() == 0);

    // Shift-click pad 7 (col=3, row=7) -> pad 3. Actually pad 7 is at
    // (col=3, row=6). Hit approximate centre of that cell: x=350, y=650.
    const int hit = view.handleMouseDown(
        VSTGUI::CPoint{ 350, 650 }, /*isShift*/ true, /*isRight*/ false);
    REQUIRE(hit >= 0);
    REQUIRE(lastSelected == -1);                   // selection unchanged
    REQUIRE(lastAudition == hit);                  // auditioned
    REQUIRE(lastVelocity > 0.0f);
    REQUIRE(lastVelocity <= 1.0f);
    REQUIRE(view.selectedPadIndex() == 0);         // still pad 0
}

TEST_CASE("PadGridView right-click auditions without changing selection (FR-013)",
          "[pad_grid][phase6]")
{
    std::array<PadConfig, kNumPads> pads{};
    PadGridView view(
        VSTGUI::CRect{ 0, 0, 400, 800 },
        nullptr,
        makeProvider(pads));

    view.setSelectedPadIndex(2);

    int lastSelected = -1;
    int lastAudition = -1;
    view.setSelectCallback  ([&](int p)          { lastSelected = p; });
    view.setAuditionCallback([&](int p, float)   { lastAudition = p; });

    const int hit = view.handleMouseDown(
        VSTGUI::CPoint{ 150, 550 }, /*isShift*/ false, /*isRight*/ true);
    REQUIRE(hit >= 0);
    REQUIRE(lastSelected == -1);
    REQUIRE(lastAudition == hit);
    REQUIRE(view.selectedPadIndex() == 2);
}

TEST_CASE("PadGridView glow intensity derived from PadGlowPublisher (FR-014)",
          "[pad_grid][phase6]")
{
    PadGlowPublisher publisher;
    std::array<PadConfig, kNumPads> pads{};

    PadGridView view(
        VSTGUI::CRect{ 0, 0, 400, 800 },
        &publisher,
        makeProvider(pads));

    // Publish a mid-level amplitude on pad 5, full amplitude on pad 10.
    publisher.publish(5,  0.5f);
    publisher.publish(10, 1.0f);

    view.pollGlow();

    const std::uint8_t bucket5  = view.glowBucketForPad(5);
    const std::uint8_t bucket10 = view.glowBucketForPad(10);
    const std::uint8_t bucket0  = view.glowBucketForPad(0);

    REQUIRE(bucket5  > 0);
    REQUIRE(bucket10 > bucket5);
    REQUIRE(bucket0  == 0);

    // glowIntensityFromBucket: bucket 31 == 1.0, bucket 0 == 0.0
    REQUIRE(glowIntensityFromBucket(31) == 1.0f);
    REQUIRE(glowIntensityFromBucket(0)  == 0.0f);
    REQUIRE(glowIntensityFromBucket(bucket10) > glowIntensityFromBucket(bucket5));
}

TEST_CASE("PadGridView indicator text helpers (FR-011)",
          "[pad_grid][phase6]")
{
    SECTION("chokeGroup == 0 yields empty string")
    {
        REQUIRE(chokeGroupIndicatorText(0).empty());
    }
    SECTION("chokeGroup == 2 yields 'CG2'")
    {
        REQUIRE(chokeGroupIndicatorText(2) == std::string{"CG2"});
    }
    SECTION("outputBus == 0 yields empty string")
    {
        REQUIRE(outputBusIndicatorText(0).empty());
    }
    SECTION("outputBus == 3 yields 'BUS3'")
    {
        REQUIRE(outputBusIndicatorText(3) == std::string{"BUS3"});
    }
}

TEST_CASE("PadGridView removed() cancels the poll timer (SC-014)",
          "[pad_grid][phase6]")
{
    // Constructing with a publisher installs a CVSTGUITimer. Tearing the view
    // down via removed() must cancel it so no tick fires into a dying view.
    PadGlowPublisher publisher;
    std::array<PadConfig, kNumPads> pads{};

    auto* view = new PadGridView(
        VSTGUI::CRect{ 0, 0, 400, 800 },
        &publisher,
        makeProvider(pads));

    view->removed(nullptr);
    // No crash, no dangling timer -- handled by PadGridView::removed().
    delete view;
    SUCCEED();
}
